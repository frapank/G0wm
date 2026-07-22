#include "notify.h"

#include <stdio.h>
#include <string.h>

#define NOTIFY_NAME "org.freedesktop.Notifications"
#define NOTIFY_OPATH "/org/freedesktop/Notifications"
#define NOTIFY_IFACE "org.freedesktop.Notifications"
#define NOTIFY_TEXTMAX 512

static struct {
    DBusConnection* conn;
    struct wl_event_loop* loop;
    struct wl_event_source* timer;
    void (*redraw)(void);
    unsigned int timeout_ms;
    dbus_uint32_t id;
    int active;
    int running;
    char text[NOTIFY_TEXTMAX];
} notify;

static void
sanitize(char* dst, size_t dstsz, const char* src)
{
    size_t i = 0;

    for (; src && *src && i + 1 < dstsz; src++)
        dst[i++] = (unsigned char)*src < ' ' ? ' ' : *src;
    dst[i] = '\0';
}

static int
notify_expire(void* data)
{
    (void)data;
    notify.active = 0;
    if (notify.redraw)
        notify.redraw();
    return 0;
}

static void
notify_arm(void)
{
    if (!notify.timer)
        notify.timer =
            wl_event_loop_add_timer(notify.loop, notify_expire, NULL);
    if (notify.timer)
        wl_event_source_timer_update(notify.timer, notify.timeout_ms);
}

static DBusHandlerResult
reply_empty(DBusConnection* conn, DBusMessage* msg)
{
    DBusMessage* reply = dbus_message_new_method_return(msg);
    DBusHandlerResult res = DBUS_HANDLER_RESULT_HANDLED;

    if (!reply || !dbus_connection_send(conn, reply, NULL))
        res = DBUS_HANDLER_RESULT_NEED_MEMORY;
    if (reply)
        dbus_message_unref(reply);
    return res;
}

static DBusHandlerResult
handle_notify(DBusConnection* conn, DBusMessage* msg)
{
    DBusError err = DBUS_ERROR_INIT;
    DBusMessage* reply;
    const char *app_name = "", *app_icon = "", *summary = "", *body = "";
    dbus_uint32_t replaces_id = 0, id;
    /* bounded well under NOTIFY_TEXTMAX: snprintf() below can't truncate */
    char capp[64], csummary[200], cbody[200];

    /* actions/hints/expire_timeout intentionally left unread */
    if (!dbus_message_get_args(msg,
                               &err,
                               DBUS_TYPE_STRING,
                               &app_name,
                               DBUS_TYPE_UINT32,
                               &replaces_id,
                               DBUS_TYPE_STRING,
                               &app_icon,
                               DBUS_TYPE_STRING,
                               &summary,
                               DBUS_TYPE_STRING,
                               &body,
                               DBUS_TYPE_INVALID)) {
        reply = dbus_message_new_error(msg, err.name, err.message);
        dbus_error_free(&err);
        goto send;
    }

    sanitize(capp, sizeof(capp), *app_name ? app_name : "?");
    sanitize(csummary, sizeof(csummary), summary);
    sanitize(cbody, sizeof(cbody), body);

    if (*csummary && *cbody)
        snprintf(notify.text,
                 sizeof(notify.text),
                 "%s: %s - %s",
                 capp,
                 csummary,
                 cbody);
    else
        snprintf(notify.text,
                 sizeof(notify.text),
                 "%s: %s",
                 capp,
                 *csummary ? csummary : cbody);

    id = ++notify.id;
    if (!id) /* 0 is reserved to mean "no notification" */
        id = ++notify.id;
    notify.id = id;
    notify.active = 1;
    notify_arm();
    if (notify.redraw)
        notify.redraw();

    reply = dbus_message_new_method_return(msg);
    if (reply)
        dbus_message_append_args(
            reply, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID);

send:
    if (!reply || !dbus_connection_send(conn, reply, NULL)) {
        if (reply)
            dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
handle_close(DBusConnection* conn, DBusMessage* msg)
{
    dbus_uint32_t id;

    if (dbus_message_get_args(
            msg, NULL, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID) &&
        notify.active && id == notify.id) {
        notify.active = 0;
        if (notify.redraw)
            notify.redraw();
    }

    return reply_empty(conn, msg);
}

static DBusHandlerResult
handle_capabilities(DBusConnection* conn, DBusMessage* msg)
{
    DBusMessage* reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter, arr;
    const char* cap = "body";
    DBusHandlerResult res = DBUS_HANDLER_RESULT_HANDLED;

    if (!reply)
        return DBUS_HANDLER_RESULT_NEED_MEMORY;

    dbus_message_iter_init_append(reply, &iter);
    if (!dbus_message_iter_open_container(
            &iter, DBUS_TYPE_ARRAY, "s", &arr) ||
        !dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &cap) ||
        !dbus_message_iter_close_container(&iter, &arr) ||
        !dbus_connection_send(conn, reply, NULL))
        res = DBUS_HANDLER_RESULT_NEED_MEMORY;

    dbus_message_unref(reply);
    return res;
}

static DBusHandlerResult
handle_serverinfo(DBusConnection* conn, DBusMessage* msg)
{
    DBusMessage* reply = dbus_message_new_method_return(msg);
    const char *name = "dwl", *vendor = "dwl", *version = "1.0",
               *spec = "1.2";
    DBusHandlerResult res = DBUS_HANDLER_RESULT_HANDLED;

    if (!reply)
        return DBUS_HANDLER_RESULT_NEED_MEMORY;

    if (!dbus_message_append_args(reply,
                                  DBUS_TYPE_STRING,
                                  &name,
                                  DBUS_TYPE_STRING,
                                  &vendor,
                                  DBUS_TYPE_STRING,
                                  &version,
                                  DBUS_TYPE_STRING,
                                  &spec,
                                  DBUS_TYPE_INVALID) ||
        !dbus_connection_send(conn, reply, NULL))
        res = DBUS_HANDLER_RESULT_NEED_MEMORY;

    dbus_message_unref(reply);
    return res;
}

static DBusHandlerResult
notify_message_handler(DBusConnection* conn, DBusMessage* msg, void* data)
{
    (void)data;

    if (dbus_message_is_method_call(msg, NOTIFY_IFACE, "Notify"))
        return handle_notify(conn, msg);
    else if (dbus_message_is_method_call(msg, NOTIFY_IFACE,
                                         "CloseNotification"))
        return handle_close(conn, msg);
    else if (dbus_message_is_method_call(msg, NOTIFY_IFACE,
                                         "GetCapabilities"))
        return handle_capabilities(conn, msg);
    else if (dbus_message_is_method_call(msg, NOTIFY_IFACE,
                                         "GetServerInformation"))
        return handle_serverinfo(conn, msg);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static const DBusObjectPathVTable notify_vtable = { .message_function =
                                                        notify_message_handler };

void
notify_start(DBusConnection* conn,
            struct wl_event_loop* loop,
            unsigned int timeout_secs,
            void (*redraw)(void))
{
    int r;

    memset(&notify, 0, sizeof(notify));
    notify.conn = conn;
    notify.loop = loop;
    notify.timeout_ms = (timeout_secs ? timeout_secs : 1) * 1000;
    notify.redraw = redraw;

    /* if another daemon (mako, dunst, swaync, ...) already owns the name,
     * don't fight it for it: just stay disabled */
    r = dbus_bus_request_name(
        conn, NOTIFY_NAME, DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL);
    if (r != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr,
               "Couldn't own %s (another notification daemon is likely "
               "running), bar notifications not available\n",
               NOTIFY_NAME);
        return;
    }

    if (!dbus_connection_register_object_path(
            conn, NOTIFY_OPATH, &notify_vtable, NULL)) {
        dbus_bus_release_name(conn, NOTIFY_NAME, NULL);
        fprintf(stderr, "Couldn't register %s\n", NOTIFY_OPATH);
        return;
    }

    notify.running = 1;
}

void
notify_stop(void)
{
    if (!notify.running)
        return;

    if (notify.timer)
        wl_event_source_remove(notify.timer);
    dbus_connection_unregister_object_path(notify.conn, NOTIFY_OPATH);
    dbus_bus_release_name(notify.conn, NOTIFY_NAME, NULL);
    notify.running = 0;
}

const char*
notify_gettext(void)
{
    return notify.active ? notify.text : NULL;
}

unsigned int
notify_getid(void)
{
    return notify.active ? (unsigned int)notify.id : 0;
}
