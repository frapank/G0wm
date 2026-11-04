/*
 * SPDX-FileCopyrightText: janetski
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_SYSTRAY_HELPERS_H
#define G0WM_SYSTRAY_HELPERS_H

#include <dbus/dbus.h>

typedef void (*PropHandler)(DBusPendingCall* pcall, void* data);

int helpers_request_property(DBusConnection* conn,
                             const char* busname,
                             const char* busobj,
                             const char* prop,
                             const char* iface,
                             PropHandler handler,
                             void* data);

#endif
