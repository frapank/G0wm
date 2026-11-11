/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: unixchad
 * SPDX-FileCopyrightText: Ben Collerson
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * input devices, the seat, selections and drag and drop
 */

#include <libinput.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "input/keyboard.h"
#include "input/pointer.h"
#include "input/seat.h"
#include "input/tablet.h"
#include "internal.h"
#include "output/monitor.h"
#include "window/client.h"

/* function declarations */
static void input_create_pointer(struct wlr_pointer* pointer);
static void input_handle_drag_icon_destroy(struct wl_listener* listener, void*);

/* function implementations */
static void input_create_pointer(struct wlr_pointer* pointer)
{
    struct libinput_device* device;
    int touchpad;
    if (wlr_input_device_is_libinput(&pointer->base) &&
        (device = wlr_libinput_get_device_handle(&pointer->base))) {

        /* only touchpads can tap */
        touchpad = libinput_device_config_tap_get_finger_count(device) > 0;
        if (touchpad) {
            libinput_device_config_tap_set_enabled(device, settings->input.tap_to_click);
            libinput_device_config_tap_set_drag_enabled(device, settings->input.tap_and_drag);
            libinput_device_config_tap_set_drag_lock_enabled(device, settings->input.drag_lock);
            libinput_device_config_tap_set_button_map(device, settings->input.tap_button_map);
        }

        if (libinput_device_config_scroll_has_natural_scroll(device))
            libinput_device_config_scroll_set_natural_scroll_enabled(
                device, touchpad ? settings->input.touchpad_natural_scrolling
                                 : settings->input.natural_scrolling);

        if (libinput_device_config_dwt_is_available(device))
            libinput_device_config_dwt_set_enabled(device, settings->input.disable_while_typing);

        if (libinput_device_config_left_handed_is_available(device))
            libinput_device_config_left_handed_set(device, settings->input.left_handed);

        if (libinput_device_config_middle_emulation_is_available(device))
            libinput_device_config_middle_emulation_set_enabled(
                device, settings->input.middle_button_emulation);

        if (libinput_device_config_scroll_get_methods(device) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
            libinput_device_config_scroll_set_method(device, settings->input.scroll_method);

        if (libinput_device_config_click_get_methods(device) != LIBINPUT_CONFIG_CLICK_METHOD_NONE)
            libinput_device_config_click_set_method(device, settings->input.click_method);

        if (libinput_device_config_send_events_get_modes(device))
            libinput_device_config_send_events_set_mode(device, settings->input.send_events_mode);

        if (libinput_device_config_accel_is_available(device)) {
            libinput_device_config_accel_set_profile(
                device,
                touchpad ? settings->input.touchpad_accel_profile : settings->input.accel_profile);
            libinput_device_config_accel_set_speed(device,
                                                   touchpad ? settings->input.touchpad_accel_speed
                                                            : settings->input.accel_speed);
        }
    }

    wlr_cursor_attach_input_device(cursor, &pointer->base);
}

static void input_handle_drag_icon_destroy(struct wl_listener* listener, void*)
{
    /* Focus enter isn't sent during drag, so refocus the focused node. */
    client_focus(client_top(selected_monitor), 1);
    input_motion(0, nullptr, 0, 0, 0, 0);
    wl_list_remove(&listener->link);
    free(listener);
}

void input_handle_new_input(struct wl_listener*, void* data)
{
    /* This event is raised by the backend when a new input device becomes
     * available. */
    struct wlr_input_device* device = data;

    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            input_create_keyboard(wlr_keyboard_from_input_device(device));
            break;
        case WLR_INPUT_DEVICE_POINTER:
            input_create_pointer(wlr_pointer_from_input_device(device));
            break;
        case WLR_INPUT_DEVICE_TABLET:
            tablet_create(wlr_tablet_from_input_device(device));
            break;
        case WLR_INPUT_DEVICE_TABLET_PAD:
            tablet_create_pad(wlr_tablet_pad_from_input_device(device));
            break;
        case WLR_INPUT_DEVICE_TOUCH:
            tablet_create_touch(wlr_touch_from_input_device(device));
            break;
        default:
            /* switches are ignored */
            break;
    }
    input_update_seat_caps();
}

void input_update_seat_caps(void)
{
    /* We need to let the wlr_seat know what our capabilities are, which is
     * communiciated to the client. In g0wm we always have a cursor, even if
     * there are no pointer devices, so we always include that capability. */
    /* TODO do we actually require a cursor? */
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!keyboard_group) /* shutting down */
        return;
    if (!wl_list_empty(&keyboard_group->wlr_group->devices))
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    if (tablet_has_touch())
        caps |= WL_SEAT_CAPABILITY_TOUCH;
    wlr_seat_set_capabilities(seat, caps);
}

void input_handle_request_start_drag(struct wl_listener*, void* data)
{
    struct wlr_seat_request_start_drag_event* event = data;
    struct wlr_touch_point* point;

    if (wlr_seat_validate_pointer_grab_serial(seat, event->origin, event->serial))
        wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
    else if (wlr_seat_validate_touch_grab_serial(seat, event->origin, event->serial, &point))
        wlr_seat_start_touch_drag(seat, event->drag, event->serial, point);
    else
        wlr_data_source_destroy(event->drag->source);
}

void input_handle_request_primary_selection(struct wl_listener*, void* data)
{
    /* This event is raised by the seat when a client wants to set the
     * selection, usually when the user copies something. wlroots allows
     * compositors to ignore such requests if they so choose, but in g0wm we
     * always honor them
     */
    struct wlr_seat_request_set_primary_selection_event* event = data;
    wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

void input_handle_request_selection(struct wl_listener*, void* data)
{
    /* This event is raised by the seat when a client wants to set the
     * selection, usually when the user copies something. wlroots allows
     * compositors to ignore such requests if they so choose, but in g0wm we
     * always honor them
     */
    struct wlr_seat_request_set_selection_event* event = data;
    wlr_seat_set_selection(seat, event->source, event->serial);
}

void input_handle_start_drag(struct wl_listener*, void* data)
{
    struct wlr_drag* drag = data;
    if (!drag->icon)
        return;

    drag->icon->data = &wlr_scene_drag_icon_create(drag_icon, drag->icon)->node;
    LISTEN_STATIC(&drag->icon->events.destroy, input_handle_drag_icon_destroy);
}

void input_handle_new_virtual_pointer(struct wl_listener*, void* data)
{
    struct wlr_virtual_pointer_v1_new_pointer_event* event = data;
    struct wlr_input_device* device = &event->new_pointer->pointer.base;

    wlr_cursor_attach_input_device(cursor, device);
    if (event->suggested_output)
        wlr_cursor_map_input_to_output(cursor, device, event->suggested_output);
}
