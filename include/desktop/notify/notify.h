/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_DESKTOP_NOTIFY_NOTIFY_H
#define G0WM_DESKTOP_NOTIFY_NOTIFY_H

#include <dbus/dbus.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-server-core.h>

#include "desktop/systray/icon.h"

/* org.freedesktop.Notifications server: one notification on screen, the rest
 * queued behind it, the closed ones kept in a short history. */

constexpr int NOTIFY_TEXTMAX = 512; /* the line of text, terminator included */

constexpr int NOTIFY_QUEUEMAX = 32;

constexpr int NOTIFY_HISTMAX = 32;

/* icons are scaled down on arrival so a full history stays small */
constexpr int NOTIFY_ICONSIZE = 64;

constexpr int NOTIFY_ACTMAX = 8; /* actions besides "default" */

/* the urgency hint, as the spec numbers it */
enum { UrgencyLow, UrgencyNormal, UrgencyCritical };

typedef struct {
    dbus_uint32_t id;
    int urgency;
    int hasdefault;
    int resident; /* stays up after an action */
    int seen;     /* has been on screen at least once */
    int transient;
    unsigned int timeout_ms; /* 0: stays until dismissed */
    uint64_t shown_ms;       /* CLOCK_MONOTONIC */
    char text[NOTIFY_TEXTMAX];
    char app[64];
    char desktop[64];
    char tag[64]; /* stack tag */
    int value;    /* progress 0-100, -1 without one */
    Icon* icon;
    struct {
        char key[64];
        char label[64];
    } act[NOTIFY_ACTMAX];
    size_t nact;
} Notification;

/* token hands out an xdg-activation token, sent to the client before an
 * action so it can raise its own window; may be NULL */
void notify_start(DBusConnection* conn,
                  struct wl_event_loop* loop,
                  unsigned int timeout_secs,
                  void (*redraw)(void),
                  const char* (*token)(void));
void notify_set_timeout(unsigned int timeout_secs);
void notify_stop(void);
/* CLOCK_MONOTONIC, in ms */
uint64_t notify_now(void);

#endif
