/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * drawing tablets (pens and pads, over tablet-v2) and touchscreens
 */
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_touch.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/bar/title.h"
#include "desktop/traypopup.h"
#include "input/pointer.h"
#include "input/seat.h"
#include "input/tablet.h"
#include "output/lock.h"
#include "output/monitor.h"
#include "window/client.h"

typedef struct {
    struct wlr_tablet* wlr_tablet;
    struct wlr_tablet_v2_tablet* v2; /* NULL if creation failed */
    struct wl_list link;             /* tablets */
    struct wl_listener destroy;
} Tablet;

typedef struct {
    struct wlr_tablet_v2_tablet_tool* v2; /* NULL for a totem */
    double ox, oy;         /* layout position of the surface it is on */
    double tilt_x, tilt_y; /* wlroots only reports the axis that changed */
    int emulating;         /* the tip went down as a left click */
    uint32_t emubuttons;   /* 1 << (button & 31), sent as pointer buttons */
    struct wl_listener set_cursor;
    struct wl_listener destroy;
} TabletTool;

typedef struct {
    struct wlr_tablet_v2_tablet_pad* v2;
    struct wlr_input_device* device;
    Tablet* tablet; /* NULL until its tablet shows up */
    struct wlr_surface* focus;
    struct wl_list link; /* pads */
    struct wl_listener button;
    struct wl_listener ring;
    struct wl_listener strip;
    struct wl_listener focus_destroy;
    struct wl_listener destroy;
} TabletPad;

typedef struct {
    struct wlr_touch* wlr_touch;
    struct wl_list link; /* tablet_touch_devices */
    struct wl_listener destroy;
} Touch;

/* function declarations */
static void tablet_emulate_button(uint32_t time, uint32_t button, int pressed);
static struct wlr_output* tablet_output_named(const char* name);
static void tablet_pad_attach(TabletPad* p);
static void tablet_handle_pad_button(struct wl_listener* listener, void* data);
static void tablet_handle_pad_destroy(struct wl_listener* listener, void* data);
static void tablet_pad_focus(TabletPad* p, struct wlr_surface* surface);
static void tablet_handle_pad_ring(struct wl_listener* listener, void* data);
static void tablet_handle_pad_strip(struct wl_listener* listener, void* data);
static void tablet_handle_pad_unfocus(struct wl_listener* listener, void* data);
static int tablet_same_group(struct wlr_input_device* a,
                             struct wlr_input_device* b);
static uint32_t tablet_stylus_button(uint32_t button);
static void tablet_configure(Tablet* t);
static void tablet_handle_destroy(struct wl_listener* listener, void* data);
static int tablet_tip_bound(void);
static TabletTool* tablet_tool_get(struct wlr_tablet_tool* wlr_tool);
static void tablet_handle_tool_destroy(struct wl_listener* listener,
                                       void* data);
static void tablet_tool_motion(TabletTool* t,
                               Tablet* tb,
                               uint32_t time,
                               double x,
                               double y);
static void tablet_handle_tool_set_cursor(struct wl_listener* listener,
                                          void* data);
static void tablet_handle_touch_destroy(struct wl_listener* listener,
                                        void* data);
static int tablet_touch_taken(struct wlr_surface* surface);

/* variables */
struct wlr_tablet_manager_v2* tablet_manager;
static struct wl_list tablet_devices = { &tablet_devices, &tablet_devices };
static struct wl_list tablet_pads = { &tablet_pads, &tablet_pads };
static struct wl_list tablet_touch_devices = { &tablet_touch_devices,
                                               &tablet_touch_devices };
/* surface origin of each touch point */
/* ponytail: indexed by id & 15, libinput hands out small slot numbers */
static struct {
    double ox, oy;
} tablet_touch_points[16];
static int32_t tablet_emulated_touch =
    -1; /* the touch point driving the pointer */

/* function implementations */
void tablet_create(struct wlr_tablet* wlr_tablet)
{
    Tablet* t = util_calloc(1, sizeof(*t));
    TabletPad* p;

    t->wlr_tablet = wlr_tablet;
    t->v2 = wlr_tablet_create(tablet_manager, seat, &wlr_tablet->base);
    wlr_tablet->data = t;
    wl_list_insert(&tablet_devices, &t->link);
    LISTEN(
        &wlr_tablet->base.events.destroy, &t->destroy, tablet_handle_destroy);

    wlr_cursor_attach_input_device(cursor, &wlr_tablet->base);
    tablet_configure(t);
    wl_list_for_each(p, &tablet_pads, link) if (!p->tablet)
        tablet_pad_attach(p);
}

void tablet_create_pad(struct wlr_tablet_pad* wlr_pad)
{
    TabletPad* p = util_calloc(1, sizeof(*p));

    p->device = &wlr_pad->base;
    p->v2 = wlr_tablet_pad_create(tablet_manager, seat, p->device);
    wl_list_insert(&tablet_pads, &p->link);
    wl_list_init(&p->focus_destroy.link);
    LISTEN(&wlr_pad->events.button, &p->button, tablet_handle_pad_button);
    LISTEN(&wlr_pad->events.ring, &p->ring, tablet_handle_pad_ring);
    LISTEN(&wlr_pad->events.strip, &p->strip, tablet_handle_pad_strip);
    LISTEN(&p->device->events.destroy, &p->destroy, tablet_handle_pad_destroy);
    tablet_pad_attach(p);
}

void tablet_create_touch(struct wlr_touch* wlr_touch)
{
    Touch* d = util_calloc(1, sizeof(*d));

    d->wlr_touch = wlr_touch;
    wl_list_insert(&tablet_touch_devices, &d->link);
    LISTEN(&wlr_touch->base.events.destroy,
           &d->destroy,
           tablet_handle_touch_destroy);
    wlr_cursor_attach_input_device(cursor, &wlr_touch->base);
    tablet_remap();
}

static void tablet_emulate_button(uint32_t time, uint32_t button, int pressed)
{
    struct wlr_pointer_button_event synth = {
        .time_msec = time,
        .button = button,
        .state = pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                         : WL_POINTER_BUTTON_STATE_RELEASED,
    };
    input_handle_button(nullptr, &synth);
    wlr_seat_pointer_notify_frame(seat);
}

int tablet_has_touch(void)
{
    return !wl_list_empty(&tablet_touch_devices);
}

/* NULL if missing or disabled */
static struct wlr_output* tablet_output_named(const char* name)
{
    Monitor* m;

    if (!name)
        return nullptr;
    wl_list_for_each(m, &monitors, link)
    {
        if (m->wlr_output->enabled && !strcmp(m->wlr_output->name, name))
            return m->wlr_output;
    }
    return nullptr;
}

static void tablet_pad_attach(TabletPad* p)
{
    Tablet* t;

    tablet_pad_focus(p, nullptr);
    p->tablet = nullptr;
    wl_list_for_each(t, &tablet_devices, link)
    {
        if (tablet_same_group(p->device, &t->wlr_tablet->base)) {
            p->tablet = t;
            break;
        }
    }
    tablet_pad_focus(p, seat->keyboard_state.focused_surface);
}

static void tablet_handle_pad_button(struct wl_listener* listener, void* data)
{
    TabletPad* p = wl_container_of(listener, p, button);
    struct wlr_tablet_pad_button_event* event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    /* bound buttons are not forwarded */
    for (const PadButton* b = pad_buttons;
         !lock_active && b < pad_buttons + pad_button_count;
         b++) {
        if (b->button == event->button && b->func) {
            if (event->state == WLR_BUTTON_PRESSED)
                b->func(&b->arg);
            return;
        }
    }
    wlr_tablet_v2_tablet_pad_notify_mode(
        p->v2, event->group, event->mode, event->time_msec);
    wlr_tablet_v2_tablet_pad_notify_button(
        p->v2,
        event->button,
        event->time_msec,
        event->state == WLR_BUTTON_PRESSED
            ? ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED
            : ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED);
}

static void tablet_handle_pad_destroy(struct wl_listener* listener, void*)
{
    TabletPad* p = wl_container_of(listener, p, destroy);

    wl_list_remove(&p->focus_destroy.link);
    wl_list_remove(&p->button.link);
    wl_list_remove(&p->ring.link);
    wl_list_remove(&p->strip.link);
    wl_list_remove(&p->destroy.link);
    wl_list_remove(&p->link);
    free(p);
}

/* pads follow keyboard focus */
static void tablet_pad_focus(TabletPad* p, struct wlr_surface* surface)
{
    if (surface == p->focus)
        return;
    if (p->focus) {
        wlr_tablet_v2_tablet_pad_notify_leave(p->v2, p->focus);
        wl_list_remove(&p->focus_destroy.link);
        wl_list_init(&p->focus_destroy.link);
        p->focus = nullptr;
    }
    if (!surface || !p->tablet || !p->tablet->v2 ||
        !wlr_surface_accepts_tablet_v2(surface, p->tablet->v2))
        return;
    wlr_tablet_v2_tablet_pad_notify_enter(p->v2, p->tablet->v2, surface);
    p->focus = surface;
    LISTEN(
        &surface->events.destroy, &p->focus_destroy, tablet_handle_pad_unfocus);
}

void tablet_handle_keyboard_focus_change(struct wl_listener*, void* data)
{
    struct wlr_seat_keyboard_focus_change_event* event = data;
    TabletPad* p;

    wl_list_for_each(p, &tablet_pads, link)
        tablet_pad_focus(p, event->new_surface);
}

static void tablet_handle_pad_ring(struct wl_listener* listener, void* data)
{
    TabletPad* p = wl_container_of(listener, p, ring);
    struct wlr_tablet_pad_ring_event* event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_tablet_v2_tablet_pad_notify_ring(p->v2,
                                         event->ring,
                                         event->position,
                                         event->source ==
                                             WLR_TABLET_PAD_RING_SOURCE_FINGER,
                                         event->time_msec);
}

static void tablet_handle_pad_strip(struct wl_listener* listener, void* data)
{
    TabletPad* p = wl_container_of(listener, p, strip);
    struct wlr_tablet_pad_strip_event* event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_tablet_v2_tablet_pad_notify_strip(
        p->v2,
        event->strip,
        event->position,
        event->source == WLR_TABLET_PAD_STRIP_SOURCE_FINGER,
        event->time_msec);
}

static void tablet_handle_pad_unfocus(struct wl_listener* listener, void*)
{
    TabletPad* p = wl_container_of(listener, p, focus_destroy);

    wl_list_remove(&p->focus_destroy.link);
    wl_list_init(&p->focus_destroy.link);
    p->focus = nullptr;
}

/* no device groups outside libinput (nested): match any tablet */
static int tablet_same_group(struct wlr_input_device* a,
                             struct wlr_input_device* b)
{
    if (!wlr_input_device_is_libinput(a) || !wlr_input_device_is_libinput(b))
        return 1;
    return libinput_device_get_device_group(
               wlr_libinput_get_device_handle(a)) ==
           libinput_device_get_device_group(wlr_libinput_get_device_handle(b));
}

static uint32_t tablet_stylus_button(uint32_t button)
{
    switch (button) {
        case BTN_STYLUS:
            return BTN_MIDDLE;
        case BTN_STYLUS2:
            return BTN_RIGHT;
        case BTN_STYLUS3:
            return BTN_SIDE;
        default: /* a tablet mouse has real ones */
            return button;
    }
}

void tablet_handle_axis(struct wl_listener*, void* data)
{
    struct wlr_tablet_tool_axis_event* event = data;
    TabletTool* t = tablet_tool_get(event->tool);
    uint32_t axes = event->updated_axes;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    /* An axis event only carries the axes it actually updated; NAN tells
     * wlroots to keep the current coordinate for the others. */
    if (axes & (WLR_TABLET_TOOL_AXIS_X | WLR_TABLET_TOOL_AXIS_Y))
        tablet_tool_motion(t,
                           event->tablet->data,
                           event->time_msec,
                           axes & WLR_TABLET_TOOL_AXIS_X ? event->x : NAN,
                           axes & WLR_TABLET_TOOL_AXIS_Y ? event->y : NAN);

    if (!t->v2 || !t->v2->focused_surface)
        return;
    if (axes & WLR_TABLET_TOOL_AXIS_PRESSURE)
        wlr_tablet_v2_tablet_tool_notify_pressure(t->v2, event->pressure);
    if (axes & WLR_TABLET_TOOL_AXIS_DISTANCE)
        wlr_tablet_v2_tablet_tool_notify_distance(t->v2, event->distance);
    if (axes & WLR_TABLET_TOOL_AXIS_TILT_X)
        t->tilt_x = event->tilt_x;
    if (axes & WLR_TABLET_TOOL_AXIS_TILT_Y)
        t->tilt_y = event->tilt_y;
    if (axes & (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y))
        wlr_tablet_v2_tablet_tool_notify_tilt(t->v2, t->tilt_x, t->tilt_y);
    if (axes & WLR_TABLET_TOOL_AXIS_ROTATION)
        wlr_tablet_v2_tablet_tool_notify_rotation(t->v2, event->rotation);
    if (axes & WLR_TABLET_TOOL_AXIS_SLIDER)
        wlr_tablet_v2_tablet_tool_notify_slider(t->v2, event->slider);
    if (axes & WLR_TABLET_TOOL_AXIS_WHEEL)
        wlr_tablet_v2_tablet_tool_notify_wheel(t->v2, event->wheel_delta, 0);
}

void tablet_handle_button(struct wl_listener*, void* data)
{
    struct wlr_tablet_tool_button_event* event = data;
    TabletTool* t = tablet_tool_get(event->tool);
    uint32_t bit = 1u << (event->button & 31);
    int pressed = event->state == WLR_BUTTON_PRESSED;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    /* released the same way it was pressed */
    if (pressed ? !t->v2 || !t->v2->focused_surface || t->emulating
                : t->emubuttons & bit) {
        if (pressed)
            t->emubuttons |= bit;
        else
            t->emubuttons &= ~bit;
        tablet_emulate_button(
            event->time_msec, tablet_stylus_button(event->button), pressed);
        return;
    }
    if (!t->v2)
        return;
    wlr_tablet_v2_tablet_tool_notify_button(
        t->v2,
        event->button,
        pressed ? ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED
                : ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED);
    wlr_tablet_tool_v2_start_implicit_grab(t->v2);
}

static void tablet_configure(Tablet* t)
{
    struct wlr_input_device* device = &t->wlr_tablet->base;
    struct libinput_device* li;

    wlr_cursor_map_input_to_output(
        cursor, device, tablet_output_named(tablet_output));
    if (wlr_input_device_is_libinput(device) &&
        (li = wlr_libinput_get_device_handle(device)) &&
        libinput_device_config_left_handed_is_available(li))
        libinput_device_config_left_handed_set(li, tablet_left_handed);
}

static void tablet_handle_destroy(struct wl_listener* listener, void*)
{
    Tablet* t = wl_container_of(listener, t, destroy);
    TabletPad* p;

    wl_list_remove(&t->destroy.link);
    wl_list_remove(&t->link);
    wl_list_for_each(p, &tablet_pads, link) if (p->tablet == t)
        tablet_pad_attach(p);
    free(t);
}

void tablet_handle_proximity(struct wl_listener*, void* data)
{
    struct wlr_tablet_tool_proximity_event* event = data;
    TabletTool* t = tablet_tool_get(event->tool);

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    if (event->state == WLR_TABLET_TOOL_PROXIMITY_IN)
        tablet_tool_motion(
            t, event->tablet->data, event->time_msec, event->x, event->y);
    else if (t->v2)
        wlr_tablet_v2_tablet_tool_notify_proximity_out(t->v2);
}

void tablet_remap(void)
{
    Tablet* t;
    Touch* d;

    wl_list_for_each(t, &tablet_devices, link) tablet_configure(t);
    wl_list_for_each(d, &tablet_touch_devices, link)
    {
        wlr_cursor_map_input_to_output(
            cursor,
            &d->wlr_touch->base,
            tablet_output_named(touch_output ? touch_output
                                             : d->wlr_touch->output_name));
    }
}

void tablet_handle_tip(struct wl_listener*, void* data)
{
    struct wlr_tablet_tool_tip_event* event = data;
    TabletTool* t = tablet_tool_get(event->tool);
    Client* c;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    if (event->state == WLR_TABLET_TOOL_TIP_UP) {
        if (t->emulating) {
            t->emulating = 0;
            tablet_emulate_button(event->time_msec, BTN_LEFT, 0);
            tablet_tool_motion(
                t, event->tablet->data, event->time_msec, NAN, NAN);
        } else if (t->v2) {
            wlr_tablet_v2_tablet_tool_notify_up(t->v2);
        }
        return;
    }

    if (t->v2 && t->v2->focused_surface && !tablet_tip_bound()) {
        /* a stroke focuses like a click */
        selected_monitor = monitor_at(cursor->x, cursor->y);
        input_node_at(
            cursor->x, cursor->y, nullptr, &c, nullptr, nullptr, nullptr);
        if (!lock_active && c &&
            (!client_is_unmanaged(c) || client_wants_focus(c)))
            client_focus(c, 1);
        wlr_tablet_v2_tablet_tool_notify_down(t->v2);
        wlr_tablet_tool_v2_start_implicit_grab(t->v2);
        return;
    }
    /* bound click or no tablet-v2 surface: act as the pointer until tip up */
    if (t->v2 && t->v2->focused_surface) {
        wlr_tablet_v2_tablet_tool_notify_proximity_out(t->v2);
        input_motion(event->time_msec, &event->tablet->base, 0, 0, 0, 0);
    }
    t->emulating = 1;
    tablet_emulate_button(event->time_msec, BTN_LEFT, 1);
}

static int tablet_tip_bound(void)
{
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;

    if (lock_active)
        return 0;
    for (const Button* b = buttons; b < buttons + button_count; b++)
        if (b->click == ClkClient && b->button == BTN_LEFT && b->func &&
            CLEANMASK(mods) == CLEANMASK(b->mod))
            return 1;
    return 0;
}

static TabletTool* tablet_tool_get(struct wlr_tablet_tool* wlr_tool)
{
    TabletTool* t = wlr_tool->data;

    if (t)
        return t;
    t = util_calloc(1, sizeof(*t));
    wl_list_init(&t->set_cursor.link);
    if ((t->v2 = wlr_tablet_tool_create(tablet_manager, seat, wlr_tool)))
        LISTEN(&t->v2->events.set_cursor,
               &t->set_cursor,
               tablet_handle_tool_set_cursor);
    LISTEN(&wlr_tool->events.destroy, &t->destroy, tablet_handle_tool_destroy);
    wlr_tool->data = t;
    return t;
}

static void tablet_handle_tool_destroy(struct wl_listener* listener, void*)
{
    TabletTool* t = wl_container_of(listener, t, destroy);

    wl_list_remove(&t->set_cursor.link);
    wl_list_remove(&t->destroy.link);
    free(t);
}

int tablet_tool_focused_by(struct wlr_tablet_v2_tablet_tool* tool,
                           struct wlr_seat_client* client)
{
    return tool && tool->focused_surface && client &&
           wl_resource_get_client(tool->focused_surface->resource) ==
               client->client;
}

/* tablet-v2 surfaces get the pen, anything else gets it as the pointer */
static void tablet_tool_motion(TabletTool* t,
                               Tablet* tb,
                               uint32_t time,
                               double x,
                               double y)
{
    struct wlr_input_device* device = &tb->wlr_tablet->base;
    struct wlr_surface* surface = nullptr;
    Client* c = nullptr;
    double lx, ly, sx = 0, sy = 0;
    int grabbed = t->v2 && wlr_tablet_tool_v2_has_implicit_grab(t->v2);

    wlr_cursor_absolute_to_layout_coords(cursor, device, x, y, &lx, &ly);
    if (t->v2 && tb->v2 && !t->emulating && input_cursor_mode == CurNormal &&
        !traypopup_active())
        input_node_at(lx, ly, &surface, &c, nullptr, &sx, &sy);
    if (surface && !wlr_surface_accepts_tablet_v2(surface, tb->v2))
        surface = nullptr;

    if (!surface && !grabbed) {
        if (t->v2)
            wlr_tablet_v2_tablet_tool_notify_proximity_out(t->v2);
        input_motion(time,
                     device,
                     lx - cursor->x,
                     ly - cursor->y,
                     lx - cursor->x,
                     ly - cursor->y);
        /* Tablets emit no frame event of their own, and clients hold
         * pointer events back until one arrives. */
        wlr_seat_pointer_notify_frame(seat);
        return;
    }

    wlr_cursor_warp_closest(cursor, device, lx, ly);
    input_cursor_moved();
    bar_title_hover(c, cursor->x, cursor->y);
    if (!grabbed) {
        /* the pen and the pointer are not both on an app at once */
        if (seat->pointer_state.focused_surface)
            wlr_seat_pointer_notify_clear_focus(seat);
        if (surface != t->v2->focused_surface && sloppy_focus && c &&
            !client_is_unmanaged(c))
            client_focus(c, 0);
        t->ox = lx - sx;
        t->oy = ly - sy;
    }
    if (surface)
        wlr_tablet_v2_tablet_tool_notify_proximity_in(t->v2, tb->v2, surface);
    else
        wlr_tablet_v2_tablet_tool_notify_proximity_out(t->v2);
    wlr_tablet_v2_tablet_tool_notify_motion(
        t->v2, cursor->x - t->ox, cursor->y - t->oy);
}

static void tablet_handle_tool_set_cursor(struct wl_listener* listener,
                                          void* data)
{
    TabletTool* t = wl_container_of(listener, t, set_cursor);
    struct wlr_tablet_v2_event_cursor* event = data;

    if (input_cursor_mode == CurNormal &&
        tablet_tool_focused_by(t->v2, event->seat_client))
        input_set_client_cursor(
            event->surface, event->hotspot_x, event->hotspot_y);
}

void tablet_handle_touch_cancel(struct wl_listener*, void* data)
{
    struct wlr_touch_cancel_event* event = data;
    struct wlr_touch_point* point;

    if (event->touch_id == tablet_emulated_touch) {
        tablet_emulated_touch = -1;
        tablet_emulate_button(event->time_msec, BTN_LEFT, 0);
    } else if ((point = wlr_seat_touch_get_point(seat, event->touch_id))) {
        wlr_seat_touch_notify_cancel(seat, point->client);
    }
}

static void tablet_handle_touch_destroy(struct wl_listener* listener, void*)
{
    Touch* d = wl_container_of(listener, d, destroy);

    wl_list_remove(&d->destroy.link);
    wl_list_remove(&d->link);
    free(d);
    input_update_seat_caps();
}

void tablet_handle_touch_down(struct wl_listener*, void* data)
{
    struct wlr_touch_down_event* event = data;
    struct wlr_surface* surface = nullptr;
    Client* c = nullptr;
    double lx, ly, sx, sy;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_cursor_absolute_to_layout_coords(
        cursor, &event->touch->base, event->x, event->y, &lx, &ly);
    if (input_cursor_mode == CurNormal && !traypopup_active())
        input_node_at(lx, ly, &surface, &c, nullptr, &sx, &sy);

    if (surface && tablet_touch_taken(surface) &&
        wlr_seat_touch_notify_down(
            seat, surface, event->time_msec, event->touch_id, sx, sy)) {
        tablet_touch_points[event->touch_id & 15].ox = lx - sx;
        tablet_touch_points[event->touch_id & 15].oy = ly - sy;
        selected_monitor = monitor_at(lx, ly);
        if (!lock_active && c &&
            (!client_is_unmanaged(c) || client_wants_focus(c)))
            client_focus(c, 1);
        return;
    }
    /* no touch support there: emulate a click, one finger at a time */
    if (tablet_emulated_touch >= 0)
        return;
    tablet_emulated_touch = event->touch_id;
    input_motion(event->time_msec,
                 &event->touch->base,
                 lx - cursor->x,
                 ly - cursor->y,
                 lx - cursor->x,
                 ly - cursor->y);
    tablet_emulate_button(event->time_msec, BTN_LEFT, 1);
}

void tablet_handle_touch_frame(struct wl_listener*, void*)
{
    wlr_seat_touch_notify_frame(seat);
}

void tablet_handle_touch_motion(struct wl_listener*, void* data)
{
    struct wlr_touch_motion_event* event = data;
    double lx, ly;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_cursor_absolute_to_layout_coords(
        cursor, &event->touch->base, event->x, event->y, &lx, &ly);
    if (event->touch_id == tablet_emulated_touch) {
        input_motion(event->time_msec,
                     &event->touch->base,
                     lx - cursor->x,
                     ly - cursor->y,
                     lx - cursor->x,
                     ly - cursor->y);
        wlr_seat_pointer_notify_frame(seat);
        return;
    }
    if (seat->drag && seat->drag->grab_type == WLR_DRAG_GRAB_KEYBOARD_TOUCH &&
        seat->drag->touch_id == event->touch_id) {
        struct wlr_surface* surface = nullptr;
        double sx = 0, sy = 0;

        wlr_scene_node_set_position(
            &drag_icon->node, (int)round(lx), (int)round(ly));
        input_node_at(lx, ly, &surface, nullptr, nullptr, &sx, &sy);
        if (surface)
            wlr_seat_touch_point_focus(
                seat, surface, event->time_msec, event->touch_id, sx, sy);
        else
            wlr_seat_touch_point_clear_focus(
                seat, event->time_msec, event->touch_id);
        wlr_seat_touch_notify_motion(
            seat, event->time_msec, event->touch_id, sx, sy);
        return;
    }
    wlr_seat_touch_notify_motion(
        seat,
        event->time_msec,
        event->touch_id,
        lx - tablet_touch_points[event->touch_id & 15].ox,
        ly - tablet_touch_points[event->touch_id & 15].oy);
}

static int tablet_touch_taken(struct wlr_surface* surface)
{
    struct wlr_seat_client* client = wlr_seat_client_for_wl_client(
        seat, wl_resource_get_client(surface->resource));

    return client && !wl_list_empty(&client->touches);
}

void tablet_handle_touch_up(struct wl_listener*, void* data)
{
    struct wlr_touch_up_event* event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    if (event->touch_id == tablet_emulated_touch) {
        tablet_emulated_touch = -1;
        tablet_emulate_button(event->time_msec, BTN_LEFT, 0);
        return;
    }
    if (wlr_seat_touch_get_point(seat, event->touch_id))
        wlr_seat_touch_notify_up(seat, event->time_msec, event->touch_id);
}
