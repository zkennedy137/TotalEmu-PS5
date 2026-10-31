/*
 * PS Launcher helper - a payload that does what the sandboxed launcher app can't.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Runs outside the app sandbox (started by the HEN's ELF loader, port 9021, or by
 * OnionHEN's payload auto-start from /data/OnionHEN/payloads). Listens on
 * 127.0.0.1:9133, one command per connection, and answers:
 *
 *   PING                      -> "OK pslauncher-helper <version>"
 *   EXISTS <path>             -> "1" or "0"
 *   LIST <path>               -> "OK" then "D name" / "F name" lines, or "ERR"
 *   READ <path>               -> "OK" then the file's bytes, or "ERR"
 *   WRITE <path>              -> body follows the command line, until the client
 *   APPEND <path>                shuts down its side; answers "OK" or "ERR"
 *   MKDIR <path>              -> "OK" or "ERR"
 *   DISCSIZE <device>         -> "OK <bytes>" or "ERR" (device: /dev/cd0, /dev/cd1...)
 *   DISCREAD <dev> <lba> <n>  -> "OK" then n * 2048 bytes of the disc, or "ERR" (n <= 64)
 *   LAUNCH <PPSAnnnnn> [arg]  -> "OK", then starts the title ~1.5 s later, after
 *                                the launcher has closed itself
 *
 * Reads are limited to /data, /mnt/usb*, /mnt/ext* and /mnt/disc; writes to
 * /data/PSLauncher. Paths containing ".." are refused. The launch call and its
 * parameter block follow SwanStationPS5's disc daemon (payload/discd.c).
 *
 * Built for Linux too (no launch, configurable roots) so the launcher and the
 * helper can be tested together on a PC.
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/types.h>
#if defined(__PROSPERO__)
#include <sys/disk.h>
#include <sys/ioctl.h>
#endif
#include <time.h>
#include <unistd.h>

#if defined(__PROSPERO__)
#include <dlfcn.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <ps5/kernel.h>
#include <ps5/klog.h>
#endif

#define HELPER_VERSION "2.9"
#define HELPER_PORT 9133
#if defined(__PROSPERO__)
static int mount_data_into_sandbox(void);  /* fwd */
static int jailbreak_pid(pid_t pid);
#endif
#define MAX_LINE 1024
#define MAX_BODY (4 * 1024 * 1024)

static const char *g_write_root = "/data/PSLauncher";
static const char *g_read_roots[8] = {"/data/", "/mnt/usb", "/mnt/ext", "/mnt/disc", NULL};
static char g_log_path[400] = "/data/PSLauncher/helper.log";
static char g_pid_path[400] = "/data/PSLauncher/helper.pid";

/* ------------------------------------------------------------------ logging */

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    char buf[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
#if defined(__PROSPERO__)
    klog_printf("[pslauncher-helper] %s\n", buf);
#endif
    fprintf(stderr, "[helper] %s\n", buf);
    FILE *f = fopen(g_log_path, "a");
    if (f)
    {
        char ts[16];
        time_t t = time(NULL);
        strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&t));
        fprintf(f, "%s %s\n", ts, buf);
        fclose(f);
    }
}

static void make_dirs(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; ++p)
    {
        if (*p != '/')
            continue;
        *p = '\0';
        mkdir(tmp, 0777);
        *p = '/';
    }
    mkdir(tmp, 0777);
}

/* ------------------------------------------------------------------ path rules */

/* any ".." path component */
static bool has_dotdot(const char *p)
{
    for (const char *s = p; (s = strstr(s, "..")); s += 2)
    {
        bool starts = s == p || s[-1] == '/';
        bool ends = s[2] == '\0' || s[2] == '/';
        if (starts && ends)
            return true;
    }
    return false;
}

static bool readable(const char *p)
{
    if (!p[0] || has_dotdot(p))
        return false;
    for (int i = 0; g_read_roots[i]; ++i)
        if (strncmp(p, g_read_roots[i], strlen(g_read_roots[i])) == 0)
            return true;
    return false;
}

static bool writable(const char *p)
{
    size_t n = strlen(g_write_root);
    return p[0] && !has_dotdot(p) && strncmp(p, g_write_root, n) == 0 && (p[n] == '/' || p[n] == '\0');
}

/* ------------------------------------------------------------------ launching */

#if defined(__PROSPERO__)
typedef struct
{
    uint32_t size;
    int user_id;
    uint32_t app_opt;
    uint64_t crash_report;
    uint64_t check_flag;
} lnc_app_param_t;
typedef int (*LaunchApp_t)(const char *, const char **, lnc_app_param_t *);
typedef int (*UsrInit_t)(void *);
typedef int (*UsrFg_t)(int *);

static LaunchApp_t p_launch;
static UsrFg_t p_fguser;

typedef struct
{
    char unused[45];
    char message[3075];
} notify_req_t;
int sceKernelSendNotificationRequest(int32_t device, void *req, size_t size, int32_t blocking);

static void notify(const char *text)
{
    static notify_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.message, sizeof(req.message), "%s", text);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static void load_launcher(void)
{
    void *h = dlopen("libSceSystemService.sprx", RTLD_NOW | RTLD_GLOBAL);
    if (h)
        p_launch = (LaunchApp_t)dlsym(h, "sceLncUtilLaunchApp");
    void *u = dlopen("libSceUserService.sprx", RTLD_NOW | RTLD_GLOBAL);
    if (u)
    {
        UsrInit_t init = (UsrInit_t)dlsym(u, "sceUserServiceInitialize");
        p_fguser = (UsrFg_t)dlsym(u, "sceUserServiceGetForegroundUser");
        if (init)
            say("sceUserServiceInitialize = 0x%08x", (unsigned)init(NULL));
    }
    say("launcher: %s", p_launch ? "sceLncUtilLaunchApp found" : "sceLncUtilLaunchApp NOT found");
}
#endif

static char g_pending_title[16];
static char g_pending_arg[MAX_LINE];
static double g_pending_at;
static int g_pending_tries;
#if defined(__PROSPERO__)
static bool g_watch_on;  /* PS5SX2 was launched: watch its controller for the return combination */
static bool g_restore_on;   /* RPCS3-5 was started with --boot: put its auto-start back when it has closed */
static bool g_restore_seen; /* RPCS3-5 has been seen running */
static double g_restore_at; /* when it was started */
static double g_restore_next;
#endif

static double now_s(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + tv.tv_usec / 1e6;
}

static bool valid_title(const char *t)
{
    if (strlen(t) != 9 || strncmp(t, "PPSA", 4) != 0)
        return false;
    for (int i = 4; i < 9; ++i)
        if (!isdigit((unsigned char)t[i]))
            return false;
    return true;
}

/* A disc drive, "/dev/cdN", which RPCS3-5 boots with --boot. */
static bool disc_drive(const char *p)
{
    return strncmp(p, "/dev/cd", 7) == 0 && isdigit((unsigned char)p[7]) && p[8] == '\0';
}

/* Returns true when finished (launched or gave up). */
static bool try_launch(void)
{
#if defined(__PROSPERO__)
    if (!p_launch)
    {
        say("launch %s: no launch function", g_pending_title);
        return true;
    }
    int user = -1;
    if (p_fguser)
        p_fguser(&user);
    lnc_app_param_t prm;
    memset(&prm, 0, sizeof(prm));
    prm.size = sizeof(prm);
    prm.user_id = user;
    /* "--boot <path>" is two arguments (KB-RPCS3 / RPCS3-5 read --boot <path>); anything else is one. */
    const bool boot = strncmp(g_pending_arg, "--boot ", 7) == 0;
    const char *boot_argv[] = {"--boot", boot ? g_pending_arg + 7 : "", NULL};
    const char *one_argv[] = {g_pending_arg, NULL};
    int rc = p_launch(g_pending_title, boot ? boot_argv : g_pending_arg[0] ? one_argv : NULL, &prm);
    say("sceLncUtilLaunchApp(%s, user %d, %s) = 0x%08x (try %d)", g_pending_title, user,
        g_pending_arg[0] ? g_pending_arg : "no args", (unsigned)rc, g_pending_tries + 1);
    /* Errors have the top bit set (0x8xxxxxxx). Anything else was accepted, so the title is starting:
     * do not call again (the 2.4 retries started and closed the title several times). */
    if (rc == 0 || ((unsigned)rc & 0x80000000u) == 0)
        return true;
#else
    say("[desktop] would launch %s %s", g_pending_title, g_pending_arg);
    return true;
#endif
    return ++g_pending_tries >= 8; /* the launcher may still be closing: try again */
}

/* ------------------------------------------------------------------ the connection */

static bool send_all(int s, const void *data, size_t n)
{
    const char *p = (const char *)data;
    while (n > 0)
    {
        ssize_t w = send(s, p, n, 0);
        if (w <= 0)
            return false;
        p += w;
        n -= (size_t)w;
    }
    return true;
}

static void reply(int s, const char *text)
{
    send_all(s, text, strlen(text));
}

/* Reads the command line; anything after the newline is the start of the body. */
static bool read_command(int s, char *line, char *extra, size_t *extra_len)
{
    size_t got = 0;
    *extra_len = 0;
    while (got < MAX_LINE - 1)
    {
        ssize_t n = recv(s, line + got, MAX_LINE - 1 - got, 0);
        if (n <= 0)
            break;
        got += (size_t)n;
        char *nl = memchr(line, '\n', got);
        if (nl)
        {
            size_t used = (size_t)(nl - line) + 1;
            *extra_len = got - used;
            memcpy(extra, nl + 1, *extra_len);
            *nl = '\0';
            if (nl > line && nl[-1] == '\r')
                nl[-1] = '\0';
            return true;
        }
    }
    line[got] = '\0';
    return got > 0;
}

static void do_list(int s, const char *path)
{
    DIR *d = opendir(path);
    if (!d)
    {
        reply(s, "ERR\n");
        return;
    }
    reply(s, "OK\n");
    struct dirent *e;
    char row[600];
    while ((e = readdir(d)))
    {
        if (e->d_name[0] == '.')
            continue;
        char full[1100];
        snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        struct stat st;
        bool dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        snprintf(row, sizeof(row), "%c %s\n", dir ? 'D' : 'F', e->d_name);
        send_all(s, row, strlen(row));
    }
    closedir(d);
}

static void do_read(int s, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        reply(s, "ERR\n");
        return;
    }
    reply(s, "OK\n");
    char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        if (!send_all(s, buf, n))
            break;
    fclose(f);
}

/* ------------------------------------------------------------------ optical discs
 * DISCSIZE <device>             -> "OK <bytes>\n", or "ERR\n"
 * DISCREAD <device> <lba> <n>   -> "OK\n" then n * 2048 bytes of user data, or "ERR\n"
 * Only the console's optical drives (/dev/cd0, /dev/cd1...) are read, at most 64 sectors a request.
 * The title's sandbox has no such device nodes, so RPCS3 reads its discs through these. */
#define DISC_SECTOR 2048
#define DISC_MAX_SECTORS 64

static bool disc_device_ok(const char *dev)
{
    return strncmp(dev, "/dev/cd", 7) == 0 && isdigit((unsigned char)dev[7]) && dev[8] == '\0';
}

static void do_disc_size(int s, const char *dev)
{
    if (!disc_device_ok(dev))
    {
        reply(s, "ERR\n");
        return;
    }
#if defined(__PROSPERO__)
    int fd = open(dev, O_RDONLY);
    off_t bytes = 0;
    if (fd < 0 || ioctl(fd, DIOCGMEDIASIZE, &bytes) != 0 || bytes <= 0)
    {
        if (fd >= 0)
            close(fd);
        reply(s, "ERR\n");
        return;
    }
    close(fd);
    char text[48];
    snprintf(text, sizeof(text), "OK %lld\n", (long long)bytes);
    reply(s, text);
#else
    (void)dev;
    reply(s, "ERR\n");
#endif
}

static void do_disc_read(int s, const char *dev, unsigned long long lba, unsigned long count)
{
    static char buf[DISC_SECTOR * DISC_MAX_SECTORS];
    if (!disc_device_ok(dev) || count == 0 || count > DISC_MAX_SECTORS)
    {
        reply(s, "ERR\n");
        return;
    }
#if defined(__PROSPERO__)
    int fd = open(dev, O_RDONLY);
    size_t want = (size_t)count * DISC_SECTOR;
    ssize_t got = fd < 0 ? -1 : pread(fd, buf, want, (off_t)(lba * DISC_SECTOR));
    if (fd >= 0)
        close(fd);
    if (got != (ssize_t)want)
    {
        reply(s, "ERR\n");
        return;
    }
    reply(s, "OK\n");
    send_all(s, buf, want);
#else
    (void)lba;
    reply(s, "ERR\n");
#endif
}

static void do_write(int s, const char *path, const char *extra, size_t extra_len, bool append)
{
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(append ? path : tmp, append ? "ab" : "wb");
    if (!f)
    {
        reply(s, "ERR\n");
        return;
    }
    size_t total = extra_len;
    bool ok = fwrite(extra, 1, extra_len, f) == extra_len;
    char buf[16384];
    ssize_t n;
    while (ok && (n = recv(s, buf, sizeof(buf), 0)) > 0)
    {
        total += (size_t)n;
        ok = total <= MAX_BODY && fwrite(buf, 1, (size_t)n, f) == (size_t)n;
    }
    ok = (fclose(f) == 0) && ok;
    if (!append)
        ok = ok && rename(tmp, path) == 0; /* write then rename: never half a file */
    if (!ok && !append)
        unlink(tmp);
    reply(s, ok ? "OK\n" : "ERR\n");
}

/* ------------------------------------------------------------------ game files
 * FILESIZE <path>               -> "OK <bytes>\n", or "ERR\n"
 * FILEREAD <path> <off> <n>     -> "OK <sent>\n" then <sent> bytes, or "ERR\n"
 * Read-only, and only under GAME_FILES_ROOT (the PS3 games PS Launcher installed or copied). A FILEREAD sends
 * at most FILE_MAX_REQUEST bytes, streamed through a 64 KB buffer. If the connection breaks after the "OK", the
 * client gets fewer bytes than <sent> and must treat the read as failed. */
#define GAME_FILES_ROOT "/data/PSLauncher/games/"
#define FILE_MAX_REQUEST (16ull * 1024 * 1024)
#define FILE_STREAM_BUF (64u * 1024)

static bool game_file_ok(const char *p)
{
    return strncmp(p, GAME_FILES_ROOT, strlen(GAME_FILES_ROOT)) == 0 && !has_dotdot(p);
}

static void do_file_size(int s, const char *path)
{
    if (!game_file_ok(path))
    {
        reply(s, "ERR\n");
        return;
    }
#if defined(__PROSPERO__)
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
    {
        if (fd >= 0)
            close(fd);
        reply(s, "ERR\n");
        return;
    }
    close(fd);
    char text[48];
    snprintf(text, sizeof(text), "OK %lld\n", (long long)st.st_size);
    reply(s, text);
#else
    (void)path;
    reply(s, "ERR\n");
#endif
}

static void do_file_read(int s, const char *path, unsigned long long offset, unsigned long long count)
{
    static char buf[FILE_STREAM_BUF];
    if (!game_file_ok(path) || count > FILE_MAX_REQUEST)
    {
        reply(s, "ERR\n");
        return;
    }
#if defined(__PROSPERO__)
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
    {
        if (fd >= 0)
            close(fd);
        reply(s, "ERR\n");
        return;
    }
    const unsigned long long size = (unsigned long long)st.st_size;
    unsigned long long left = 0;
    if (offset < size)
        left = (size - offset) < count ? (size - offset) : count;
    char head[48];
    snprintf(head, sizeof(head), "OK %llu\n", left);
    reply(s, head);
    unsigned long long pos = offset;
    while (left > 0)
    {
        const size_t want = left < sizeof(buf) ? (size_t)left : sizeof(buf);
        const ssize_t got = pread(fd, buf, want, (off_t)pos);
        if (got <= 0 || !send_all(s, buf, (size_t)got))
            break;
        pos += (unsigned long long)got;
        left -= (unsigned long long)got;
    }
    close(fd);
#else
    (void)offset;
    reply(s, "ERR\n");
#endif
}

static void handle(int s)
{
    static char line[MAX_LINE];
    static char extra[MAX_LINE];
    size_t extra_len = 0;
    struct timeval tv = {3, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (!read_command(s, line, extra, &extra_len))
        return;

    char *arg = strchr(line, ' ');
    if (arg)
        *arg++ = '\0';
    else
        arg = line + strlen(line);
    const char *cmd = line;

    if (strcmp(cmd, "PING") == 0)
    {
#if defined(__PROSPERO__)
        /* On first PING the app's sandbox exists; mount /data into it. */
        static int sandbox_mounted = 0;
        if (!sandbox_mounted)
            sandbox_mounted = (mount_data_into_sandbox() == 0) ? 1 : 0;
#endif
        reply(s, "OK pslauncher-helper " HELPER_VERSION "\n");
    }
    else if (strcmp(cmd, "EXISTS") == 0)
    {
        struct stat st;
        reply(s, readable(arg) && stat(arg, &st) == 0 ? "1\n" : "0\n");
    }
    else if (strcmp(cmd, "LIST") == 0)
    {
        if (readable(arg))
            do_list(s, arg);
        else
            reply(s, "ERR\n");
    }
    else if (strcmp(cmd, "READ") == 0)
    {
        if (readable(arg))
            do_read(s, arg);
        else
            reply(s, "ERR\n");
    }
    else if (strcmp(cmd, "DISCSIZE") == 0)
        do_disc_size(s, arg);
    else if (strcmp(cmd, "DISCREAD") == 0)
    {
        char dev[32];
        unsigned long long lba = 0;
        unsigned long count = 0;
        if (sscanf(arg, "%31s %llu %lu", dev, &lba, &count) == 3)
            do_disc_read(s, dev, lba, count);
        else
            reply(s, "ERR\n");
    }
    else if (strcmp(cmd, "FILESIZE") == 0)
        do_file_size(s, arg);
    else if (strcmp(cmd, "FILEREAD") == 0)
    {
        /* the path may contain spaces: the offset and the count are the last two fields */
        char *count_text = strrchr(arg, ' ');
        char *offset_text = NULL;
        if (count_text)
        {
            *count_text++ = '\0';
            offset_text = strrchr(arg, ' ');
            if (offset_text)
                *offset_text++ = '\0';
        }
        char *offset_end = NULL;
        char *count_end = NULL;
        const unsigned long long offset = offset_text ? strtoull(offset_text, &offset_end, 10) : 0;
        const unsigned long long count = count_text ? strtoull(count_text, &count_end, 10) : 0;
        if (offset_text && count_text && *offset_text && *count_text && *offset_end == '\0' && *count_end == '\0')
            do_file_read(s, arg, offset, count);
        else
            reply(s, "ERR\n");
    }
    else if (strcmp(cmd, "WRITE") == 0 || strcmp(cmd, "APPEND") == 0)
    {
        if (writable(arg))
            do_write(s, arg, extra, extra_len, cmd[0] == 'A');
        else
        {
            say("refused %s %s (outside %s)", cmd, arg, g_write_root);
            reply(s, "ERR\n");
        }
    }
    else if (strcmp(cmd, "MKDIR") == 0)
    {
        if (writable(arg))
        {
            make_dirs(arg);
            reply(s, "OK\n");
        }
        else
            reply(s, "ERR\n");
    }
    else if (strcmp(cmd, "JAILBREAK") == 0)
    {
        /* JAILBREAK <pid>  — elevates the target process so it can read /data. */
#if defined(__PROSPERO__)
        pid_t target = arg ? (pid_t)atoi(arg) : 0;
        if (target > 0 && jailbreak_pid(target) == 0)
            reply(s, "OK\n");
        else
            reply(s, "ERR\n");
#else
        reply(s, "OK\n");  /* desktop: no-op */
#endif
    }
    else if (strcmp(cmd, "LAUNCH") == 0)
    {
        char *game = strchr(arg, ' ');
        if (game)
            *game++ = '\0';
        /* a path after "--boot " is checked as a path */
        const char *gamePath = game && strncmp(game, "--boot ", 7) == 0 ? game + 7 : game;
        if (!valid_title(arg) || (gamePath && gamePath[0] && !readable(gamePath) && !disc_drive(gamePath)))
        {
            say("refused LAUNCH %s", arg);
            reply(s, "ERR\n");
            return;
        }
#if defined(__PROSPERO__)
        if (strcmp(arg, "PPSA99203") == 0)
            g_watch_on = true;  /* PS5SX2 is starting: watch for the return combination */
        if (strcmp(arg, "PPSA99303") == 0 && game && strncmp(game, "--boot ", 7) == 0)
        {
            g_restore_on = true;  /* RPCS3-5 is starting a game: its auto-start goes back on when it closes */
            g_restore_seen = false;
            g_restore_at = now_s();
            g_restore_next = 0;
        }
#endif
        snprintf(g_pending_title, sizeof(g_pending_title), "%s", arg);
        snprintf(g_pending_arg, sizeof(g_pending_arg), "%s", game ? game : "");
        g_pending_at = now_s() + 1.5; /* time for the launcher to close itself */
        g_pending_tries = 0;
        say("queued launch of %s %s", g_pending_title, g_pending_arg);
        reply(s, "OK\n");
    }
    else
        reply(s, "ERR unknown command\n");
}

#if defined(__PROSPERO__)
/* ------------------------------------------------------------------ the controller watch
 * While PS5SX2 runs (started by LAUNCH PPSA99203), holding L2 + R2 + touchpad for two seconds
 * closes PS5SX2 and brings PS Launcher back to the front. PS5SX2 stamps logs/ps5sx2-alive.txt every 2 s while it
 * runs, so the controller is read only while that stamp is fresh. The calls and the button
 * bits are the ones PS5SX2 uses (its ps5/coreorbis/orbis-shims: ProsperoUsbPad.cpp, OrbisPadMap.h). */
typedef int (*PadInit_t)(void);
typedef int (*PadOpen_t)(int32_t, int32_t, int32_t, const void *);
typedef int (*PadHandle_t)(int32_t, int32_t, int32_t);
typedef int (*PadRead_t)(int32_t, void *);
static PadInit_t p_padinit;
static PadOpen_t p_padopen;
static PadHandle_t p_padhandle;
static PadRead_t p_padread;
static int g_pad_handle = -1;
static bool g_watch_held;
static double g_watch_since;
static bool g_watch_logged_read;
#define WATCH_BUTTONS (0x00000100u | 0x00000200u | 0x00100000u) /* L2, R2, touchpad click */
#define WATCH_HOLD_S 2.0
#define WATCH_ALIVE "/data/PCSX2/logs/ps5sx2-alive.txt"

static void pad_load(void)
{
    void *h = dlopen("libScePad.sprx", RTLD_NOW | RTLD_GLOBAL);
    if (!h)
    {
        say("pad: libScePad.sprx not loaded (%s)", dlerror());
        return;
    }
    p_padinit = (PadInit_t)dlsym(h, "scePadInit");
    p_padopen = (PadOpen_t)dlsym(h, "scePadOpen");
    p_padhandle = (PadHandle_t)dlsym(h, "scePadGetHandle");
    p_padread = (PadRead_t)dlsym(h, "scePadReadState");
    if (!p_padinit || !p_padopen || !p_padhandle || !p_padread)
    {
        say("pad: a ScePad function is missing");
        return;
    }
    say("pad: scePadInit = 0x%08x", (unsigned)p_padinit());
}

/* PS5SX2's stamp is fresh: the app is running. */
static bool ps5sx2_running(void)
{
    struct stat st;
    return stat(WATCH_ALIVE, &st) == 0 && time(NULL) - st.st_mtime < 10;
}

static int pad_handle(void)
{
    if (g_pad_handle >= 0 || !p_padread || !p_padopen)
        return g_pad_handle;
    int32_t user = -1;
    if (p_fguser)
        p_fguser(&user);
    if (user < 0)
        return -1;
    int h = p_padopen(user, 0, 0, NULL);
    if (h < 0)
        h = p_padhandle(user, 0, 0);
    if (h >= 0)
    {
        g_pad_handle = h;
        say("pad: handle %d for user %d", h, (int)user);
    }
    return g_pad_handle;
}

/* The app's title, from sceKernelGetAppInfo (as the sandbox elevator reads it). */
typedef struct
{
    uint32_t app_id;
    uint64_t unknown1;
    char title_id[14];
    char unknown2[0x3c];
} WatchAppInfo;
extern int sceKernelGetAppInfo(pid_t pid, WatchAppInfo *info);

/* The processes of a title: up to max of them, in out. */
static int title_pids(const char *title, pid_t *out, int max)
{
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t len = 0;
    if (sysctl(mib, 4, NULL, &len, NULL, 0) != 0 || len == 0)
        return 0;
    struct kinfo_proc *procs = malloc(len);
    if (!procs)
        return 0;
    int n = 0;
    if (sysctl(mib, 4, procs, &len, NULL, 0) == 0)
    {
        for (size_t i = 0; i < len / sizeof(*procs) && n < max; ++i)
        {
            WatchAppInfo info;
            memset(&info, 0, sizeof(info));
            if (sceKernelGetAppInfo(procs[i].ki_pid, &info) == 0 && strncmp(info.title_id, title, 9) == 0)
                out[n++] = procs[i].ki_pid;
        }
    }
    free(procs);
    return n;
}

/* Ends a title: SIGTERM, up to three seconds for it to leave, then SIGKILL. */
static void close_title(const char *title)
{
    pid_t pids[16];
    const int n = title_pids(title, pids, 16);
    if (n == 0)
    {
        say("close %s: not running", title);
        return;
    }
    say("close %s: %d process(es), SIGTERM", title, n);
    for (int i = 0; i < n; ++i)
        kill(pids[i], SIGTERM);
    for (int wait = 0; wait < 30; ++wait)
    {
        bool alive = false;
        for (int i = 0; i < n; ++i)
            if (kill(pids[i], 0) == 0)
                alive = true;
        if (!alive)
            break;
        usleep(100000);
    }
    for (int i = 0; i < n; ++i)
        if (kill(pids[i], 0) == 0)
        {
            say("close %s: pid %d did not stop, SIGKILL", title, (int)pids[i]);
            kill(pids[i], SIGKILL);
        }
}

static void pad_poll(void)
{
    if (!g_watch_on || !p_padread)
        return;
    if (!ps5sx2_running())
    {
        g_watch_held = false;  /* the app is closed: nothing to watch */
        return;
    }
    const int h = pad_handle();
    if (h < 0)
        return;
    /* ScePadData: the button mask first, then the sticks and triggers (PS5SX2 reads the same layout). */
    uint8_t data[512];
    memset(data, 0, sizeof(data));
    const int rc = p_padread(h, data);
    if (!g_watch_logged_read)
    {
        g_watch_logged_read = true;
        say("pad: first read rc=0x%08x", (unsigned)rc);
    }
    uint32_t buttons;
    memcpy(&buttons, data, sizeof(buttons));
    const bool held = (buttons & WATCH_BUTTONS) == WATCH_BUTTONS;
    const double t = now_s();
    if (held && !g_watch_held)
    {
        g_watch_held = true;
        g_watch_since = t;
        say("pad: L2 + R2 + touchpad held");
    }
    else if (!held && g_watch_held)
    {
        g_watch_held = false;
        say("pad: released before %.1f s", WATCH_HOLD_S);
    }
    if (held && t - g_watch_since >= WATCH_HOLD_S)
    {
        say("pad: held %.1f s: closing PS5SX2 and bringing PS Launcher back", t - g_watch_since);
        g_watch_on = false;
        g_watch_held = false;
        close_title("PPSA99203");
        snprintf(g_pending_title, sizeof(g_pending_title), "%s", "PPSA97500");
        g_pending_arg[0] = '\0';
        g_pending_at = now_s() + 1.0;  /* PS5SX2 has gone: now start PS Launcher */
        g_pending_tries = 0;
    }
}
#endif

/* ------------------------------------------------------------------ RPCS3-5's auto-start
 * PS Launcher turns off "Automatically start games after boot" in RPCS3-5's config before it starts
 * RPCS3-5 with a game. When RPCS3-5 has closed, the helper turns it back on. */
#define RPCS3_CONFIG "/data/homebrew/PPSA99303/rpcs3/config.yml"

static void restore_autoboot(void)
{
    static char text[65536];
    FILE *f = fopen(RPCS3_CONFIG, "rb");
    if (!f)
    {
        say("restore: %s not found", RPCS3_CONFIG);
        return;
    }
    const size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    const char *off = "Automatically start games after boot: false";
    const char *on = "Automatically start games after boot: true";
    char *at = strstr(text, off);
    if (!at)
    {
        say("restore: the auto-start setting is already on, or not there");
        return;
    }
    /* "false" is one character longer than "true": move the rest of the file along */
    const size_t offLen = strlen(off), onLen = strlen(on);
    memmove(at + onLen, at + offLen, strlen(at + offLen) + 1);
    memcpy(at, on, onLen);
    const size_t length = strlen(text);
    f = fopen(RPCS3_CONFIG, "wb");
    if (!f)
    {
        say("restore: could not write %s", RPCS3_CONFIG);
        return;
    }
    fwrite(text, 1, length, f);
    fclose(f);
    say("restore: RPCS3-5 starts games after boot again");
}

static void restore_poll(void)
{
    if (!g_restore_on)
        return;
    const double t = now_s();
    if (t < g_restore_next)
        return;
    g_restore_next = t + 2.0;
    pid_t pids[16];
    const int running = title_pids("PPSA99303", pids, 16);
    if (running > 0)
        g_restore_seen = true;
    if ((g_restore_seen && running == 0) || t - g_restore_at > 300.0)
    {
        g_restore_on = false;
        restore_autoboot();
    }
}

/* ------------------------------------------------------------------ one helper at a time */

#if defined(__PROSPERO__)
/* The process with this pid, if it is still there: its start time is what tells it apart from a later
 * process that got the same pid. Payload Manager runs every payload as "payload.elf", so the name cannot. */
static int proc_info(pid_t pid, struct kinfo_proc *out)
{
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
    size_t len = sizeof(*out);
    memset(out, 0, sizeof(*out));
    return sysctl(mib, 4, out, &len, NULL, 0) == 0 && len > 0 && out->ki_pid == pid;
}
#endif

/* helper.pid: "<pid> <start seconds> <start microseconds>" (2.5 wrote only the pid). */
static void take_over(void)
{
    FILE *f = fopen(g_pid_path, "r");
    int old = 0;
    long long sec = -1, usec = -1;
    if (f)
    {
        const int got = fscanf(f, "%d %lld %lld", &old, &sec, &usec);
        fclose(f);
        if (got >= 1 && old > 0 && old != (int)getpid())
        {
#if defined(__PROSPERO__)
            struct kinfo_proc kp;
            if (proc_info((pid_t)old, &kp))
            {
                if (got == 3)
                {
                    /* the same process only if both the pid and its start time match */
                    if (kp.ki_start.tv_sec == sec && kp.ki_start.tv_usec == usec)
                    {
                        say("stopping the previous helper (pid %d)", old);
                        kill((pid_t)old, SIGKILL);
                        usleep(300000); /* let it release the port */
                    }
                    else
                        say("pid %d now belongs to another process: not stopped", old);
                }
                else if (strstr(kp.ki_comm, "payload") || strstr(kp.ki_comm, "helper"))
                {
                    /* a 2.5 helper.pid has no start time: stop it once, by name, as 2.5 did */
                    say("stopping the previous helper (pid %d, %s, no start time on record)", old, kp.ki_comm);
                    kill((pid_t)old, SIGKILL);
                    usleep(300000);
                }
            }
#endif
        }
    }
    f = fopen(g_pid_path, "w");
    if (f)
    {
#if defined(__PROSPERO__)
        struct kinfo_proc self;
        if (proc_info(getpid(), &self))
            fprintf(f, "%d %lld %lld\n", (int)getpid(), (long long)self.ki_start.tv_sec, (long long)self.ki_start.tv_usec);
        else
#endif
            fprintf(f, "%d\n", (int)getpid());
        fclose(f);
    }
}

/* Memory, to size the disc and file read buffers: the PS5 kernel's own memory calls (the BSD VM counters
 * are not on the console). The flexible budget is what anonymous mappings and libc draw on. */
static double g_mem_next;
static bool g_mem_resolved;
#if defined(__PROSPERO__)
static int32_t (*p_flex_available)(size_t *);
static int32_t (*p_flex_configured)(size_t *);
static int64_t (*p_direct_size)(void);

static void mem_resolve(void)
{
    if (g_mem_resolved)
        return;
    g_mem_resolved = true;
    void *k = dlopen("libkernel.sprx", RTLD_NOW | RTLD_GLOBAL);
    if (!k)
    {
        say("memory: libkernel.sprx could not be opened");
        return;
    }
    p_flex_available = (int32_t (*)(size_t *))dlsym(k, "sceKernelAvailableFlexibleMemorySize");
    p_flex_configured = (int32_t (*)(size_t *))dlsym(k, "sceKernelConfiguredFlexibleMemorySize");
    p_direct_size = (int64_t (*)(void))dlsym(k, "sceKernelGetDirectMemorySize");
    say("memory: calls found: available %s, configured %s, direct %s", p_flex_available ? "yes" : "no",
        p_flex_configured ? "yes" : "no", p_direct_size ? "yes" : "no");
}
#endif

static void mem_log(void)
{
#if defined(__PROSPERO__)
    mem_resolve();
    size_t configured = 0, available = 0;
    if (p_flex_configured)
    {
        const int32_t rc = p_flex_configured(&configured);
        if (rc == 0)
            say("memory: flexible budget %zu MB", configured >> 20);
        else
            say("memory: flexible budget call returned 0x%08x", (unsigned)rc);
    }
    if (p_flex_available)
    {
        const int32_t rc = p_flex_available(&available);
        if (rc == 0)
            say("memory: flexible available %zu MB", available >> 20);
        else
            say("memory: flexible available call returned 0x%08x", (unsigned)rc);
    }
    if (p_direct_size)
        say("memory: direct memory %lld MB", (long long)(p_direct_size() >> 20));
#endif
}

static int open_server(void)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
#if defined(__PROSPERO__)
    a.sin_len = sizeof(a);
#endif
    a.sin_family = AF_INET;
    a.sin_port = htons(HELPER_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* this console only */
    for (int tries = 0; tries < 20; ++tries)
    {
        if (bind(s, (struct sockaddr *)&a, sizeof(a)) == 0 && listen(s, 8) == 0)
            return s;
        usleep(250000);
    }
    close(s);
    return -1;
}


/* ---------- nullfs sandbox mount ----------------------------------------
 * Mounts /data into the running PSLauncher sandbox so the emulator core can
 * open game and BIOS files directly. Scans /mnt/sandbox/ for the folder
 * whose app0/sce_sys/param.json contains our title ID, then nmount()s /data
 * into that folder's data/ subdirectory (creating it if needed).
 * FreeBSD nullfs: the mounted path appears read-write to the app but the
 * helper's own write restrictions still apply (the app uses the helper for
 * writes, not direct file access).
 * -------------------------------------------------------------------------*/
#define SANDBOX_BASE    "/mnt/sandbox"
#define TARGET_TITLE    "PPSA97500"

static void iov_set(struct iovec *iov, const char *name, const void *val, size_t len)
{
    iov->iov_base = (void *)name;
    iov->iov_len  = strlen(name) + 1;
    (iov+1)->iov_base = (void *)val;
    (iov+1)->iov_len  = len;
}

#if defined(__PROSPERO__)
static int mount_data_into_sandbox(void)
{
    DIR *d = opendir(SANDBOX_BASE);
    if (!d) { say("sandbox: can't open " SANDBOX_BASE " (%s)", strerror(errno)); return -1; }

    struct dirent *de;
    char param[300], data_dir[300];
    int mounted = 0;
    while ((de = readdir(d)) != NULL && !mounted) {
        if (de->d_name[0] == '.') continue;
        /* Read the param.json to find which title this sandbox belongs to. */
        snprintf(param, sizeof param, SANDBOX_BASE "/%s/app0/sce_sys/param.json",
                 de->d_name);
        FILE *f = fopen(param, "r");
        if (!f) continue;
        char buf[4096] = {0};
        fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        if (!strstr(buf, TARGET_TITLE)) continue;

        /* Found PSLauncher's sandbox. Create the mount point and mount. */
        snprintf(data_dir, sizeof data_dir, SANDBOX_BASE "/%s/data", de->d_name);
        mkdir(data_dir, 0755);

        /* FreeBSD nmount: fstype=nullfs, fspath=target, from=source */
        struct iovec iov[8];
        const char *fstype = "nullfs";
        const char *from   = "/data";
        iov_set(&iov[0], "fstype",  fstype,    strlen(fstype)+1);
        iov_set(&iov[2], "fspath",  data_dir,  strlen(data_dir)+1);
        iov_set(&iov[4], "from",    from,      strlen(from)+1);
        iov_set(&iov[6], "errmsg",  NULL,      0);
        iov[7].iov_base = NULL; iov[7].iov_len = 0;

        int r = nmount(iov, 8, 0);
        if (r == 0) {
            say("sandbox: mounted /data -> %s", data_dir);
            mounted = 1;
        } else {
            say("sandbox: nmount failed for %s (%s)", data_dir, strerror(errno));
        }
    }
    closedir(d);
    if (!mounted) say("sandbox: no " TARGET_TITLE " sandbox found in " SANDBOX_BASE);
    return mounted ? 0 : -1;
}


/* ---------- process jailbreak -------------------------------------------
 * Elevates the target process (PSLauncher) the same way Lapy does:
 *  - authid  → 0x4800000000010003 (jailbreak auth id)
 *  - caps    → all 0xff (full capabilities)
 *  - attrs   → all 0 with byte 0x83 set to 0x80 (unsandboxed)
 *  - fd_rdir → rootvnode (escape the filesystem jail)
 *  - fd_jdir → rootvnode
 * The SDK's kernel_* functions work on any PID, not just getpid().
 * -------------------------------------------------------------------------*/
static int jailbreak_pid(pid_t pid)
{
    say("jailbreak: starting on pid %d", (int)pid);
    uint8_t caps[16], attrs[32];
    memset(caps, 0xff, sizeof(caps));
    memset(attrs, 0x00, sizeof(attrs));
    attrs[0] = 0x80;  /* cr_sceAttr[0]: unsandboxed */

    int32_t rc;
    int r = 0;

    rc = kernel_set_ucred_authid(pid, 0x4800000000010003ull);
    say("jailbreak: authid rc=%d", (int)rc); r |= rc;

    rc = kernel_set_ucred_caps(pid, caps);
    say("jailbreak: caps rc=%d", (int)rc); r |= rc;

    rc = kernel_set_ucred_attrs(pid, attrs);
    say("jailbreak: attrs rc=%d", (int)rc); r |= rc;

    /* Get rootvnode and filedesc before changing UID to avoid any permission issues. */
    intptr_t rootvnode = kernel_get_root_vnode();
    intptr_t fd = kernel_get_proc_filedesc(pid);
    say("jailbreak: rootvnode=0x%llx fd=0x%llx rdir_off=%lld jdir_off=%lld",
        (unsigned long long)rootvnode, (unsigned long long)fd,
        (long long)KERNEL_OFFSET_FILEDESC_FD_RDIR,
        (long long)KERNEL_OFFSET_FILEDESC_FD_JDIR);

    if (fd && rootvnode) {
        rc = kernel_copyin(&rootvnode, fd + KERNEL_OFFSET_FILEDESC_FD_RDIR, sizeof(rootvnode));
        say("jailbreak: rdir rc=%d", (int)rc); r |= rc;
        rc = kernel_copyin(&rootvnode, fd + KERNEL_OFFSET_FILEDESC_FD_JDIR, sizeof(rootvnode));
        say("jailbreak: jdir rc=%d", (int)rc); r |= rc;
    } else {
        say("jailbreak: null fd or rootvnode, skipping jail escape");
        r = -1;
    }

    if (r == 0)
        say("jailbreak: pid %d ok", (int)pid);
    else
        say("jailbreak: pid %d FAILED r=%d", (int)pid, r);
    return r;
}
#endif /* __PROSPERO__ */

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
#if defined(__PROSPERO__)
    uint8_t caps[16];
    memset(caps, 0xff, sizeof(caps));
    kernel_set_ucred_authid(getpid(), 0x4800000000010003ull);
    kernel_set_ucred_caps(getpid(), caps);
#else
    /* Desktop testing: PSL_HELPER_ROOT replaces /data (e.g. ./testdata). */
    static char data_root[300], write_root[320];
    const char *r = getenv("PSL_HELPER_ROOT");
    if (r)
    {
        snprintf(data_root, sizeof(data_root), "%s/", r);
        snprintf(write_root, sizeof(write_root), "%s/data", r);
        g_read_roots[0] = data_root;
        g_read_roots[1] = NULL;
        g_write_root = write_root;
        snprintf(g_log_path, sizeof(g_log_path), "%s/helper.log", write_root);
        snprintf(g_pid_path, sizeof(g_pid_path), "%s/helper.pid", write_root);
    }
#endif
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    make_dirs(g_write_root);
    say("PS Launcher helper %s, pid %d", HELPER_VERSION, (int)getpid());
    take_over();
#if defined(__PROSPERO__)
    load_launcher();
    pad_load();
    /* Mount /data into PSLauncher's sandbox so the emulator core can read
     * game and BIOS files directly without going through the helper. */
    mount_data_into_sandbox();
#endif

    int server = open_server();
    if (server < 0)
    {
        say("can't listen on 127.0.0.1:%d (%s)", HELPER_PORT, strerror(errno));
        return 1;
    }
    say("listening on 127.0.0.1:%d", HELPER_PORT);
#if defined(__PROSPERO__)
    notify("PS Launcher helper is running");
#endif

    for (;;)
    {
        /* another helper took over: stop */
        FILE *f = fopen(g_pid_path, "r");
        int p = 0;
        if (f)
        {
            if (fscanf(f, "%d", &p) == 1 && p > 0 && p != (int)getpid())
            {
                fclose(f);
                say("another helper took over: stopping");
                break;
            }
            fclose(f);
        }

        if (now_s() >= g_mem_next)
        {
            mem_log();
            g_mem_next = now_s() + 10.0;
        }

        if (g_pending_title[0] && now_s() >= g_pending_at)
        {
            if (try_launch())
                g_pending_title[0] = '\0';
            else
                g_pending_at = now_s() + 0.5;
        }

        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(server, &rd);
#if defined(__PROSPERO__)
        pad_poll();
        restore_poll();
#endif
        struct timeval tv = {0, 100000};
        if (select(server + 1, &rd, NULL, NULL, &tv) > 0)
        {
            int c = accept(server, NULL, NULL);
            if (c >= 0)
            {
                handle(c);
                close(c);
            }
        }
    }
    close(server);
    return 0;
}
