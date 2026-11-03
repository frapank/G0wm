/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: MIT
 */

#ifndef G0WM_UTIL_H
#define G0WM_UTIL_H

#include <stddef.h>

[[noreturn, gnu::format(printf, 1, 2)]] void die(const char* fmt, ...);
[[nodiscard]] void* ecalloc(size_t nmemb, size_t size);
[[nodiscard]] int fd_set_nonblock(int fd);
[[nodiscard]] size_t putcp(char* dst, size_t room, unsigned long cp);

#endif
