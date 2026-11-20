/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "input/tap.h"
#include "core/g0wm.h"
#include "core/settings.h"
#include "desktop/bar/notification.h"
#include "desktop/runner/runner.h"
#include "desktop/traypopup.h"
#include "input/keyboard.h"
#include "output/lock.h"
#include "window/client.h"

static InputTap* input_taps;

void input_tap_cancel(InputTap* tap)
{
    if (tap->timer)
        wl_event_source_timer_update(tap->timer, 0);
    tap->count = 0;
    tap->callback = nullptr;
}

void input_tap_destroy(InputTap* tap)
{
    if (!tap->timer)
        return;
    wl_event_source_remove(tap->timer);
    for (InputTap** link = &input_taps; *link; link = &(*link)->next) {
        if (*link == tap) {
            *link = tap->next;
            break;
        }
    }
    *tap = (InputTap){};
}

void input_taps_cancel(void)
{
    for (InputTap* tap = input_taps; tap; tap = tap->next)
        input_tap_cancel(tap);
}

void input_taps_destroy(void)
{
    while (input_taps)
        input_tap_destroy(input_taps);
}

static int input_tap_fire(void* data)
{
    InputTap* tap = data;
    void (*callback)(const ActionArg*) = tap->callback;
    ActionArg argument = tap->argument;
    int allowed = tap->area || input_key_binding_allowed(callback);
    input_tap_cancel(tap);
    if (callback && allowed && !lock_active && !runner_active && !traypopup_active() &&
        !bar_notification_picking() && tap->monitor == selected_monitor &&
        (tap->area != CLICK_AREA_CLIENT + 1 || tap->target == client_top(selected_monitor)))
        callback(&argument);
    return 0;
}

unsigned int input_tap_begin(InputTap* tap,
                             uint32_t modifiers,
                             uint32_t code,
                             unsigned int area,
                             void* target,
                             ActionArg context,
                             uint32_t time)
{
    if (tap->count &&
        (tap->modifiers != modifiers || tap->code != code || tap->area != area ||
         tap->target != target || tap->context.unsigned_integer != context.unsigned_integer ||
         time - tap->time > (uint32_t)settings->input.tap_delay))
        input_tap_fire(tap);
    tap->modifiers = modifiers;
    tap->code = code;
    tap->area = area;
    tap->target = target;
    tap->monitor = selected_monitor;
    tap->context = context;
    tap->time = time;
    return ++tap->count;
}

int input_tap_end(InputTap* tap, void (*callback)(const ActionArg*), ActionArg argument, int higher)
{
    tap->callback = callback;
    tap->argument = argument;
    if (!higher) {
        input_tap_cancel(tap);
        if (callback)
            callback(&argument);
        return callback != nullptr;
    }
    if (!tap->timer) {
        tap->timer = wl_event_loop_add_timer(event_loop, input_tap_fire, tap);
        if (!tap->timer) {
            input_tap_cancel(tap);
            return 1;
        }
        tap->next = input_taps;
        input_taps = tap;
    }
    wl_event_source_timer_update(tap->timer, settings->input.tap_delay);
    return 1;
}
