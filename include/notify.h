#ifndef DWLNOTIFY_H
#define DWLNOTIFY_H

#include <dbus/dbus.h>
#include <wayland-server-core.h>

/* Minimal org.freedesktop.Notifications server: only ever tracks a single
 * notification (the most recent one always replaces whatever was shown
 * before) and drops actions/hints/icons entirely, since the only consumer
 * is one line of text in the bar. */

void notify_start(DBusConnection* conn,
                  struct wl_event_loop* loop,
                  unsigned int timeout_secs,
                  void (*redraw)(void));
void notify_stop(void);

/* NULL if there is no notification currently active. */
const char* notify_gettext(void);
/* 0 if there is no notification currently active. */
unsigned int notify_getid(void);

#endif /* DWLNOTIFY_H */
