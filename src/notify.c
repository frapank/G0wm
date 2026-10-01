#include "notify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define NOTIFY_NAME "org.freedesktop.Notifications"
#define NOTIFY_OPATH "/org/freedesktop/Notifications"
#define NOTIFY_IFACE "org.freedesktop.Notifications"
/* An unbounded expire_timeout would let one client hold the bar (and hide the
 * window title) for the rest of the session. */
#define NOTIFY_TIMEOUT_MAX 60000
/* image-data bigger than this on a side is refused rather than decoded */
#define NOTIFY_IMAGEMAX 4096

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

static void release(Notification* n)
{
    if (n->icon)
        destroyicon(n->icon);
    n->icon = NULL;
}

/* same id, or same app and stack tag */
static int sameslot(const Notification* a, const Notification* b)
{
    return a->id == b->id ||
           (*a->tag && !strcmp(a->tag, b->tag) && !strcmp(a->app, b->app));
}

/* Pushes n to the head of the history, taking over its icon. An entry for
 * the same id or stack tag is replaced, so ten volume key presses leave one
 * entry. */
static void histpush(Notification* n)
{
    size_t i;

    if (n->transient) {
        release(n);
        return;
    }
    for (i = 0; i < notify.nhist && !sameslot(&notify.hist[i], n); i++)
        ;
    if (i == notify.nhist && notify.nhist == NOTIFY_HISTMAX)
        i = notify.nhist - 1; /* full: drop the oldest */
    if (i < notify.nhist) {
        release(&notify.hist[i]);
        memmove(&notify.hist[i],
                &notify.hist[i + 1],
                (notify.nhist - i - 1) * sizeof(*notify.hist));
        notify.nhist--;
    }
    memmove(&notify.hist[1], &notify.hist[0], notify.nhist * sizeof(*n));
    notify.hist[0] = *n;
    notify.nhist++;
    n->icon = NULL;
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
    if (reason == ClosedByCall)
        release(&notify.cur);
    else
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

/* dunst's hint, then the older Ubuntu one */
static void stack_tag(DBusMessage* msg, char* dst, size_t dstsz)
{
    DBusMessageIter var;
    const char* val;

    *dst = '\0';
    if (!hint(msg, "x-dunst-stack-tag", DBUS_TYPE_STRING, &var) &&
        !hint(msg, "x-canonical-private-synchronous", DBUS_TYPE_STRING, &var))
        return;
    dbus_message_iter_get_basic(&var, &val);
    sanitize(dst, dstsz, val);
}

/* clients send it signed or unsigned */
static int value(DBusMessage* msg)
{
    DBusMessageIter var;
    dbus_int32_t i;
    dbus_uint32_t u;

    if (hint(msg, "value", DBUS_TYPE_INT32, &var)) {
        dbus_message_iter_get_basic(&var, &i);
    } else if (hint(msg, "value", DBUS_TYPE_UINT32, &var)) {
        dbus_message_iter_get_basic(&var, &u);
        i = u > 100 ? 100 : (dbus_int32_t)u;
    } else {
        return -1;
    }
    return i < 0 ? 0 : i > 100 ? 100 : i;
}

/* UTF-8 encodes cp into dst, 0 if it doesn't fit or isn't valid */
static size_t putcp(char* dst, size_t room, unsigned long cp)
{
    if (!cp || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
        return 0;
    if (cp < 0x80 && room > 1) {
        dst[0] = (char)cp;
        return 1;
    } else if (cp < 0x800 && room > 2) {
        dst[0] = (char)(0xC0 | cp >> 6);
        dst[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000 && room > 3) {
        dst[0] = (char)(0xE0 | cp >> 12);
        dst[1] = (char)(0x80 | (cp >> 6 & 0x3F));
        dst[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp >= 0x10000 && room > 4) {
        dst[0] = (char)(0xF0 | cp >> 18);
        dst[1] = (char)(0x80 | (cp >> 12 & 0x3F));
        dst[2] = (char)(0x80 | (cp >> 6 & 0x3F));
        dst[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

/* Flattens body markup to plain text: entities are decoded, the tags the
 * spec allows are dropped (<br> becomes a space), any other < is kept. */
static void demarkup(char* dst, size_t dstsz, const char* src)
{
    static const struct {
        const char* name;
        char c;
    } ents[] = { { "amp;", '&' },
                 { "lt;", '<' },
                 { "gt;", '>' },
                 { "quot;", '"' },
                 { "apos;", '\'' } };
    static const char* const tags[] = { "b", "i", "u", "a", "img", "br" };
    const char *p, *end;
    unsigned long cp;
    size_t i = 0, k, n;
    char* stop;

    if (!dstsz)
        return;
    for (p = src; *p && i + 1 < dstsz;) {
        if (*p == '&') {
            for (k = 0; k < sizeof(ents) / sizeof(*ents); k++)
                if (!strncmp(p + 1, ents[k].name, strlen(ents[k].name)))
                    break;
            if (k < sizeof(ents) / sizeof(*ents)) {
                dst[i++] = ents[k].c;
                p += 1 + strlen(ents[k].name);
                continue;
            }
            if (p[1] == '#') {
                cp = p[2] == 'x' || p[2] == 'X' ? strtoul(p + 3, &stop, 16)
                                                : strtoul(p + 2, &stop, 10);
                if (*stop == ';' && stop > p + 2 &&
                    (n = putcp(dst + i, dstsz - i, cp))) {
                    i += n;
                    p = stop + 1;
                    continue;
                }
            }
        } else if (*p == '<' && (end = strchr(p, '>'))) {
            const char* name = p + 1 + (p[1] == '/');
            for (n = 0; name[n] && !strchr(" \t\n/>", name[n]); n++)
                ;
            for (k = 0; k < sizeof(tags) / sizeof(*tags); k++)
                if (strlen(tags[k]) == n && !strncasecmp(name, tags[k], n))
                    break;
            if (k < sizeof(tags) / sizeof(*tags)) {
                if (!strcmp(tags[k], "br"))
                    dst[i++] = ' ';
                p = end + 1;
                continue;
            }
        }
        dst[i++] = *p++;
    }
    dst[i] = '\0';
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

/* (iiibiiay): width, height, rowstride, has_alpha, bits_per_sample,
 * channels, data. Validated before any byte is read. */
static Icon* image_data(DBusMessage* msg, const char* name)
{
    DBusMessageIter var, st, arr;
    dbus_int32_t v[3], bps, ch;
    dbus_bool_t alpha;
    const uint8_t* data;
    int i, len;

    if (!hint(msg, name, DBUS_TYPE_STRUCT, &var))
        return NULL;
    dbus_message_iter_recurse(&var, &st);
    for (i = 0; i < 3; i++, dbus_message_iter_next(&st)) {
        if (dbus_message_iter_get_arg_type(&st) != DBUS_TYPE_INT32)
            return NULL;
        dbus_message_iter_get_basic(&st, &v[i]);
    }
    if (dbus_message_iter_get_arg_type(&st) != DBUS_TYPE_BOOLEAN)
        return NULL;
    dbus_message_iter_get_basic(&st, &alpha);
    dbus_message_iter_next(&st);
    if (dbus_message_iter_get_arg_type(&st) != DBUS_TYPE_INT32)
        return NULL;
    dbus_message_iter_get_basic(&st, &bps);
    dbus_message_iter_next(&st);
    if (dbus_message_iter_get_arg_type(&st) != DBUS_TYPE_INT32)
        return NULL;
    dbus_message_iter_get_basic(&st, &ch);
    dbus_message_iter_next(&st);
    if (dbus_message_iter_get_arg_type(&st) != DBUS_TYPE_ARRAY ||
        dbus_message_iter_get_element_type(&st) != DBUS_TYPE_BYTE)
        return NULL;
    dbus_message_iter_recurse(&st, &arr);
    dbus_message_iter_get_fixed_array(&arr, &data, &len);

    if (v[0] <= 0 || v[1] <= 0 || v[0] > NOTIFY_IMAGEMAX ||
        v[1] > NOTIFY_IMAGEMAX || bps != 8 || ch != (alpha ? 4 : 3) ||
        v[2] < v[0] * ch ||
        (int64_t)len < (int64_t)v[2] * (v[1] - 1) + (int64_t)v[0] * ch)
        return NULL;
    return createiconfromdata(data, v[0], v[1], v[2], alpha, NOTIFY_ICONSIZE);
}

/* a path, a file:// URI or an icon name */
static Icon* image_named(const char* s)
{
    if (!s || !*s)
        return NULL;
    if (!strncmp(s, "file://", 7))
        s += 7;
    return createiconfromname(s, NULL, NOTIFY_ICONSIZE);
}

/* in the spec's order of precedence */
static Icon* notify_icon(DBusMessage* msg, const char* app_icon)
{
    DBusMessageIter var;
    const char* path;
    Icon* icon;

    if ((icon = image_data(msg, "image-data")) ||
        (icon = image_data(msg, "image_data")))
        return icon;
    if (hint(msg, "image-path", DBUS_TYPE_STRING, &var) ||
        hint(msg, "image_path", DBUS_TYPE_STRING, &var)) {
        dbus_message_iter_get_basic(&var, &path);
        if ((icon = image_named(path)))
            return icon;
    }
    if ((icon = image_named(app_icon)))
        return icon;
    return image_data(msg, "icon_data");
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
    char capp[64], csummary[200], cbody[200], plain[1024];

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
    demarkup(plain, sizeof(plain), body);
    sanitize(cbody, sizeof(cbody), plain);
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
    stack_tag(msg, n.tag, sizeof(n.tag));
    n.value = value(msg);
    n.icon = notify_icon(msg, app_icon);

    /* Updates, by id or by stack tag, keep their place on screen or in the
     * queue. */
    id = replaces_id;
    if (!id && *n.tag) {
        if (notify.active && sameslot(&notify.cur, &n))
            id = notify.cur.id;
        for (i = 0; !id && i < notify.nqueue; i++)
            if (sameslot(&notify.queue[i], &n))
                id = notify.queue[i].id;
    }
    if (!id)
        id = ++notify.seq;
    if (!id) /* the counter wrapped; 0 means "no notification" */
        id = ++notify.seq;
    n.id = id;

    for (i = 0; i < notify.nqueue && notify.queue[i].id != id; i++)
        ;
    if (notify.active && notify.cur.id == id) {
        release(&notify.cur);
        notify.cur = n;
        if (!show()) {
            notify_closed(id, ClosedUndefined);
            histpush(&notify.cur);
            shownext();
        }
    } else if (i < notify.nqueue) {
        release(&notify.queue[i]);
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
        release(&n);
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
    const char* caps[] = { "body", "body-markup", "actions", "icon-static" };
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
        release(&n);
    }
    notify_clear(ClosedUndefined);
    while (notify.nhist)
        release(&notify.hist[--notify.nhist]);
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
    release(&notify.hist[i]);
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
