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

/* Copies src into dst keeping only whole, well-formed UTF-8 codepoints:
 * control characters become spaces, invalid bytes are dropped and a sequence
 * that doesn't fit is left out entirely. Anything on this path comes straight
 * from an arbitrary bus client, and half a codepoint renders as garbage. */
void util_sanitize(char* dst, size_t dstsz, const char* src)
{
    size_t written = 0;

    if (!dstsz)
        return;
    size_t sequence_length;
    for (unsigned char byte; src && (byte = (unsigned char)*src); src += sequence_length) {
        sequence_length = byte < 0x80   ? 1
                          : byte < 0xC2 ? 0 /* continuation byte or overlong lead */
                          : byte < 0xE0 ? 2
                          : byte < 0xF0 ? 3
                          : byte < 0xF5 ? 4
                                        : 0;
        if (!sequence_length) {
            sequence_length = 1;
            continue;
        }
        size_t valid_length = 1;
        while (valid_length < sequence_length && ((unsigned char)src[valid_length] & 0xC0) == 0x80)
            valid_length++;
        if (valid_length <
            sequence_length) { /* truncated sequence: drop the lead byte and resync */
            sequence_length = 1;
            continue;
        }
        if (written + sequence_length >= dstsz)
            break;
        if (byte < ' ' || byte == 0x7f) {
            dst[written++] = ' '; /* sequence_length is 1 here */
            continue;
        }
        for (size_t offset = 0; offset < sequence_length; offset++)
            dst[written++] = (char)src[offset];
    }
    dst[written] = '\0';
}
