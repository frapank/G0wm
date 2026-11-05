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
        dbus_connection_send(notify_state.conn, sig, nullptr);
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
           (*first->tag && !strcmp(first->tag, second->tag) && !strcmp(first->app, second->app));
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
    for (i = 0; i < notify_state.nhist && !notify_same_slot(&notify_state.hist[i], notification);
         i++) {}
    if (i == notify_state.nhist && notify_state.nhist == NOTIFY_HISTMAX)
        i = notify_state.nhist - 1; /* full: drop the oldest */
    if (i < notify_state.nhist) {
        notify_release(&notify_state.hist[i]);
        memmove(&notify_state.hist[i], &notify_state.hist[i + 1],
                (notify_state.nhist - i - 1) * sizeof(*notify_state.hist));
        notify_state.nhist--;
    }
    memmove(&notify_state.hist[1], &notify_state.hist[0],
            notify_state.nhist * sizeof(*notification));
    notify_state.hist[0] = *notification;
    notify_state.nhist++;
    notification->icon = nullptr;
}

void notify_unqueue(size_t i, Notification* dst)
{
    *dst = notify_state.queue[i];
    memmove(&notify_state.queue[i], &notify_state.queue[i + 1],
            (notify_state.nqueue - i - 1) * sizeof(*notify_state.queue));
    notify_state.nqueue--;
}

/* A full queue sends its oldest expiring entry straight to the history. */
void notify_enqueue(Notification* notification, int front)
{
    Notification old;
    size_t i;

    if (notify_state.nqueue == NOTIFY_QUEUEMAX) {
        for (i = 0; i < notify_state.nqueue && !notify_state.queue[i].timeout_ms; i++) {}
        notify_unqueue(i < notify_state.nqueue ? i : 0, &old);
        notify_closed(old.id, ClosedUndefined);
        notify_history_push(&old);
    }
    if (front) {
        memmove(&notify_state.queue[1], &notify_state.queue[0],
                notify_state.nqueue * sizeof(*notify_state.queue));
        notify_state.queue[0] = *notification;
    } else {
        notify_state.queue[notify_state.nqueue] = *notification;
    }
    notify_state.nqueue++;
}

static int notify_expire(void* data)
{
    (void)data;
    notify_clear(ClosedExpired);
    return 0;
}

/* 0 disarms */
int notify_arm(unsigned int timeout_ms)
{
    if (!notify_state.timer && timeout_ms)
        notify_state.timer = wl_event_loop_add_timer(notify_state.loop, notify_expire, nullptr);
    if (!notify_state.timer)
        return !timeout_ms;
    return wl_event_source_timer_update(notify_state.timer, (int)timeout_ms) == 0;
}

int notify_show(void)
{
    notify_state.cur.shown_ms = notify_now();
    notify_state.cur.seen = 1;
    if (notify_state.held) {
        notify_state.heldleft = notify_state.cur.timeout_ms;
        notify_state.active = 1;
        return 1;
    }
    /* Without a timer the notification would sit in the bar forever, so the
     * caller is expected to drop it instead of showing it unexpirable. */
    notify_state.active = notify_arm(notify_state.cur.timeout_ms);
    return notify_state.active;
}

/* An unseen critical goes first (it was held back by a pick); seen ones go
 * last, or everything else would wait behind them forever. */
void notify_show_next(void)
{
    size_t i;

    while (!notify_state.active && notify_state.nqueue) {
        for (i = 0; i < notify_state.nqueue; i++)
            if (notify_state.queue[i].urgency == UrgencyCritical && !notify_state.queue[i].seen)
                break;
        if (i == notify_state.nqueue)
            for (i = 0; i < notify_state.nqueue && !notify_state.queue[i].timeout_ms; i++) {}
        notify_unqueue(i < notify_state.nqueue ? i : 0, &notify_state.cur);
        if (!notify_show()) {
            notify_closed(notify_state.cur.id, ClosedUndefined);
            notify_history_push(&notify_state.cur);
        }
    }
}

/* Takes the current notification down and brings up the next one. */
void notify_clear(dbus_uint32_t reason)
{
    if (!notify_state.active)
        return;
    notify_state.active = 0;
    notify_state.held = 0;
    notify_arm(0);
    notify_closed(notify_state.cur.id, reason);
    /* closed by the client: not worth keeping */
    if (reason == ClosedByCall)
        notify_release(&notify_state.cur);
    else
        notify_history_push(&notify_state.cur);
    notify_show_next();
    if (notify_state.redraw)
        notify_state.redraw();
}

const Notification* notify_current(void)
{
    return notify_state.active ? &notify_state.cur : nullptr;
}

size_t notify_queued(void)
{
    return notify_state.nqueue;
}

const Notification* notify_history(size_t i)
{
    return i < notify_state.nhist ? &notify_state.hist[i] : nullptr;
}

size_t notify_history_length(void)
{
    return notify_state.nhist;
}

void notify_history_remove(size_t i)
{
    if (i >= notify_state.nhist)
        return;
    notify_release(&notify_state.hist[i]);
    memmove(&notify_state.hist[i], &notify_state.hist[i + 1],
            (notify_state.nhist - i - 1) * sizeof(*notify_state.hist));
    notify_state.nhist--;
    if (notify_state.redraw)
        notify_state.redraw();
}

void notify_dismiss(void)
{
    notify_clear(ClosedDismissed);
}

void notify_dismiss_all(void)
{
    if (!notify_state.active)
        return;
    /* not through notify_clear(), which would bring up each queued one */
    notify_state.active = 0;
    notify_arm(0);
    notify_closed(notify_state.cur.id, ClosedDismissed);
    notify_history_push(&notify_state.cur);
    while (notify_state.nqueue) {
        Notification notification;
        notify_unqueue(0, &notification);
        notify_closed(notification.id, ClosedDismissed);
        notify_history_push(&notification);
    }
    if (notify_state.redraw)
        notify_state.redraw();
}

/* The activation token goes first, as the spec asks, so the client can
 * raise the right window itself. */
static void notify_invoke_action(const char* key)
{
    DBusMessage* sig;
    const char* token;

    if (notify_state.token && (token = notify_state.token()) &&
        (sig = dbus_message_new_signal(NOTIFY_OPATH, NOTIFY_IFACE, "ActivationToken"))) {
        if (dbus_message_append_args(sig, DBUS_TYPE_UINT32, &notify_state.cur.id, DBUS_TYPE_STRING,
                                     &token, DBUS_TYPE_INVALID))
            dbus_connection_send(notify_state.conn, sig, nullptr);
        dbus_message_unref(sig);
    }
    if ((sig = dbus_message_new_signal(NOTIFY_OPATH, NOTIFY_IFACE, "ActionInvoked"))) {
        if (dbus_message_append_args(sig, DBUS_TYPE_UINT32, &notify_state.cur.id, DBUS_TYPE_STRING,
                                     &key, DBUS_TYPE_INVALID))
            dbus_connection_send(notify_state.conn, sig, nullptr);
        dbus_message_unref(sig);
    }
    if (notify_state.cur.resident) {
        notify_hold(0);
        if (notify_state.redraw)
            notify_state.redraw();
    } else {
        notify_clear(ClosedDismissed);
    }
}

void notify_invoke(void)
{
    if (!notify_state.active)
        return;
    if (notify_state.cur.hasdefault)
        notify_invoke_action("default");
    else
        notify_clear(ClosedDismissed);
}

void notify_action(size_t i)
{
    if (notify_state.active && i < notify_state.cur.nact)
        notify_invoke_action(notify_state.cur.act[i].key);
}

void notify_hold(int hold)
{
    const uint64_t now = notify_now();

    if (!notify_state.active || !hold == !notify_state.held)
        return;
    if (hold) {
        const uint64_t end_ms = notify_state.cur.shown_ms + notify_state.cur.timeout_ms;
        notify_state.heldleft = !notify_state.cur.timeout_ms ? 0
                                : end_ms > now               ? (unsigned int)(end_ms - now)
                                                             : 1;
        notify_arm(0);
        notify_state.held = 1;
        return;
    }

    notify_state.held = 0;
    /* a critical that arrived meanwhile takes the box now */
    size_t critical_index = 0;
    while (critical_index < notify_state.nqueue &&
           !(notify_state.queue[critical_index].urgency == UrgencyCritical &&
             !notify_state.queue[critical_index].seen))
        critical_index++;
    if (critical_index < notify_state.nqueue && notify_state.cur.urgency != UrgencyCritical) {
        Notification promoted;
        notify_unqueue(critical_index, &promoted);
        notify_state.active = 0;
        notify_enqueue(&notify_state.cur, 1);
        notify_state.cur = promoted;
        if (!notify_show()) {
            notify_closed(notify_state.cur.id, ClosedUndefined);
            notify_history_push(&notify_state.cur);
            notify_show_next();
        }
        return;
    }
    if (notify_state.cur.timeout_ms) {
        notify_state.cur.shown_ms = now - (notify_state.cur.timeout_ms - notify_state.heldleft);
        /* no redraw from here: the bar may be drawing */
        notify_arm(notify_state.heldleft);
    }
}

int notify_held(unsigned int* left_ms)
{
    if (notify_state.held && left_ms)
        *left_ms = notify_state.heldleft;
    return notify_state.held;
}
