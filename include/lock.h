/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_LOCK_H
#define G0WM_LOCK_H

#include <wayland-server-core.h>

void destroylocksurface(struct wl_listener* listener, void* data);
void locksession(struct wl_listener* listener, void* data);

extern int locked;

#endif
