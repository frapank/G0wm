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

void axisnotify(struct wl_listener* listener, void* data);
void buttonpress(struct wl_listener* listener, void* data);
void createkbinhibitor(struct wl_listener* listener, void* data);
KeyboardGroup* createkeyboardgroup(void);
void createpointerconstraint(struct wl_listener* listener, void* data);
void cursorframe(struct wl_listener* listener, void* data);
void cursormoved(void);
void destroykeyboardgroup(struct wl_listener* listener, void* data);
void holdbegin(struct wl_listener* listener, void* data);
void holdend(struct wl_listener* listener, void* data);
void inputdevice(struct wl_listener* listener, void* data);
void motionabsolute(struct wl_listener* listener, void* data);
void motionnotify(uint32_t time,
                  struct wlr_input_device* device,
                  double sx,
                  double sy,
                  double sx_unaccel,
                  double sy_unaccel);
void motionrelative(struct wl_listener* listener, void* data);
void moveresize(const Arg* arg);
void pinchbegin(struct wl_listener* listener, void* data);
void pinchend(struct wl_listener* listener, void* data);
void pinchupdate(struct wl_listener* listener, void* data);
void requeststartdrag(struct wl_listener* listener, void* data);
void seatcaps(void);
void setclientcursor(struct wlr_surface* surface, int hotspot_x, int hotspot_y);
void setcursor(struct wl_listener* listener, void* data);
void setcursorshape(struct wl_listener* listener, void* data);
void setpsel(struct wl_listener* listener, void* data);
void setsel(struct wl_listener* listener, void* data);
void startdrag(struct wl_listener* listener, void* data);
void swipebegin(struct wl_listener* listener, void* data);
void swipeend(struct wl_listener* listener, void* data);
void swipeupdate(struct wl_listener* listener, void* data);
void titlegrab(Client* c);
void toggleinhibit(const Arg* arg);
void virtualkeyboard(struct wl_listener* listener, void* data);
void virtualpointer(struct wl_listener* listener, void* data);
void warpcursor(const Client* c);
void xytonode(double x,
              double y,
              struct wlr_surface** psurface,
              Client** pc,
              LayerSurface** pl,
              double* nx,
              double* ny);

extern struct last_cursor_state last_cursor;
extern unsigned int cursor_mode;
extern Client* grabc;

#endif
