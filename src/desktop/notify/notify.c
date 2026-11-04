/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * starting and stopping the org.freedesktop.Notifications server
 */

#include <dbus/dbus.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <wayland-server-core.h>

#include "desktop/notify/notify.h"
#include "internal.h"

#define NOTIFY_NAME "org.freedesktop.Notifications"

/* variables */
NotifyState notify_state;
static const DBusObjectPathVTable notify_vtable = { .message_function =
                                                        notify_handle_message };

/* function implementations */
uint64_t notify_now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1'000'000;
}

void notify_start(DBusConnection* conn,
                  struct wl_event_loop* loop,
                  unsigned int timeout_secs,
                  void (*redraw)(void),
                  const char* (*token)(void))
{
    int r;

    if (!conn || !loop)
        return;

    notify_state = (typeof(notify_state)){};
    notify_state.conn = conn;
    notify_state.loop = loop;
    notify_set_timeout(timeout_secs);
    notify_state.redraw = redraw;
    notify_state.token = token;

    /* if another daemon (mako, dunst, swaync, ...) already owns the name,
     * don't fight it for it: just stay disabled */
    r = dbus_bus_request_name(
        conn, NOTIFY_NAME, DBUS_NAME_FLAG_DO_NOT_QUEUE, nullptr);
    if (r != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr,
                "Couldn't own %s (another notification daemon is likely "
                "running), bar notifications not available\n",
                NOTIFY_NAME);
        return;
    }

    if (!dbus_connection_register_object_path(
            conn, NOTIFY_OPATH, &notify_vtable, nullptr)) {
        dbus_bus_release_name(conn, NOTIFY_NAME, nullptr);
        fprintf(stderr, "Couldn't register %s\n", NOTIFY_OPATH);
        return;
    }

    notify_state.running = 1;
}

void notify_stop(void)
{
    Notification n;

    if (!notify_state.running)
        return;

    /* nothing is worth redrawing on the way out, but clients still deserve
     * to hear that their notification went away */
    notify_state.redraw = nullptr;
    while (notify_state.nqueue) {
        notify_unqueue(0, &n);
        notify_closed(n.id, ClosedUndefined);
        notify_release(&n);
    }
    notify_clear(ClosedUndefined);
    while (notify_state.nhist)
        notify_release(&notify_state.hist[--notify_state.nhist]);
    if (notify_state.timer) {
        wl_event_source_remove(notify_state.timer);
        notify_state.timer = nullptr;
    }
    dbus_connection_unregister_object_path(notify_state.conn, NOTIFY_OPATH);
    dbus_bus_release_name(notify_state.conn, NOTIFY_NAME, nullptr);
    notify_state.running = 0;
}

void notify_set_timeout(unsigned int timeout_secs)
{
    notify_state.timeout_ms = timeout_secs > NOTIFY_TIMEOUT_MAX / 1000
                                  ? NOTIFY_TIMEOUT_MAX
                                  : (timeout_secs ? timeout_secs : 1) * 1000;
}
