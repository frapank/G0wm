/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_LOCK_H
#define G0WM_LOCK_H

#include <wayland-server-core.h>

void lock_handle_surface_destroy(struct wl_listener* listener, void*);
void lock_handle_new_lock(struct wl_listener*, void* data);

extern int lock_active;

#endif
