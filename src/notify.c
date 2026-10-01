#include "notify.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

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
    dbus_uint32_t seq; /* handed out to clients that don't pick one */
    int active;
    int running;
    Notification cur; /* valid when active */
    Notification queue[NOTIFY_QUEUEMAX];
    size_t nqueue;
    Notification hist[NOTIFY_HISTMAX]; /* newest first */
    size_t nhist;
} notify;

uint64_t notify_now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

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

/* Pushes n to the head of the history. An update sent after the
 * notification closed replaces the entry it left. */
static void histpush(Notification* n)
{
    size_t i;

    if (n->transient)
        return;
    for (i = 0; i < notify.nhist && notify.hist[i].id != n->id; i++)
        ;
    if (i == notify.nhist && notify.nhist == NOTIFY_HISTMAX)
        i = notify.nhist - 1; /* full: drop the oldest */
    if (i < notify.nhist) {
        memmove(&notify.hist[i],
                &notify.hist[i + 1],
                (notify.nhist - i - 1) * sizeof(*notify.hist));
        notify.nhist--;
    }
    memmove(&notify.hist[1], &notify.hist[0], notify.nhist * sizeof(*n));
    notify.hist[0] = *n;
    notify.nhist++;
}

static void unqueue(size_t i, Notification* dst)
{
    *dst = notify.queue[i];
    memmove(&notify.queue[i],
            &notify.queue[i + 1],
            (notify.nqueue - i - 1) * sizeof(*notify.queue));
    notify.nqueue--;
}

/* A full queue sends its oldest expiring entry straight to the history. */
static void enqueue(Notification* n, int front)
{
    Notification old;
    size_t i;

    if (notify.nqueue == NOTIFY_QUEUEMAX) {
        for (i = 0; i < notify.nqueue && !notify.queue[i].timeout_ms; i++)
            ;
        unqueue(i < notify.nqueue ? i : 0, &old);
        notify_closed(old.id, ClosedUndefined);
        histpush(&old);
    }
    if (front) {
        memmove(&notify.queue[1],
                &notify.queue[0],
                notify.nqueue * sizeof(*notify.queue));
        notify.queue[0] = *n;
    } else {
        notify.queue[notify.nqueue] = *n;
    }
    notify.nqueue++;
}

static void notify_clear(dbus_uint32_t reason);

static int notify_expire(void* data)
{
    (void)data;
    notify_clear(ClosedExpired);
    return 0;
}

/* 0 disarms */
static int notify_arm(unsigned int ms)
{
    if (!notify.timer && ms)
        notify.timer =
            wl_event_loop_add_timer(notify.loop, notify_expire, NULL);
    if (!notify.timer)
        return !ms;
    return wl_event_source_timer_update(notify.timer, (int)ms) == 0;
}

static int show(void)
{
    notify.cur.shown_ms = notify_now();
    /* Without a timer the notification would sit in the bar forever, so the
     * caller is expected to drop it instead of showing it unexpirable. */
    notify.active = notify_arm(notify.cur.timeout_ms);
    return notify.active;
}

/* The ones that stay until dismissed go last, or the rest would wait behind
 * them forever. */
static void shownext(void)
{
    size_t i;

    while (!notify.active && notify.nqueue) {
        for (i = 0; i < notify.nqueue && !notify.queue[i].timeout_ms; i++)
            ;
        unqueue(i < notify.nqueue ? i : 0, &notify.cur);
        if (!show()) {
            notify_closed(notify.cur.id, ClosedUndefined);
            histpush(&notify.cur);
        }
    }
}

/* Takes the current notification down and brings up the next one. */
static void notify_clear(dbus_uint32_t reason)
{
    if (!notify.active)
        return;
    notify.active = 0;
    notify_arm(0);
    notify_closed(notify.cur.id, reason);
    /* closed by the client: not worth keeping */
    if (reason != ClosedByCall)
        histpush(&notify.cur);
    shownext();
    if (notify.redraw)
        notify.redraw();
}

/* Points iter at the n-th Notify argument: actions, hints and expire_timeout
 * sit past containers dbus_message_get_args() won't hand back as is. */
static int argat(DBusMessage* msg, DBusMessageIter* iter, int n, int type)
{
    if (!dbus_message_iter_init(msg, iter))
        return 0;
    while (n--)
        if (!dbus_message_iter_next(iter))
            return 0;
    return dbus_message_iter_get_arg_type(iter) == type;
}

static int expire_timeout(DBusMessage* msg)
{
    DBusMessageIter iter;
    dbus_int32_t ms;

    if (!argat(msg, &iter, 7, DBUS_TYPE_INT32))
        return -1;
    dbus_message_iter_get_basic(&iter, &ms);
    return ms;
}

/* actions is a flat (key, label, ...) list; only "default", a click on the
 * notification itself, is of any use here. */
static int has_default(DBusMessage* msg)
{
    DBusMessageIter iter, arr;
    const char* key;
    int i;

    if (!argat(msg, &iter, 5, DBUS_TYPE_ARRAY))
        return 0;
    dbus_message_iter_recurse(&iter, &arr);
    for (i = 0; dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING;
         i++, dbus_message_iter_next(&arr)) {
        dbus_message_iter_get_basic(&arr, &key);
        if (!(i & 1) && !strcmp(key, "default"))
            return 1;
    }
    return 0;
}

static int hint(DBusMessage* msg,
                const char* name,
                int type,
                DBusMessageIter* var)
{
    DBusMessageIter iter, arr, entry;
    const char* key;

    if (!argat(msg, &iter, 6, DBUS_TYPE_ARRAY))
        return 0;
    dbus_message_iter_recurse(&iter, &arr);
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
static void desktop_entry(DBusMessage* msg, char* dst, size_t dstsz)
{
    DBusMessageIter var;
    const char* val;

    *dst = '\0';
    if (!hint(msg, "desktop-entry", DBUS_TYPE_STRING, &var))
        return;
    dbus_message_iter_get_basic(&var, &val);
    sanitize(dst, dstsz, val);
}

static int boolhint(DBusMessage* msg, const char* name)
{
    DBusMessageIter var;
    dbus_bool_t b;

    if (!hint(msg, name, DBUS_TYPE_BOOLEAN, &var))
        return 0;
    dbus_message_iter_get_basic(&var, &b);
    return b;
}

static int urgency(DBusMessage* msg)
{
    DBusMessageIter var;
    unsigned char u;

    if (!hint(msg, "urgency", DBUS_TYPE_BYTE, &var))
        return UrgencyNormal;
    dbus_message_iter_get_basic(&var, &u);
    return u <= UrgencyCritical ? u : UrgencyNormal;
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
    Notification n = { 0 };
    size_t i;
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

    ms = expire_timeout(msg);
    /* -1 asks for the server default and 0 for "never expire"; the bar has a
     * single slot shared with the window title, so neither gets to sit there
     * forever. */
    if (ms <= 0)
        ms = (int)notify.timeout_ms;
    else if (ms > NOTIFY_TIMEOUT_MAX)
        ms = NOTIFY_TIMEOUT_MAX;
    n.urgency = urgency(msg);
    n.timeout_ms = n.urgency == UrgencyCritical ? 0 : (unsigned int)ms;
    n.transient = boolhint(msg, "transient");
    n.hasdefault = has_default(msg);

    sanitize(capp, sizeof(capp), *app_name ? app_name : "?");
    sanitize(csummary, sizeof(csummary), summary);
    sanitize(cbody, sizeof(cbody), body);
    if (*csummary && *cbody)
        snprintf(n.text, sizeof(n.text), "%s: %s - %s", capp, csummary, cbody);
    else
        snprintf(n.text,
                 sizeof(n.text),
                 "%s: %s",
                 capp,
                 *csummary ? csummary : cbody);
    snprintf(n.app, sizeof(n.app), "%s", capp);
    desktop_entry(msg, n.desktop, sizeof(n.desktop));

    /* An update keeps its id and its place on screen or in the queue. */
    id = replaces_id ? replaces_id : ++notify.seq;
    if (!id) /* the counter wrapped; 0 means "no notification" */
        id = ++notify.seq;
    n.id = id;

    for (i = 0; i < notify.nqueue && notify.queue[i].id != id; i++)
        ;
    if (notify.active && notify.cur.id == id) {
        notify.cur = n;
        if (!show()) {
            notify_closed(id, ClosedUndefined);
            histpush(&notify.cur);
            shownext();
        }
    } else if (i < notify.nqueue) {
        notify.queue[i] = n;
    } else if (!notify.active || !notify.cur.timeout_ms ||
               n.urgency == UrgencyCritical) {
        /* shown now if the box is free, sticky, or this one is critical;
         * what it covers goes back to the front of the queue */
        if (notify.active) {
            notify_arm(0);
            notify.active = 0;
            enqueue(&notify.cur, 1);
        }
        notify.cur = n;
        if (!show()) {
            notify_closed(id, ClosedUndefined);
            histpush(&notify.cur);
            shownext();
        }
    } else {
        enqueue(&n, 0);
    }
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
    Notification n;
    dbus_uint32_t id;
    size_t i;

    if (!dbus_message_get_args(
            msg, NULL, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID))
        return reply_empty(conn, msg);
    for (i = 0; i < notify.nqueue && notify.queue[i].id != id; i++)
        ;
    if (notify.active && id == notify.cur.id) {
        notify_clear(ClosedByCall);
    } else if (id && i < notify.nqueue) {
        unqueue(i, &n);
        notify_closed(id, ClosedByCall);
        if (notify.redraw)
            notify.redraw();
    }

    return reply_empty(conn, msg);
}

static DBusHandlerResult handle_capabilities(DBusConnection* conn,
                                             DBusMessage* msg)
{
    DBusMessage* reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter, arr;
    const char* caps[] = { "body", "actions" };
    size_t i;
    DBusHandlerResult res = DBUS_HANDLER_RESULT_HANDLED;

    if (!reply)
        return DBUS_HANDLER_RESULT_NEED_MEMORY;

    dbus_message_iter_init_append(reply, &iter);
    if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &arr))
        res = DBUS_HANDLER_RESULT_NEED_MEMORY;
    for (i = 0;
         res == DBUS_HANDLER_RESULT_HANDLED && i < sizeof(caps) / sizeof(*caps);
         i++)
        if (!dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &caps[i]))
            res = DBUS_HANDLER_RESULT_NEED_MEMORY;
    if (res != DBUS_HANDLER_RESULT_HANDLED ||
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
    const char *name = "g0wm", *vendor = "g0wm", *version = "1.0",
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
    notify_settimeout(timeout_secs);
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
    Notification n;

    if (!notify.running)
        return;

    /* nothing is worth redrawing on the way out, but clients still deserve
     * to hear that their notification went away */
    notify.redraw = NULL;
    while (notify.nqueue) {
        unqueue(0, &n);
        notify_closed(n.id, ClosedUndefined);
    }
    notify_clear(ClosedUndefined);
    notify.nhist = 0;
    if (notify.timer) {
        wl_event_source_remove(notify.timer);
        notify.timer = NULL;
    }
    dbus_connection_unregister_object_path(notify.conn, NOTIFY_OPATH);
    dbus_bus_release_name(notify.conn, NOTIFY_NAME, NULL);
    notify.running = 0;
}

void notify_settimeout(unsigned int timeout_secs)
{
    notify.timeout_ms = timeout_secs > NOTIFY_TIMEOUT_MAX / 1000
                            ? NOTIFY_TIMEOUT_MAX
                            : (timeout_secs ? timeout_secs : 1) * 1000;
}

const Notification* notify_current(void)
{
    return notify.active ? &notify.cur : NULL;
}

size_t notify_queued(void)
{
    return notify.nqueue;
}

const Notification* notify_history(size_t i)
{
    return i < notify.nhist ? &notify.hist[i] : NULL;
}

size_t notify_histlen(void)
{
    return notify.nhist;
}

void notify_histremove(size_t i)
{
    if (i >= notify.nhist)
        return;
    memmove(&notify.hist[i],
            &notify.hist[i + 1],
            (notify.nhist - i - 1) * sizeof(*notify.hist));
    notify.nhist--;
    if (notify.redraw)
        notify.redraw();
}

void notify_dismiss(void)
{
    notify_clear(ClosedDismissed);
}

void notify_dismissall(void)
{
    Notification n;

    if (!notify.active)
        return;
    /* not through notify_clear(), which would bring up each queued one */
    notify.active = 0;
    notify_arm(0);
    notify_closed(notify.cur.id, ClosedDismissed);
    histpush(&notify.cur);
    while (notify.nqueue) {
        unqueue(0, &n);
        notify_closed(n.id, ClosedDismissed);
        histpush(&n);
    }
    if (notify.redraw)
        notify.redraw();
}

void notify_invoke(void)
{
    DBusMessage* sig;
    const char* key = "default";

    if (!notify.active)
        return;
    if (notify.cur.hasdefault &&
        (sig = dbus_message_new_signal(
             NOTIFY_OPATH, NOTIFY_IFACE, "ActionInvoked"))) {
        if (dbus_message_append_args(sig,
                                     DBUS_TYPE_UINT32,
                                     &notify.cur.id,
                                     DBUS_TYPE_STRING,
                                     &key,
                                     DBUS_TYPE_INVALID))
            dbus_connection_send(notify.conn, sig, NULL);
        dbus_message_unref(sig);
    }
    notify_clear(ClosedDismissed);
}
