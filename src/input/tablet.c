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
    struct wlr_tablet_v2_tablet* protocol_tablet; /* NULL if creation failed */
    struct wl_list link;                          /* tablets */
    struct wl_listener destroy;
} TabletDevice;

typedef struct {
    struct wlr_tablet_v2_tablet_tool* protocol_tool; /* NULL for a totem */
    double surface_layout_x, surface_layout_y;       /* layout position of the surface it is on */
    double tilt_x, tilt_y;     /* wlroots only reports the axis that changed */
    int is_emulating_pointer;  /* the tip went down as a left click */
    uint32_t emulated_buttons; /* 1 << (button & 31), sent as pointer buttons */
    struct wl_listener set_cursor;
    struct wl_listener destroy;
} TabletTool;

typedef struct {
    struct wlr_tablet_v2_tablet_pad* protocol_pad;
    struct wlr_input_device* device;
    TabletDevice* tablet; /* NULL until its tablet shows up */
    struct wlr_surface* focused_surface;
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
} TouchDevice;

/* function declarations */
static void tablet_emulate_button(uint32_t time, uint32_t button, int pressed);
static struct wlr_output* tablet_output_named(const char* name);
static void tablet_pad_attach(TabletPad* pad);
static void tablet_handle_pad_button(struct wl_listener* listener, void* data);
static void tablet_handle_pad_destroy(struct wl_listener* listener, void*);
static void tablet_pad_focus(TabletPad* pad, struct wlr_surface* surface);
static void tablet_handle_pad_ring(struct wl_listener* listener, void* data);
static void tablet_handle_pad_strip(struct wl_listener* listener, void* data);
static void tablet_handle_pad_unfocus(struct wl_listener* listener, void*);
static int tablet_same_group(struct wlr_input_device* first, struct wlr_input_device* second);
static uint32_t tablet_stylus_button(uint32_t button);
static void tablet_configure(TabletDevice* tablet);
static void tablet_handle_destroy(struct wl_listener* listener, void*);
static int tablet_tip_bound(void);
static TabletTool* tablet_tool_get(struct wlr_tablet_tool* wlr_tool);
static void tablet_handle_tool_destroy(struct wl_listener* listener, void*);
static void tablet_tool_motion(TabletTool* tool,
                               TabletDevice* tablet,
                               uint32_t time,
                               double x,
                               double y);
static void tablet_handle_tool_set_cursor(struct wl_listener* listener, void* data);
static void tablet_handle_touch_destroy(struct wl_listener* listener, void*);
static int tablet_touch_taken(struct wlr_surface* surface);

/* variables */
struct wlr_tablet_manager_v2* tablet_manager;
static struct wl_list tablet_devices = { &tablet_devices, &tablet_devices };
static struct wl_list tablet_pads = { &tablet_pads, &tablet_pads };
static struct wl_list tablet_touch_devices = { &tablet_touch_devices, &tablet_touch_devices };
/* surface origin of each touch point */
/* ponytail: indexed by id & 15, libinput hands out small slot numbers */
static struct TouchPointOrigin {
    double surface_layout_x, surface_layout_y;
} tablet_touch_points[16];
static int32_t tablet_emulated_touch = -1; /* the touch point driving the pointer */

/* function implementations */
void tablet_create(struct wlr_tablet* wlr_tablet)
{
    TabletDevice* tablet = util_calloc(1, sizeof(*tablet));

    tablet->wlr_tablet = wlr_tablet;
    tablet->protocol_tablet = wlr_tablet_create(tablet_manager, seat, &wlr_tablet->base);
    wlr_tablet->data = tablet;
    wl_list_insert(&tablet_devices, &tablet->link);
    LISTEN(&wlr_tablet->base.events.destroy, &tablet->destroy, tablet_handle_destroy);

    wlr_cursor_attach_input_device(cursor, &wlr_tablet->base);
    tablet_configure(tablet);
    TabletPad* pad;
    wl_list_for_each(pad, &tablet_pads, link)
        if (!pad->tablet)
            tablet_pad_attach(pad);
}

void tablet_create_pad(struct wlr_tablet_pad* wlr_pad)
{
    TabletPad* pad = util_calloc(1, sizeof(*pad));

    pad->device = &wlr_pad->base;
    pad->protocol_pad = wlr_tablet_pad_create(tablet_manager, seat, pad->device);
    wl_list_insert(&tablet_pads, &pad->link);
    wl_list_init(&pad->focus_destroy.link);
    LISTEN(&wlr_pad->events.button, &pad->button, tablet_handle_pad_button);
    LISTEN(&wlr_pad->events.ring, &pad->ring, tablet_handle_pad_ring);
    LISTEN(&wlr_pad->events.strip, &pad->strip, tablet_handle_pad_strip);
    LISTEN(&pad->device->events.destroy, &pad->destroy, tablet_handle_pad_destroy);
    tablet_pad_attach(pad);
}

void tablet_create_touch(struct wlr_touch* wlr_touch)
{
    TouchDevice* touch = util_calloc(1, sizeof(*touch));

    touch->wlr_touch = wlr_touch;
    wl_list_insert(&tablet_touch_devices, &touch->link);
    LISTEN(&wlr_touch->base.events.destroy, &touch->destroy, tablet_handle_touch_destroy);
    wlr_cursor_attach_input_device(cursor, &wlr_touch->base);
    tablet_remap();
}

static void tablet_emulate_button(uint32_t time, uint32_t button, int pressed)
{
    struct wlr_pointer_button_event synth = {
        .time_msec = time,
        .button = button,
        .state = pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED,
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

    if (!name)
        return nullptr;
    Monitor* monitor;
    wl_list_for_each(monitor, &monitors, link) {
        if (monitor->wlr_output->enabled && !strcmp(monitor->wlr_output->name, name))
            return monitor->wlr_output;
    }
    return nullptr;
}

static void tablet_pad_attach(TabletPad* pad)
{
    tablet_pad_focus(pad, nullptr);
    pad->tablet = nullptr;
    TabletDevice* tablet;
    wl_list_for_each(tablet, &tablet_devices, link) {
        if (tablet_same_group(pad->device, &tablet->wlr_tablet->base)) {
            pad->tablet = tablet;
            break;
        }
    }
    tablet_pad_focus(pad, seat->keyboard_state.focused_surface);
}

static void tablet_handle_pad_button(struct wl_listener* listener, void* data)
{
    TabletPad* pad = wl_container_of(listener, pad, button);
    struct wlr_tablet_pad_button_event* event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    /* bound buttons are not forwarded */
    for (const TabletPadBinding *pad_binding = settings->bindings.tablet_pad_bindings,
                                *end = pad_binding + settings->bindings.tablet_pad_binding_count;
         !lock_active && pad_binding < end; pad_binding++) {
        if (pad_binding->button == event->button && pad_binding->callback) {
            if (event->state == WLR_BUTTON_PRESSED)
                pad_binding->callback(&pad_binding->argument);
            return;
        }
    }
    wlr_tablet_v2_tablet_pad_notify_mode(pad->protocol_pad, event->group, event->mode,
                                         event->time_msec);
    wlr_tablet_v2_tablet_pad_notify_button(pad->protocol_pad, event->button, event->time_msec,
                                           event->state == WLR_BUTTON_PRESSED
                                               ? ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED
                                               : ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED);
}

static void tablet_handle_pad_destroy(struct wl_listener* listener, void*)
{
    TabletPad* pad = wl_container_of(listener, pad, destroy);

    wl_list_remove(&pad->focus_destroy.link);
    wl_list_remove(&pad->button.link);
    wl_list_remove(&pad->ring.link);
    wl_list_remove(&pad->strip.link);
    wl_list_remove(&pad->destroy.link);
    wl_list_remove(&pad->link);
    free(pad);
}

/* pads follow keyboard focus */
static void tablet_pad_focus(TabletPad* pad, struct wlr_surface* surface)
{
    if (surface == pad->focused_surface)
        return;
    if (pad->focused_surface) {
        wlr_tablet_v2_tablet_pad_notify_leave(pad->protocol_pad, pad->focused_surface);
        wl_list_remove(&pad->focus_destroy.link);
        wl_list_init(&pad->focus_destroy.link);
        pad->focused_surface = nullptr;
    }
    if (!surface || !pad->tablet || !pad->tablet->protocol_tablet ||
        !wlr_surface_accepts_tablet_v2(surface, pad->tablet->protocol_tablet))
        return;
    wlr_tablet_v2_tablet_pad_notify_enter(pad->protocol_pad, pad->tablet->protocol_tablet, surface);
    pad->focused_surface = surface;
    LISTEN(&surface->events.destroy, &pad->focus_destroy, tablet_handle_pad_unfocus);
}

void tablet_handle_keyboard_focus_change(struct wl_listener*, void* data)
{
    struct wlr_seat_keyboard_focus_change_event* event = data;
    TabletPad* pad;

    wl_list_for_each(pad, &tablet_pads, link)
        tablet_pad_focus(pad, event->new_surface);
}

static void tablet_handle_pad_ring(struct wl_listener* listener, void* data)
{
    TabletPad* pad = wl_container_of(listener, pad, ring);
    struct wlr_tablet_pad_ring_event* event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_tablet_v2_tablet_pad_notify_ring(pad->protocol_pad, event->ring, event->position,
                                         event->source == WLR_TABLET_PAD_RING_SOURCE_FINGER,
                                         event->time_msec);
}

static void tablet_handle_pad_strip(struct wl_listener* listener, void* data)
{
    TabletPad* pad = wl_container_of(listener, pad, strip);
    struct wlr_tablet_pad_strip_event* event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_tablet_v2_tablet_pad_notify_strip(pad->protocol_pad, event->strip, event->position,
                                          event->source == WLR_TABLET_PAD_STRIP_SOURCE_FINGER,
                                          event->time_msec);
}

static void tablet_handle_pad_unfocus(struct wl_listener* listener, void*)
{
    TabletPad* pad = wl_container_of(listener, pad, focus_destroy);

    wl_list_remove(&pad->focus_destroy.link);
    wl_list_init(&pad->focus_destroy.link);
    pad->focused_surface = nullptr;
}

/* no device groups outside libinput (nested): match any tablet */
static int tablet_same_group(struct wlr_input_device* first, struct wlr_input_device* second)
{
    if (!wlr_input_device_is_libinput(first) || !wlr_input_device_is_libinput(second))
        return 1;
    return libinput_device_get_device_group(wlr_libinput_get_device_handle(first)) ==
           libinput_device_get_device_group(wlr_libinput_get_device_handle(second));
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
    TabletTool* tool = tablet_tool_get(event->tool);
    uint32_t axes = event->updated_axes;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    /* An axis event only carries the axes it actually updated; NAN tells
     * wlroots to keep the current coordinate for the others. */
    if (axes & (WLR_TABLET_TOOL_AXIS_X | WLR_TABLET_TOOL_AXIS_Y))
        tablet_tool_motion(tool, event->tablet->data, event->time_msec,
                           axes & WLR_TABLET_TOOL_AXIS_X ? event->x : NAN,
                           axes & WLR_TABLET_TOOL_AXIS_Y ? event->y : NAN);

    if (!tool->protocol_tool || !tool->protocol_tool->focused_surface)
        return;
    if (axes & WLR_TABLET_TOOL_AXIS_PRESSURE)
        wlr_tablet_v2_tablet_tool_notify_pressure(tool->protocol_tool, event->pressure);
    if (axes & WLR_TABLET_TOOL_AXIS_DISTANCE)
        wlr_tablet_v2_tablet_tool_notify_distance(tool->protocol_tool, event->distance);
    if (axes & WLR_TABLET_TOOL_AXIS_TILT_X)
        tool->tilt_x = event->tilt_x;
    if (axes & WLR_TABLET_TOOL_AXIS_TILT_Y)
        tool->tilt_y = event->tilt_y;
    if (axes & (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y))
        wlr_tablet_v2_tablet_tool_notify_tilt(tool->protocol_tool, tool->tilt_x, tool->tilt_y);
    if (axes & WLR_TABLET_TOOL_AXIS_ROTATION)
        wlr_tablet_v2_tablet_tool_notify_rotation(tool->protocol_tool, event->rotation);
    if (axes & WLR_TABLET_TOOL_AXIS_SLIDER)
        wlr_tablet_v2_tablet_tool_notify_slider(tool->protocol_tool, event->slider);
    if (axes & WLR_TABLET_TOOL_AXIS_WHEEL)
        wlr_tablet_v2_tablet_tool_notify_wheel(tool->protocol_tool, event->wheel_delta, 0);
}

void tablet_handle_button(struct wl_listener*, void* data)
{
    struct wlr_tablet_tool_button_event* event = data;
    TabletTool* tool = tablet_tool_get(event->tool);
    uint32_t bit = 1u << (event->button & 31);
    int pressed = event->state == WLR_BUTTON_PRESSED;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    /* released the same way it was pressed */
    if (pressed ? !tool->protocol_tool || !tool->protocol_tool->focused_surface ||
                      tool->is_emulating_pointer
                : tool->emulated_buttons & bit) {
        if (pressed)
            tool->emulated_buttons |= bit;
        else
            tool->emulated_buttons &= ~bit;
        tablet_emulate_button(event->time_msec, tablet_stylus_button(event->button), pressed);
        return;
    }
    if (!tool->protocol_tool)
        return;
    wlr_tablet_v2_tablet_tool_notify_button(tool->protocol_tool, event->button,
                                            pressed ? ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED
                                                    : ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED);
    wlr_tablet_tool_v2_start_implicit_grab(tool->protocol_tool);
}

static void tablet_configure(TabletDevice* tablet)
{
    struct wlr_input_device* device = &tablet->wlr_tablet->base;

    wlr_cursor_map_input_to_output(cursor, device,
                                   tablet_output_named(settings->input.tablet_output));
    if (!wlr_input_device_is_libinput(device))
        return;
    struct libinput_device* handle = wlr_libinput_get_device_handle(device);
    if (handle && libinput_device_config_left_handed_is_available(handle))
        libinput_device_config_left_handed_set(handle, settings->input.tablet_left_handed);
}

static void tablet_handle_destroy(struct wl_listener* listener, void*)
{
    TabletDevice* tablet = wl_container_of(listener, tablet, destroy);

    wl_list_remove(&tablet->destroy.link);
    wl_list_remove(&tablet->link);
    TabletPad* pad;
    wl_list_for_each(pad, &tablet_pads, link)
        if (pad->tablet == tablet)
            tablet_pad_attach(pad);
    free(tablet);
}

void tablet_handle_proximity(struct wl_listener*, void* data)
{
    struct wlr_tablet_tool_proximity_event* event = data;
    TabletTool* tool = tablet_tool_get(event->tool);

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    if (event->state == WLR_TABLET_TOOL_PROXIMITY_IN)
        tablet_tool_motion(tool, event->tablet->data, event->time_msec, event->x, event->y);
    else if (tool->protocol_tool)
        wlr_tablet_v2_tablet_tool_notify_proximity_out(tool->protocol_tool);
}

void tablet_remap(void)
{
    TabletDevice* tablet;

    wl_list_for_each(tablet, &tablet_devices, link)
        tablet_configure(tablet);
    TouchDevice* touch;
    wl_list_for_each(touch, &tablet_touch_devices, link) {
        wlr_cursor_map_input_to_output(cursor, &touch->wlr_touch->base,
                                       tablet_output_named(settings->input.touch_output
                                                               ? settings->input.touch_output
                                                               : touch->wlr_touch->output_name));
    }
}

void tablet_handle_tip(struct wl_listener*, void* data)
{
    struct wlr_tablet_tool_tip_event* event = data;
    TabletTool* tool = tablet_tool_get(event->tool);

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    if (event->state == WLR_TABLET_TOOL_TIP_UP) {
        if (tool->is_emulating_pointer) {
            tool->is_emulating_pointer = 0;
            tablet_emulate_button(event->time_msec, BTN_LEFT, 0);
            tablet_tool_motion(tool, event->tablet->data, event->time_msec, NAN, NAN);
        } else if (tool->protocol_tool) {
            wlr_tablet_v2_tablet_tool_notify_up(tool->protocol_tool);
        }
        return;
    }

    WindowClient* client;
    if (tool->protocol_tool && tool->protocol_tool->focused_surface && !tablet_tip_bound()) {
        /* a stroke focuses like a click */
        selected_monitor = monitor_at(cursor->x, cursor->y);
        input_node_at(cursor->x, cursor->y, nullptr, &client, nullptr, nullptr, nullptr);
        if (!lock_active && client && (!client_is_unmanaged(client) || client_wants_focus(client)))
            client_focus(client, 1);
        wlr_tablet_v2_tablet_tool_notify_down(tool->protocol_tool);
        wlr_tablet_tool_v2_start_implicit_grab(tool->protocol_tool);
        return;
    }
    /* bound click or no tablet-v2 surface: act as the pointer until tip up */
    if (tool->protocol_tool && tool->protocol_tool->focused_surface) {
        wlr_tablet_v2_tablet_tool_notify_proximity_out(tool->protocol_tool);
        input_motion(event->time_msec, &event->tablet->base, 0, 0, 0, 0);
    }
    tool->is_emulating_pointer = 1;
    tablet_emulate_button(event->time_msec, BTN_LEFT, 1);
}

static int tablet_tip_bound(void)
{
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;

    if (lock_active)
        return 0;
    for (const MouseBinding *binding = settings->bindings.mouse_bindings,
                            *end = binding + settings->bindings.mouse_binding_count;
         binding < end; binding++)
        if (binding->click_area == CLICK_AREA_CLIENT && binding->button == BTN_LEFT &&
            binding->callback && CLEANMASK(mods) == CLEANMASK(binding->modifiers))
            return 1;
    return 0;
}

static TabletTool* tablet_tool_get(struct wlr_tablet_tool* wlr_tool)
{
    TabletTool* tool = wlr_tool->data;

    if (tool)
        return tool;
    tool = util_calloc(1, sizeof(*tool));
    wl_list_init(&tool->set_cursor.link);
    if ((tool->protocol_tool = wlr_tablet_tool_create(tablet_manager, seat, wlr_tool)))
        LISTEN(&tool->protocol_tool->events.set_cursor, &tool->set_cursor,
               tablet_handle_tool_set_cursor);
    LISTEN(&wlr_tool->events.destroy, &tool->destroy, tablet_handle_tool_destroy);
    wlr_tool->data = tool;
    return tool;
}

static void tablet_handle_tool_destroy(struct wl_listener* listener, void*)
{
    TabletTool* tool = wl_container_of(listener, tool, destroy);

    wl_list_remove(&tool->set_cursor.link);
    wl_list_remove(&tool->destroy.link);
    free(tool);
}

int tablet_tool_focused_by(struct wlr_tablet_v2_tablet_tool* tool, struct wlr_seat_client* client)
{
    return tool && tool->focused_surface && client &&
           wl_resource_get_client(tool->focused_surface->resource) == client->client;
}

/* tablet-v2 surfaces get the pen, anything else gets it as the pointer */
static void tablet_tool_motion(TabletTool* tool,
                               TabletDevice* tablet,
                               uint32_t time,
                               double x,
                               double y)
{
    struct wlr_input_device* device = &tablet->wlr_tablet->base;
    struct wlr_surface* surface = nullptr;
    WindowClient* client = nullptr;
    double surface_x = 0;
    double surface_y = 0;
    const int grabbed =
        tool->protocol_tool && wlr_tablet_tool_v2_has_implicit_grab(tool->protocol_tool);

    double layout_x;
    double layout_y;
    wlr_cursor_absolute_to_layout_coords(cursor, device, x, y, &layout_x, &layout_y);
    if (tool->protocol_tool && tablet->protocol_tablet && !tool->is_emulating_pointer &&
        input_cursor_mode == CURSOR_MODE_NORMAL && !traypopup_active())
        input_node_at(layout_x, layout_y, &surface, &client, nullptr, &surface_x, &surface_y);
    if (surface && !wlr_surface_accepts_tablet_v2(surface, tablet->protocol_tablet))
        surface = nullptr;

    if (!surface && !grabbed) {
        if (tool->protocol_tool)
            wlr_tablet_v2_tablet_tool_notify_proximity_out(tool->protocol_tool);
        input_motion(time, device, layout_x - cursor->x, layout_y - cursor->y, layout_x - cursor->x,
                     layout_y - cursor->y);
        /* Tablets emit no frame event of their own, and clients hold
         * pointer events back until one arrives. */
        wlr_seat_pointer_notify_frame(seat);
        return;
    }

    wlr_cursor_warp_closest(cursor, device, layout_x, layout_y);
    input_cursor_moved();
    bar_title_hover(client, cursor->x, cursor->y);
    if (!grabbed) {
        /* the pen and the pointer are not both on an app at once */
        if (seat->pointer_state.focused_surface)
            wlr_seat_pointer_notify_clear_focus(seat);
        if (surface != tool->protocol_tool->focused_surface && settings->input.sloppy_focus &&
            client && !client_is_unmanaged(client))
            client_focus(client, 0);
        tool->surface_layout_x = layout_x - surface_x;
        tool->surface_layout_y = layout_y - surface_y;
    }
    if (surface)
        wlr_tablet_v2_tablet_tool_notify_proximity_in(tool->protocol_tool, tablet->protocol_tablet,
                                                      surface);
    else
        wlr_tablet_v2_tablet_tool_notify_proximity_out(tool->protocol_tool);
    wlr_tablet_v2_tablet_tool_notify_motion(tool->protocol_tool, cursor->x - tool->surface_layout_x,
                                            cursor->y - tool->surface_layout_y);
}

static void tablet_handle_tool_set_cursor(struct wl_listener* listener, void* data)
{
    TabletTool* tool = wl_container_of(listener, tool, set_cursor);
    struct wlr_tablet_v2_event_cursor* event = data;

    if (input_cursor_mode == CURSOR_MODE_NORMAL &&
        tablet_tool_focused_by(tool->protocol_tool, event->seat_client))
        input_set_client_cursor(event->surface, event->hotspot_x, event->hotspot_y);
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
    TouchDevice* touch = wl_container_of(listener, touch, destroy);

    wl_list_remove(&touch->destroy.link);
    wl_list_remove(&touch->link);
    free(touch);
    input_update_seat_caps();
}

void tablet_handle_touch_down(struct wl_listener*, void* data)
{
    struct wlr_touch_down_event* event = data;
    struct wlr_surface* surface = nullptr;
    WindowClient* client = nullptr;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    double layout_x;
    double layout_y;
    wlr_cursor_absolute_to_layout_coords(cursor, &event->touch->base, event->x, event->y, &layout_x,
                                         &layout_y);
    double surface_x;
    double surface_y;
    if (input_cursor_mode == CURSOR_MODE_NORMAL && !traypopup_active())
        input_node_at(layout_x, layout_y, &surface, &client, nullptr, &surface_x, &surface_y);

    if (surface && tablet_touch_taken(surface) &&
        wlr_seat_touch_notify_down(seat, surface, event->time_msec, event->touch_id, surface_x,
                                   surface_y)) {
        tablet_touch_points[event->touch_id & 15].surface_layout_x = layout_x - surface_x;
        tablet_touch_points[event->touch_id & 15].surface_layout_y = layout_y - surface_y;
        selected_monitor = monitor_at(layout_x, layout_y);
        if (!lock_active && client && (!client_is_unmanaged(client) || client_wants_focus(client)))
            client_focus(client, 1);
        return;
    }
    /* no touch support there: emulate a click, one finger at a time */
    if (tablet_emulated_touch >= 0)
        return;
    tablet_emulated_touch = event->touch_id;
    input_motion(event->time_msec, &event->touch->base, layout_x - cursor->x, layout_y - cursor->y,
                 layout_x - cursor->x, layout_y - cursor->y);
    tablet_emulate_button(event->time_msec, BTN_LEFT, 1);
}

void tablet_handle_touch_frame(struct wl_listener*, void*)
{
    wlr_seat_touch_notify_frame(seat);
}

void tablet_handle_touch_motion(struct wl_listener*, void* data)
{
    struct wlr_touch_motion_event* event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    double layout_x;
    double layout_y;
    wlr_cursor_absolute_to_layout_coords(cursor, &event->touch->base, event->x, event->y, &layout_x,
                                         &layout_y);
    if (event->touch_id == tablet_emulated_touch) {
        input_motion(event->time_msec, &event->touch->base, layout_x - cursor->x,
                     layout_y - cursor->y, layout_x - cursor->x, layout_y - cursor->y);
        wlr_seat_pointer_notify_frame(seat);
        return;
    }
    if (seat->drag && seat->drag->grab_type == WLR_DRAG_GRAB_KEYBOARD_TOUCH &&
        seat->drag->touch_id == event->touch_id) {
        struct wlr_surface* surface = nullptr;
        double surface_x = 0;
        double surface_y = 0;

        wlr_scene_node_set_position(&drag_icon->node, (int)round(layout_x), (int)round(layout_y));
        input_node_at(layout_x, layout_y, &surface, nullptr, nullptr, &surface_x, &surface_y);
        if (surface)
            wlr_seat_touch_point_focus(seat, surface, event->time_msec, event->touch_id, surface_x,
                                       surface_y);
        else
            wlr_seat_touch_point_clear_focus(seat, event->time_msec, event->touch_id);
        wlr_seat_touch_notify_motion(seat, event->time_msec, event->touch_id, surface_x, surface_y);
        return;
    }
    wlr_seat_touch_notify_motion(
        seat, event->time_msec, event->touch_id,
        layout_x - tablet_touch_points[event->touch_id & 15].surface_layout_x,
        layout_y - tablet_touch_points[event->touch_id & 15].surface_layout_y);
}

static int tablet_touch_taken(struct wlr_surface* surface)
{
    struct wlr_seat_client* client =
        wlr_seat_client_for_wl_client(seat, wl_resource_get_client(surface->resource));

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
