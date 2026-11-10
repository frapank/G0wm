/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the notification on screen, the queue behind it and the history
 */

#include <dbus/dbus.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wayland-server-core.h>

#include "desktop/notify/notify.h"
#include "desktop/notify/queue.h"
#include "desktop/systray/icon.h"
#include "internal.h"

/* function implementations */
void notify_closed(dbus_uint32_t id, dbus_uint32_t reason)
{
    DBusMessage* sig;

    if (!id || !(sig = dbus_message_new_signal(NOTIFY_OPATH, NOTIFY_IFACE, "NotificationClosed")))
        return;
    if (dbus_message_append_args(sig, DBUS_TYPE_UINT32, &id, DBUS_TYPE_UINT32, &reason,
                                 DBUS_TYPE_INVALID))
        dbus_connection_send(notify_state.connection, sig, nullptr);
    dbus_message_unref(sig);
}

void notify_release(Notification* notification)
{
    if (notification->icon)
        icon_destroy(notification->icon);
    notification->icon = nullptr;
}

/* same id, or same app and stack tag */
int notify_same_slot(const Notification* first, const Notification* second)
{
    return first->id == second->id ||
           (*first->stack_tag && !strcmp(first->stack_tag, second->stack_tag) &&
            !strcmp(first->app_name, second->app_name));
}

/* Pushes n to the head of the history, taking over its icon. An entry for
 * the same id or stack tag is replaced, so ten volume key presses leave one
 * entry. */
void notify_history_push(Notification* notification)
{

    if (notification->transient) {
        notify_release(notification);
        return;
    }
    size_t i;
    for (i = 0; i < notify_state.history_count &&
                !notify_same_slot(&notify_state.history[i], notification);
         i++) {}
    if (i == notify_state.history_count && notify_state.history_count == NOTIFY_HISTMAX)
        i = notify_state.history_count - 1; /* full: drop the oldest */
    if (i < notify_state.history_count) {
        notify_release(&notify_state.history[i]);
        memmove(&notify_state.history[i], &notify_state.history[i + 1],
                (notify_state.history_count - i - 1) * sizeof(*notify_state.history));
        notify_state.history_count--;
    }
    memmove(&notify_state.history[1], &notify_state.history[0],
            notify_state.history_count * sizeof(*notification));
    notify_state.history[0] = *notification;
    notify_state.history_count++;
    notification->icon = nullptr;
}

void notify_unqueue(size_t i, Notification* destination)
{
    *destination = notify_state.queue[i];
    memmove(&notify_state.queue[i], &notify_state.queue[i + 1],
            (notify_state.queue_count - i - 1) * sizeof(*notify_state.queue));
    notify_state.queue_count--;
}

/* A full queue sends its oldest expiring entry straight to the history. */
void notify_enqueue(Notification* notification, int front)
{
    Notification previous_notification;
    size_t i;

    if (notify_state.queue_count == NOTIFY_QUEUEMAX) {
        for (i = 0; i < notify_state.queue_count && !notify_state.queue[i].timeout_ms; i++) {}
        notify_unqueue(i < notify_state.queue_count ? i : 0, &previous_notification);
        notify_closed(previous_notification.id, NOTIFICATION_CLOSED_UNDEFINED);
        notify_history_push(&previous_notification);
    }
    if (front) {
        memmove(&notify_state.queue[1], &notify_state.queue[0],
                notify_state.queue_count * sizeof(*notify_state.queue));
        notify_state.queue[0] = *notification;
    } else {
        notify_state.queue[notify_state.queue_count] = *notification;
    }
    notify_state.queue_count++;
}

static int notify_expire(void* data)
{
    (void)data;
    notify_clear(NOTIFICATION_CLOSED_EXPIRED);
    return 0;
}

/* 0 disarms */
int notify_arm(unsigned int timeout_ms)
{
    if (!notify_state.timer && timeout_ms)
        notify_state.timer =
            wl_event_loop_add_timer(notify_state.event_loop, notify_expire, nullptr);
    if (!notify_state.timer)
        return !timeout_ms;
    return wl_event_source_timer_update(notify_state.timer, (int)timeout_ms) == 0;
}

int notify_show(void)
{
    notify_state.current.shown_at_ms = notify_now();
    notify_state.current.seen = 1;
    if (notify_state.is_paused) {
        notify_state.remaining_timeout_ms = notify_state.current.timeout_ms;
        notify_state.has_current = 1;
        return 1;
    }
    /* Without a timer the notification would sit in the bar forever, so the
     * caller is expected to drop it instead of showing it unexpirable. */
    notify_state.has_current = notify_arm(notify_state.current.timeout_ms);
    return notify_state.has_current;
}

/* An unseen critical goes first (it was held back by a pick); seen ones go
 * last, or everything else would wait behind them forever. */
void notify_show_next(void)
{
    size_t i;

    while (!notify_state.has_current && notify_state.queue_count) {
        for (i = 0; i < notify_state.queue_count; i++)
            if (notify_state.queue[i].urgency == NOTIFICATION_URGENCY_CRITICAL &&
                !notify_state.queue[i].seen)
                break;
        if (i == notify_state.queue_count)
            for (i = 0; i < notify_state.queue_count && !notify_state.queue[i].timeout_ms; i++) {}
        notify_unqueue(i < notify_state.queue_count ? i : 0, &notify_state.current);
        if (!notify_show()) {
            notify_closed(notify_state.current.id, NOTIFICATION_CLOSED_UNDEFINED);
            notify_history_push(&notify_state.current);
        }
    }
}

/* Takes the current notification down and brings up the next one. */
void notify_clear(dbus_uint32_t reason)
{
    if (!notify_state.has_current)
        return;
    notify_state.has_current = 0;
    notify_state.is_paused = 0;
    notify_arm(0);
    notify_closed(notify_state.current.id, reason);
    /* closed by the client: not worth keeping */
    if (reason == NOTIFICATION_CLOSED_BY_REQUEST)
        notify_release(&notify_state.current);
    else
        notify_history_push(&notify_state.current);
    notify_show_next();
    if (notify_state.redraw_callback)
        notify_state.redraw_callback();
}

const Notification* notify_current(void)
{
    return notify_state.has_current ? &notify_state.current : nullptr;
}

size_t notify_queued(void)
{
    return notify_state.queue_count;
}

const Notification* notify_history(size_t i)
{
    return i < notify_state.history_count ? &notify_state.history[i] : nullptr;
}

size_t notify_history_length(void)
{
    return notify_state.history_count;
}

void notify_history_remove(size_t i)
{
    if (i >= notify_state.history_count)
        return;
    notify_release(&notify_state.history[i]);
    memmove(&notify_state.history[i], &notify_state.history[i + 1],
            (notify_state.history_count - i - 1) * sizeof(*notify_state.history));
    notify_state.history_count--;
    if (notify_state.redraw_callback)
        notify_state.redraw_callback();
}

void notify_dismiss(void)
{
    notify_clear(NOTIFICATION_CLOSED_DISMISSED);
}

void notify_dismiss_all(void)
{
    if (!notify_state.has_current)
        return;
    /* not through notify_clear(), which would bring up each queued one */
    notify_state.has_current = 0;
    notify_arm(0);
    notify_closed(notify_state.current.id, NOTIFICATION_CLOSED_DISMISSED);
    notify_history_push(&notify_state.current);
    while (notify_state.queue_count) {
        Notification notification;
        notify_unqueue(0, &notification);
        notify_closed(notification.id, NOTIFICATION_CLOSED_DISMISSED);
        notify_history_push(&notification);
    }
    if (notify_state.redraw_callback)
        notify_state.redraw_callback();
}

/* The activation token goes first, as the spec asks, so the client can
 * raise the right window itself. */
static void notify_invoke_action(const char* key)
{
    DBusMessage* sig;
    const char* token;

    if (notify_state.activation_token_callback &&
        (token = notify_state.activation_token_callback()) &&
        (sig = dbus_message_new_signal(NOTIFY_OPATH, NOTIFY_IFACE, "ActivationToken"))) {
        if (dbus_message_append_args(sig, DBUS_TYPE_UINT32, &notify_state.current.id,
                                     DBUS_TYPE_STRING, &token, DBUS_TYPE_INVALID))
            dbus_connection_send(notify_state.connection, sig, nullptr);
        dbus_message_unref(sig);
    }
    if ((sig = dbus_message_new_signal(NOTIFY_OPATH, NOTIFY_IFACE, "ActionInvoked"))) {
        if (dbus_message_append_args(sig, DBUS_TYPE_UINT32, &notify_state.current.id,
                                     DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID))
            dbus_connection_send(notify_state.connection, sig, nullptr);
        dbus_message_unref(sig);
    }
    if (notify_state.current.resident) {
        notify_hold(0);
        if (notify_state.redraw_callback)
            notify_state.redraw_callback();
    } else {
        notify_clear(NOTIFICATION_CLOSED_DISMISSED);
    }
}

void notify_invoke(void)
{
    if (!notify_state.has_current)
        return;
    if (notify_state.current.has_default_action)
        notify_invoke_action("default");
    else
        notify_clear(NOTIFICATION_CLOSED_DISMISSED);
}

void notify_action(size_t i)
{
    if (notify_state.has_current && i < notify_state.current.action_count)
        notify_invoke_action(notify_state.current.actions[i].key);
}

void notify_hold(int hold)
{
    const uint64_t now = notify_now();

    if (!notify_state.has_current || !hold == !notify_state.is_paused)
        return;
    if (hold) {
        const uint64_t end_ms = notify_state.current.shown_at_ms + notify_state.current.timeout_ms;
        notify_state.remaining_timeout_ms = !notify_state.current.timeout_ms ? 0
                                            : end_ms > now ? (unsigned int)(end_ms - now)
                                                           : 1;
        notify_arm(0);
        notify_state.is_paused = 1;
        return;
    }

    notify_state.is_paused = 0;
    /* a critical that arrived meanwhile takes the box now */
    size_t critical_index = 0;
    while (critical_index < notify_state.queue_count &&
           !(notify_state.queue[critical_index].urgency == NOTIFICATION_URGENCY_CRITICAL &&
             !notify_state.queue[critical_index].seen))
        critical_index++;
    if (critical_index < notify_state.queue_count &&
        notify_state.current.urgency != NOTIFICATION_URGENCY_CRITICAL) {
        Notification promoted;
        notify_unqueue(critical_index, &promoted);
        notify_state.has_current = 0;
        notify_enqueue(&notify_state.current, 1);
        notify_state.current = promoted;
        if (!notify_show()) {
            notify_closed(notify_state.current.id, NOTIFICATION_CLOSED_UNDEFINED);
            notify_history_push(&notify_state.current);
            notify_show_next();
        }
        return;
    }
    if (notify_state.current.timeout_ms) {
        notify_state.current.shown_at_ms =
            now - (notify_state.current.timeout_ms - notify_state.remaining_timeout_ms);
        /* no redraw from here: the bar may be drawing */
        notify_arm(notify_state.remaining_timeout_ms);
    }
}

int notify_held(unsigned int* left_ms)
{
    if (notify_state.is_paused && left_ms)
        *left_ms = notify_state.remaining_timeout_ms;
    return notify_state.is_paused;
}
