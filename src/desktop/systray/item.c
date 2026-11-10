/*
 * SPDX-FileCopyrightText: janetski
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "desktop/systray/item.h"
#include "desktop/systray/helpers.h"
#include "desktop/systray/icon.h"
#include "desktop/systray/watcher.h"

#include <dbus/dbus.h>
#include <stdckdint.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// IWYU pragma: no_include "dbus/dbus-protocol.h"
// IWYU pragma: no_include "dbus/dbus-shared.h"

constexpr int RULEBSIZE = 256;
#define MIN(A, B) ((A) < (B) ? (A) : (B))

static const char* item_match_string = "type='signal',"
                                       "sender='%s',"
                                       "interface='" SNI_NAME "',"
                                       "member='NewIcon'";

static TrayWatcher* item_get_watcher(const TrayItem* item)
{
    if (!item)
        return nullptr;

    return item->watcher;
}

static DBusConnection* item_get_connection(const TrayItem* item)
{
    if (!item || !item->watcher)
        return nullptr;

    return item->watcher->connection;
}

static const uint8_t* item_extract_image(DBusMessageIter* iter,
                                         dbus_int32_t* width,
                                         dbus_int32_t* height,
                                         int* size)
{
    DBusMessageIter vals;
    DBusMessageIter bytes;
    const uint8_t* pixels;
    int expected_size;

    dbus_message_iter_recurse(iter, &vals);
    if (dbus_message_iter_get_arg_type(&vals) != DBUS_TYPE_INT32)
        goto fail;
    dbus_message_iter_get_basic(&vals, width);

    dbus_message_iter_next(&vals);
    if (dbus_message_iter_get_arg_type(&vals) != DBUS_TYPE_INT32)
        goto fail;
    dbus_message_iter_get_basic(&vals, height);

    dbus_message_iter_next(&vals);
    if (dbus_message_iter_get_arg_type(&vals) != DBUS_TYPE_ARRAY)
        goto fail;
    dbus_message_iter_recurse(&vals, &bytes);
    if (dbus_message_iter_get_arg_type(&bytes) != DBUS_TYPE_BYTE)
        goto fail;
    dbus_message_iter_get_fixed_array(&bytes, &pixels, size);
    if (*size == 0 || *width <= 0 || *height <= 0 || ckd_mul(&expected_size, *width, *height) ||
        ckd_mul(&expected_size, expected_size, 4) || expected_size != *size)
        goto fail;

    return pixels;

fail:
    return nullptr;
}

static int item_select_image(DBusMessageIter* iter, int target_width)
{
    int i = 0;

    DBusMessageIter vals;
    dbus_int32_t cur_width;
    do {
        dbus_message_iter_recurse(iter, &vals);
        if (dbus_message_iter_get_arg_type(&vals) != DBUS_TYPE_INT32)
            return -1;
        dbus_message_iter_get_basic(&vals, &cur_width);
        if (cur_width >= target_width)
            return i;

        i++;
    } while (dbus_message_iter_next(iter));

    /* return last index if desired not found */
    return --i;
}

/*
 * Copy of the string a property Get replied with, NULL if it has none. The
 * reply is consumed either way. `what` names it in the error, NULL is quiet.
 */
static char* item_take_string_property(DBusPendingCall* pending, const char* what)
{
    DBusError err = DBUS_ERROR_INIT;
    DBusMessage* reply = nullptr;
    DBusMessageIter iter;
    DBusMessageIter val;
    char* dup = nullptr;
    const char* str;
    int type;

    reply = dbus_pending_call_steal_reply(pending);
    if (!reply)
        goto out;

    if (dbus_set_error_from_message(&err, reply)) {
        if (what)
            fprintf(stderr, "DBus Error: %s - %s: Couldn't get %s\n", err.name, err.message, what);
        goto out;
    }

    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
        goto out;
    dbus_message_iter_recurse(&iter, &val);
    type = dbus_message_iter_get_arg_type(&val);
    if (type != DBUS_TYPE_STRING && type != DBUS_TYPE_OBJECT_PATH)
        goto out;
    dbus_message_iter_get_basic(&val, &str);

    dup = strdup(str);

out:
    dbus_error_free(&err);
    if (reply)
        dbus_message_unref(reply);
    if (pending)
        dbus_pending_call_unref(pending);
    return dup;
}

static void item_handle_menu_path_ready(DBusPendingCall* pending, void* data)
{
    TrayItem* item = data;

    free(item->menu_object_path);
    item->menu_object_path = item_take_string_property(pending, "menupath");
}

/* Second half of the fallback, where the name becomes a file. */
static void item_handle_icon_name_ready(DBusPendingCall* pending, void* data)
{
    TrayItem* item = data;

    char* name = item_take_string_property(pending, nullptr);
    if (!name)
        return;

    RasterIcon* icon = icon_create_from_name(name, item->icon_theme_path,
                                             watcher_get_icon_size(item_get_watcher(item)));
    free(name);
    if (!icon)
        return;

    if (item->icon)
        icon_destroy(item->icon);
    item->icon = icon;
    watcher_update_trays(item_get_watcher(item));
}

/* IconThemePath has to be in before IconName resolves, so they chain. */
static void item_handle_theme_path_ready(DBusPendingCall* pending, void* data)
{
    TrayItem* item = data;

    free(item->icon_theme_path);
    item->icon_theme_path = item_take_string_property(pending, nullptr);

    helpers_request_property(item_get_connection(item), item->bus_name, item->object_path,
                             "IconName", SNI_IFACE, item_handle_icon_name_ready, item);
}

/*
 * Gets the Id dbus property, which is the name of the application,
 * most of the time...
 * The initial letter will be used as a fallback icon
 */
static void item_handle_id_ready(DBusPendingCall* pending, void* data)
{
    TrayItem* item = data;

    char* id = item_take_string_property(pending, "appid");
    if (!id)
        return;

    free(item->app_id);
    item->app_id = id;

    /* Don't trigger update if this item already has a real icon */
    if (!item->icon)
        watcher_update_trays(item_get_watcher(item));
}

static void item_handle_pixmap_ready(DBusPendingCall* pending, void* data)
{
    TrayItem* item = data;

    DBusMessage* reply = nullptr;
    DBusMessageIter iter;
    DBusMessageIter array;
    DBusMessageIter select;
    DBusMessageIter strct;
    RasterIcon* icon = nullptr;
    TrayWatcher* watcher;
    dbus_int32_t width;
    dbus_int32_t height;
    int selected_index;
    int size;
    const uint8_t* buf;

    watcher = item_get_watcher(item);

    reply = dbus_pending_call_steal_reply(pending);
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR)
        goto fail;
    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
        goto fail;
    dbus_message_iter_recurse(&iter, &array);
    if (dbus_message_iter_get_arg_type(&array) != DBUS_TYPE_ARRAY)
        goto fail;
    dbus_message_iter_recurse(&array, &select);
    if (dbus_message_iter_get_arg_type(&select) != DBUS_TYPE_STRUCT)
        goto fail;
    selected_index = item_select_image(&select, 22); // Get the 22*22 image
    if (selected_index < 0)
        goto fail;

    dbus_message_iter_recurse(&array, &strct);
    if (dbus_message_iter_get_arg_type(&strct) != DBUS_TYPE_STRUCT)
        goto fail;
    for (int i = 0; i < selected_index; i++)
        dbus_message_iter_next(&strct);
    buf = item_extract_image(&strct, &width, &height, &size);
    if (!buf)
        goto fail;

    if (!item->icon) {
        /* First icon */
        icon = icon_create(buf, width, height, size);
        if (!icon)
            goto fail;
        item->icon = icon;
        watcher_update_trays(watcher);

    } else if (memcmp(item->icon->original_pixels, buf,
                      MIN(item->icon->original_size_bytes, (size_t)size)) != 0) {
        /* New icon */
        icon_destroy(item->icon);
        item->icon = nullptr;
        icon = icon_create(buf, width, height, size);
        if (!icon)
            goto fail;
        item->icon = icon;
        watcher_update_trays(watcher);

    } else {
        /* Icon didn't change */
    }

    dbus_message_unref(reply);
    dbus_pending_call_unref(pending);
    return;

fail:
    if (icon)
        icon_destroy(icon);
    if (reply)
        dbus_message_unref(reply);
    if (pending)
        dbus_pending_call_unref(pending);

    /* no pixmap, but the item may still name a themed icon like Steam does */
    helpers_request_property(item_get_connection(item), item->bus_name, item->object_path,
                             "IconThemePath", SNI_IFACE, item_handle_theme_path_ready, item);
}

static DBusHandlerResult item_handle_new_icon(TrayItem* item,
                                              DBusConnection* connection,
                                              DBusMessage* message)
{
    const char* sender = dbus_message_get_sender(message);

    if (sender && strcmp(sender, item->bus_name) == 0) {
        helpers_request_property(connection, item->bus_name, item->object_path, "IconPixmap",
                                 SNI_IFACE, item_handle_pixmap_ready, item);

        return DBUS_HANDLER_RESULT_HANDLED;

    } else {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
}

static DBusHandlerResult item_filter_bus(DBusConnection* connection,
                                         DBusMessage* message,
                                         void* data)
{
    TrayItem* item = data;

    if (dbus_message_is_signal(message, SNI_IFACE, "NewIcon"))
        return item_handle_new_icon(item, connection, message);
    else
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

TrayItem* item_create(const char* bus_name, const char* object_path, TrayWatcher* watcher)
{
    DBusConnection* connection;
    TrayItem* item;
    char* bus_name_copy = nullptr;
    char* object_path_copy = nullptr;
    char match_rule[RULEBSIZE];

    if (snprintf(match_rule, sizeof(match_rule), item_match_string, bus_name) >= RULEBSIZE)
        return nullptr;

    item = calloc(1, sizeof(TrayItem));
    bus_name_copy = strdup(bus_name);
    object_path_copy = strdup(object_path);
    if (!item || !bus_name_copy || !object_path_copy)
        goto fail;

    connection = watcher->connection;
    item->bus_name = bus_name_copy;
    item->object_path = object_path_copy;
    item->watcher = watcher;

    if (!dbus_connection_add_filter(connection, item_filter_bus, item, nullptr))
        goto fail;
    dbus_bus_add_match(connection, match_rule, nullptr);

    helpers_request_property(connection, bus_name, object_path, "IconPixmap", SNI_IFACE,
                             item_handle_pixmap_ready, item);

    helpers_request_property(connection, bus_name, object_path, "Id", SNI_IFACE,
                             item_handle_id_ready, item);

    helpers_request_property(connection, bus_name, object_path, "Menu", SNI_IFACE,
                             item_handle_menu_path_ready, item);

    return item;

fail:
    free(bus_name_copy);
    free(object_path_copy);
    free(item);
    return nullptr;
}

void item_destroy(TrayItem* item)
{

    DBusConnection* connection = item_get_connection(item);

    char match_rule[RULEBSIZE];
    if (snprintf(match_rule, sizeof(match_rule), item_match_string, item->bus_name) < RULEBSIZE) {
        dbus_bus_remove_match(connection, match_rule, nullptr);
        dbus_connection_remove_filter(connection, item_filter_bus, item);
    }
    if (item->icon)
        icon_destroy(item->icon);
    free(item->menu_object_path);
    free(item->bus_name);
    free(item->object_path);
    free(item->app_id);
    free(item->icon_theme_path);
    free(item);
}

void item_activate(TrayItem* item)
{
    DBusConnection* connection;
    DBusMessage* message = nullptr;
    dbus_int32_t x = 0;
    dbus_int32_t y = 0;

    connection = item_get_connection(item);

    if (!(message = dbus_message_new_method_call(item->bus_name, item->object_path, SNI_IFACE,
                                                 "Activate")) ||
        !dbus_message_append_args(message, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y,
                                  DBUS_TYPE_INVALID) ||
        !dbus_connection_send_with_reply(connection, message, nullptr, -1)) {
        goto fail;
    }

    dbus_message_unref(message);
    return;

fail:
    if (message)
        dbus_message_unref(message);
}
