#ifndef G0WMDBUS_H
#define G0WMDBUS_H

#include <dbus/dbus.h>
#include <wayland-server-core.h>

struct wl_event_source* startbus(DBusConnection* conn,
                                 struct wl_event_loop* loop);
void stopbus(DBusConnection* conn, struct wl_event_source* bus_source);

#endif /* G0WMDBUS_H */
