/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: MIT
 */

#ifndef G0WM_UTIL_H
#define G0WM_UTIL_H

#include <stddef.h>

[[noreturn, gnu::format(printf, 1, 2)]] void util_die(const char* fmt, ...);
[[nodiscard]] void* util_calloc(size_t nmemb, size_t size);
[[nodiscard]] int util_set_nonblock(int fd);
[[nodiscard]] size_t util_put_codepoint(char* dst,
                                        size_t room,
                                        unsigned long cp);

#endif
