/* See license/dwm.txt for copyright and license details. */
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

void die(const char* fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
        fputc(' ', stderr);
        perror(NULL);
    } else {
        fputc('\n', stderr);
    }

    exit(1);
}

void* ecalloc(size_t nmemb, size_t size)
{
    void* p;

    if (!(p = calloc(nmemb, size)))
        die("calloc:");
    return p;
}

int fd_set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        perror("fcntl(F_GETFL):");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl(F_SETFL):");
        return -1;
    }

    return 0;
}

/* UTF-8 encodes cp into dst, 0 if it doesn't fit or isn't valid */
size_t putcp(char* dst, size_t room, unsigned long cp)
{
    if (!cp || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
        return 0;
    if (cp < 0x80 && room > 1) {
        dst[0] = (char)cp;
        return 1;
    } else if (cp < 0x800 && room > 2) {
        dst[0] = (char)(0xC0 | cp >> 6);
        dst[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000 && room > 3) {
        dst[0] = (char)(0xE0 | cp >> 12);
        dst[1] = (char)(0x80 | (cp >> 6 & 0x3F));
        dst[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp >= 0x10000 && room > 4) {
        dst[0] = (char)(0xF0 | cp >> 18);
        dst[1] = (char)(0x80 | (cp >> 12 & 0x3F));
        dst[2] = (char)(0x80 | (cp >> 6 & 0x3F));
        dst[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}
