/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_CORE_G0WM_INTERNAL_H
#define G0WM_CORE_G0WM_INTERNAL_H

#include <stddef.h>
#include <sys/types.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_xdg_activation_v1.h>

extern struct wl_display* display;
extern struct wlr_session* session;
extern struct wlr_xdg_activation_v1* activation;
extern struct wlr_backend* backend;
extern pid_t* autostart_pids;
extern size_t autostart_len;

void cleanup(void);
void setup(void);
void handle_signal(int signo);
const char* activation_token_new(void);

#endif
