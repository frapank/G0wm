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
enum NotificationCloseReason {
    NOTIFICATION_CLOSED_EXPIRED = 1,
    NOTIFICATION_CLOSED_DISMISSED,
    NOTIFICATION_CLOSED_BY_REQUEST,
    NOTIFICATION_CLOSED_UNDEFINED
};

typedef struct {
    DBusConnection* connection;
    struct wl_event_loop* event_loop;
    struct wl_event_source* timer;
    void (*redraw_callback)(void);
    const char* (*activation_token_callback)(void);
    unsigned int timeout_ms;
    dbus_uint32_t next_notification_id; /* handed out to clients that don't pick one */
    int has_current;
    int is_running;
    int is_paused;
    unsigned int remaining_timeout_ms; /* timeout left when paused */
    Notification current;              /* valid when has_current */
    Notification queue[NOTIFY_QUEUEMAX];
    size_t queue_count;
    Notification history[NOTIFY_HISTMAX]; /* newest first */
    size_t history_count;
} NotificationState;

extern NotificationState notify_state;

void notify_closed(dbus_uint32_t id, dbus_uint32_t reason);
void notify_release(Notification* notification);
int notify_same_slot(const Notification* first, const Notification* second);
void notify_history_push(Notification* notification);
void notify_unqueue(size_t i, Notification* destination);
void notify_enqueue(Notification* notification, int front);
int notify_arm(unsigned int timeout_ms);
int notify_show(void);
void notify_show_next(void);
void notify_clear(dbus_uint32_t reason);
DBusHandlerResult notify_handle_message(DBusConnection* connection,
                                        DBusMessage* message,
                                        void* data);

#endif
