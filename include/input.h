/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: unixchad
 * SPDX-FileCopyrightText: Ben Collerson
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_INPUT_H
#define G0WM_INPUT_H

#include <linux/input-event-codes.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <xkbcommon/xkbcommon.h>

#include "g0wm.h"

#define CLEANMASK(mask) (mask & ~WLR_MODIFIER_CAPS)

enum : unsigned int { CurNormal, CurPressed, CurMove, CurResize }; /* cursor */

struct KeyboardGroup {
    struct wlr_keyboard_group* wlr_group;

    int nsyms;
    const xkb_keysym_t* keysyms; /* invalid if nsyms == 0 */
    uint32_t mods;               /* invalid if nsyms == 0 */
    struct wl_event_source* key_repeat_source;

    struct wl_listener modifiers;
    struct wlr_virtual_keyboard_v1* vkb; /* NULL for the physical keyboards */
    uint32_t imkeys[KEY_CNT / 32]; /* held keys whose press went to the IM */
    struct wl_listener key;
    struct wl_listener destroy;
};
static_assert(KEY_CNT % 32 == 0);

struct last_cursor_state {
    enum wp_cursor_shape_device_v1_shape shape;
    struct wlr_surface* surface;
    bool has_client_cursor;
    int hotspot_x;
    int hotspot_y;
    struct wl_listener destroy;
};

void input_handle_axis(struct wl_listener* listener, void* data);
void input_handle_button(struct wl_listener* listener, void* data);
void input_handle_new_keyboard_inhibitor(struct wl_listener* listener,
                                         void* data);
KeyboardGroup* input_create_keyboard_group(void);
void input_handle_new_pointer_constraint(struct wl_listener* listener,
                                         void* data);
void input_handle_frame(struct wl_listener* listener, void* data);
void input_cursor_moved(void);
void input_handle_keyboard_group_destroy(struct wl_listener* listener,
                                         void* data);
void input_handle_hold_begin(struct wl_listener* listener, void* data);
void input_handle_hold_end(struct wl_listener* listener, void* data);
void input_handle_new_input(struct wl_listener* listener, void* data);
void input_handle_motion_absolute(struct wl_listener* listener, void* data);
void input_motion(uint32_t time,
                  struct wlr_input_device* device,
                  double sx,
                  double sy,
                  double sx_unaccel,
                  double sy_unaccel);
void input_handle_motion(struct wl_listener* listener, void* data);
void input_move_resize(const Arg* arg);
void input_handle_pinch_begin(struct wl_listener* listener, void* data);
void input_handle_pinch_end(struct wl_listener* listener, void* data);
void input_handle_pinch_update(struct wl_listener* listener, void* data);
void input_handle_request_start_drag(struct wl_listener* listener, void* data);
void input_update_seat_caps(void);
void input_set_client_cursor(struct wlr_surface* surface,
                             int hotspot_x,
                             int hotspot_y);
void input_handle_request_cursor(struct wl_listener* listener, void* data);
void input_handle_request_cursor_shape(struct wl_listener* listener,
                                       void* data);
void input_handle_request_primary_selection(struct wl_listener* listener,
                                            void* data);
void input_handle_request_selection(struct wl_listener* listener, void* data);
void input_handle_start_drag(struct wl_listener* listener, void* data);
void input_handle_swipe_begin(struct wl_listener* listener, void* data);
void input_handle_swipe_end(struct wl_listener* listener, void* data);
void input_handle_swipe_update(struct wl_listener* listener, void* data);
void input_title_grab(Client* c);
void input_toggle_inhibit(const Arg* arg);
void input_handle_new_virtual_keyboard(struct wl_listener* listener,
                                       void* data);
void input_handle_new_virtual_pointer(struct wl_listener* listener, void* data);
void input_warp_cursor(const Client* c);
void input_node_at(double x,
                   double y,
                   struct wlr_surface** psurface,
                   Client** pc,
                   LayerSurface** pl,
                   double* nx,
                   double* ny);

extern struct last_cursor_state input_last_cursor;
extern unsigned int input_cursor_mode;
extern Client* input_grab_client;

#endif
