#ifndef G0WMNOTIFY_H
#define G0WMNOTIFY_H

#include <dbus/dbus.h>
#include <wayland-server-core.h>

/* Minimal org.freedesktop.Notifications server: tracks one notification at a
 * time, drops icons, keeps only the "default" action. The bar just shows one
 * line of text. */

/* Size of that line, including the terminator. */
#define NOTIFY_TEXTMAX 512

void notify_start(DBusConnection* conn,
                  struct wl_event_loop* loop,
                  unsigned int timeout_secs,
                  void (*redraw)(void));
void notify_stop(void);

/* NULL if there is no notification currently active. */
const char* notify_gettext(void);
/* 0 if there is no notification currently active. */
unsigned int notify_getid(void);
/* Drop the current notification as if the user had clicked it away. */
void notify_dismiss(void);
/* Fire the notification's "default" action, if it has one, and drop it. */
void notify_invoke(void);
/* App name, or its desktop-entry hint if desktop is set ("" if none sent).
 * NULL if there is no notification currently active. */
const char* notify_getapp(int desktop);

#endif /* G0WMNOTIFY_H */
