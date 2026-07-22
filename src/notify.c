#include "notify.h"

#include <stdio.h>
#include <string.h>

#define NOTIFY_NAME "org.freedesktop.Notifications"
#define NOTIFY_OPATH "/org/freedesktop/Notifications"
#define NOTIFY_IFACE "org.freedesktop.Notifications"
/* An unbounded expire_timeout would let one client hold the bar (and hide the
 * window title) for the rest of the session. */
#define NOTIFY_TIMEOUT_MAX 60000

/* org.freedesktop.Notifications.NotificationClosed reasons */
enum { ClosedExpired = 1, ClosedDismissed, ClosedByCall, ClosedUndefined };

static struct {
    DBusConnection* conn;
    struct wl_event_loop* loop;
    struct wl_event_source* timer;
    void (*redraw)(void);
    unsigned int timeout_ms;
    dbus_uint32_t id;  /* id of the notification on screen */
    dbus_uint32_t seq; /* handed out to clients that don't pick one */
    int active;
    int running;
    char text[NOTIFY_TEXTMAX];
} notify;

/* Copies src into dst keeping only whole, well-formed UTF-8 codepoints:
 * control characters become spaces, invalid bytes are dropped and a sequence
 * that doesn't fit is left out entirely. Anything on this path comes straight
 * from an arbitrary bus client, and half a codepoint renders as garbage. */
static void sanitize(char* dst, size_t dstsz, const char* src)
{
    size_t i = 0, n, k;
    unsigned char c;

    if (!dstsz)
        return;
    for (; src && (c = (unsigned char)*src); src += n) {
        n = c < 0x80   ? 1
            : c < 0xC2 ? 0 /* continuation byte or overlong lead */
            : c < 0xE0 ? 2
            : c < 0xF0 ? 3
            : c < 0xF5 ? 4
                       : 0;
        if (!n) {
            n = 1;
            continue;
        }
        for (k = 1; k < n; k++)
            if (((unsigned char)src[k] & 0xC0) != 0x80)
                break;
        if (k < n) { /* truncated sequence: drop the lead byte and resync */
            n = 1;
            continue;
        }
        if (i + n >= dstsz)
            break;
        if (c < ' ' || c == 0x7f) {
            dst[i++] = ' '; /* n is 1 here */
            continue;
        }
        for (k = 0; k < n; k++)
            dst[i++] = (char)src[k];
    }
    dst[i] = '\0';
}

static void notify_closed(dbus_uint32_t id, dbus_uint32_t reason)
{
    DBusMessage* sig;

    if (!id || !(sig = dbus_message_new_signal(
                     NOTIFY_OPATH, NOTIFY_IFACE, "NotificationClosed")))
        return;
    if (dbus_message_append_args(sig,
                                 DBUS_TYPE_UINT32,
                                 &id,
                                 DBUS_TYPE_UINT32,
                                 &reason,
                                 DBUS_TYPE_INVALID))
        dbus_connection_send(notify.conn, sig, NULL);
    dbus_message_unref(sig);
}

/* Clears the current notification, if any, and tells the bar to redraw so
 * whatever the notification was covering (the window title) comes back. */
static void notify_clear(dbus_uint32_t reason)
{
    if (!notify.active)
        return;
    notify.active = 0;
    if (notify.timer)
        wl_event_source_timer_update(notify.timer, 0);
    notify_closed(notify.id, reason);
    if (notify.redraw)
        notify.redraw();
}

static int notify_expire(void* data)
{
    (void)data;
    notify_clear(ClosedExpired);
    return 0;
}

static int notify_arm(unsigned int ms)
{
    if (!notify.timer)
        notify.timer =
            wl_event_loop_add_timer(notify.loop, notify_expire, NULL);
    /* Without a timer the notification would sit in the bar forever, so the
     * caller is expected to drop it instead of showing it unexpirable. */
    return notify.timer &&
           wl_event_source_timer_update(notify.timer, (int)ms) == 0;
}

/* expire_timeout is the 8th Notify argument, past the actions array and the
 * hints dict, so it can't be reached with dbus_message_get_args(). */
static int expire_timeout(DBusMessage* msg)
{
    DBusMessageIter iter;
    dbus_int32_t ms;
    int i;

    if (!dbus_message_iter_init(msg, &iter))
        return -1;
    for (i = 0; i < 7; i++)
        if (!dbus_message_iter_next(&iter))
            return -1;
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32)
        return -1;
    dbus_message_iter_get_basic(&iter, &ms);
    return ms;
}

static DBusHandlerResult reply_empty(DBusConnection* conn, DBusMessage* msg)
{
    DBusMessage* reply = dbus_message_new_method_return(msg);
    DBusHandlerResult res = DBUS_HANDLER_RESULT_HANDLED;

    if (!reply || !dbus_connection_send(conn, reply, NULL))
        res = DBUS_HANDLER_RESULT_NEED_MEMORY;
    if (reply)
        dbus_message_unref(reply);
    return res;
}

static DBusHandlerResult handle_notify(DBusConnection* conn, DBusMessage* msg)
{
    DBusError err = DBUS_ERROR_INIT;
    DBusMessage* reply;
    const char *app_name = "", *app_icon = "", *summary = "", *body = "";
    dbus_uint32_t replaces_id = 0, id;
    int ms;
    /* bounded well under NOTIFY_TEXTMAX: snprintf() below can't truncate */
    char capp[64], csummary[200], cbody[200];

    /* actions/hints intentionally left unread; expire_timeout comes after
     * them and is picked up separately below */
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
        reply = dbus_message_new_error(
            msg,
            err.name ? err.name : DBUS_ERROR_INVALID_ARGS,
            err.message ? err.message : "bad Notify arguments");
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

    ms = expire_timeout(msg);
    /* -1 asks for the server default and 0 for "never expire"; the bar has a
     * single slot shared with the window title, so neither gets to sit there
     * forever.
     * ponytail: no persistent notifications, add a queue if that's wanted. */
    if (ms <= 0)
        ms = (int)notify.timeout_ms;
    else if (ms > NOTIFY_TIMEOUT_MAX)
        ms = NOTIFY_TIMEOUT_MAX;

    /* A client updating its own notification keeps the id it was given;
     * anything else displaces whatever was on screen. */
    id = replaces_id ? replaces_id : ++notify.seq;
    if (!id) /* the counter wrapped; 0 means "no notification" */
        id = ++notify.seq;
    if (notify.active && notify.id != id)
        notify_closed(notify.id, ClosedUndefined);

    notify.id = id;
    notify.active = notify_arm((unsigned int)ms);
    if (!notify.active) /* no timer: don't show what we can't take down */
        notify_closed(id, ClosedUndefined);
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

static DBusHandlerResult handle_close(DBusConnection* conn, DBusMessage* msg)
{
    dbus_uint32_t id;

    if (dbus_message_get_args(
            msg, NULL, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID) &&
        notify.active && id == notify.id)
        notify_clear(ClosedByCall);

    return reply_empty(conn, msg);
}

static DBusHandlerResult handle_capabilities(DBusConnection* conn,
                                             DBusMessage* msg)
{
    DBusMessage* reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter, arr;
    const char* cap = "body";
    DBusHandlerResult res = DBUS_HANDLER_RESULT_HANDLED;

    if (!reply)
        return DBUS_HANDLER_RESULT_NEED_MEMORY;

    dbus_message_iter_init_append(reply, &iter);
    if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &arr) ||
        !dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &cap) ||
        !dbus_message_iter_close_container(&iter, &arr) ||
        !dbus_connection_send(conn, reply, NULL))
        res = DBUS_HANDLER_RESULT_NEED_MEMORY;

    dbus_message_unref(reply);
    return res;
}

static DBusHandlerResult handle_serverinfo(DBusConnection* conn,
                                           DBusMessage* msg)
{
    DBusMessage* reply = dbus_message_new_method_return(msg);
    const char *name = "dwl", *vendor = "dwl", *version = "1.0", *spec = "1.2";
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

static DBusHandlerResult notify_message_handler(DBusConnection* conn,
                                                DBusMessage* msg,
                                                void* data)
{
    (void)data;

    if (dbus_message_is_method_call(msg, NOTIFY_IFACE, "Notify"))
        return handle_notify(conn, msg);
    else if (dbus_message_is_method_call(
                 msg, NOTIFY_IFACE, "CloseNotification"))
        return handle_close(conn, msg);
    else if (dbus_message_is_method_call(msg, NOTIFY_IFACE, "GetCapabilities"))
        return handle_capabilities(conn, msg);
    else if (dbus_message_is_method_call(
                 msg, NOTIFY_IFACE, "GetServerInformation"))
        return handle_serverinfo(conn, msg);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static const DBusObjectPathVTable notify_vtable = {
    .message_function = notify_message_handler
};

void notify_start(DBusConnection* conn,
                  struct wl_event_loop* loop,
                  unsigned int timeout_secs,
                  void (*redraw)(void))
{
    int r;

    if (!conn || !loop)
        return;

    memset(&notify, 0, sizeof(notify));
    notify.conn = conn;
    notify.loop = loop;
    notify.timeout_ms = timeout_secs > NOTIFY_TIMEOUT_MAX / 1000
                            ? NOTIFY_TIMEOUT_MAX
                            : (timeout_secs ? timeout_secs : 1) * 1000;
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

void notify_stop(void)
{
    if (!notify.running)
        return;

    /* nothing is worth redrawing on the way out, but clients still deserve
     * to hear that their notification went away */
    notify.redraw = NULL;
    notify_clear(ClosedUndefined);
    if (notify.timer) {
        wl_event_source_remove(notify.timer);
        notify.timer = NULL;
    }
    dbus_connection_unregister_object_path(notify.conn, NOTIFY_OPATH);
    dbus_bus_release_name(notify.conn, NOTIFY_NAME, NULL);
    notify.running = 0;
}

void notify_dismiss(void)
{
    notify_clear(ClosedDismissed);
}

const char* notify_gettext(void)
{
    return notify.active ? notify.text : NULL;
}

unsigned int notify_getid(void)
{
    return notify.active ? (unsigned int)notify.id : 0;
}
