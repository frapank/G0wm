/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "notify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "util.h"

#define NOTIFY_NAME "org.freedesktop.Notifications"
#define NOTIFY_OPATH "/org/freedesktop/Notifications"
#define NOTIFY_IFACE "org.freedesktop.Notifications"
/* An unbounded notify_expire_timeout would let one client hold the bar (and
 * hide the window title) for the rest of the session. */
constexpr int NOTIFY_TIMEOUT_MAX = 60'000;
/* image-data bigger than this on a side is refused rather than decoded */
constexpr int NOTIFY_IMAGEMAX = 4096;

/* org.freedesktop.Notifications.NotificationClosed reasons */
enum { ClosedExpired = 1, ClosedDismissed, ClosedByCall, ClosedUndefined };

static struct {
    DBusConnection* conn;
    struct wl_event_loop* loop;
    struct wl_event_source* timer;
    void (*redraw)(void);
    const char* (*token)(void);
    unsigned int timeout_ms;
    dbus_uint32_t seq; /* handed out to clients that don't pick one */
    int active;
    int running;
    int held;
    unsigned int heldleft; /* time cur had left when held */
    Notification cur;      /* valid when active */
    Notification queue[NOTIFY_QUEUEMAX];
    size_t nqueue;
    Notification hist[NOTIFY_HISTMAX]; /* newest first */
    size_t nhist;
} notify_state;

uint64_t notify_now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1'000'000;
}

/* Copies src into dst keeping only whole, well-formed UTF-8 codepoints:
 * control characters become spaces, invalid bytes are dropped and a sequence
 * that doesn't fit is left out entirely. Anything on this path comes straight
 * from an arbitrary bus client, and half a codepoint renders as garbage. */
static void notify_sanitize(char* dst, size_t dstsz, const char* src)
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
        dbus_connection_send(notify_state.conn, sig, nullptr);
    dbus_message_unref(sig);
}

static void notify_release(Notification* n)
{
    if (n->icon)
        icon_destroy(n->icon);
    n->icon = nullptr;
}

/* same id, or same app and stack tag */
static int notify_same_slot(const Notification* a, const Notification* b)
{
    return a->id == b->id ||
           (*a->tag && !strcmp(a->tag, b->tag) && !strcmp(a->app, b->app));
}

/* Pushes n to the head of the history, taking over its icon. An entry for
 * the same id or stack tag is replaced, so ten volume key presses leave one
 * entry. */
static void notify_history_push(Notification* n)
{
    size_t i;

    if (n->transient) {
        notify_release(n);
        return;
    }
    for (i = 0;
         i < notify_state.nhist && !notify_same_slot(&notify_state.hist[i], n);
         i++)
        ;
    if (i == notify_state.nhist && notify_state.nhist == NOTIFY_HISTMAX)
        i = notify_state.nhist - 1; /* full: drop the oldest */
    if (i < notify_state.nhist) {
        notify_release(&notify_state.hist[i]);
        memmove(&notify_state.hist[i],
                &notify_state.hist[i + 1],
                (notify_state.nhist - i - 1) * sizeof(*notify_state.hist));
        notify_state.nhist--;
    }
    memmove(&notify_state.hist[1],
            &notify_state.hist[0],
            notify_state.nhist * sizeof(*n));
    notify_state.hist[0] = *n;
    notify_state.nhist++;
    n->icon = nullptr;
}

static void notify_unqueue(size_t i, Notification* dst)
{
    *dst = notify_state.queue[i];
    memmove(&notify_state.queue[i],
            &notify_state.queue[i + 1],
            (notify_state.nqueue - i - 1) * sizeof(*notify_state.queue));
    notify_state.nqueue--;
}

/* A full queue sends its oldest expiring entry straight to the history. */
static void notify_enqueue(Notification* n, int front)
{
    Notification old;
    size_t i;

    if (notify_state.nqueue == NOTIFY_QUEUEMAX) {
        for (i = 0;
             i < notify_state.nqueue && !notify_state.queue[i].timeout_ms;
             i++)
            ;
        notify_unqueue(i < notify_state.nqueue ? i : 0, &old);
        notify_closed(old.id, ClosedUndefined);
        notify_history_push(&old);
    }
    if (front) {
        memmove(&notify_state.queue[1],
                &notify_state.queue[0],
                notify_state.nqueue * sizeof(*notify_state.queue));
        notify_state.queue[0] = *n;
    } else {
        notify_state.queue[notify_state.nqueue] = *n;
    }
    notify_state.nqueue++;
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
    if (!notify_state.timer && ms)
        notify_state.timer =
            wl_event_loop_add_timer(notify_state.loop, notify_expire, nullptr);
    if (!notify_state.timer)
        return !ms;
    return wl_event_source_timer_update(notify_state.timer, (int)ms) == 0;
}

static int notify_show(void)
{
    notify_state.cur.shown_ms = notify_now();
    notify_state.cur.seen = 1;
    if (notify_state.held) {
        notify_state.heldleft = notify_state.cur.timeout_ms;
        notify_state.active = 1;
        return 1;
    }
    /* Without a timer the notification would sit in the bar forever, so the
     * caller is expected to drop it instead of showing it unexpirable. */
    notify_state.active = notify_arm(notify_state.cur.timeout_ms);
    return notify_state.active;
}

/* An unseen critical goes first (it was held back by a pick); seen ones go
 * last, or everything else would wait behind them forever. */
static void notify_show_next(void)
{
    size_t i;

    while (!notify_state.active && notify_state.nqueue) {
        for (i = 0; i < notify_state.nqueue; i++)
            if (notify_state.queue[i].urgency == UrgencyCritical &&
                !notify_state.queue[i].seen)
                break;
        if (i == notify_state.nqueue)
            for (i = 0;
                 i < notify_state.nqueue && !notify_state.queue[i].timeout_ms;
                 i++)
                ;
        notify_unqueue(i < notify_state.nqueue ? i : 0, &notify_state.cur);
        if (!notify_show()) {
            notify_closed(notify_state.cur.id, ClosedUndefined);
            notify_history_push(&notify_state.cur);
        }
    }
}

/* Takes the current notification down and brings up the next one. */
static void notify_clear(dbus_uint32_t reason)
{
    if (!notify_state.active)
        return;
    notify_state.active = 0;
    notify_state.held = 0;
    notify_arm(0);
    notify_closed(notify_state.cur.id, reason);
    /* closed by the client: not worth keeping */
    if (reason == ClosedByCall)
        notify_release(&notify_state.cur);
    else
        notify_history_push(&notify_state.cur);
    notify_show_next();
    if (notify_state.redraw)
        notify_state.redraw();
}

/* Points iter at the n-th Notify argument: actions, hints and
 * notify_expire_timeout sit past containers dbus_message_get_args() won't hand
 * back as is. */
static int notify_arg_at(DBusMessage* msg,
                         DBusMessageIter* iter,
                         int n,
                         int type)
{
    if (!dbus_message_iter_init(msg, iter))
        return 0;
    while (n--)
        if (!dbus_message_iter_next(iter))
            return 0;
    return dbus_message_iter_get_arg_type(iter) == type;
}

static int notify_expire_timeout(DBusMessage* msg)
{
    DBusMessageIter iter;
    dbus_int32_t ms;

    if (!notify_arg_at(msg, &iter, 7, DBUS_TYPE_INT32))
        return -1;
    dbus_message_iter_get_basic(&iter, &ms);
    return ms;
}

/* actions is a flat (key, label, ...) list. Keys too long to store are
 * skipped, not cut: they must go back to the client unchanged. */
static void notify_actions(DBusMessage* msg, Notification* n)
{
    DBusMessageIter iter, arr;
    const char *key, *label;

    if (!notify_arg_at(msg, &iter, 5, DBUS_TYPE_ARRAY))
        return;
    dbus_message_iter_recurse(&iter, &arr);
    while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING) {
        dbus_message_iter_get_basic(&arr, &key);
        dbus_message_iter_next(&arr);
        if (dbus_message_iter_get_arg_type(&arr) != DBUS_TYPE_STRING)
            break;
        dbus_message_iter_get_basic(&arr, &label);
        dbus_message_iter_next(&arr);
        if (!strcmp(key, "default")) {
            n->hasdefault = 1;
        } else if (n->nact < NOTIFY_ACTMAX &&
                   strlen(key) < sizeof(n->act[0].key)) {
            memcpy(n->act[n->nact].key, key, strlen(key) + 1);
            notify_sanitize(n->act[n->nact].label,
                            sizeof(n->act[0].label),
                            *label ? label : key);
            n->nact++;
        }
    }
}

static int notify_hint(DBusMessage* msg,
                       const char* name,
                       int type,
                       DBusMessageIter* var)
{
    DBusMessageIter iter, arr, entry;
    const char* key;

    if (!notify_arg_at(msg, &iter, 6, DBUS_TYPE_ARRAY))
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
static void notify_desktop_entry(DBusMessage* msg, char* dst, size_t dstsz)
{
    DBusMessageIter var;
    const char* val;

    *dst = '\0';
    if (!notify_hint(msg, "desktop-entry", DBUS_TYPE_STRING, &var))
        return;
    dbus_message_iter_get_basic(&var, &val);
    notify_sanitize(dst, dstsz, val);
}

static int notify_bool_hint(DBusMessage* msg, const char* name)
{
    DBusMessageIter var;
    dbus_bool_t b;

    if (!notify_hint(msg, name, DBUS_TYPE_BOOLEAN, &var))
        return 0;
    dbus_message_iter_get_basic(&var, &b);
    return b;
}

/* dunst's hint, then the older Ubuntu one */
static void notify_stack_tag(DBusMessage* msg, char* dst, size_t dstsz)
{
    DBusMessageIter var;
    const char* val;

    *dst = '\0';
    if (!notify_hint(msg, "x-dunst-stack-tag", DBUS_TYPE_STRING, &var) &&
        !notify_hint(
            msg, "x-canonical-private-synchronous", DBUS_TYPE_STRING, &var))
        return;
    dbus_message_iter_get_basic(&var, &val);
    notify_sanitize(dst, dstsz, val);
}

/* clients send it signed or unsigned */
static int notify_value(DBusMessage* msg)
{
    DBusMessageIter var;
    dbus_int32_t i;
    dbus_uint32_t u;

    if (notify_hint(msg, "value", DBUS_TYPE_INT32, &var)) {
        dbus_message_iter_get_basic(&var, &i);
    } else if (notify_hint(msg, "value", DBUS_TYPE_UINT32, &var)) {
        dbus_message_iter_get_basic(&var, &u);
        i = u > 100 ? 100 : (dbus_int32_t)u;
    } else {
        return -1;
    }
    return i < 0 ? 0 : i > 100 ? 100 : i;
}

/* Flattens body markup to plain text: entities are decoded, the tags the
 * spec allows are dropped (<br> becomes a space), any other < is kept. */
static void notify_demarkup(char* dst, size_t dstsz, const char* src)
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
                    (n = util_put_codepoint(dst + i, dstsz - i, cp))) {
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

static int notify_urgency(DBusMessage* msg)
{
    DBusMessageIter var;
    unsigned char u;

    if (!notify_hint(msg, "urgency", DBUS_TYPE_BYTE, &var))
        return UrgencyNormal;
    dbus_message_iter_get_basic(&var, &u);
    return u <= UrgencyCritical ? u : UrgencyNormal;
}

/* (iiibiiay): width, height, rowstride, has_alpha, bits_per_sample,
 * channels, data. Validated before any byte is read. */
static Icon* notify_image_data(DBusMessage* msg, const char* name)
{
    DBusMessageIter var, st, arr;
    dbus_int32_t v[3], bps, ch;
    dbus_bool_t alpha;
    const uint8_t* data;
    int i, len;

    if (!notify_hint(msg, name, DBUS_TYPE_STRUCT, &var))
        return nullptr;
    dbus_message_iter_recurse(&var, &st);
    for (i = 0; i < 3; i++, dbus_message_iter_next(&st)) {
        if (dbus_message_iter_get_arg_type(&st) != DBUS_TYPE_INT32)
            return nullptr;
        dbus_message_iter_get_basic(&st, &v[i]);
    }
    if (dbus_message_iter_get_arg_type(&st) != DBUS_TYPE_BOOLEAN)
        return nullptr;
    dbus_message_iter_get_basic(&st, &alpha);
    dbus_message_iter_next(&st);
    if (dbus_message_iter_get_arg_type(&st) != DBUS_TYPE_INT32)
        return nullptr;
    dbus_message_iter_get_basic(&st, &bps);
    dbus_message_iter_next(&st);
    if (dbus_message_iter_get_arg_type(&st) != DBUS_TYPE_INT32)
        return nullptr;
    dbus_message_iter_get_basic(&st, &ch);
    dbus_message_iter_next(&st);
    if (dbus_message_iter_get_arg_type(&st) != DBUS_TYPE_ARRAY ||
        dbus_message_iter_get_element_type(&st) != DBUS_TYPE_BYTE)
        return nullptr;
    dbus_message_iter_recurse(&st, &arr);
    dbus_message_iter_get_fixed_array(&arr, &data, &len);

    if (v[0] <= 0 || v[1] <= 0 || v[0] > NOTIFY_IMAGEMAX ||
        v[1] > NOTIFY_IMAGEMAX || bps != 8 || ch != (alpha ? 4 : 3) ||
        v[2] < v[0] * ch ||
        (int64_t)len < (int64_t)v[2] * (v[1] - 1) + (int64_t)v[0] * ch)
        return nullptr;
    return icon_create_from_data(
        data, v[0], v[1], v[2], alpha, NOTIFY_ICONSIZE);
}

/* a path, a file:// URI or an icon name */
static Icon* notify_image_named(const char* s)
{
    if (!s || !*s)
        return nullptr;
    if (!strncmp(s, "file://", 7))
        s += 7;
    return icon_create_from_name(s, nullptr, NOTIFY_ICONSIZE);
}

/* in the spec's order of precedence */
static Icon* notify_icon(DBusMessage* msg, const char* app_icon)
{
    DBusMessageIter var;
    const char* path;
    Icon* icon;

    if ((icon = notify_image_data(msg, "image-data")) ||
        (icon = notify_image_data(msg, "image_data")))
        return icon;
    if (notify_hint(msg, "image-path", DBUS_TYPE_STRING, &var) ||
        notify_hint(msg, "image_path", DBUS_TYPE_STRING, &var)) {
        dbus_message_iter_get_basic(&var, &path);
        if ((icon = notify_image_named(path)))
            return icon;
    }
    if ((icon = notify_image_named(app_icon)))
        return icon;
    return notify_image_data(msg, "icon_data");
}

static DBusHandlerResult notify_reply_empty(DBusConnection* conn,
                                            DBusMessage* msg)
{
    DBusMessage* reply = dbus_message_new_method_return(msg);
    DBusHandlerResult res = DBUS_HANDLER_RESULT_HANDLED;

    if (!reply || !dbus_connection_send(conn, reply, nullptr))
        res = DBUS_HANDLER_RESULT_NEED_MEMORY;
    if (reply)
        dbus_message_unref(reply);
    return res;
}

static DBusHandlerResult notify_handle_notify(DBusConnection* conn,
                                              DBusMessage* msg)
{
    DBusError err = DBUS_ERROR_INIT;
    DBusMessage* reply;
    const char *app_name = "", *app_icon = "", *summary = "", *body = "";
    dbus_uint32_t replaces_id = 0, id;
    Notification n = {};
    size_t i;
    int ms;
    /* bounded well under NOTIFY_TEXTMAX: snprintf() below can't truncate */
    char capp[64], csummary[200], cbody[200], plain[1024];

    /* actions/hints intentionally left unread; notify_expire_timeout comes
     * after them and is picked up separately below */
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

    ms = notify_expire_timeout(msg);
    /* -1 asks for the server default and 0 for "never expire"; the bar has a
     * single slot shared with the window title, so neither gets to sit there
     * forever. */
    if (ms <= 0)
        ms = (int)notify_state.timeout_ms;
    else if (ms > NOTIFY_TIMEOUT_MAX)
        ms = NOTIFY_TIMEOUT_MAX;
    n.urgency = notify_urgency(msg);
    n.timeout_ms = n.urgency == UrgencyCritical ? 0 : (unsigned int)ms;
    n.transient = notify_bool_hint(msg, "transient");
    n.resident = notify_bool_hint(msg, "resident");
    notify_actions(msg, &n);

    notify_sanitize(capp, sizeof(capp), *app_name ? app_name : "?");
    notify_sanitize(csummary, sizeof(csummary), summary);
    notify_demarkup(plain, sizeof(plain), body);
    notify_sanitize(cbody, sizeof(cbody), plain);
    if (*csummary && *cbody)
        snprintf(n.text, sizeof(n.text), "%s: %s - %s", capp, csummary, cbody);
    else
        snprintf(n.text,
                 sizeof(n.text),
                 "%s: %s",
                 capp,
                 *csummary ? csummary : cbody);
    snprintf(n.app, sizeof(n.app), "%s", capp);
    notify_desktop_entry(msg, n.desktop, sizeof(n.desktop));
    notify_stack_tag(msg, n.tag, sizeof(n.tag));
    n.value = notify_value(msg);
    n.icon = notify_icon(msg, app_icon);

    /* Updates, by id or by stack tag, keep their place on screen or in the
     * queue. */
    id = replaces_id;
    if (!id && *n.tag) {
        if (notify_state.active && notify_same_slot(&notify_state.cur, &n))
            id = notify_state.cur.id;
        for (i = 0; !id && i < notify_state.nqueue; i++)
            if (notify_same_slot(&notify_state.queue[i], &n))
                id = notify_state.queue[i].id;
    }
    if (!id)
        id = ++notify_state.seq;
    if (!id) /* the counter wrapped; 0 means "no notification" */
        id = ++notify_state.seq;
    n.id = id;

    for (i = 0; i < notify_state.nqueue && notify_state.queue[i].id != id; i++)
        ;
    if (notify_state.active && notify_state.cur.id == id) {
        notify_release(&notify_state.cur);
        notify_state.cur = n;
        if (!notify_show()) {
            notify_closed(id, ClosedUndefined);
            notify_history_push(&notify_state.cur);
            notify_show_next();
        }
    } else if (i < notify_state.nqueue) {
        notify_release(&notify_state.queue[i]);
        notify_state.queue[i] = n;
    } else if (notify_state.held) {
        /* the user is picking an action: nothing takes the box */
        notify_enqueue(&n, n.urgency == UrgencyCritical);
    } else if (!notify_state.active || !notify_state.cur.timeout_ms ||
               n.urgency == UrgencyCritical) {
        /* shown now if the box is free, sticky, or this one is critical;
         * what it covers goes back to the front of the queue */
        if (notify_state.active) {
            notify_arm(0);
            notify_state.active = 0;
            notify_enqueue(&notify_state.cur, 1);
        }
        notify_state.cur = n;
        if (!notify_show()) {
            notify_closed(id, ClosedUndefined);
            notify_history_push(&notify_state.cur);
            notify_show_next();
        }
    } else {
        notify_enqueue(&n, 0);
    }
    if (notify_state.redraw)
        notify_state.redraw();

    reply = dbus_message_new_method_return(msg);
    if (reply)
        dbus_message_append_args(
            reply, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID);

send:
    if (!reply || !dbus_connection_send(conn, reply, nullptr)) {
        if (reply)
            dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult notify_handle_close(DBusConnection* conn,
                                             DBusMessage* msg)
{
    Notification n;
    dbus_uint32_t id;
    size_t i;

    if (!dbus_message_get_args(
            msg, nullptr, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID))
        return notify_reply_empty(conn, msg);
    for (i = 0; i < notify_state.nqueue && notify_state.queue[i].id != id; i++)
        ;
    if (notify_state.active && id == notify_state.cur.id) {
        notify_clear(ClosedByCall);
    } else if (id && i < notify_state.nqueue) {
        notify_unqueue(i, &n);
        notify_release(&n);
        notify_closed(id, ClosedByCall);
        if (notify_state.redraw)
            notify_state.redraw();
    }

    return notify_reply_empty(conn, msg);
}

static DBusHandlerResult notify_handle_capabilities(DBusConnection* conn,
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
        !dbus_connection_send(conn, reply, nullptr))
        res = DBUS_HANDLER_RESULT_NEED_MEMORY;

    dbus_message_unref(reply);
    return res;
}

static DBusHandlerResult notify_handle_server_info(DBusConnection* conn,
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
        !dbus_connection_send(conn, reply, nullptr))
        res = DBUS_HANDLER_RESULT_NEED_MEMORY;

    dbus_message_unref(reply);
    return res;
}

static DBusHandlerResult notify_handle_message(DBusConnection* conn,
                                               DBusMessage* msg,
                                               void* data)
{
    (void)data;

    if (dbus_message_is_method_call(msg, NOTIFY_IFACE, "Notify"))
        return notify_handle_notify(conn, msg);
    else if (dbus_message_is_method_call(
                 msg, NOTIFY_IFACE, "CloseNotification"))
        return notify_handle_close(conn, msg);
    else if (dbus_message_is_method_call(msg, NOTIFY_IFACE, "GetCapabilities"))
        return notify_handle_capabilities(conn, msg);
    else if (dbus_message_is_method_call(
                 msg, NOTIFY_IFACE, "GetServerInformation"))
        return notify_handle_server_info(conn, msg);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static const DBusObjectPathVTable notify_vtable = { .message_function =
                                                        notify_handle_message };

void notify_start(DBusConnection* conn,
                  struct wl_event_loop* loop,
                  unsigned int timeout_secs,
                  void (*redraw)(void),
                  const char* (*token)(void))
{
    int r;

    if (!conn || !loop)
        return;

    notify_state = (typeof(notify_state)){};
    notify_state.conn = conn;
    notify_state.loop = loop;
    notify_set_timeout(timeout_secs);
    notify_state.redraw = redraw;
    notify_state.token = token;

    /* if another daemon (mako, dunst, swaync, ...) already owns the name,
     * don't fight it for it: just stay disabled */
    r = dbus_bus_request_name(
        conn, NOTIFY_NAME, DBUS_NAME_FLAG_DO_NOT_QUEUE, nullptr);
    if (r != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr,
                "Couldn't own %s (another notification daemon is likely "
                "running), bar notifications not available\n",
                NOTIFY_NAME);
        return;
    }

    if (!dbus_connection_register_object_path(
            conn, NOTIFY_OPATH, &notify_vtable, nullptr)) {
        dbus_bus_release_name(conn, NOTIFY_NAME, nullptr);
        fprintf(stderr, "Couldn't register %s\n", NOTIFY_OPATH);
        return;
    }

    notify_state.running = 1;
}

void notify_stop(void)
{
    Notification n;

    if (!notify_state.running)
        return;

    /* nothing is worth redrawing on the way out, but clients still deserve
     * to hear that their notification went away */
    notify_state.redraw = nullptr;
    while (notify_state.nqueue) {
        notify_unqueue(0, &n);
        notify_closed(n.id, ClosedUndefined);
        notify_release(&n);
    }
    notify_clear(ClosedUndefined);
    while (notify_state.nhist)
        notify_release(&notify_state.hist[--notify_state.nhist]);
    if (notify_state.timer) {
        wl_event_source_remove(notify_state.timer);
        notify_state.timer = nullptr;
    }
    dbus_connection_unregister_object_path(notify_state.conn, NOTIFY_OPATH);
    dbus_bus_release_name(notify_state.conn, NOTIFY_NAME, nullptr);
    notify_state.running = 0;
}

void notify_set_timeout(unsigned int timeout_secs)
{
    notify_state.timeout_ms = timeout_secs > NOTIFY_TIMEOUT_MAX / 1000
                                  ? NOTIFY_TIMEOUT_MAX
                                  : (timeout_secs ? timeout_secs : 1) * 1000;
}

const Notification* notify_current(void)
{
    return notify_state.active ? &notify_state.cur : nullptr;
}

size_t notify_queued(void)
{
    return notify_state.nqueue;
}

const Notification* notify_history(size_t i)
{
    return i < notify_state.nhist ? &notify_state.hist[i] : nullptr;
}

size_t notify_history_length(void)
{
    return notify_state.nhist;
}

void notify_history_remove(size_t i)
{
    if (i >= notify_state.nhist)
        return;
    notify_release(&notify_state.hist[i]);
    memmove(&notify_state.hist[i],
            &notify_state.hist[i + 1],
            (notify_state.nhist - i - 1) * sizeof(*notify_state.hist));
    notify_state.nhist--;
    if (notify_state.redraw)
        notify_state.redraw();
}

void notify_dismiss(void)
{
    notify_clear(ClosedDismissed);
}

void notify_dismiss_all(void)
{
    Notification n;

    if (!notify_state.active)
        return;
    /* not through notify_clear(), which would bring up each queued one */
    notify_state.active = 0;
    notify_arm(0);
    notify_closed(notify_state.cur.id, ClosedDismissed);
    notify_history_push(&notify_state.cur);
    while (notify_state.nqueue) {
        notify_unqueue(0, &n);
        notify_closed(n.id, ClosedDismissed);
        notify_history_push(&n);
    }
    if (notify_state.redraw)
        notify_state.redraw();
}

/* The activation token goes first, as the spec asks, so the client can
 * raise the right window itself. */
static void notify_invoke_action(const char* key)
{
    DBusMessage* sig;
    const char* token;

    if (notify_state.token && (token = notify_state.token()) &&
        (sig = dbus_message_new_signal(
             NOTIFY_OPATH, NOTIFY_IFACE, "ActivationToken"))) {
        if (dbus_message_append_args(sig,
                                     DBUS_TYPE_UINT32,
                                     &notify_state.cur.id,
                                     DBUS_TYPE_STRING,
                                     &token,
                                     DBUS_TYPE_INVALID))
            dbus_connection_send(notify_state.conn, sig, nullptr);
        dbus_message_unref(sig);
    }
    if ((sig = dbus_message_new_signal(
             NOTIFY_OPATH, NOTIFY_IFACE, "ActionInvoked"))) {
        if (dbus_message_append_args(sig,
                                     DBUS_TYPE_UINT32,
                                     &notify_state.cur.id,
                                     DBUS_TYPE_STRING,
                                     &key,
                                     DBUS_TYPE_INVALID))
            dbus_connection_send(notify_state.conn, sig, nullptr);
        dbus_message_unref(sig);
    }
    if (notify_state.cur.resident) {
        notify_hold(0);
        if (notify_state.redraw)
            notify_state.redraw();
    } else {
        notify_clear(ClosedDismissed);
    }
}

void notify_invoke(void)
{
    if (!notify_state.active)
        return;
    if (notify_state.cur.hasdefault)
        notify_invoke_action("default");
    else
        notify_clear(ClosedDismissed);
}

void notify_action(size_t i)
{
    if (notify_state.active && i < notify_state.cur.nact)
        notify_invoke_action(notify_state.cur.act[i].key);
}

void notify_hold(int on)
{
    Notification n;
    uint64_t now = notify_now(), end;
    size_t i;

    if (!notify_state.active || !on == !notify_state.held)
        return;
    if (on) {
        end = notify_state.cur.shown_ms + notify_state.cur.timeout_ms;
        notify_state.heldleft = !notify_state.cur.timeout_ms ? 0
                                : end > now ? (unsigned int)(end - now)
                                            : 1;
        notify_arm(0);
        notify_state.held = 1;
        return;
    }

    notify_state.held = 0;
    /* a critical that arrived meanwhile takes the box now */
    for (i = 0; i < notify_state.nqueue; i++)
        if (notify_state.queue[i].urgency == UrgencyCritical &&
            !notify_state.queue[i].seen)
            break;
    if (i < notify_state.nqueue &&
        notify_state.cur.urgency != UrgencyCritical) {
        notify_unqueue(i, &n);
        notify_state.active = 0;
        notify_enqueue(&notify_state.cur, 1);
        notify_state.cur = n;
        if (!notify_show()) {
            notify_closed(notify_state.cur.id, ClosedUndefined);
            notify_history_push(&notify_state.cur);
            notify_show_next();
        }
        return;
    }
    if (notify_state.cur.timeout_ms) {
        notify_state.cur.shown_ms =
            now - (notify_state.cur.timeout_ms - notify_state.heldleft);
        /* no redraw from here: the bar may be drawing */
        notify_arm(notify_state.heldleft);
    }
}

int notify_held(unsigned int* left_ms)
{
    if (notify_state.held && left_ms)
        *left_ms = notify_state.heldleft;
    return notify_state.held;
}
