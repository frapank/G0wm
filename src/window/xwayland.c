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
static void xwayland_handle_request_activate(struct wl_listener* listener,
                                             void* data);
static void xwayland_handle_associate(struct wl_listener* listener, void* data);
static void xwayland_handle_request_configure(struct wl_listener* listener,
                                              void* data);
static void xwayland_handle_dissociate(struct wl_listener* listener,
                                       void* data);
static void xwayland_handle_request_minimize(struct wl_listener* listener,
                                             void* data);
static void xwayland_handle_set_hints(struct wl_listener* listener, void* data);
#endif /* XWAYLAND */

/* function implementations */
#ifdef XWAYLAND
static void xwayland_handle_request_activate(struct wl_listener* listener,
                                             void*)
{
    Client* c = wl_container_of(listener, c, activate);

    /* Only "managed" windows can be activated */
    if (!client_is_unmanaged(c))
        wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

static void xwayland_handle_associate(struct wl_listener* listener, void*)
{
    Client* c = wl_container_of(listener, c, associate);

    LISTEN(&client_surface(c)->events.map, &c->map, client_handle_map);
    LISTEN(&client_surface(c)->events.unmap, &c->unmap, client_handle_unmap);
}

static void xwayland_handle_request_configure(struct wl_listener* listener,
                                              void* data)
{
    Client* c = wl_container_of(listener, c, configure);
    struct wlr_xwayland_surface_configure_event* event = data;
    if (!client_surface(c) || !client_surface(c)->mapped) {
        wlr_xwayland_surface_configure(c->surface.xwayland,
                                       event->x,
                                       event->y,
                                       event->width,
                                       event->height);
        return;
    }
    /* Unmanaged clients never get a monitor, so this precedes the c->mon
     * check below. */
    if (client_is_unmanaged(c)) {
        wlr_scene_node_set_position(&c->scene->node, event->x, event->y);
        wlr_xwayland_surface_configure(c->surface.xwayland,
                                       event->x,
                                       event->y,
                                       event->width,
                                       event->height);
        return;
    }
    /* c->mon is dereferenced below: monitor_close() leaves mapped clients
     * without a monitor whenever the last output goes away. */
    if (!c->mon) {
        wlr_xwayland_surface_configure(c->surface.xwayland,
                                       event->x,
                                       event->y,
                                       event->width,
                                       event->height);
        return;
    }
    /* A fullscreen client does not get to pick its own geometry. Games under
     * XWayland keep re-asserting their internal resolution, and honouring that
     * would shrink the window to a box in the corner of the black fullscreen
     * backdrop while the game happily keeps rendering. Restate the size it
     * actually has instead. */
    if (c->isfullscreen) {
        client_resize(c, c->mon->m, 0);
        return;
    }

    if (c->isfloating && c != input_grab_client) {
        client_resize(c,
                      (struct wlr_box){ .x = event->x - c->bw,
                                        .y = event->y - c->bw,
                                        .width = event->width + c->bw * 2,
                                        .height = event->height + c->bw * 2 },
                      0);
    } else {
        layout_arrange(c->mon);
    }
}

void xwayland_handle_new_surface(struct wl_listener*, void* data)
{
    struct wlr_xwayland_surface* xsurface = data;
    Client* c;

    /* Allocate a Client for this surface */
    c = xsurface->data = util_calloc(1, sizeof(*c));
    c->surface.xwayland = xsurface;
    c->type = X11;
    c->bw = client_is_unmanaged(c) ? 0 : corner_border_width();
    c->opacity = c->opacity_unfocus = opacity_unfocus;
    c->opacity_focus = opacity_focus;
    c->isprivate = always_private;

    /* Listen to the various events it can emit */
    LISTEN(
        &xsurface->events.associate, &c->associate, xwayland_handle_associate);
    LISTEN(&xsurface->events.destroy, &c->destroy, client_handle_destroy);
    LISTEN(&xsurface->events.dissociate,
           &c->dissociate,
           xwayland_handle_dissociate);
    LISTEN(&xsurface->events.request_activate,
           &c->activate,
           xwayland_handle_request_activate);
    LISTEN(&xsurface->events.request_configure,
           &c->configure,
           xwayland_handle_request_configure);
    LISTEN(&xsurface->events.request_fullscreen,
           &c->fullscreen,
           client_handle_request_fullscreen);
    LISTEN(
        &xsurface->events.set_hints, &c->set_hints, xwayland_handle_set_hints);
    LISTEN(&xsurface->events.request_minimize,
           &c->minimize,
           xwayland_handle_request_minimize);
    LISTEN(&xsurface->events.set_title, &c->set_title, client_handle_set_title);
}

static void xwayland_handle_dissociate(struct wl_listener* listener, void*)
{
    Client* c = wl_container_of(listener, c, dissociate);
    wl_list_remove(&c->map.link);
    wl_list_remove(&c->unmap.link);
}

static void xwayland_handle_request_minimize(struct wl_listener* listener,
                                             void* data)
{
    Client* c = wl_container_of(listener, c, minimize);
    struct wlr_xwayland_minimize_event* event = data;

    /* There is nowhere to minimize to, but the client must hear back or it
     * thinks it still is. Grant it only while unfocused: activation undoes
     * it (client_activate_surface()). */
    wlr_xwayland_surface_set_minimized(
        c->surface.xwayland,
        event->minimize &&
            client_surface(c) != seat->keyboard_state.focused_surface);
}

static void xwayland_handle_set_hints(struct wl_listener* listener, void*)
{
    Client* c = wl_container_of(listener, c, set_hints);
    struct wlr_surface* surface = client_surface(c);
    if (c == client_top(selected_monitor) || !c->surface.xwayland->hints)
        return;

    c->isurgent = xcb_icccm_wm_hints_get_urgency(c->surface.xwayland->hints);
    bar_draw_all();

    if (c->isurgent && surface && surface->mapped)
        opacity_set_border_color(c, SchemeUrg);
}

void xwayland_handle_ready(struct wl_listener*, void*)
{
    struct wlr_xcursor* xcursor;

    /* assign the one and only seat */
    wlr_xwayland_set_seat(xwayland, seat);

    /* Set the default XWayland cursor to match the rest of g0wm. */
    if ((xcursor =
             wlr_xcursor_manager_get_xcursor(cursor_manager, "default", 1)))
        wlr_xwayland_set_cursor(
            xwayland,
            wlr_xcursor_image_get_buffer(xcursor->images[0]),
            xcursor->images[0]->hotspot_x,
            xcursor->images[0]->hotspot_y);
}

#endif /* XWAYLAND */
