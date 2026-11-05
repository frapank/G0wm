/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_XWAYLAND_H
#define G0WM_XWAYLAND_H

#include <wayland-server-core.h>

#ifdef XWAYLAND
void xwayland_handle_new_surface(struct wl_listener*, void* data);
void xwayland_handle_ready(struct wl_listener*, void*);
#endif

#endif
