/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * XWayland surfaces
 */
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/box.h>
#include <wlr/xcursor.h>
#ifdef XWAYLAND
#include <wlr/xwayland.h>
#include <xcb/xcb_icccm.h>
#endif

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/bar/bar.h"
#include "input/pointer.h"
#include "render/corner.h"
#include "render/opacity.h"
#include "window/client.h"
#include "window/layout.h"
#include "window/xwayland.h"

/* function declarations */
#ifdef XWAYLAND
static void xwayland_handle_request_activate(struct wl_listener* listener, void*);
static void xwayland_handle_associate(struct wl_listener* listener, void*);
static void xwayland_handle_request_configure(struct wl_listener* listener, void* data);
static void xwayland_handle_dissociate(struct wl_listener* listener, void*);
static void xwayland_handle_request_minimize(struct wl_listener* listener, void* data);
static void xwayland_handle_set_hints(struct wl_listener* listener, void*);
#endif /* XWAYLAND */

/* function implementations */
#ifdef XWAYLAND
static void xwayland_handle_request_activate(struct wl_listener* listener, void*)
{
    WindowClient* client = wl_container_of(listener, client, activate);

    /* Only "managed" windows can be activated */
    if (!client_is_unmanaged(client))
        wlr_xwayland_surface_activate(client->surface.xwayland, 1);
}

static void xwayland_handle_associate(struct wl_listener* listener, void*)
{
    WindowClient* client = wl_container_of(listener, client, associate);

    LISTEN(&client_surface(client)->events.map, &client->map, client_handle_map);
    LISTEN(&client_surface(client)->events.unmap, &client->unmap, client_handle_unmap);
}

static void xwayland_handle_request_configure(struct wl_listener* listener, void* data)
{
    WindowClient* client = wl_container_of(listener, client, configure);
    struct wlr_xwayland_surface_configure_event* event = data;
    if (!client_surface(client) || !client_surface(client)->mapped) {
        wlr_xwayland_surface_configure(client->surface.xwayland, event->x, event->y, event->width,
                                       event->height);
        return;
    }
    /* Unmanaged clients never get a monitor, so this precedes the client->monitor
     * check below. */
    if (client_is_unmanaged(client)) {
        wlr_scene_node_set_position(&client->scene->node, event->x, event->y);
        wlr_xwayland_surface_configure(client->surface.xwayland, event->x, event->y, event->width,
                                       event->height);
        return;
    }
    /* client->monitor is dereferenced below: monitor_close() leaves mapped clients
     * without a monitor whenever the last output goes away. */
    if (!client->monitor) {
        wlr_xwayland_surface_configure(client->surface.xwayland, event->x, event->y, event->width,
                                       event->height);
        return;
    }
    /* A fullscreen client does not get to pick its own geometry. Games under
     * XWayland keep re-asserting their internal resolution, and honouring that
     * would shrink the window to a box in the corner of the black fullscreen
     * backdrop while the game happily keeps rendering. Restate the size it
     * actually has instead. */
    if (client->is_fullscreen) {
        client_resize(client, client->monitor->geometry, 0);
        return;
    }

    if (client->is_floating && client != input_grab_client) {
        client_resize(client,
                      (struct wlr_box){ .x = event->x - client->border_width,
                                        .y = event->y - client->border_width,
                                        .width = event->width + client->border_width * 2,
                                        .height = event->height + client->border_width * 2 },
                      0);
    } else {
        layout_arrange(client->monitor);
    }
}

void xwayland_handle_new_surface(struct wl_listener*, void* data)
{
    struct wlr_xwayland_surface* xsurface = data;

    /* Allocate a WindowClient for this surface */
    WindowClient* client = xsurface->data = util_calloc(1, sizeof(*client));
    client->surface.xwayland = xsurface;
    client->type = SURFACE_TYPE_X11;
    client->border_width = client_is_unmanaged(client) ? 0 : corner_border_width();
    client->opacity = client->unfocused_opacity = settings->opacity.unfocus;
    client->focused_opacity = settings->opacity.focus;
    client->is_private = settings->windows.always_private;

    /* Listen to the various events it can emit */
    LISTEN(&xsurface->events.associate, &client->associate, xwayland_handle_associate);
    LISTEN(&xsurface->events.destroy, &client->destroy, client_handle_destroy);
    LISTEN(&xsurface->events.dissociate, &client->dissociate, xwayland_handle_dissociate);
    LISTEN(&xsurface->events.request_activate, &client->activate, xwayland_handle_request_activate);
    LISTEN(&xsurface->events.request_configure, &client->configure,
           xwayland_handle_request_configure);
    LISTEN(&xsurface->events.request_fullscreen, &client->fullscreen,
           client_handle_request_fullscreen);
    LISTEN(&xsurface->events.set_hints, &client->set_hints, xwayland_handle_set_hints);
    LISTEN(&xsurface->events.request_minimize, &client->minimize, xwayland_handle_request_minimize);
    LISTEN(&xsurface->events.set_title, &client->set_title, client_handle_set_title);
}

static void xwayland_handle_dissociate(struct wl_listener* listener, void*)
{
    WindowClient* client = wl_container_of(listener, client, dissociate);
    wl_list_remove(&client->map.link);
    wl_list_remove(&client->unmap.link);
}

static void xwayland_handle_request_minimize(struct wl_listener* listener, void* data)
{
    WindowClient* client = wl_container_of(listener, client, minimize);
    struct wlr_xwayland_minimize_event* event = data;

    /* There is nowhere to minimize to, but the client must hear back or it
     * thinks it still is. Grant it only while unfocused: activation undoes
     * it (client_activate_surface()). */
    wlr_xwayland_surface_set_minimized(client->surface.xwayland,
                                       event->minimize && client_surface(client) !=
                                                              seat->keyboard_state.focused_surface);
}

static void xwayland_handle_set_hints(struct wl_listener* listener, void*)
{
    WindowClient* client = wl_container_of(listener, client, set_hints);
    struct wlr_surface* surface = client_surface(client);
    if (client == client_top(selected_monitor) || !client->surface.xwayland->hints)
        return;

    client->is_urgent = xcb_icccm_wm_hints_get_urgency(client->surface.xwayland->hints);
    bar_draw_all();

    if (client->is_urgent && surface && surface->mapped)
        opacity_set_border_color(client, COLOR_SCHEME_URGENT);
}

void xwayland_handle_ready(struct wl_listener*, void*)
{

    /* assign the one and only seat */
    wlr_xwayland_set_seat(xwayland, seat);

    struct wlr_xcursor* xcursor;
    /* Set the default XWayland cursor to match the rest of g0wm. */
    if ((xcursor = wlr_xcursor_manager_get_xcursor(cursor_manager, "default", 1)))
        wlr_xwayland_set_cursor(xwayland, wlr_xcursor_image_get_buffer(xcursor->images[0]),
                                xcursor->images[0]->hotspot_x, xcursor->images[0]->hotspot_y);
}

#endif /* XWAYLAND */
