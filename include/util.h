/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: MIT
 */

void die(const char* fmt, ...);
void* ecalloc(size_t nmemb, size_t size);
int fd_set_nonblock(int fd);
size_t putcp(char* dst, size_t room, unsigned long cp);
