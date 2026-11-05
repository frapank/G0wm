/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_TABLET_H
#define G0WM_TABLET_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_touch.h>

void tablet_create(struct wlr_tablet* wlr_tablet);
void tablet_create_pad(struct wlr_tablet_pad* wlr_pad);
void tablet_create_touch(struct wlr_touch* wlr_touch);
int tablet_has_touch(void);
void tablet_handle_keyboard_focus_change(struct wl_listener*, void* data);
void tablet_handle_axis(struct wl_listener*, void* data);
void tablet_handle_button(struct wl_listener*, void* data);
void tablet_handle_proximity(struct wl_listener*, void* data);
void tablet_remap(void);
void tablet_handle_tip(struct wl_listener*, void* data);
int tablet_tool_focused_by(struct wlr_tablet_v2_tablet_tool* tool, struct wlr_seat_client* client);
void tablet_handle_touch_cancel(struct wl_listener*, void* data);
void tablet_handle_touch_down(struct wl_listener*, void* data);
void tablet_handle_touch_frame(struct wl_listener*, void*);
void tablet_handle_touch_motion(struct wl_listener*, void* data);
void tablet_handle_touch_up(struct wl_listener*, void* data);

extern struct wlr_tablet_manager_v2* tablet_manager;

#endif
