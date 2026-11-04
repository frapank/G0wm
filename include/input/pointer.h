/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: unixchad
 * SPDX-FileCopyrightText: Ben Collerson
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_INPUT_POINTER_H
#define G0WM_INPUT_POINTER_H

#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>

#include "core/g0wm.h"

#define CLEANMASK(mask) (mask & ~WLR_MODIFIER_CAPS)

enum : unsigned int { CurNormal, CurPressed, CurMove, CurResize }; /* cursor */

struct last_cursor_state {
    enum wp_cursor_shape_device_v1_shape shape;
    struct wlr_surface* surface;
    bool has_client_cursor;
    int hotspot_x;
    int hotspot_y;
    struct wl_listener destroy;
};

extern struct last_cursor_state input_last_cursor;

extern unsigned int input_cursor_mode;

extern Client* input_grab_client;

void input_handle_axis(struct wl_listener* listener, void* data);
void input_handle_button(struct wl_listener* listener, void* data);
void input_handle_new_pointer_constraint(struct wl_listener* listener,
                                         void* data);
void input_handle_frame(struct wl_listener* listener, void* data);
void input_cursor_moved(void);
void input_handle_motion_absolute(struct wl_listener* listener, void* data);
void input_motion(uint32_t time,
                  struct wlr_input_device* device,
                  double sx,
                  double sy,
                  double sx_unaccel,
                  double sy_unaccel);
void input_handle_motion(struct wl_listener* listener, void* data);
void input_move_resize(const Arg* arg);
void input_set_client_cursor(struct wlr_surface* surface,
                             int hotspot_x,
                             int hotspot_y);
void input_handle_request_cursor(struct wl_listener* listener, void* data);
void input_handle_request_cursor_shape(struct wl_listener* listener,
                                       void* data);
void input_title_grab(Client* c);
void input_warp_cursor(const Client* c);
void input_node_at(double x,
                   double y,
                   struct wlr_surface** psurface,
                   Client** pc,
                   LayerSurface** pl,
                   double* nx,
                   double* ny);

#endif
