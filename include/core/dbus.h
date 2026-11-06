/*
 * SPDX-FileCopyrightText: janetski
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_DBUS_H
#define G0WM_DBUS_H

#include <dbus/dbus.h>
#include <wayland-server-core.h>

[[nodiscard]] struct wl_event_source* bus_start(DBusConnection* connection,
                                                struct wl_event_loop* loop);
void bus_stop(DBusConnection* connection, struct wl_event_source* bus_source);

#endif
