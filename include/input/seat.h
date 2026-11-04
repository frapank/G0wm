/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: unixchad
 * SPDX-FileCopyrightText: Ben Collerson
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_INPUT_SEAT_H
#define G0WM_INPUT_SEAT_H

#include <wayland-server-core.h>

void input_handle_new_input(struct wl_listener* listener, void* data);
void input_handle_request_start_drag(struct wl_listener* listener, void* data);
void input_update_seat_caps(void);
void input_handle_request_primary_selection(struct wl_listener* listener,
                                            void* data);
void input_handle_request_selection(struct wl_listener* listener, void* data);
void input_handle_start_drag(struct wl_listener* listener, void* data);
void input_handle_new_virtual_pointer(struct wl_listener* listener, void* data);

#endif
