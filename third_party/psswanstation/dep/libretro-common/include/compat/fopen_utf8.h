/* Copyright  (C) 2010-2020 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (fopen_utf8.h).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __LIBRETRO_SDK_COMPAT_FOPEN_UTF8_H
#define __LIBRETRO_SDK_COMPAT_FOPEN_UTF8_H

#ifdef _WIN32
/* Defined to error rather than fopen_utf8, to make it clear to everyone reading the code that not worrying about utf16 is fine */
/* TODO: enable */
/* #define fopen (use fopen_utf8 instead) */
void *fopen_utf8(const char * filename, const char * mode);
#else
#if defined(__PROSPERO__)
/* On PS5, fopen() checks sandbox credentials after a jailbreak and crashes.
 * Use open()+fdopen() instead, which goes through the raw syscall and bypasses
 * the Sony libc sandbox check. */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
static inline void* fopen_utf8_ps5(const char* path, const char* mode) {
    int flags = 0;
    if (mode[0] == 'r') flags = (mode[1] == '+') ? O_RDWR : O_RDONLY;
    else if (mode[0] == 'w') flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a') flags = O_WRONLY | O_CREAT | O_APPEND;
    if (mode[0] != 'r' && mode[1] == '+') flags = O_RDWR | O_CREAT | (mode[0] == 'w' ? O_TRUNC : 0);
    int fd = open(path, flags, 0644);
    if (fd < 0) return NULL;
    FILE* fp = fdopen(fd, mode);
    if (!fp) { close(fd); return NULL; }
    return fp;
}
#define fopen_utf8 fopen_utf8_ps5
#else
#define fopen_utf8 fopen
#endif
#endif
#endif
