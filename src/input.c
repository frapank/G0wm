/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: unixchad
 * SPDX-FileCopyrightText: Ben Collerson
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * keyboard, pointer, gesture and drag-and-drop handling
 */
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/util/box.h>
#include <wlr/util/region.h>
#include <xkbcommon/xkbcommon.h>

#include "bar.h"
#include "client.h"
#include "g0wm.h"
#include "input.h"
#include "layout.h"
#include "lock.h"
#include "magnifier.h"
#include "monitor.h"
#include "runner.h"
#include "settings.h"
#include "systray/tray.h"
#include "tablet.h"
#include "textinput.h"
#include "traypopup.h"
#include "util.h"

typedef struct {
    struct wlr_pointer_constraint_v1* constraint;
    struct wl_listener destroy;
} PointerConstraint;

/* function declarations */
static unsigned int input_bar_click(Monitor* pm, Arg* arg);
static void input_create_keyboard(struct wlr_keyboard* keyboard);
static void input_create_pointer(struct wlr_pointer* pointer);
static void input_cursor_constrain(
    struct wlr_pointer_constraint_v1* constraint);
static int input_gesture_bound(int pinch, uint32_t fingers);
static void input_gesture_run(unsigned int motion);
static void input_cursor_warp_to_hint(void);
static void input_handle_drag_icon_destroy(struct wl_listener* listener,
                                           void* data);
static void input_handle_pointer_constraint_destroy(
    struct wl_listener* listener,
    void* data);
static void input_grab(Client* c, unsigned int mode);
static void input_cursor_activity(void);
static int input_hide_cursor(void* data);
static struct wlr_keyboard_shortcuts_inhibitor_v1* input_focused_inhibitor(
    void);
static int input_key_binding(uint32_t mods, xkb_keysym_t sym);
static void input_handle_key(struct wl_listener* listener, void* data);
static void input_handle_modifiers(struct wl_listener* listener, void* data);
static int input_key_repeat(void* data);
static void input_magnet(struct wlr_box* b);
static void input_magnet_edge(int pos, int size, int edge, int* dist, int* to);
static void input_pointer_focus(Client* c,
                                struct wlr_surface* surface,
                                double sx,
                                double sy,
                                uint32_t time);
static void input_handle_last_cursor_destroy(struct wl_listener* listener,
                                             void* data);
static int input_wheel_binding(const struct wlr_pointer_axis_event* event);

/* variables */
struct last_cursor_state input_last_cursor;
unsigned int input_cursor_mode;
Client* input_grab_client;
static struct wlr_pointer_constraint_v1* input_active_constraint;
static bool input_cursor_hidden = false;
static int input_grab_x, input_grab_y; /* client-relative */
static double input_press_x, input_press_y;
/* gesture taken by a binding or by the magnifier */
static struct {
    int ours;
    uint32_t fingers;
    double dx, dy, scale;
    float zoom; /* magnifier level the pinch began at, 0 if not zooming */
} input_gesture;
static uint32_t
    input_runner_repeat_codepoint; /* codepoint the armed key repeat types */

/* function implementations */
/* The part of pm's bar the cursor is over, ClkRoot if it is not over it.
 * arg picks up the tag or tray item the part stands for. */
static unsigned int input_bar_click(Monitor* pm, Arg* arg)
{
    unsigned int i = 0, x = 0;
    double cx;
    int traywidth = 0, statusw;
    struct wlr_scene_node* node;
    struct wlr_scene_buffer* buffer;
    /* under bar_single_monitor the bar shows the focused monitor's tags */
    Monitor* s = bar_single_monitor && selected_monitor ? selected_monitor : pm;

    if (exclusive_focus || !pm ||
        !(node = wlr_scene_node_at(&layers[LyrBottom]->node,
                                   cursor->x,
                                   cursor->y,
                                   nullptr,
                                   nullptr)) ||
        !(buffer = wlr_scene_buffer_from_node(node)) ||
        buffer != pm->scene_buffer)
        return ClkRoot;

    cx = (cursor->x - pm->m.x - bar_padding) * pm->wlr_output->scale;
    traywidth = tray_get_width(pm->tray);
    statusw = bar_status_shown(pm) ? STATUSW(pm) : 0;
    do
        x += bar_tag_width(pm, s, i);
    while (cx >= x && ++i < tag_count);
    if (i < tag_count) {
        arg->ui = 1 << i;
        return ClkTagBar;
    } else if (bar_layout_symbol && cx < x + TEXTW(pm, s->ltsymbol))
        return ClkLtSymbol;
    else if (traywidth && cx > pm->b.width - traywidth) {
        arg->ui = tray_index_at(pm->tray, cx - (pm->b.width - traywidth));
        return ClkTray;
    } else if (cx > pm->b.width - (statusw + traywidth)) {
        arg->i = (int)(cx - (pm->b.width - (statusw + traywidth)));
        return ClkStatus;
    }
    /* x + 1, so 0 still means "from the keyboard" */
    arg->ui = (unsigned int)cx + 1;
    return ClkTitle;
}

void input_handle_axis(struct wl_listener*, void* data)
{
    /* This event is forwarded by the cursor when a pointer emits an axis event,
     * for example when you move the scroll wheel. */
    struct wlr_pointer_axis_event* event = data;
    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    input_cursor_activity();
    {
        Client* c;
        Arg arg;
        Monitor* pm = monitor_at(cursor->x, cursor->y);
        struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
        int horizontal =
            event->orientation == WL_POINTER_AXIS_HORIZONTAL_SCROLL ||
            (keyboard &&
             wlr_keyboard_get_modifiers(keyboard) & WLR_MODIFIER_SHIFT);

        input_node_at(
            cursor->x, cursor->y, nullptr, &c, nullptr, nullptr, nullptr);
        if (!lock_active && !c && input_bar_click(pm, &arg) == ClkTitle &&
            bar_notification_wheel(pm, event->delta, horizontal))
            return;
    }
    if (!lock_active && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        Client* c;
        Arg arg;
        Monitor* pm = monitor_at(cursor->x, cursor->y);

        input_node_at(
            cursor->x, cursor->y, nullptr, &c, nullptr, nullptr, nullptr);
        if (!c && input_bar_click(pm, &arg) == ClkStatus &&
            bar_status_scroll(pm, arg.i, event->delta))
            return;
    }
    if (!lock_active && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL &&
        input_wheel_binding(event))
        return;
    /* Notify the client with pointer focus of the axis event. */
    wlr_seat_pointer_notify_axis(seat,
                                 event->time_msec,
                                 event->orientation,
                                 event->delta,
                                 event->delta_discrete,
                                 event->source,
                                 event->relative_direction);
}

void input_handle_button(struct wl_listener*, void* data)
{
    unsigned int click;
    struct wlr_pointer_button_event* event = data;
    struct wlr_keyboard* keyboard;
    Monitor* pm; /* the monitor the pointer is on, bar included */
    uint32_t mods;
    Arg arg = {
        .v = nullptr
    }; /* .v is the widest member: zeroes the whole union */
    Client* c;
    const Button* b;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    input_cursor_activity();

    /* the open menu owns the pointer, nothing under it sees the click */
    if (!lock_active && traypopup_active()) {
        if (event->state == WL_POINTER_BUTTON_STATE_PRESSED)
            traypopup_click(cursor->x, cursor->y);
        return;
    }

    click = ClkRoot;
    input_node_at(cursor->x, cursor->y, nullptr, &c, nullptr, nullptr, nullptr);
    if (c)
        click = ClkClient;

    switch (event->state) {
        case WL_POINTER_BUTTON_STATE_PRESSED:
            input_cursor_mode = CurPressed;
            pm = monitor_at(cursor->x, cursor->y);
            if (lock_active) {
                selected_monitor = pm;
                break;
            }

            if (!c && (click = input_bar_click(pm, &arg)) != ClkRoot) {
                /* The bar belongs to pm, but under bar_single_monitor it stands
                 * in for the focused monitor: leaving selected_monitor alone is
                 * what lands the click on the monitor whose tags are on show.
                 */
                if (!bar_single_monitor || !selected_monitor)
                    selected_monitor = pm;
            } else {
                selected_monitor = pm;
            }

            /* Change focus if the button was _pressed_ over a client */
            input_node_at(
                cursor->x, cursor->y, nullptr, &c, nullptr, nullptr, nullptr);
            if (click == ClkClient &&
                (!client_is_unmanaged(c) || client_wants_focus(c)))
                client_focus(c, 1);

            if (click == ClkStatus &&
                bar_status_click(pm,
                                 arg.i,
                                 event->button == BTN_LEFT     ? 1
                                 : event->button == BTN_RIGHT  ? 2
                                 : event->button == BTN_MIDDLE ? 3
                                                               : 0))
                return;

            keyboard = wlr_seat_get_keyboard(seat);
            mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
            if (click == ClkClient && !CLEANMASK(mods) &&
                event->button == BTN_LEFT &&
                bar_title_click(c, cursor->x, cursor->y))
                return;
            for (b = buttons; b < buttons + button_count; b++) {
                if (CLEANMASK(mods) == CLEANMASK(b->mod) &&
                    event->button == b->button && click == b->click &&
                    b->func) {
                    b->func((click == ClkTagBar || click == ClkTray ||
                             click == ClkTitle) &&
                                    b->arg.i == 0
                                ? &arg
                                : &b->arg);
                    return;
                }
            }
            break;
        case WL_POINTER_BUTTON_STATE_RELEASED:
            /* If you released any buttons, we exit interactive move/resize
             * mode. */
            /* TODO: should reset to the pointer focus's current
             * input_handle_request_cursor */
            if (!lock_active && input_cursor_mode != CurNormal &&
                input_cursor_mode != CurPressed) {
                wlr_cursor_set_xcursor(cursor, cursor_manager, "default");
                input_cursor_mode = CurNormal;
                /* Drop the window off on its new monitor */
                selected_monitor = monitor_at(cursor->x, cursor->y);
                client_set_monitor(input_grab_client, selected_monitor, 0);
                input_grab_client = nullptr;
                return;
            }
            input_cursor_mode = CurNormal;
            input_grab_client = nullptr;
            break;
    }
    /* If the event wasn't handled by the compositor, notify the client with
     * pointer focus that a button press has occurred */
    wlr_seat_pointer_notify_button(
        seat, event->time_msec, event->button, event->state);
}

static void input_create_keyboard(struct wlr_keyboard* keyboard)
{
    /* Set the keymap to match the group keymap */
    wlr_keyboard_set_keymap(keyboard,
                            keyboard_group->wlr_group->keyboard.keymap);

    /* Add the new keyboard to the group */
    wlr_keyboard_group_add_keyboard(keyboard_group->wlr_group, keyboard);
}

KeyboardGroup* input_create_keyboard_group(void)
{
    KeyboardGroup* group = util_calloc(1, sizeof(*group));
    struct xkb_context* context;
    struct xkb_keymap* keymap;

    group->wlr_group = wlr_keyboard_group_create();
    group->wlr_group->data = group;

    /* Prepare an XKB keymap and assign it to the keyboard group. */
    context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!(keymap = xkb_keymap_new_from_names(
              context, &xkb_rules, XKB_KEYMAP_COMPILE_NO_FLAGS)))
        util_die("failed to compile keymap");

    wlr_keyboard_set_keymap(&group->wlr_group->keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);

    wlr_keyboard_set_repeat_info(
        &group->wlr_group->keyboard, repeat_rate, repeat_delay);

    /* Set up listeners for keyboard events */
    LISTEN(
        &group->wlr_group->keyboard.events.key, &group->key, input_handle_key);
    LISTEN(&group->wlr_group->keyboard.events.modifiers,
           &group->modifiers,
           input_handle_modifiers);

    group->key_repeat_source =
        wl_event_loop_add_timer(event_loop, input_key_repeat, group);

    /* A seat can only have one keyboard, but this is a limitation of the
     * Wayland protocol - not wlroots. We assign all connected keyboards to the
     * same wlr_keyboard_group, which provides a single wlr_keyboard interface
     * for all of them. Set this combined wlr_keyboard as the seat keyboard.
     */
    wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
    return group;
}

void input_handle_new_keyboard_inhibitor(struct wl_listener*, void* data)
{
    wlr_keyboard_shortcuts_inhibitor_v1_activate(data);
}

static void input_create_pointer(struct wlr_pointer* pointer)
{
    struct libinput_device* device;
    int touchpad;
    if (wlr_input_device_is_libinput(&pointer->base) &&
        (device = wlr_libinput_get_device_handle(&pointer->base))) {

        /* only touchpads can tap */
        touchpad = libinput_device_config_tap_get_finger_count(device) > 0;
        if (touchpad) {
            libinput_device_config_tap_set_enabled(device, tap_to_click);
            libinput_device_config_tap_set_drag_enabled(device, tap_and_drag);
            libinput_device_config_tap_set_drag_lock_enabled(device, drag_lock);
            libinput_device_config_tap_set_button_map(device, tap_button_map);
        }

        if (libinput_device_config_scroll_has_natural_scroll(device))
            libinput_device_config_scroll_set_natural_scroll_enabled(
                device,
                touchpad ? touchpad_natural_scrolling : natural_scrolling);

        if (libinput_device_config_dwt_is_available(device))
            libinput_device_config_dwt_set_enabled(device,
                                                   disable_while_typing);

        if (libinput_device_config_left_handed_is_available(device))
            libinput_device_config_left_handed_set(device, left_handed);

        if (libinput_device_config_middle_emulation_is_available(device))
            libinput_device_config_middle_emulation_set_enabled(
                device, middle_button_emulation);

        if (libinput_device_config_scroll_get_methods(device) !=
            LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
            libinput_device_config_scroll_set_method(device, scroll_method);

        if (libinput_device_config_click_get_methods(device) !=
            LIBINPUT_CONFIG_CLICK_METHOD_NONE)
            libinput_device_config_click_set_method(device, click_method);

        if (libinput_device_config_send_events_get_modes(device))
            libinput_device_config_send_events_set_mode(device,
                                                        send_events_mode);

        if (libinput_device_config_accel_is_available(device)) {
            libinput_device_config_accel_set_profile(
                device, touchpad ? touchpad_accel_profile : accel_profile);
            libinput_device_config_accel_set_speed(
                device, touchpad ? touchpad_accel_speed : accel_speed);
        }
    }

    wlr_cursor_attach_input_device(cursor, &pointer->base);
}

void input_handle_new_pointer_constraint(struct wl_listener*, void* data)
{
    PointerConstraint* pointer_constraint =
        util_calloc(1, sizeof(*pointer_constraint));
    pointer_constraint->constraint = data;
    LISTEN(&pointer_constraint->constraint->events.destroy,
           &pointer_constraint->destroy,
           input_handle_pointer_constraint_destroy);
}

static void input_cursor_constrain(struct wlr_pointer_constraint_v1* constraint)
{
    if (input_active_constraint == constraint)
        return;

    if (input_active_constraint)
        wlr_pointer_constraint_v1_send_deactivated(input_active_constraint);

    input_active_constraint = constraint;
    wlr_pointer_constraint_v1_send_activated(constraint);
}

void input_handle_frame(struct wl_listener*, void*)
{
    /* This event is forwarded by the cursor when a pointer emits a frame
     * event. Frame events are sent after regular pointer events to group
     * multiple events together. For instance, two axis events may happen at the
     * same time, in which case a frame event won't be sent in between. */
    /* Notify the client with pointer focus of the frame event. */
    wlr_seat_pointer_notify_frame(seat);
}

/* Gestures with a binding are not sent to the client, the binding runs when
 * the fingers lift. */

constexpr double SWIPE_MIN = 100.0;
constexpr double PINCH_IN = 0.8;
constexpr double PINCH_OUT = 1.25;

static int input_gesture_bound(int pinch, uint32_t fingers)
{
    const Gesture* g;

    if (lock_active)
        return 0;
    for (g = gestures; g < gestures + gesture_count; g++)
        if ((g->motion >= PinchIn) == pinch && g->fingers == fingers)
            return 1;
    return 0;
}

static void input_gesture_run(unsigned int motion)
{
    const Gesture* g;

    for (g = gestures; g < gestures + gesture_count; g++)
        if (g->motion == motion && g->fingers == input_gesture.fingers &&
            g->func)
            g->func(&g->arg);
}

void input_handle_swipe_begin(struct wl_listener*, void* data)
{
    struct wlr_pointer_swipe_begin_event* event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    if ((input_gesture.ours = input_gesture_bound(0, event->fingers))) {
        input_gesture.fingers = event->fingers;
        input_gesture.dx = input_gesture.dy = 0;
        return;
    }
    wlr_pointer_gestures_v1_send_swipe_begin(
        pointer_gestures, seat, event->time_msec, event->fingers);
}

void input_handle_swipe_update(struct wl_listener*, void* data)
{
    struct wlr_pointer_swipe_update_event* event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    if (input_gesture.ours) {
        input_gesture.dx += event->dx;
        input_gesture.dy += event->dy;
        return;
    }
    wlr_pointer_gestures_v1_send_swipe_update(
        pointer_gestures, seat, event->time_msec, event->dx, event->dy);
}

void input_handle_swipe_end(struct wl_listener*, void* data)
{
    struct wlr_pointer_swipe_end_event* event = data;

    if (!input_gesture.ours) {
        wlr_pointer_gestures_v1_send_swipe_end(
            pointer_gestures, seat, event->time_msec, event->cancelled);
        return;
    }
    input_gesture.ours = 0;
    if (event->cancelled || lock_active ||
        MAX(fabs(input_gesture.dx), fabs(input_gesture.dy)) < SWIPE_MIN)
        return;
    if (fabs(input_gesture.dx) >= fabs(input_gesture.dy))
        input_gesture_run(input_gesture.dx < 0 ? SwipeLeft : SwipeRight);
    else
        input_gesture_run(input_gesture.dy < 0 ? SwipeUp : SwipeDown);
}

void input_handle_pinch_begin(struct wl_listener*, void* data)
{
    struct wlr_pointer_pinch_begin_event* event = data;
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
    Monitor* m;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    if (!lock_active && pinch_zoom_modifier &&
        CLEANMASK(mods) == CLEANMASK(pinch_zoom_modifier) &&
        (m = monitor_at(cursor->x, cursor->y))) {
        input_gesture.zoom = m->zoom;
        return;
    }
    if ((input_gesture.ours = input_gesture_bound(1, event->fingers))) {
        input_gesture.fingers = event->fingers;
        input_gesture.scale = 1.0;
        return;
    }
    wlr_pointer_gestures_v1_send_pinch_begin(
        pointer_gestures, seat, event->time_msec, event->fingers);
}

void input_handle_pinch_update(struct wl_listener*, void* data)
{
    struct wlr_pointer_pinch_update_event* event = data;
    Monitor* m;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    if (input_gesture.zoom) {
        /* the zoom follows the fingers */
        if ((m = monitor_at(cursor->x, cursor->y)))
            magnifier_zoom(m, input_gesture.zoom * (float)event->scale);
        return;
    }
    if (input_gesture.ours) {
        input_gesture.scale = event->scale;
        return;
    }
    wlr_pointer_gestures_v1_send_pinch_update(pointer_gestures,
                                              seat,
                                              event->time_msec,
                                              event->dx,
                                              event->dy,
                                              event->scale,
                                              event->rotation);
}

void input_handle_pinch_end(struct wl_listener*, void* data)
{
    struct wlr_pointer_pinch_end_event* event = data;

    if (input_gesture.zoom) {
        input_gesture.zoom = 0;
        return;
    }
    if (!input_gesture.ours) {
        wlr_pointer_gestures_v1_send_pinch_end(
            pointer_gestures, seat, event->time_msec, event->cancelled);
        return;
    }
    input_gesture.ours = 0;
    if (event->cancelled || lock_active)
        return;
    if (input_gesture.scale <= PINCH_IN)
        input_gesture_run(PinchIn);
    else if (input_gesture.scale >= PINCH_OUT)
        input_gesture_run(PinchOut);
}

void input_handle_hold_begin(struct wl_listener*, void* data)
{
    struct wlr_pointer_hold_begin_event* event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_pointer_gestures_v1_send_hold_begin(
        pointer_gestures, seat, event->time_msec, event->fingers);
}

void input_handle_hold_end(struct wl_listener*, void* data)
{
    struct wlr_pointer_hold_end_event* event = data;

    wlr_pointer_gestures_v1_send_hold_end(
        pointer_gestures, seat, event->time_msec, event->cancelled);
}

static void input_cursor_warp_to_hint(void)
{
    Client* c = nullptr;
    double sx = input_active_constraint->current.cursor_hint.x;
    double sy = input_active_constraint->current.cursor_hint.y;

    client_from_surface(input_active_constraint->surface, &c, nullptr);
    if (c && input_active_constraint->current.cursor_hint.enabled) {
        wlr_cursor_warp(
            cursor, nullptr, sx + c->geom.x + c->bw, sy + c->geom.y + c->bw);
        wlr_seat_pointer_warp(input_active_constraint->seat, sx, sy);
    }
}

static void input_handle_drag_icon_destroy(struct wl_listener* listener, void*)
{
    /* Focus enter isn't sent during drag, so refocus the focused node. */
    client_focus(client_top(selected_monitor), 1);
    input_motion(0, nullptr, 0, 0, 0, 0);
    wl_list_remove(&listener->link);
    free(listener);
}

static void input_handle_pointer_constraint_destroy(
    struct wl_listener* listener,
    void*)
{
    PointerConstraint* pointer_constraint =
        wl_container_of(listener, pointer_constraint, destroy);

    if (input_active_constraint == pointer_constraint->constraint) {
        input_cursor_warp_to_hint();
        input_active_constraint = nullptr;
    }

    wl_list_remove(&pointer_constraint->destroy.link);
    free(pointer_constraint);
}

void input_handle_keyboard_group_destroy(struct wl_listener* listener, void*)
{
    KeyboardGroup* group = wl_container_of(listener, group, destroy);
    wl_event_source_remove(group->key_repeat_source);
    wl_list_remove(&group->key.link);
    wl_list_remove(&group->modifiers.link);
    wl_list_remove(&group->destroy.link);
    wlr_keyboard_group_destroy(group->wlr_group);
    free(group);
}

static void input_cursor_activity(void)
{
    if (!input_cursor_hidden)
        return;

    input_cursor_hidden = false;

    if (input_last_cursor.shape) {
        wlr_cursor_set_xcursor(
            cursor,
            cursor_manager,
            wlr_cursor_shape_v1_name(input_last_cursor.shape));
    } else if (input_last_cursor.has_client_cursor) {
        /* surface may be NULL here: the client explicitly hid its cursor
         * (e.g. a game with pointer lock), which must be honored rather
         * than falling back to the default arrow below. */
        wlr_cursor_set_surface(cursor,
                               input_last_cursor.surface,
                               input_last_cursor.hotspot_x,
                               input_last_cursor.hotspot_y);
    } else {
        wlr_cursor_set_xcursor(cursor, cursor_manager, "default");
    }
}

static int input_hide_cursor(void*)
{
    wlr_cursor_unset_image(cursor);
    input_cursor_hidden = true;
    return 1;
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

static struct wlr_keyboard_shortcuts_inhibitor_v1* input_focused_inhibitor(void)
{
    struct wlr_keyboard_shortcuts_inhibitor_v1* inhibitor;

    wl_list_for_each(inhibitor, &keyboard_inhibit_manager->inhibitors, link)
    {
        if (inhibitor->seat == seat &&
            inhibitor->surface == seat->keyboard_state.focused_surface)
            return inhibitor;
    }
    return nullptr;
}

static int input_key_binding(uint32_t mods, xkb_keysym_t sym)
{
    /*
     * Here we handle compositor keybindings. This is when the compositor is
     * processing keys, rather than passing them on to the client for its own
     * processing.
     */
    const Key* k;
    struct wlr_keyboard_shortcuts_inhibitor_v1* inhibitor =
        input_focused_inhibitor();
    int inhibited = inhibitor && inhibitor->active;

    for (k = keys; k < keys + key_count; k++) {
        if (CLEANMASK(mods) == CLEANMASK(k->mod) &&
            xkb_keysym_to_lower(sym) == xkb_keysym_to_lower(k->keysym) &&
            k->func && (!inhibited || k->func == input_toggle_inhibit)) {
            k->func(&k->arg);
            return 1;
        }
    }
    return 0;
}

static void input_handle_key(struct wl_listener* listener, void* data)
{
    int i;
    /* This event is raised when a key is pressed or released. */
    KeyboardGroup* group = wl_container_of(listener, group, key);
    struct wlr_keyboard_key_event* event = data;

    /* Translate libinput keycode -> xkbcommon */
    uint32_t keycode = event->keycode + 8;
    /* Get a list of keysyms based on the keymap for this keyboard */
    const xkb_keysym_t* syms;
    int nsyms = xkb_state_key_get_syms(
        group->wlr_group->keyboard.xkb_state, keycode, &syms);

    int handled = 0;
    uint32_t mods = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

    /* hide cursor when typing starts */
    if (hide_cursor_when_typing && !input_cursor_hidden &&
        event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
        input_hide_cursor(nullptr);

    /* While the prompt is open, every key belongs to it: swallow press and
     * release instead of matching keybindings or forwarding to the client.
     * Returning early skips the repeat bookkeeping below, so disarm here what
     * that would have disarmed: the keystroke that opened the prompt counted
     * as handled and left a repeat armed, which would replay the binding and
     * toggle the prompt straight back off. */
    if (runner_active) {
        if (!lock_active && nsyms > 0 &&
            event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            /* only the primary keysym: the codepoint belongs to the key, not
             * to each of the syms it can produce, so looping would type it
             * once per sym */
            uint32_t codepoint = xkb_state_key_get_utf32(
                group->wlr_group->keyboard.xkb_state, keycode);
            runner_key(syms[0], mods, codepoint);
            /* Arm the repeat for the prompt the way a handled binding does,
             * so a held key (backspace above all) repeats into it. Return and
             * Escape close the prompt from inside runner_key(), and there is
             * nothing left to repeat into once they have. */
            if (runner_active &&
                group->wlr_group->keyboard.repeat_info.delay > 0) {
                group->mods = mods;
                group->keysyms = syms;
                group->nsyms = nsyms;
                input_runner_repeat_codepoint = codepoint;
                runner_repeating = 1;
                wl_event_source_timer_update(
                    group->key_repeat_source,
                    group->wlr_group->keyboard.repeat_info.delay);
                return;
            }
        }
        group->nsyms = 0;
        runner_repeating = 0;
        wl_event_source_timer_update(group->key_repeat_source, 0);
        return;
    }

    /* the open menu holds the keyboard, Escape closes it */
    if (!lock_active && traypopup_active()) {
        if (nsyms > 0 && event->state == WL_KEYBOARD_KEY_STATE_PRESSED &&
            syms[0] == XKB_KEY_Escape)
            traypopup_dismiss();
        group->nsyms = 0;
        wl_event_source_timer_update(group->key_repeat_source, 0);
        return;
    }

    if (!lock_active && bar_notification_picking()) {
        if (nsyms > 0 && event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
            bar_notification_pick_key(syms[0]);
        group->nsyms = 0;
        wl_event_source_timer_update(group->key_repeat_source, 0);
        return;
    }

    /* On _press_ if there is no active screen locker,
     * attempt to process a compositor keybinding. */
    if (!lock_active && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (i = 0; i < nsyms; i++)
            handled = input_key_binding(mods, syms[i]) || handled;
    }

    if (handled && group->wlr_group->keyboard.repeat_info.delay > 0) {
        group->mods = mods;
        group->keysyms = syms;
        group->nsyms = nsyms;
        wl_event_source_timer_update(
            group->key_repeat_source,
            group->wlr_group->keyboard.repeat_info.delay);
    } else {
        group->nsyms = 0;
        wl_event_source_timer_update(group->key_repeat_source, 0);
    }

    if (handled)
        return;

    if (textinput_key(group, event))
        return;

    wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
    /* Pass unhandled keycodes along to the client. */
    wlr_seat_keyboard_notify_key(
        seat, event->time_msec, event->keycode, event->state);
}

static void input_handle_modifiers(struct wl_listener* listener, void*)
{
    /* This event is raised when a modifier key, such as shift or alt, is
     * pressed. We simply communicate this to the client. */
    KeyboardGroup* group = wl_container_of(listener, group, modifiers);

    if (textinput_modifiers(group))
        return;

    wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
    /* Send modifiers to the client. */
    wlr_seat_keyboard_notify_modifiers(seat,
                                       &group->wlr_group->keyboard.modifiers);
}

static int input_key_repeat(void* data)
{
    KeyboardGroup* group = data;
    int i;
    if (!group->nsyms || group->wlr_group->keyboard.repeat_info.rate <= 0)
        return 0;
    /* the repeat of the key that opened the picker would close it */
    if (bar_notification_picking())
        return 0;
    if (runner_active) {
        /* Holding the binding down past the repeat delay would replay it
         * while the prompt it just opened is up, closing it again: only a
         * repeat the prompt armed itself may run. Rearming after the fact
         * keeps a key that closed the prompt from repeating into nothing. */
        if (!runner_repeating)
            return 0;
        runner_key(
            group->keysyms[0], group->mods, input_runner_repeat_codepoint);
        if (runner_active)
            wl_event_source_timer_update(
                group->key_repeat_source,
                1000 / group->wlr_group->keyboard.repeat_info.rate);
        else
            group->nsyms = 0;
        return 0;
    }

    wl_event_source_timer_update(
        group->key_repeat_source,
        1000 / group->wlr_group->keyboard.repeat_info.rate);

    for (i = 0; i < group->nsyms; i++)
        input_key_binding(group->mods, group->keysyms[i]);

    return 0;
}

void input_handle_motion_absolute(struct wl_listener*, void* data)
{
    /* This event is forwarded by the cursor when a pointer emits an _absolute_
     * motion event, from 0..1 on each axis. This happens, for example, when
     * wlroots is running under a Wayland window rather than KMS+DRM, and you
     * move the mouse over the window. You could enter the window from any edge,
     * so we have to warp the mouse there. Also, some hardware emits these
     * events. */
    struct wlr_pointer_motion_absolute_event* event = data;
    double lx, ly, dx, dy;

    if (!event->time_msec) /* this is 0 with virtual pointers */
        wlr_cursor_warp_absolute(
            cursor, &event->pointer->base, event->x, event->y);

    wlr_cursor_absolute_to_layout_coords(
        cursor, &event->pointer->base, event->x, event->y, &lx, &ly);
    dx = lx - cursor->x;
    dy = ly - cursor->y;
    input_motion(event->time_msec, &event->pointer->base, dx, dy, dx, dy);
}

void input_cursor_moved(void)
{
    Monitor* m = monitor_at(cursor->x, cursor->y);

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    input_cursor_activity();

    /* the part of a zoomed monitor on show follows the pointer */
    if (m && m->zoom > 1)
        wlr_output_schedule_frame(m->wlr_output);

    /* Update selected_monitor (even while dragging a window) */
    if (sloppy_focus) {
        Monitor* old = selected_monitor;
        selected_monitor = monitor_at(cursor->x, cursor->y);
        /* the single bar follows the focus, and crossing into empty
         * space changes it without going through client_focus() */
        if ((bar_single_monitor || bar_status == BarStatusFocus) &&
            selected_monitor != old)
            bar_draw_all();
    }
}

void input_motion(uint32_t time,
                  struct wlr_input_device* device,
                  double dx,
                  double dy,
                  double dx_unaccel,
                  double dy_unaccel)
{
    double sx = 0, sy = 0, sx_confined, sy_confined;
    Client *c = nullptr, *w = nullptr;
    LayerSurface* l = nullptr;
    struct wlr_surface* surface = nullptr;
    struct wlr_pointer_constraint_v1* constraint;
    struct wlr_box box;

    /* Find the client under the pointer and send the event along. */
    input_node_at(cursor->x, cursor->y, &surface, &c, nullptr, &sx, &sy);
    bar_title_hover(c, cursor->x, cursor->y);

    if (input_cursor_mode == CurPressed && !seat->drag &&
        surface != seat->pointer_state.focused_surface &&
        client_from_surface(seat->pointer_state.focused_surface, &w, &l) >= 0) {
        c = w;
        surface = seat->pointer_state.focused_surface;
        sx = cursor->x - (l ? l->scene->node.x : w->geom.x);
        sy = cursor->y - (l ? l->scene->node.y : w->geom.y);
    }

    /* time is 0 in internal calls meant to restore pointer focus. */
    if (time) {
        wlr_relative_pointer_manager_v1_send_relative_motion(
            relative_pointer_manager,
            seat,
            (uint64_t)time * 1000,
            dx,
            dy,
            dx_unaccel,
            dy_unaccel);

        wl_list_for_each(constraint, &pointer_constraints->constraints, link)
            input_cursor_constrain(constraint);

        if (input_active_constraint && input_cursor_mode != CurResize &&
            input_cursor_mode != CurMove) {
            /* own variable: c is the client under the cursor, and the
             * constraint may sit on an unmapped one (gamescope) */
            Client* cc = nullptr;
            client_from_surface(input_active_constraint->surface, &cc, nullptr);
            if (cc && input_active_constraint->surface ==
                          seat->pointer_state.focused_surface) {
                sx = cursor->x - cc->geom.x - cc->bw;
                sy = cursor->y - cc->geom.y - cc->bw;
                if (wlr_region_confine(&input_active_constraint->region,
                                       sx,
                                       sy,
                                       sx + dx,
                                       sy + dy,
                                       &sx_confined,
                                       &sy_confined)) {
                    dx = sx_confined - sx;
                    dy = sy_confined - sy;
                }

                if (input_active_constraint->type ==
                    WLR_POINTER_CONSTRAINT_V1_LOCKED)
                    return;
            }
        }

        wlr_cursor_move(cursor, device, dx, dy);
        input_cursor_moved();
    }

    /* Update drag icon's position */
    wlr_scene_node_set_position(
        &drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));

    /* hovering the open menu, so nothing below it takes focus */
    if (!lock_active && traypopup_active()) {
        traypopup_motion(cursor->x, cursor->y);
        if (!seat->drag && !input_cursor_hidden)
            wlr_cursor_set_xcursor(cursor, cursor_manager, "default");
        return;
    }

    if (input_cursor_mode == CurPressed && input_grab_client && time &&
        !lock_active &&
        (cursor->x - input_press_x) * (cursor->x - input_press_x) +
                (cursor->y - input_press_y) * (cursor->y - input_press_y) >=
            move_threshold * move_threshold)
        input_grab(input_grab_client, CurMove);

    /* If we are currently grabbing the mouse, handle and return */
    if (input_cursor_mode == CurMove) {
        /* A tiled client keeps its slot in the layout: dragging it just swaps
         * it with the tile under the cursor, i3/sway style. */
        if (!input_grab_client->isfloating) {
            if (time && c && c != input_grab_client && !c->isfloating &&
                c->mon == input_grab_client->mon &&
                VISIBLEON(input_grab_client, input_grab_client->mon)) {
                layout_swap_clients(input_grab_client, c);
                layout_arrange(input_grab_client->mon);
            }
            return;
        }
        /* Move the grabbed client to the new position. */
        box = (struct wlr_box){ .x = (int)round(cursor->x) - input_grab_x,
                                .y = (int)round(cursor->y) - input_grab_y,
                                .width = input_grab_client->geom.width,
                                .height = input_grab_client->geom.height };
        input_magnet(&box);
        client_resize(input_grab_client, box, 1);
        return;
    } else if (input_cursor_mode == CurResize) {
        client_resize(
            input_grab_client,
            (struct wlr_box){
                .x = input_grab_client->geom.x,
                .y = input_grab_client->geom.y,
                .width = (int)round(cursor->x) - input_grab_client->geom.x,
                .height = (int)round(cursor->y) - input_grab_client->geom.y },
            1);
        return;
    }

    /* If there's no client surface under the cursor, set the cursor image to a
     * default. This is what makes the cursor image appear when you move it
     * off of a client or over its border. */
    if (!surface && !seat->drag && !input_cursor_hidden)
        wlr_cursor_set_xcursor(cursor, cursor_manager, "default");

    input_pointer_focus(c, surface, sx, sy, time);
}

void input_handle_motion(struct wl_listener*, void* data)
{
    /* This event is forwarded by the cursor when a pointer emits a _relative_
     * pointer motion event (i.e. a delta) */
    struct wlr_pointer_motion_event* event = data;
    /* The cursor doesn't move unless we tell it to. The cursor automatically
     * handles constraining the motion to the output layout, as well as any
     * special configuration applied for the specific input device which
     * generated the event. You can pass nullptr for the device if you want to
     * move the cursor around without any input. */
    input_motion(event->time_msec,
                 &event->pointer->base,
                 event->delta_x,
                 event->delta_y,
                 event->unaccel_dx,
                 event->unaccel_dy);
}

void input_move_resize(const Arg* arg)
{
    Client* c;

    if (input_cursor_mode != CurNormal && input_cursor_mode != CurPressed)
        return;
    input_node_at(cursor->x, cursor->y, nullptr, &c, nullptr, nullptr, nullptr);
    input_press_x = cursor->x;
    input_press_y = cursor->y;
    input_grab(c, arg->ui);
}

/* Moving c from the title bar waits for move_threshold of motion, so a plain
 * click stays a click: until then input_grab_client is set but
 * input_cursor_mode is CurPressed.
 */
void input_title_grab(Client* c)
{
    input_grab_client = c;
    input_press_x = cursor->x;
    input_press_y = cursor->y;
}

static void input_grab(Client* c, unsigned int mode)
{
    input_grab_client = nullptr;
    if (!c || client_is_unmanaged(c) || c->isfullscreen)
        return;
    input_grab_client = c;

    /* Float the window and tell input_motion to grab it - except when moving a
     * tiled client, which stays tiled and is swapped with the tile it is
     * dragged onto instead. */
    if (mode != CurMove || c->isfloating || !c->mon)
        client_set_floating(c, 1);
    switch (input_cursor_mode = mode) {
        case CurMove:
            input_grab_x = (int)round(input_press_x) - c->geom.x;
            input_grab_y = (int)round(input_press_y) - c->geom.y;
            wlr_cursor_set_xcursor(cursor, cursor_manager, "grabbing");
            break;
        case CurResize:
            /* Doesn't work for X11 output - the next absolute motion event
             * returns the cursor to where it started */
            wlr_cursor_warp_closest(cursor,
                                    nullptr,
                                    c->geom.x + c->geom.width,
                                    c->geom.y + c->geom.height);
            wlr_cursor_set_xcursor(cursor, cursor_manager, "se-resize");
            break;
    }
}

static void input_pointer_focus(Client* c,
                                struct wlr_surface* surface,
                                double sx,
                                double sy,
                                uint32_t time)
{
    struct timespec now;

    /* Only a real client surface takes focus: hovering a title bar (a tab) or
     * a border has no surface, and must not select anything until clicked. */
    if (surface && surface != seat->pointer_state.focused_surface &&
        sloppy_focus && time && c && !client_is_unmanaged(c))
        client_focus(c, 0);

    /* If surface is NULL, clear pointer focus */
    if (!surface) {
        wlr_seat_pointer_notify_clear_focus(seat);
        return;
    }

    if (!time) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        time = now.tv_sec * 1000 + now.tv_nsec / 1'000'000;
    }

    /* Let the client know that the mouse cursor has entered one
     * of its surfaces, and make keyboard focus follow if desired.
     * wlroots makes this a no-op if surface is already focused */
    wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

void input_handle_request_start_drag(struct wl_listener*, void* data)
{
    struct wlr_seat_request_start_drag_event* event = data;
    struct wlr_touch_point* point;

    if (wlr_seat_validate_pointer_grab_serial(
            seat, event->origin, event->serial))
        wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
    else if (wlr_seat_validate_touch_grab_serial(
                 seat, event->origin, event->serial, &point))
        wlr_seat_start_touch_drag(seat, event->drag, event->serial, point);
    else
        wlr_data_source_destroy(event->drag->source);
}

static void input_handle_last_cursor_destroy(struct wl_listener*, void*)
{
    /* When the surface is destroyed, clear our reference to it */
    input_last_cursor.surface = nullptr;
    wl_list_remove(&input_last_cursor.destroy.link);
    wl_list_init(&input_last_cursor.destroy.link);
}

void input_handle_request_cursor(struct wl_listener*, void* data)
{
    /* This event is raised by the seat when a client provides a cursor image */
    struct wlr_seat_pointer_request_set_cursor_event* event = data;
    /* If we're "grabbing" the cursor, don't use the client's image, we will
     * restore it after "grabbing" sending a leave event, followed by a enter
     * event, which will result in the client requesting set the cursor surface
     */
    if (input_cursor_mode != CurNormal && input_cursor_mode != CurPressed)
        return;
    /* This can be sent by any client, so we check to make sure this one
     * actually has pointer focus first. If so, we can tell the cursor to
     * use the provided surface as the cursor image. It will set the
     * hardware cursor on the output that it's currently on and continue to
     * do so as the cursor moves between outputs. */
    if (event->seat_client == seat->pointer_state.focused_client)
        input_set_client_cursor(
            event->surface, event->hotspot_x, event->hotspot_y);
}

void input_set_client_cursor(struct wlr_surface* surface,
                             int hotspot_x,
                             int hotspot_y)
{
    input_last_cursor.shape = 0;
    input_last_cursor.surface = surface;
    input_last_cursor.has_client_cursor = true;
    input_last_cursor.hotspot_x = hotspot_x;
    input_last_cursor.hotspot_y = hotspot_y;

    wl_list_remove(&input_last_cursor.destroy.link);
    wl_list_init(&input_last_cursor.destroy.link);
    if (surface) {
        input_last_cursor.destroy.notify = input_handle_last_cursor_destroy;
        wl_signal_add(&surface->events.destroy, &input_last_cursor.destroy);
    }

    if (!input_cursor_hidden)
        wlr_cursor_set_surface(cursor, surface, hotspot_x, hotspot_y);
}

void input_handle_request_cursor_shape(struct wl_listener*, void* data)
{
    struct wlr_cursor_shape_manager_v1_request_set_shape_event* event = data;
    if (input_cursor_mode != CurNormal && input_cursor_mode != CurPressed)
        return;
    /* This can be sent by any client, so we check to make sure this one
     * actually has pointer (or pen) focus first. If so, we can tell the
     * cursor to use the provided cursor shape. */
    if (event->device_type ==
                WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_TABLET_TOOL
            ? tablet_tool_focused_by(event->tablet_tool, event->seat_client)
            : event->seat_client == seat->pointer_state.focused_client) {
        input_last_cursor.shape = event->shape;
        input_last_cursor.surface = nullptr;

        if (!input_cursor_hidden)
            wlr_cursor_set_xcursor(
                cursor, cursor_manager, wlr_cursor_shape_v1_name(event->shape));
    }
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

void input_toggle_inhibit(const Arg*)
{
    struct wlr_keyboard_shortcuts_inhibitor_v1* inhibitor =
        input_focused_inhibitor();

    if (!inhibitor)
        return;
    if (inhibitor->active)
        wlr_keyboard_shortcuts_inhibitor_v1_deactivate(inhibitor);
    else
        wlr_keyboard_shortcuts_inhibitor_v1_activate(inhibitor);
}

void input_handle_new_virtual_keyboard(struct wl_listener*, void* data)
{
    struct wlr_virtual_keyboard_v1* kb = data;
    /* virtual keyboards shouldn't share keyboard group */
    KeyboardGroup* group = input_create_keyboard_group();
    group->vkb = kb;
    /* Set the keymap to match the group keymap */
    wlr_keyboard_set_keymap(&kb->keyboard, group->wlr_group->keyboard.keymap);
    LISTEN(&kb->keyboard.base.events.destroy,
           &group->destroy,
           input_handle_keyboard_group_destroy);

    /* Add the new keyboard to the group */
    wlr_keyboard_group_add_keyboard(group->wlr_group, &kb->keyboard);
}

void input_handle_new_virtual_pointer(struct wl_listener*, void* data)
{
    struct wlr_virtual_pointer_v1_new_pointer_event* event = data;
    struct wlr_input_device* device = &event->new_pointer->pointer.base;

    wlr_cursor_attach_input_device(cursor, device);
    if (event->suggested_output)
        wlr_cursor_map_input_to_output(cursor, device, event->suggested_output);
}

/* Sticks a moved floating window to the nearby edges of the window area and
 * of the floating windows beside it */
static void input_magnet(struct wlr_box* b)
{
    Monitor* m = monitor_at(cursor->x, cursor->y);
    Client* c;
    int dx = move_snap + 1, dy = move_snap + 1, x = b->x, y = b->y;

    if (!m || move_snap <= 0)
        return;
    input_magnet_edge(b->x, b->width, m->w.x, &dx, &x);
    input_magnet_edge(b->x, b->width, m->w.x + m->w.width, &dx, &x);
    input_magnet_edge(b->y, b->height, m->w.y, &dy, &y);
    input_magnet_edge(b->y, b->height, m->w.y + m->w.height, &dy, &y);
    wl_list_for_each(c, &clients, link)
    {
        if (c == input_grab_client || !c->isfloating || c->isfullscreen ||
            !VISIBLEON(c, m))
            continue;
        if (b->y < c->geom.y + c->geom.height && c->geom.y < b->y + b->height) {
            input_magnet_edge(b->x, b->width, c->geom.x, &dx, &x);
            input_magnet_edge(
                b->x, b->width, c->geom.x + c->geom.width, &dx, &x);
        }
        if (b->x < c->geom.x + c->geom.width && c->geom.x < b->x + b->width) {
            input_magnet_edge(b->y, b->height, c->geom.y, &dy, &y);
            input_magnet_edge(
                b->y, b->height, c->geom.y + c->geom.height, &dy, &y);
        }
    }
    b->x = x;
    b->y = y;
}

/* Either side of [pos, pos + size) onto edge, if closer than *dist */
static void input_magnet_edge(int pos, int size, int edge, int* dist, int* to)
{
    if (abs(edge - pos) < *dist) {
        *dist = abs(edge - pos);
        *to = edge;
    }
    if (abs(edge - pos - size) < *dist) {
        *dist = abs(edge - pos - size);
        *to = edge - size;
    }
}

void input_warp_cursor(const Client* c)
{
    if (input_cursor_mode != CurNormal)
        return;

    if (!c && selected_monitor) {
        wlr_cursor_warp_closest(
            cursor,
            nullptr,
            selected_monitor->w.x + selected_monitor->w.width / 2.0,
            selected_monitor->w.y + selected_monitor->w.height / 2.0);
    } else if (c && (cursor->x < c->geom.x ||
                     cursor->x > c->geom.x + c->geom.width ||
                     cursor->y < c->geom.y ||
                     cursor->y > c->geom.y + c->geom.height)) {
        wlr_cursor_warp_closest(cursor,
                                nullptr,
                                c->geom.x + c->geom.width / 2.0,
                                c->geom.y + c->geom.height / 2.0);
    }
}

/* Runs the button bound to the wheel, if any. A notch is 120 in value120,
 * smooth scrolling without notches counts one every 15 units, the angle
 * libinput gives a notch, so a touchpad steps at about a wheel's pace. */
static int input_wheel_binding(const struct wlr_pointer_axis_event* event)
{
    static double notches;
    double step = event->delta_discrete ? event->delta_discrete / 120.0
                                        : event->delta / 15.0;
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
    unsigned int wheel = event->delta < 0 ? WheelUp : WheelDown;
    unsigned int click, mode;
    int title;
    const Button* b;
    Client* c;
    Arg arg;

    input_node_at(cursor->x, cursor->y, nullptr, &c, nullptr, nullptr, nullptr);
    click =
        c ? ClkClient : input_bar_click(monitor_at(cursor->x, cursor->y), &arg);
    title = c && bar_on_title_bar(c, cursor->x, cursor->y);
    for (b = buttons; b < buttons + button_count; b++) {
        if (CLEANMASK(mods) != CLEANMASK(b->mod) || b->button != wheel ||
            (b->click != click && !(b->click == ClkWinTitle && title)) ||
            !b->func)
            continue;
        if ((notches < 0) != (step < 0))
            notches = 0;
        /* pressed, as for a click, so focusing does not warp the pointer
         * away from where it scrolls */
        mode = input_cursor_mode;
        input_cursor_mode = CurPressed;
        for (notches += step; fabs(notches) >= 1;
             notches -= notches < 0 ? -1 : 1)
            b->func(&b->arg);
        input_cursor_mode = mode;
        return 1;
    }
    return 0;
}

void input_node_at(double x,
                   double y,
                   struct wlr_surface** psurface,
                   Client** pc,
                   LayerSurface** pl,
                   double* nx,
                   double* ny)
{
    struct wlr_scene_node *node, *pnode;
    struct wlr_surface* surface = nullptr;
    struct wlr_scene_surface* scene_surface = nullptr;
    Client* c = nullptr;
    LayerSurface* l = nullptr;
    int layer;

    for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
        if (!(node = wlr_scene_node_at(&layers[layer]->node, x, y, nx, ny)))
            continue;

        if (node->type == WLR_SCENE_NODE_BUFFER) {
            scene_surface = wlr_scene_surface_try_from_buffer(
                wlr_scene_buffer_from_node(node));
            if (!scene_surface) {
                /* A title bar carries no surface but knows its client */
                for (pnode = node; pnode && !c; pnode = &pnode->parent->node)
                    c = pnode->data;
                if (c && c->type == LayerShell)
                    c = nullptr;
                if (c)
                    break;
                continue;
            }
            surface = scene_surface->surface;
        }
        /* Walk the tree to find a node that knows the client */
        for (pnode = node; pnode && !c; pnode = &pnode->parent->node)
            c = pnode->data;
        if (c && c->type == LayerShell) {
            c = nullptr;
            l = pnode->data;
        }
    }

    if (psurface)
        *psurface = surface;
    if (pc)
        *pc = c;
    if (pl)
        *pl = l;
}
