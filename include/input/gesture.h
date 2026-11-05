/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: unixchad
 * SPDX-FileCopyrightText: Ben Collerson
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_INPUT_GESTURE_H
#define G0WM_INPUT_GESTURE_H

#include <wayland-server-core.h>

void input_handle_hold_begin(struct wl_listener*, void* data);
void input_handle_hold_end(struct wl_listener*, void* data);
void input_handle_pinch_begin(struct wl_listener*, void* data);
void input_handle_pinch_end(struct wl_listener*, void* data);
void input_handle_pinch_update(struct wl_listener*, void* data);
void input_handle_swipe_begin(struct wl_listener*, void* data);
void input_handle_swipe_end(struct wl_listener*, void* data);
void input_handle_swipe_update(struct wl_listener*, void* data);

#endif
