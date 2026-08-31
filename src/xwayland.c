/*
 * See LICENSE file for copyright and license details.
 *
 * XWayland surfaces
 */
#include "g0wn.h"

/* function declarations */
#ifdef XWAYLAND
static void activatex11(struct wl_listener* listener, void* data);
static void associatex11(struct wl_listener* listener, void* data);
static void configurex11(struct wl_listener* listener, void* data);
static void dissociatex11(struct wl_listener* listener, void* data);
static void sethints(struct wl_listener* listener, void* data);
#endif /* XWAYLAND */

/* function implementations */
#ifdef XWAYLAND
static void activatex11(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, activate);

    /* Only "managed" windows can be activated */
    if (!client_is_unmanaged(c))
        wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

static void associatex11(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, associate);

    LISTEN(&client_surface(c)->events.map, &c->map, mapnotify);
    LISTEN(&client_surface(c)->events.unmap, &c->unmap, unmapnotify);
}

static void configurex11(struct wl_listener* listener, void* data)
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
    /* c->mon is dereferenced below: closemon() leaves mapped clients without a
     * monitor whenever the last output goes away. */
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
        resize(c, c->mon->m, 0);
        return;
    }

    if ((c->isfloating && c != grabc) || !c->mon->lt[c->mon->sellt]->arrange) {
        resize(c,
               (struct wlr_box){ .x = event->x - c->bw,
                                 .y = event->y - c->bw,
                                 .width = event->width + c->bw * 2,
                                 .height = event->height + c->bw * 2 },
               0);
    } else {
        arrange(c->mon);
    }
}

void createnotifyx11(struct wl_listener* listener, void* data)
{
    struct wlr_xwayland_surface* xsurface = data;
    Client* c;

    /* Allocate a Client for this surface */
    c = xsurface->data = ecalloc(1, sizeof(*c));
    c->surface.xwayland = xsurface;
    c->type = X11;
    c->bw = client_is_unmanaged(c) ? 0 : borderpx;
    c->opacity = c->opacity_unfocus = opacity_unfocus;
    c->opacity_focus = opacity_focus;

    /* Listen to the various events it can emit */
    LISTEN(&xsurface->events.associate, &c->associate, associatex11);
    LISTEN(&xsurface->events.destroy, &c->destroy, destroynotify);
    LISTEN(&xsurface->events.dissociate, &c->dissociate, dissociatex11);
    LISTEN(&xsurface->events.request_activate, &c->activate, activatex11);
    LISTEN(&xsurface->events.request_configure, &c->configure, configurex11);
    LISTEN(
        &xsurface->events.request_fullscreen, &c->fullscreen, fullscreennotify);
    LISTEN(&xsurface->events.set_hints, &c->set_hints, sethints);
    LISTEN(&xsurface->events.set_title, &c->set_title, updatetitle);
}

static void dissociatex11(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, dissociate);
    wl_list_remove(&c->map.link);
    wl_list_remove(&c->unmap.link);
}

static void sethints(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, set_hints);
    struct wlr_surface* surface = client_surface(c);
    if (c == focustop(selmon) || !c->surface.xwayland->hints)
        return;

    c->isurgent = xcb_icccm_wm_hints_get_urgency(c->surface.xwayland->hints);
    drawbars();

    if (c->isurgent && surface && surface->mapped)
        setbordercolor(c, SchemeUrg);
}

void xwaylandready(struct wl_listener* listener, void* data)
{
    struct wlr_xcursor* xcursor;

    /* assign the one and only seat */
    wlr_xwayland_set_seat(xwayland, seat);

    /* Set the default XWayland cursor to match the rest of g0wn. */
    if ((xcursor = wlr_xcursor_manager_get_xcursor(cursor_mgr, "default", 1)))
        wlr_xwayland_set_cursor(
            xwayland,
            wlr_xcursor_image_get_buffer(xcursor->images[0]),
            xcursor->images[0]->hotspot_x,
            xcursor->images[0]->hotspot_y);
}

#endif /* XWAYLAND */
