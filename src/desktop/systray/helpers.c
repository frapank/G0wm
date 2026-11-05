/*
 * SPDX-FileCopyrightText: janetski
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "desktop/systray/helpers.h"

#include <dbus/dbus.h>

#include <errno.h>

// IWYU pragma: no_include "dbus/dbus-protocol.h"
// IWYU pragma: no_include "dbus/dbus-shared.h"

int helpers_request_property(DBusConnection* connection,
                             const char* bus_name,
                             const char* object_path,
                             const char* property,
                             const char* interface,
                             PropHandler handler,
                             void* data)
{
    DBusMessage* message = nullptr;
    DBusPendingCall* pending = nullptr;

    if (!(message = dbus_message_new_method_call(bus_name, object_path, DBUS_INTERFACE_PROPERTIES,
                                                 "Get")) ||
        !dbus_message_append_args(message, DBUS_TYPE_STRING, &interface, DBUS_TYPE_STRING,
                                  &property, DBUS_TYPE_INVALID) ||
        !dbus_connection_send_with_reply(connection, message, &pending, -1) ||
        !dbus_pending_call_set_notify(pending, handler, data, nullptr)) {
        goto fail;
    }

    dbus_message_unref(message);
    return 0;

fail:
    if (pending) {
        dbus_pending_call_cancel(pending);
        dbus_pending_call_unref(pending);
    }
    if (message)
        dbus_message_unref(message);
    return -ENOMEM;
}
