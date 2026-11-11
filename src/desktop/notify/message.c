/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the D-Bus calls of the notification server
 */

#include <dbus/dbus.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "core/util.h"
#include "desktop/notify/notify.h"
#include "desktop/systray/icon.h"
#include "internal.h"

/* image-data bigger than this on a side is refused rather than decoded */
constexpr int NOTIFY_IMAGEMAX = 4096;

/* function implementations */
/* Copies src into dst keeping only whole, well-formed UTF-8 codepoints:
 * control characters become spaces, invalid bytes are dropped and a sequence
 * that doesn't fit is left out entirely. Anything on this path comes straight
 * from an arbitrary bus client, and half a codepoint renders as garbage. */
static void notify_sanitize(char* dst, size_t dstsz, const char* src)
{
    size_t written = 0;

    if (!dstsz)
        return;
    size_t sequence_length;
    for (unsigned char byte; src && (byte = (unsigned char)*src); src += sequence_length) {
        sequence_length = byte < 0x80   ? 1
                          : byte < 0xC2 ? 0 /* continuation byte or overlong lead */
                          : byte < 0xE0 ? 2
                          : byte < 0xF0 ? 3
                          : byte < 0xF5 ? 4
                                        : 0;
        if (!sequence_length) {
            sequence_length = 1;
            continue;
        }
        size_t valid_length = 1;
        while (valid_length < sequence_length && ((unsigned char)src[valid_length] & 0xC0) == 0x80)
            valid_length++;
        if (valid_length <
            sequence_length) { /* truncated sequence: drop the lead byte and resync */
            sequence_length = 1;
            continue;
        }
        if (written + sequence_length >= dstsz)
            break;
        if (byte < ' ' || byte == 0x7f) {
            dst[written++] = ' '; /* sequence_length is 1 here */
            continue;
        }
        for (size_t offset = 0; offset < sequence_length; offset++)
            dst[written++] = (char)src[offset];
    }
    dst[written] = '\0';
}

/* Points iter at the n-th Notify argument: actions, hints and
 * notify_expire_timeout sit past containers dbus_message_get_args() won't hand
 * back as is. */
static int notify_arg_at(DBusMessage* message, DBusMessageIter* iter, int argument_index, int type)
{
    if (!dbus_message_iter_init(message, iter))
        return 0;
    while (argument_index--)
        if (!dbus_message_iter_next(iter))
            return 0;
    return dbus_message_iter_get_arg_type(iter) == type;
}

static int notify_expire_timeout(DBusMessage* message)
{
    DBusMessageIter iter;

    if (!notify_arg_at(message, &iter, 7, DBUS_TYPE_INT32))
        return -1;
    dbus_int32_t timeout_ms;
    dbus_message_iter_get_basic(&iter, &timeout_ms);
    return timeout_ms;
}

/* actions is a flat (key, label, ...) list. Keys too long to store are
 * skipped, not cut: they must go back to the client unchanged. */
static void notify_actions(DBusMessage* message, Notification* notification)
{
    DBusMessageIter iter;

    if (!notify_arg_at(message, &iter, 5, DBUS_TYPE_ARRAY))
        return;
    DBusMessageIter arr;
    dbus_message_iter_recurse(&iter, &arr);
    const char* key;
    const char* label;
    while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING) {
        dbus_message_iter_get_basic(&arr, &key);
        dbus_message_iter_next(&arr);
        if (dbus_message_iter_get_arg_type(&arr) != DBUS_TYPE_STRING)
            break;
        dbus_message_iter_get_basic(&arr, &label);
        dbus_message_iter_next(&arr);
        if (!strcmp(key, "default")) {
            notification->has_default_action = 1;
        } else if (notification->action_count < NOTIFY_ACTMAX &&
                   strlen(key) < sizeof(notification->actions[0].key)) {
            memcpy(notification->actions[notification->action_count].key, key, strlen(key) + 1);
            notify_sanitize(notification->actions[notification->action_count].label,
                            sizeof(notification->actions[0].label), *label ? label : key);
            notification->action_count++;
        }
    }
}

static int notify_hint(DBusMessage* message, const char* name, int type, DBusMessageIter* var)
{
    DBusMessageIter iter;

    if (!notify_arg_at(message, &iter, 6, DBUS_TYPE_ARRAY))
        return 0;
    DBusMessageIter arr;
    dbus_message_iter_recurse(&iter, &arr);
    DBusMessageIter entry;
    const char* key;
    for (; dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_DICT_ENTRY;
         dbus_message_iter_next(&arr)) {
        dbus_message_iter_recurse(&arr, &entry);
        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING)
            return 0; /* not the a{sv} the spec asks for */
        dbus_message_iter_get_basic(&entry, &key);
        if (strcmp(key, name) || !dbus_message_iter_next(&entry))
            continue;
        dbus_message_iter_recurse(&entry, var);
        return dbus_message_iter_get_arg_type(var) == type;
    }
    return 0;
}

/* The desktop-entry hint names the sender's .desktop file, which is also
 * what a Wayland client usually sets as its app_id. */
static void notify_desktop_entry(DBusMessage* message, char* dst, size_t dstsz)
{
    DBusMessageIter var;

    *dst = '\0';
    if (!notify_hint(message, "desktop-entry", DBUS_TYPE_STRING, &var))
        return;
    const char* val;
    dbus_message_iter_get_basic(&var, &val);
    notify_sanitize(dst, dstsz, val);
}

static int notify_bool_hint(DBusMessage* message, const char* name)
{
    DBusMessageIter var;

    if (!notify_hint(message, name, DBUS_TYPE_BOOLEAN, &var))
        return 0;
    dbus_bool_t value;
    dbus_message_iter_get_basic(&var, &value);
    return value;
}

/* dunst's hint, then the older Ubuntu one */
static void notify_stack_tag(DBusMessage* message, char* dst, size_t dstsz)
{
    DBusMessageIter var;

    *dst = '\0';
    if (!notify_hint(message, "x-dunst-stack-tag", DBUS_TYPE_STRING, &var) &&
        !notify_hint(message, "x-canonical-private-synchronous", DBUS_TYPE_STRING, &var))
        return;
    const char* val;
    dbus_message_iter_get_basic(&var, &val);
    notify_sanitize(dst, dstsz, val);
}

/* clients send it signed or unsigned */
static int notify_value(DBusMessage* message)
{
    DBusMessageIter var;
    dbus_int32_t value;

    if (notify_hint(message, "value", DBUS_TYPE_INT32, &var)) {
        dbus_message_iter_get_basic(&var, &value);
    } else if (notify_hint(message, "value", DBUS_TYPE_UINT32, &var)) {
        dbus_uint32_t unsigned_value;
        dbus_message_iter_get_basic(&var, &unsigned_value);
        value = unsigned_value > 100 ? 100 : (dbus_int32_t)unsigned_value;
    } else {
        return -1;
    }
    return value < 0 ? 0 : value > 100 ? 100 : value;
}

/* Flattens body markup to plain text: entities are decoded, the tags the
 * spec allows are dropped (<br> becomes a space), any other < is kept. */
static void notify_demarkup(char* dst, size_t dstsz, const char* src)
{
    static const struct MarkupEntity {
        const char* name;
        char character;
    } entities[] = {
        { "amp;", '&' }, { "lt;", '<' }, { "gt;", '>' }, { "quot;", '"' }, { "apos;", '\'' }
    };
    static const char* const tags[] = { "b", "i", "u", "a", "img", "br" };
    size_t written = 0;

    if (!dstsz)
        return;
    for (const char* cursor = src; *cursor && written + 1 < dstsz;) {
        if (*cursor == '&') {
            size_t entity_index = 0;
            while (entity_index < sizeof(entities) / sizeof(*entities) &&
                   strncmp(cursor + 1, entities[entity_index].name,
                           strlen(entities[entity_index].name)))
                entity_index++;
            if (entity_index < sizeof(entities) / sizeof(*entities)) {
                dst[written++] = entities[entity_index].character;
                cursor += 1 + strlen(entities[entity_index].name);
                continue;
            }
            if (cursor[1] == '#') {
                char* number_end;
                const unsigned long codepoint = cursor[2] == 'x' || cursor[2] == 'X'
                                                    ? strtoul(cursor + 3, &number_end, 16)
                                                    : strtoul(cursor + 2, &number_end, 10);
                size_t encoded_length;
                if (*number_end == ';' && number_end > cursor + 2 &&
                    (encoded_length =
                         util_put_codepoint(dst + written, dstsz - written, codepoint))) {
                    written += encoded_length;
                    cursor = number_end + 1;
                    continue;
                }
            }
        } else if (*cursor == '<') {
            const char* tag_end = strchr(cursor, '>');
            if (tag_end) {
                const char* name = cursor + 1 + (cursor[1] == '/');
                size_t name_length = 0;
                while (name[name_length] && !strchr(" \t\n/>", name[name_length]))
                    name_length++;
                size_t tag_index = 0;
                while (tag_index < sizeof(tags) / sizeof(*tags) &&
                       !(strlen(tags[tag_index]) == name_length &&
                         !strncasecmp(name, tags[tag_index], name_length)))
                    tag_index++;
                if (tag_index < sizeof(tags) / sizeof(*tags)) {
                    if (!strcmp(tags[tag_index], "br"))
                        dst[written++] = ' ';
                    cursor = tag_end + 1;
                    continue;
                }
            }
        }
        dst[written++] = *cursor++;
    }
    dst[written] = '\0';
}

static int notify_urgency(DBusMessage* message)
{
    DBusMessageIter var;

    if (!notify_hint(message, "urgency", DBUS_TYPE_BYTE, &var))
        return NOTIFICATION_URGENCY_NORMAL;
    unsigned char level;
    dbus_message_iter_get_basic(&var, &level);
    return level <= NOTIFICATION_URGENCY_CRITICAL ? level : NOTIFICATION_URGENCY_NORMAL;
}

/* (iiibiiay): width, height, rowstride, has_alpha, bits_per_sample,
 * channels, data. Validated before any byte is read. */
static RasterIcon* notify_image_data(DBusMessage* message, const char* name)
{
    DBusMessageIter var;

    if (!notify_hint(message, name, DBUS_TYPE_STRUCT, &var))
        return nullptr;
    DBusMessageIter structure;
    dbus_message_iter_recurse(&var, &structure);
    /* width, height, rowstride */
    dbus_int32_t dimensions[3];
    for (int i = 0; i < 3; i++, dbus_message_iter_next(&structure)) {
        if (dbus_message_iter_get_arg_type(&structure) != DBUS_TYPE_INT32)
            return nullptr;
        dbus_message_iter_get_basic(&structure, &dimensions[i]);
    }
    if (dbus_message_iter_get_arg_type(&structure) != DBUS_TYPE_BOOLEAN)
        return nullptr;
    dbus_bool_t alpha;
    dbus_message_iter_get_basic(&structure, &alpha);
    dbus_message_iter_next(&structure);
    if (dbus_message_iter_get_arg_type(&structure) != DBUS_TYPE_INT32)
        return nullptr;
    dbus_int32_t bits_per_sample;
    dbus_message_iter_get_basic(&structure, &bits_per_sample);
    dbus_message_iter_next(&structure);
    if (dbus_message_iter_get_arg_type(&structure) != DBUS_TYPE_INT32)
        return nullptr;
    dbus_int32_t channels;
    dbus_message_iter_get_basic(&structure, &channels);
    dbus_message_iter_next(&structure);
    if (dbus_message_iter_get_arg_type(&structure) != DBUS_TYPE_ARRAY ||
        dbus_message_iter_get_element_type(&structure) != DBUS_TYPE_BYTE)
        return nullptr;
    DBusMessageIter array;
    dbus_message_iter_recurse(&structure, &array);
    const uint8_t* data;
    int length;
    dbus_message_iter_get_fixed_array(&array, &data, &length);

    if (dimensions[0] <= 0 || dimensions[1] <= 0 || dimensions[0] > NOTIFY_IMAGEMAX ||
        dimensions[1] > NOTIFY_IMAGEMAX || bits_per_sample != 8 || channels != (alpha ? 4 : 3) ||
        dimensions[2] < dimensions[0] * channels ||
        (int64_t)length <
            (int64_t)dimensions[2] * (dimensions[1] - 1) + (int64_t)dimensions[0] * channels)
        return nullptr;
    return icon_create_from_data(data, dimensions[0], dimensions[1], dimensions[2], alpha,
                                 NOTIFY_ICONSIZE);
}

/* a path, a file:// URI or an icon name */
static RasterIcon* notify_image_named(const char* name)
{
    if (!name || !*name)
        return nullptr;
    if (!strncmp(name, "file://", 7))
        name += 7;
    return icon_create_from_name(name, nullptr, NOTIFY_ICONSIZE);
}

/* in the spec's order of precedence */
static RasterIcon* notify_icon(DBusMessage* message, const char* app_icon)
{
    RasterIcon* icon;

    if ((icon = notify_image_data(message, "image-data")) ||
        (icon = notify_image_data(message, "image_data")))
        return icon;
    DBusMessageIter var;
    const char* path;
    if (notify_hint(message, "image-path", DBUS_TYPE_STRING, &var) ||
        notify_hint(message, "image_path", DBUS_TYPE_STRING, &var)) {
        dbus_message_iter_get_basic(&var, &path);
        if ((icon = notify_image_named(path)))
            return icon;
    }
    if ((icon = notify_image_named(app_icon)))
        return icon;
    return notify_image_data(message, "icon_data");
}

static DBusHandlerResult notify_reply_empty(DBusConnection* connection, DBusMessage* message)
{
    DBusMessage* reply = dbus_message_new_method_return(message);
    DBusHandlerResult res = DBUS_HANDLER_RESULT_HANDLED;

    if (!reply || !dbus_connection_send(connection, reply, nullptr))
        res = DBUS_HANDLER_RESULT_NEED_MEMORY;
    if (reply)
        dbus_message_unref(reply);
    return res;
}

static DBusHandlerResult notify_handle_notify(DBusConnection* connection, DBusMessage* message)
{
    DBusError err = DBUS_ERROR_INIT;
    DBusMessage* reply;
    const char *app_name = "", *app_icon = "", *summary = "", *body = "";
    dbus_uint32_t replaces_id = 0;
    dbus_uint32_t id;
    Notification notification = {};
    /* bounded well under NOTIFY_TEXTMAX: snprintf() below can't truncate */
    char capp[64], csummary[200], cbody[200], plain[1024];

    /* actions/hints intentionally left unread; notify_expire_timeout comes
     * after them and is picked up separately below */
    if (!dbus_message_get_args(message, &err, DBUS_TYPE_STRING, &app_name, DBUS_TYPE_UINT32,
                               &replaces_id, DBUS_TYPE_STRING, &app_icon, DBUS_TYPE_STRING,
                               &summary, DBUS_TYPE_STRING, &body, DBUS_TYPE_INVALID)) {
        reply = dbus_message_new_error(message, err.name ? err.name : DBUS_ERROR_INVALID_ARGS,
                                       err.message ? err.message : "bad Notify arguments");
        dbus_error_free(&err);
        goto send;
    }

    int timeout_ms = notify_expire_timeout(message);
    /* -1 asks for the server default and 0 for "never expire"; the bar has a
     * single slot shared with the window title, so neither gets to sit there
     * forever. */
    if (timeout_ms <= 0)
        timeout_ms = (int)notify_state.timeout_ms;
    else if (timeout_ms > NOTIFY_TIMEOUT_MAX)
        timeout_ms = NOTIFY_TIMEOUT_MAX;
    notification.urgency = notify_urgency(message);
    notification.timeout_ms =
        notification.urgency == NOTIFICATION_URGENCY_CRITICAL ? 0 : (unsigned int)timeout_ms;
    notification.transient = notify_bool_hint(message, "transient");
    notification.resident = notify_bool_hint(message, "resident");
    notify_actions(message, &notification);

    notify_sanitize(capp, sizeof(capp), *app_name ? app_name : "?");
    notify_sanitize(csummary, sizeof(csummary), summary);
    notify_demarkup(plain, sizeof(plain), body);
    notify_sanitize(cbody, sizeof(cbody), plain);
    if (*csummary && *cbody)
        snprintf(notification.text, sizeof(notification.text), "%s: %s - %s", capp, csummary,
                 cbody);
    else
        snprintf(notification.text, sizeof(notification.text), "%s: %s", capp,
                 *csummary ? csummary : cbody);
    snprintf(notification.app_name, sizeof(notification.app_name), "%s", capp);
    notify_desktop_entry(message, notification.desktop_entry, sizeof(notification.desktop_entry));
    notify_stack_tag(message, notification.stack_tag, sizeof(notification.stack_tag));
    notification.progress_percent = notify_value(message);
    notification.icon = notify_icon(message, app_icon);

    /* Updates, by id or by stack tag, keep their place on screen or in the
     * queue. */
    id = replaces_id;
    if (!id && *notification.stack_tag) {
        if (notify_state.has_current && notify_same_slot(&notify_state.current, &notification))
            id = notify_state.current.id;
        for (size_t queue_index = 0; !id && queue_index < notify_state.queue_count; queue_index++)
            if (notify_same_slot(&notify_state.queue[queue_index], &notification))
                id = notify_state.queue[queue_index].id;
    }
    if (!id)
        id = ++notify_state.next_notification_id;
    if (!id) /* the counter wrapped; 0 means "no notification" */
        id = ++notify_state.next_notification_id;
    notification.id = id;

    size_t queue_index = 0;
    while (queue_index < notify_state.queue_count && notify_state.queue[queue_index].id != id)
        queue_index++;
    if (notify_state.has_current && notify_state.current.id == id) {
        notify_release(&notify_state.current);
        notify_state.current = notification;
        if (!notify_show()) {
            notify_closed(id, NOTIFICATION_CLOSED_UNDEFINED);
            notify_history_push(&notify_state.current);
            notify_show_next();
        }
    } else if (queue_index < notify_state.queue_count) {
        notify_release(&notify_state.queue[queue_index]);
        notify_state.queue[queue_index] = notification;
    } else if (notify_state.is_paused) {
        /* the user is picking an action: nothing takes the box */
        notify_enqueue(&notification, notification.urgency == NOTIFICATION_URGENCY_CRITICAL);
    } else if (!notify_state.has_current || !notify_state.current.timeout_ms ||
               notification.urgency == NOTIFICATION_URGENCY_CRITICAL) {
        /* shown now if the box is free, sticky, or this one is critical;
         * what it covers goes back to the front of the queue */
        if (notify_state.has_current) {
            notify_arm(0);
            notify_state.has_current = 0;
            notify_enqueue(&notify_state.current, 1);
        }
        notify_state.current = notification;
        if (!notify_show()) {
            notify_closed(id, NOTIFICATION_CLOSED_UNDEFINED);
            notify_history_push(&notify_state.current);
            notify_show_next();
        }
    } else {
        notify_enqueue(&notification, 0);
    }
    if (notify_state.redraw_callback)
        notify_state.redraw_callback();

    reply = dbus_message_new_method_return(message);
    if (reply)
        dbus_message_append_args(reply, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID);

send:
    if (!reply || !dbus_connection_send(connection, reply, nullptr)) {
        if (reply)
            dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult notify_handle_close(DBusConnection* connection, DBusMessage* message)
{
    dbus_uint32_t id;

    if (!dbus_message_get_args(message, nullptr, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID))
        return notify_reply_empty(connection, message);
    size_t queue_index = 0;
    while (queue_index < notify_state.queue_count && notify_state.queue[queue_index].id != id)
        queue_index++;
    if (notify_state.has_current && id == notify_state.current.id) {
        notify_clear(NOTIFICATION_CLOSED_BY_REQUEST);
    } else if (id && queue_index < notify_state.queue_count) {
        Notification closed;
        notify_unqueue(queue_index, &closed);
        notify_release(&closed);
        notify_closed(id, NOTIFICATION_CLOSED_BY_REQUEST);
        if (notify_state.redraw_callback)
            notify_state.redraw_callback();
    }

    return notify_reply_empty(connection, message);
}

static DBusHandlerResult notify_handle_capabilities(DBusConnection* connection,
                                                    DBusMessage* message)
{
    DBusMessage* reply = dbus_message_new_method_return(message);
    const char* caps[] = { "body", "body-markup", "actions", "icon-static" };
    DBusHandlerResult res = DBUS_HANDLER_RESULT_HANDLED;

    if (!reply)
        return DBUS_HANDLER_RESULT_NEED_MEMORY;

    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);
    DBusMessageIter arr;
    if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &arr))
        res = DBUS_HANDLER_RESULT_NEED_MEMORY;
    for (size_t i = 0; res == DBUS_HANDLER_RESULT_HANDLED && i < sizeof(caps) / sizeof(*caps); i++)
        if (!dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &caps[i]))
            res = DBUS_HANDLER_RESULT_NEED_MEMORY;
    if (res != DBUS_HANDLER_RESULT_HANDLED || !dbus_message_iter_close_container(&iter, &arr) ||
        !dbus_connection_send(connection, reply, nullptr))
        res = DBUS_HANDLER_RESULT_NEED_MEMORY;

    dbus_message_unref(reply);
    return res;
}

static DBusHandlerResult notify_handle_server_info(DBusConnection* connection, DBusMessage* message)
{
    DBusMessage* reply = dbus_message_new_method_return(message);
    const char *name = "g0wm", *vendor = "g0wm", *version = "1.0", *spec = "1.2";
    DBusHandlerResult res = DBUS_HANDLER_RESULT_HANDLED;

    if (!reply)
        return DBUS_HANDLER_RESULT_NEED_MEMORY;

    if (!dbus_message_append_args(reply, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &vendor,
                                  DBUS_TYPE_STRING, &version, DBUS_TYPE_STRING, &spec,
                                  DBUS_TYPE_INVALID) ||
        !dbus_connection_send(connection, reply, nullptr))
        res = DBUS_HANDLER_RESULT_NEED_MEMORY;

    dbus_message_unref(reply);
    return res;
}

DBusHandlerResult notify_handle_message(DBusConnection* connection,
                                        DBusMessage* message,
                                        void* data)
{
    (void)data;

    if (dbus_message_is_method_call(message, NOTIFY_IFACE, "Notify"))
        return notify_handle_notify(connection, message);
    else if (dbus_message_is_method_call(message, NOTIFY_IFACE, "CloseNotification"))
        return notify_handle_close(connection, message);
    else if (dbus_message_is_method_call(message, NOTIFY_IFACE, "GetCapabilities"))
        return notify_handle_capabilities(connection, message);
    else if (dbus_message_is_method_call(message, NOTIFY_IFACE, "GetServerInformation"))
        return notify_handle_server_info(connection, message);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}
