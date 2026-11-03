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

void createtablet(struct wlr_tablet* wlr_tablet);
void createtabletpad(struct wlr_tablet_pad* wlr_pad);
void createtouch(struct wlr_touch* wlr_touch);
int hastouch(void);
void padkeyboardfocus(struct wl_listener* listener, void* data);
void tabletaxis(struct wl_listener* listener, void* data);
void tabletbutton(struct wl_listener* listener, void* data);
void tabletproximity(struct wl_listener* listener, void* data);
void tabletremap(void);
void tablettip(struct wl_listener* listener, void* data);
int toolfocusedby(struct wlr_tablet_v2_tablet_tool* tool,
                  struct wlr_seat_client* client);
void touchcancel(struct wl_listener* listener, void* data);
void touchdown(struct wl_listener* listener, void* data);
void touchframe(struct wl_listener* listener, void* data);
void touchmotion(struct wl_listener* listener, void* data);
void touchup(struct wl_listener* listener, void* data);

extern struct wlr_tablet_manager_v2* tablet_mgr;

#endif
