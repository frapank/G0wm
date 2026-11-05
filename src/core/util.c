/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/util.h"

void util_die(const char* fmt, ...)
{

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
        fputc(' ', stderr);
        perror(nullptr);
    } else {
        fputc('\n', stderr);
    }

    exit(1);
}

void* util_calloc(size_t count, size_t size)
{
    void* memory = calloc(count, size);

    if (!memory)
        util_die("calloc:");
    return memory;
}

int util_set_nonblock(int fd)
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

/* UTF-8 encodes codepoint into out, 0 if it doesn't fit or isn't valid */
size_t util_put_codepoint(char* out, size_t capacity, unsigned long codepoint)
{
    if (!codepoint || (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF)
        return 0;
    if (codepoint < 0x80 && capacity > 1) {
        out[0] = (char)codepoint;
        return 1;
    } else if (codepoint < 0x800 && capacity > 2) {
        out[0] = (char)(0xC0 | codepoint >> 6);
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint < 0x10000 && capacity > 3) {
        out[0] = (char)(0xE0 | codepoint >> 12);
        out[1] = (char)(0x80 | (codepoint >> 6 & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else if (codepoint >= 0x10000 && capacity > 4) {
        out[0] = (char)(0xF0 | codepoint >> 18);
        out[1] = (char)(0x80 | (codepoint >> 12 & 0x3F));
        out[2] = (char)(0x80 | (codepoint >> 6 & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;
}
