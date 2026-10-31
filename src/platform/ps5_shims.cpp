// PS Launcher - stand-ins for symbols the PS5 libraries don't provide to a title.
// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(__PROSPERO__)
#include <cctype>
#include <cstddef>
#include <cstdio>

extern "C" {
// nl_langinfo(CODESET) stand-in (same as SwanStationPS5's ps5_shims.c).
// ___mb_cur_max now comes from the SDK's libc.a, linked for the core's locale use.
char* nl_langinfo(int) { return const_cast<char*>("UTF-8"); }

// The PS5's libc has ps5_open_memstream (platform layer), not the standard name.
FILE* ps5_open_memstream(char** bufp, size_t* sizep);
FILE* open_memstream(char** bufp, size_t* sizep) { return ps5_open_memstream(bufp, sizep); }

// compiler-rt's CPU feature table. SwanStation only checks it to warn about SSE2/AVX;
// the PS5's Zen 2 has feature bits 0-10 (CMOV..AVX2). (From SwanStationPS5's ps5_shims.c.)
struct ProcessorModel { unsigned vendor, type, subtype; unsigned features[1]; };
struct ProcessorModel __cpu_model = {2 /* AMD */, 0, 0, {(1u << 11) - 1}};
int __cpu_indicator_init(void) { return 0; }

// AGC imports are provided by the APP_VULKAN link stubs.

// strcasestr exists on the PS5 only in libScePosixForWebKit, the browser's library,
// which crashed SwanStationPS5 when called from a title. SDL2 uses it; defining it
// here keeps the linker from importing that module.
char* strcasestr(const char* haystack, const char* needle) {
    if (!*needle) return const_cast<char*>(haystack);
    for (; *haystack; ++haystack) {
        const char *h = haystack, *n = needle;
        while (*h && *n && std::tolower((unsigned char)*h) == std::tolower((unsigned char)*n)) ++h, ++n;
        if (!*n) return const_cast<char*>(haystack);
    }
    return nullptr;
}

// PacBrew's SDL2 supports USB keyboards and the on-screen keyboard through
// libSceKeyboard and libSceImeDialog. Importing either module stops a title from
// launching ("Can't start the game or app", found by SwanStationPS5 on hardware).
// The launcher needs neither: each stand-in reports "not supported" and SDL runs
// without a keyboard.
#define PSL_UNAVAILABLE ((int)0x80020016)
int sceKeyboardInit(void) { return PSL_UNAVAILABLE; }
int sceKeyboardOpen(int, int, int, void*) { return PSL_UNAVAILABLE; }
int sceKeyboardClose(int) { return PSL_UNAVAILABLE; }
int sceKeyboardReadState(int, void*) { return PSL_UNAVAILABLE; }
int sceImeDialogInit(const void*, void*) { return PSL_UNAVAILABLE; }
int sceImeDialogGetStatus(void) { return 0; /* SCE_IME_DIALOG_STATUS_NONE */ }
int sceImeDialogGetResult(void*) { return PSL_UNAVAILABLE; }
int sceImeDialogTerm(void) { return PSL_UNAVAILABLE; }
}

// std::bad_alloc / std::bad_array_new_length now come from the linked libc++abi.a.

#endif

// File-I/O wrapping on the APP_VULKAN link path. The platform layer's
// open_memstream wraps fclose/fflush; the build renames the layer's own
// __wrap_fclose/__wrap_fflush to ps5_memstream_fclose/fflush. This app doesn't
// intercept file I/O, so every one of these is just the real libc call.
#if defined(__PROSPERO__)
extern "C" {
int __real_fclose(FILE*);
int __real_fflush(FILE*);
int __wrap_fclose(FILE* f) { return __real_fclose(f); }
int __wrap_fflush(FILE* f) { return __real_fflush(f); }
}
#endif
