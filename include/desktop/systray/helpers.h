/*
 * SPDX-FileCopyrightText: janetski
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_SYSTRAY_HELPERS_H
#define G0WM_SYSTRAY_HELPERS_H

#include <dbus/dbus.h>

typedef void (*PropHandler)(DBusPendingCall* pcall, void* data);

int helpers_request_property(DBusConnection* connection,
                             const char* bus_name,
                             const char* object_path,
                             const char* property,
                             const char* interface,
                             PropHandler handler,
                             void* data);

#endif
