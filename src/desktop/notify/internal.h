/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_DESKTOP_NOTIFY_INTERNAL_H
#define G0WM_DESKTOP_NOTIFY_INTERNAL_H

#include <dbus/dbus.h>
#include <stddef.h>
#include <wayland-server-core.h>

#include "desktop/notify/notify.h"

#define NOTIFY_OPATH "/org/freedesktop/Notifications"

#define NOTIFY_IFACE "org.freedesktop.Notifications"

/* An unbounded notify_expire_timeout would let one client hold the bar (and
 * hide the window title) for the rest of the session. */
constexpr int NOTIFY_TIMEOUT_MAX = 60'000;

/* org.freedesktop.Notifications.NotificationClosed reasons */
enum { ClosedExpired = 1, ClosedDismissed, ClosedByCall, ClosedUndefined };

typedef struct {
    DBusConnection* conn;
    struct wl_event_loop* loop;
    struct wl_event_source* timer;
    void (*redraw)(void);
    const char* (*token)(void);
    unsigned int timeout_ms;
    dbus_uint32_t seq; /* handed out to clients that don't pick one */
    int active;
    int running;
    int held;
    unsigned int heldleft; /* time cur had left when held */
    Notification cur;      /* valid when active */
    Notification queue[NOTIFY_QUEUEMAX];
    size_t nqueue;
    Notification hist[NOTIFY_HISTMAX]; /* newest first */
    size_t nhist;
} NotifyState;

extern NotifyState notify_state;

void notify_closed(dbus_uint32_t id, dbus_uint32_t reason);
void notify_release(Notification* notification);
int notify_same_slot(const Notification* first, const Notification* second);
void notify_history_push(Notification* notification);
void notify_unqueue(size_t i, Notification* dst);
void notify_enqueue(Notification* notification, int front);
int notify_arm(unsigned int timeout_ms);
int notify_show(void);
void notify_show_next(void);
void notify_clear(dbus_uint32_t reason);
DBusHandlerResult notify_handle_message(DBusConnection* connection,
                                        DBusMessage* message,
                                        void* data);

#endif
