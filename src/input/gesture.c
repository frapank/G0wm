/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: unixchad
 * SPDX-FileCopyrightText: Ben Collerson
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * touchpad swipe, pinch and hold gestures
 */

#include <math.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_seat.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "input/gesture.h"
#include "input/pointer.h"
#include "output/lock.h"
#include "output/magnifier.h"
#include "output/monitor.h"

/* Gestures with a binding are not sent to the client, the binding runs when
 * the fingers lift. */
constexpr double SWIPE_MIN = 100.0;

constexpr double PINCH_IN = 0.8;

constexpr double PINCH_OUT = 1.25;

/* function declarations */
static int input_gesture_bound(int pinch, uint32_t fingers);
static void input_gesture_run(unsigned int motion);

/* variables */
/* gesture taken by a binding or by the magnifier */
static struct {
    int ours;
    uint32_t fingers;
    double dx, dy, scale;
    float zoom; /* magnifier level the pinch began at, 0 if not zooming */
} input_gesture;

/* function implementations */
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
