// Crash reporter: on SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT, appends the faulting
// address, RIP/RSP and likely return addresses from the stack to
// /data/PSLauncher/main.log (the launcher's one log), so a crash can be mapped
// to a source line with the eboot's symbols instead of bisecting with logs.
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <ucontext.h>

void pslFaultLog(const char* line);

extern "C" void psl_fault_anchor(void) {}

static char g_altstack[64 * 1024];
static char g_step[96] = "startup";

static void put(int fd, const char* s) {
    if (fd >= 0) (void)!write(fd, s, std::strlen(s));
}

// Called by the play loop so a crash names the step it was in.
void pslSetStep(const char* step) {
    std::strncpy(g_step, step, sizeof g_step - 1);
    g_step[sizeof g_step - 1] = '\0';
}

static void onFault(int sig, siginfo_t* si, void* ctx) {
    int fd = open("/data/PSLauncher/main.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    char line[256];
    const ucontext_t* uc = static_cast<const ucontext_t*>(ctx);
    // The console's machine context lies 48 bytes past the upstream header's
    // uc_mcontext (the PS5 fork's context.h: "six 64-bit words later"). Reading
    // at the upstream offset gives the wrong registers.
    const mcontext_t& m = *reinterpret_cast<const mcontext_t*>(
        reinterpret_cast<const char*>(uc) + offsetof(ucontext_t, uc_mcontext) + 48);
    const uintptr_t rip = (uintptr_t)m.mc_rip;
    const uintptr_t rsp = (uintptr_t)m.mc_rsp;
    const uintptr_t anchor = (uintptr_t)&psl_fault_anchor;
    std::snprintf(line, sizeof line, "FAULT sig=%d code=%d addr=%p rip=%p rsp=%p anchor=%p step=\"%s\"\n", sig,
                  si ? si->si_code : -1, si ? si->si_addr : nullptr, (void*)rip, (void*)rsp, (void*)anchor,
                  g_step);
    put(fd, line);
    // Registers and the bytes at rip show which value was bad, even when the
    // stack cannot be walked. rip is where the fault happened, so it is mapped.
    std::snprintf(line, sizeof line,
                  "  rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx rbp=%llx\n",
                  (unsigned long long)m.mc_rax, (unsigned long long)m.mc_rbx, (unsigned long long)m.mc_rcx,
                  (unsigned long long)m.mc_rdx, (unsigned long long)m.mc_rsi, (unsigned long long)m.mc_rdi,
                  (unsigned long long)m.mc_rbp);
    put(fd, line);
    std::snprintf(line, sizeof line, "  r8=%llx r9=%llx r10=%llx r11=%llx r12=%llx r13=%llx r14=%llx r15=%llx\n",
                  (unsigned long long)m.mc_r8, (unsigned long long)m.mc_r9, (unsigned long long)m.mc_r10,
                  (unsigned long long)m.mc_r11, (unsigned long long)m.mc_r12, (unsigned long long)m.mc_r13,
                  (unsigned long long)m.mc_r14, (unsigned long long)m.mc_r15);
    put(fd, line);
    // Scanning a stack pointer that is null, tiny or misaligned faults inside the
    // handler and kills the process before the record is complete, so skip it.
    const bool rspUsable = rsp >= (1u << 20) && (rsp & 7) == 0;
    if (!rspUsable) {
        put(fd, "  stack not scanned: rsp is not a usable stack pointer\n");
    } else {
        // Stack words that point near our own code are likely return addresses.
        const uintptr_t lo = anchor > (96u << 20) ? anchor - (96u << 20) : 0, hi = anchor + (96u << 20);
        const uintptr_t* sp = reinterpret_cast<const uintptr_t*>(rsp);
        int found = 0;
        for (int i = 0; i < 512 && found < 24; ++i) {
            const uintptr_t v = sp[i];
            if (v >= lo && v < hi) {
                std::snprintf(line, sizeof line, "  stack[%d]=%p\n", i, (void*)v);
                put(fd, line);
                ++found;
            }
        }
    }
    if (fd >= 0) close(fd);
    _exit(128 + sig);
}

void installFaultHandler() {
    stack_t ss{};
    ss.ss_sp = g_altstack;
    ss.ss_size = sizeof g_altstack;
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = onFault;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    const int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
    for (int s : sigs) sigaction(s, &sa, nullptr);
    char buf[96];
    std::snprintf(buf, sizeof buf, "fault handler installed (anchor %p)", (void*)&psl_fault_anchor);
    pslFaultLog(buf);
}
