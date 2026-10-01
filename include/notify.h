#ifndef G0WMNOTIFY_H
#define G0WMNOTIFY_H

#include <dbus/dbus.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-server-core.h>

#include "icon.h"

/* org.freedesktop.Notifications server: one notification on screen, the rest
 * queued behind it, the closed ones kept in a short history. */

#define NOTIFY_TEXTMAX 512 /* the line of text, terminator included */
#define NOTIFY_QUEUEMAX 32
#define NOTIFY_HISTMAX 32
/* icons are scaled down on arrival so a full history stays small */
#define NOTIFY_ICONSIZE 64

/* the urgency hint, as the spec numbers it */
enum { UrgencyLow, UrgencyNormal, UrgencyCritical };

typedef struct {
    dbus_uint32_t id;
    int urgency;
    int hasdefault;
    int transient;
    unsigned int timeout_ms; /* 0: stays until dismissed */
    uint64_t shown_ms;       /* CLOCK_MONOTONIC */
    char text[NOTIFY_TEXTMAX];
    char app[64];
    char desktop[64];
    char tag[64]; /* stack tag */
    int value;    /* progress 0-100, -1 without one */
    Icon* icon;
} Notification;

void notify_start(DBusConnection* conn,
                  struct wl_event_loop* loop,
                  unsigned int timeout_secs,
                  void (*redraw)(void));
void notify_settimeout(unsigned int timeout_secs);
void notify_stop(void);

/* NULL if nothing is on screen */
const Notification* notify_current(void);
size_t notify_queued(void);
/* newest first, NULL past the end */
const Notification* notify_history(size_t i);
size_t notify_histlen(void);
void notify_histremove(size_t i);

/* Drops the current one; the next queued takes its place. */
void notify_dismiss(void);
void notify_dismissall(void);
/* Fires "default", if offered, and drops the notification. */
void notify_invoke(void);

/* CLOCK_MONOTONIC, in ms */
uint64_t notify_now(void);

#endif /* G0WMNOTIFY_H */
