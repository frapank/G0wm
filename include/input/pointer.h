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

enum CursorMode : unsigned int {
    CURSOR_MODE_NORMAL,
    CURSOR_MODE_PRESSED,
    CURSOR_MODE_MOVE,
    CURSOR_MODE_RESIZE
}; /* cursor */

struct CursorState {
    enum wp_cursor_shape_device_v1_shape shape;
    struct wlr_surface* surface;
    bool has_client_cursor;
    int hotspot_x;
    int hotspot_y;
    struct wl_listener surface_destroy;
};

extern struct CursorState input_last_cursor;

extern unsigned int input_cursor_mode;

extern WindowClient* input_grab_client;

void input_handle_axis(struct wl_listener*, void* data);
void input_handle_button(struct wl_listener*, void* data);
void input_handle_new_pointer_constraint(struct wl_listener*, void* data);
void input_handle_frame(struct wl_listener*, void*);
void input_cursor_moved(void);
void input_handle_motion_absolute(struct wl_listener*, void* data);
void input_motion(uint32_t time,
                  struct wlr_input_device* device,
                  double delta_x,
                  double delta_y,
                  double delta_x_unaccel,
                  double delta_y_unaccel);
void input_handle_motion(struct wl_listener*, void* data);
void input_move_resize(const ActionArg* argument);
void input_set_client_cursor(struct wlr_surface* surface, int hotspot_x, int hotspot_y);
void input_handle_request_cursor(struct wl_listener*, void* data);
void input_handle_request_cursor_shape(struct wl_listener*, void* data);
void input_title_grab(WindowClient* client);
void input_warp_cursor(const WindowClient* client);
void input_node_at(double x,
                   double y,
                   struct wlr_surface** out_surface,
                   WindowClient** out_client,
                   LayerSurface** out_layer_surface,
                   double* node_x,
                   double* node_y);

#endif
