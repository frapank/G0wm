/*
 * See LICENSE file for copyright and license details.
 *
 * client lifecycle: mapping, geometry, focus and decorations
 */
#include "g0wm.h"

/* function declarations */
/* function declarations */
static void applybounds(Client* c, struct wlr_box* bbox);
static void applyrules(Client* c);
static void commitnotify(struct wl_listener* listener, void* data);
static void commitpopup(struct wl_listener* listener, void* data);
static void destroydecoration(struct wl_listener* listener, void* data);
static void maximizenotify(struct wl_listener* listener, void* data);
static void requestdecorationmode(struct wl_listener* listener, void* data);
static void setfullscreen(Client* c, int fullscreen);

/* function implementations */
static void applybounds(Client* c, struct wlr_box* bbox)
{
    /* set minimum possible */
    c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
    c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);

    if (c->geom.x >= bbox->x + bbox->width)
        c->geom.x = bbox->x + bbox->width - c->geom.width;
    if (c->geom.y >= bbox->y + bbox->height)
        c->geom.y = bbox->y + bbox->height - c->geom.height;
    if (c->geom.x + c->geom.width <= bbox->x)
        c->geom.x = bbox->x;
    if (c->geom.y + c->geom.height <= bbox->y)
        c->geom.y = bbox->y;
}

static void applyrules(Client* c)
{
    /* rule matching */
    const char *appid, *title;
    uint32_t newtags = 0;
    int i;
    const Rule* r;
    Monitor *mon = selmon, *m;

    appid = client_get_appid(c);
    title = client_get_title(c);

    for (r = rules; r < END(rules); r++) {
        if ((!r->title || strstr(title, r->title)) &&
            (!r->id || strstr(appid, r->id))) {
            c->isfloating = r->isfloating;
            if (r->opacity_focus > 0)
                c->opacity_focus = r->opacity_focus;
            if (r->opacity_unfocus > 0)
                c->opacity = c->opacity_unfocus = r->opacity_unfocus;
            newtags |= r->tags;
            i = 0;
            wl_list_for_each(m, &mons, link)
            {
                if (r->monitor == i++)
                    mon = m;
            }
        }
    }

    c->isfloating |= client_is_float_type(c);
    setmon(c, mon, newtags);
}

static void commitnotify(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, commit);

    if (c->surface.xdg->initial_commit) {
        /*
         * Get the monitor this client will be rendered on
         * Note that if the user set a rule in which the client is placed on
         * a different monitor based on its title, this will likely select
         * a wrong monitor.
         */
        applyrules(c);
        if (c->mon) {
            client_set_scale(client_surface(c), c->mon->wlr_output->scale);
        }
        setmon(c, NULL, 0); /* Make sure to reapply rules in mapnotify() */

        wlr_xdg_toplevel_set_wm_capabilities(
            c->surface.xdg->toplevel,
            WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
        if (c->decoration)
            requestdecorationmode(&c->set_decoration_mode, c->decoration);
        wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
        return;
    }

    resize(c, c->geom, (c->isfloating && !c->isfullscreen));

    /* mark a pending resize as completed */
    if (c->resize && c->resize <= c->surface.xdg->current.configure_serial)
        c->resize = 0;
}

static void commitpopup(struct wl_listener* listener, void* data)
{
    struct wlr_surface* surface = data;
    struct wlr_xdg_popup* popup = wlr_xdg_popup_try_from_wlr_surface(surface);
    LayerSurface* l = NULL;
    Client* c = NULL;
    struct wlr_box box;
    int type = -1;

    if (!popup->base->initial_commit)
        return;

    type = toplevel_from_wlr_surface(popup->base->surface, &c, &l);
    if (!popup->parent || type < 0)
        return;
    /* The parent may have been unmapped in the meantime, dropping its scene
     * tree; there is nothing to parent the popup to. */
    if (!popup->parent->data) {
        wlr_xdg_popup_destroy(popup);
        return;
    }
    popup->base->surface->data =
        wlr_scene_xdg_surface_create(popup->parent->data, popup->base);
    if ((l && !l->mon) || (c && !c->mon)) {
        wlr_xdg_popup_destroy(popup);
        return;
    }
    box = type == LayerShell ? l->mon->m : c->mon->w;
    box.x -= (type == LayerShell ? l->scene->node.x : c->geom.x);
    box.y -= (type == LayerShell ? l->scene->node.y : c->geom.y);
    wlr_xdg_popup_unconstrain_from_box(popup, &box);
    wl_list_remove(&listener->link);
    free(listener);
}

void createdecoration(struct wl_listener* listener, void* data)
{
    struct wlr_xdg_toplevel_decoration_v1* deco = data;
    Client* c = deco->toplevel->base->data;
    c->decoration = deco;

    LISTEN(&deco->events.request_mode,
           &c->set_decoration_mode,
           requestdecorationmode);
    LISTEN(&deco->events.destroy, &c->destroy_decoration, destroydecoration);

    requestdecorationmode(&c->set_decoration_mode, deco);
}

void createnotify(struct wl_listener* listener, void* data)
{
    /* This event is raised when a client creates a new toplevel (application
     * window). */
    struct wlr_xdg_toplevel* toplevel = data;
    Client* c = NULL;

    /* Allocate a Client for this surface */
    c = toplevel->base->data = ecalloc(1, sizeof(*c));
    c->surface.xdg = toplevel->base;
    c->bw = borderpx;
    c->opacity = c->opacity_unfocus = opacity_unfocus;
    c->opacity_focus = opacity_focus;

    LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
    LISTEN(&toplevel->base->surface->events.map, &c->map, mapnotify);
    LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmapnotify);
    LISTEN(&toplevel->events.destroy, &c->destroy, destroynotify);
    LISTEN(
        &toplevel->events.request_fullscreen, &c->fullscreen, fullscreennotify);
    LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
    LISTEN(&toplevel->events.set_title, &c->set_title, updatetitle);
}

void createpopup(struct wl_listener* listener, void* data)
{
    /* This event is raised when a client (either xdg-shell or layer-shell)
     * creates a new popup. */
    struct wlr_xdg_popup* popup = data;
    LISTEN_STATIC(&popup->base->surface->events.commit, commitpopup);
}

static void destroydecoration(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, destroy_decoration);

    wl_list_remove(&c->destroy_decoration.link);
    wl_list_remove(&c->set_decoration_mode.link);
}

void destroynotify(struct wl_listener* listener, void* data)
{
    /* Called when the xdg_toplevel is destroyed. */
    Client* c = wl_container_of(listener, c, destroy);
    wl_list_remove(&c->destroy.link);
    wl_list_remove(&c->set_title.link);
    wl_list_remove(&c->fullscreen.link);
#ifdef XWAYLAND
    if (c->type != XDGShell) {
        wl_list_remove(&c->activate.link);
        wl_list_remove(&c->associate.link);
        wl_list_remove(&c->configure.link);
        wl_list_remove(&c->dissociate.link);
        wl_list_remove(&c->set_hints.link);
    } else
#endif
    {
        wl_list_remove(&c->commit.link);
        wl_list_remove(&c->map.link);
        wl_list_remove(&c->unmap.link);
        wl_list_remove(&c->maximize.link);
    }
    free(c);
}

void focusclient(Client* c, int lift)
{
    struct wlr_surface* old = seat->keyboard_state.focused_surface;
    int unused_lx, unused_ly, old_client_type;
    Client* old_c = NULL;
    LayerSurface* old_l = NULL;

    if (locked)
        return;

    /* Warp cursor to center of client if it is outside */
    if (lift)
        warpcursor(c);

    /* Raise client in stacking order if requested */
    if (c && lift)
        wlr_scene_node_raise_to_top(&c->scene->node);

    if (c && client_surface(c) == old)
        return;

    if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) ==
        XDGShell) {
        struct wlr_xdg_popup *popup, *tmp;
        wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
            wlr_xdg_popup_destroy(popup);
    }

    /* Put the new client atop the focus stack and select its monitor */
    if (c && !client_is_unmanaged(c)) {
        wl_list_remove(&c->flink);
        wl_list_insert(&fstack, &c->flink);
        selmon = c->mon;
        c->isurgent = 0;
        c->opacity = c->opacity_focus;

        /* Don't change border color if there is an exclusive focus or we are
         * handling a drag operation */
        if (!exclusive_focus && !seat->drag)
            setbordercolor(c, SchemeSel);

        /* tabbed() otherwise only reruns on arrange() */
        if (c->mon && c->mon->lt[c->mon->sellt]->arrange == tabbed &&
            !c->isfloating && !c->isfullscreen)
            tabbed(c->mon);
    }

    /* Deactivate old client if focus is changing */
    if (old && (!c || client_surface(c) != old)) {
        /* If an overlay is focused, don't focus or activate the client,
         * but only update its position in fstack to render its border with its
         * color and focus it after the overlay is closed. */
        if (old_client_type == LayerShell &&
            wlr_scene_node_coords(
                &old_l->scene->node, &unused_lx, &unused_ly) &&
            old_l->layer_surface->current.layer >=
                ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
            return;
        } else if (old_c && old_c == exclusive_focus &&
                   client_wants_focus(old_c)) {
            return;
            /* Don't deactivate old client if the new one wants focus, as this
             * causes issues with winecfg and probably other clients */
        } else if (old_c && !client_is_unmanaged(old_c) &&
                   (!c || !client_wants_focus(c))) {
            setbordercolor(old_c, SchemeNorm);
            client_activate_surface(old, 0);
            old_c->opacity = old_c->opacity_unfocus;
        }
    }
    drawbars();

    if (!c) {
        /* With no client, all we have left is to clear focus */
        wlr_seat_keyboard_notify_clear_focus(seat);
        return;
    }

    /* Change cursor surface */
    motionnotify(0, NULL, 0, 0, 0, 0);

    /* Have a client, so focus its top-level wlr_surface */
    client_notify_enter(client_surface(c), wlr_seat_get_keyboard(seat));

    /* Activate the new client */
    client_activate_surface(client_surface(c), 1);
}

/* We probably should change the name of this: it sounds like it
 * will focus the topmost client of this mon, when actually will
 * only return that client */
Client* focustop(Monitor* m)
{
    Client* c;
    wl_list_for_each(c, &fstack, flink)
    {
        if (VISIBLEON(c, m))
            return c;
    }
    return NULL;
}

void fullscreennotify(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, fullscreen);
    setfullscreen(c, client_wants_fullscreen(c));
}

void killclient(const Arg* arg)
{
    Client* sel = focustop(selmon);
    if (sel)
        client_send_close(sel);
}

void mapnotify(struct wl_listener* listener, void* data)
{
    /* Called when the surface is mapped, or ready to display on-screen. */
    Client* p = NULL;
    Client *w, *c = wl_container_of(listener, c, map);
    Monitor* m;
    int i;

    /* Create scene tree for this client and its border */
    c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrTile]);
    /* Enabled later by a call to arrange() */
    wlr_scene_node_set_enabled(&c->scene->node, client_is_unmanaged(c));
    c->scene_surface =
        c->type == XDGShell
            ? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
            : wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
    c->scene->node.data = c->scene_surface->node.data = c;

    client_get_geometry(c, &c->geom);

    /* Handle unmanaged clients first so we can return prior create borders */
    if (client_is_unmanaged(c)) {
        /* Unmanaged clients always are floating */
        wlr_scene_node_reparent(&c->scene->node, layers[LyrFloat]);
        wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
        client_set_size(c, c->geom.width, c->geom.height);
        if (client_wants_focus(c)) {
            focusclient(c, 1);
            exclusive_focus = c;
        }
        goto unset_fullscreen;
    }

    for (i = 0; i < 4; i++) {
        c->border[i] = wlr_scene_rect_create(
            c->scene, 0, 0, (float[]){ 0.0f, 0.0f, 0.0f, 0.0f });
        c->border[i]->node.data = c;
    }
    /* the colour comes from here, once all four exist */
    setbordercolor(c, c->isurgent ? SchemeUrg : SchemeNorm);

#ifdef TITLEBAR
    c->title = wlr_scene_buffer_create(c->scene, NULL);
    c->title->node.data = c;
    wlr_scene_node_set_enabled(&c->title->node, 0);
#endif

#ifdef INTEGRATED_BACKGROUND
    /* the frosted backdrop goes under the lot: the surface, which is what it
     * shows through, and the decorations, which opacity_deco may fade too */
    c->blur = wlr_scene_buffer_create(c->scene, NULL);
    c->blur->node.data = c;
    wlr_scene_node_set_enabled(&c->blur->node, 0);
    wlr_scene_node_lower_to_bottom(&c->blur->node);

#ifdef TITLEBAR
    /* backs the title bar alone, which is what tabs need */
    c->titleblur = wlr_scene_buffer_create(c->scene, NULL);
    c->titleblur->node.data = c;
    wlr_scene_node_set_enabled(&c->titleblur->node, 0);
    wlr_scene_node_lower_to_bottom(&c->titleblur->node);
#endif
#endif

    /* Initialize client geometry with room for border */
    client_set_tiled(
        c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
    c->geom.width += 2 * c->bw;
    c->geom.height += 2 * c->bw;

    /* Insert this client into client lists. */
    wl_list_insert(clients.prev,
                   &c->link); /* attach at the bottom of the stack */
    wl_list_insert(&fstack, &c->flink);

    /* done here rather than in applyrules(): clients with a parent skip it */
    c->hasopacity = opacityallowed(client_get_appid(c));

    /* Set initial monitor, tags, floating status, and focus:
     * we always consider floating, clients that have parent and thus
     * we set the same tags and monitor as its parent.
     * If there is no parent, apply rules */
    if ((p = client_get_parent(c))) {
        c->isfloating = 1;
        setmon(c, p->mon, p->tags);
    } else {
        applyrules(c);
    }
    drawbars();

unset_fullscreen:
    m = c->mon ? c->mon : xytomon(c->geom.x, c->geom.y);
    wl_list_for_each(w, &clients, link)
    {
        if (w != c && w != p && w->isfullscreen && m == w->mon &&
            (w->tags & c->tags))
            setfullscreen(w, 0);
    }
}

static void maximizenotify(struct wl_listener* listener, void* data)
{
    /* This event is raised when a client would like to maximize itself,
     * typically because the user clicked on the maximize button on
     * client-side decorations. g0wm doesn't support maximization, but
     * to conform to xdg-shell protocol we still must send a configure.
     * Since xdg-shell protocol v5 we should ignore request of unsupported
     * capabilities, just schedule a empty configure when the client uses <5
     * protocol version
     * wlr_xdg_surface_schedule_configure() is used to send an empty reply. */
    Client* c = wl_container_of(listener, c, maximize);
    if (c->surface.xdg->initialized &&
        wl_resource_get_version(c->surface.xdg->toplevel->resource) <
            XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
        wlr_xdg_surface_schedule_configure(c->surface.xdg);
}

static void requestdecorationmode(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, set_decoration_mode);
    if (c->surface.xdg->initialized)
        wlr_xdg_toplevel_decoration_v1_set_mode(
            c->decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void resize(Client* c, struct wlr_box geo, int interact)
{
    struct wlr_box* bbox;
    struct wlr_box clip;
    int th = 0;

    if (!c->mon || !client_surface(c)->mapped)
        return;

#ifdef TITLEBAR
    th = titleheight(c);
#endif

    bbox = interact ? &sgeom : &c->mon->w;

    client_set_bounds(c, geo.width, geo.height);
    c->geom = geo;
    applybounds(c, bbox);

    /* Update scene-graph, including borders */
    wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
    wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw + th);
    wlr_scene_rect_set_size(c->border[0], c->geom.width, c->bw);
    wlr_scene_rect_set_size(c->border[1], c->geom.width, c->bw);
    wlr_scene_rect_set_size(c->border[2], c->bw, c->geom.height - 2 * c->bw);
    wlr_scene_rect_set_size(c->border[3], c->bw, c->geom.height - 2 * c->bw);
    wlr_scene_node_set_position(&c->border[1]->node, 0, c->geom.height - c->bw);
    wlr_scene_node_set_position(&c->border[2]->node, 0, c->bw);
    wlr_scene_node_set_position(
        &c->border[3]->node, c->geom.width - c->bw, c->bw);

    /* this is a no-op if size hasn't changed */
    c->resize = client_set_size(
        c, c->geom.width - 2 * c->bw, MAX(1, c->geom.height - 2 * c->bw - th));
    client_get_clip(c, &clip);
    clip.height -= th;
    wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);

#ifdef TITLEBAR
    settitle(c);
#endif
}

void setfloating(Client* c, int floating)
{
    Client* p = client_get_parent(c);
    c->isfloating = floating;
    /* If in floating layout do not change the client's layer */
    if (!c->mon || !client_surface(c)->mapped ||
        !c->mon->lt[c->mon->sellt]->arrange)
        return;
    wlr_scene_node_reparent(
        &c->scene->node,
        layers[c->isfullscreen || (p && p->isfullscreen) ? LyrFS
               : c->isfloating                           ? LyrFloat
                                                         : LyrTile]);
    arrange(c->mon);
    drawbars();
}

static void setfullscreen(Client* c, int fullscreen)
{
    c->isfullscreen = fullscreen;
    if (!c->mon || !client_surface(c)->mapped)
        return;
    c->bw = fullscreen ? 0 : borderpx;
    client_set_fullscreen(c, fullscreen);
    wlr_scene_node_reparent(&c->scene->node,
                            layers[c->isfullscreen ? LyrFS
                                   : c->isfloating ? LyrFloat
                                                   : LyrTile]);

    if (fullscreen) {
        c->prev = c->geom;
        resize(c, c->mon->m, 0);
    } else {
        /* restore previous size instead of arrange for floating windows since
         * client positions are set by the user and cannot be recalculated */
        resize(c, c->prev, 0);
    }
    arrange(c->mon);
    drawbars();
}

void setmon(Client* c, Monitor* m, uint32_t newtags)
{
    Monitor* oldmon = c->mon;

    if (oldmon == m)
        return;
    c->mon = m;
    c->prev = c->geom;

    /* Scene graph sends surface leave/enter events on move and resize */
    if (oldmon)
        arrange(oldmon);
    if (m) {
        /* Make sure window actually overlaps with the monitor */
        resize(c, c->geom, 0);
        c->tags =
            newtags ? newtags
                    : m->tagset[m->seltags]; /* assign tags of target monitor */
        c->prev.x = (m->w.width - c->prev.width) / 2 + m->m.x;
        c->prev.y = (m->w.height - c->prev.height) / 2 + m->m.y;
        setfullscreen(c, c->isfullscreen); /* This will call arrange(c->mon) */
        setfloating(c, c->isfloating);
    }
    focusclient(focustop(selmon), 1);
}

void togglefloating(const Arg* arg)
{
    Client* sel = focustop(selmon);
    /* return if fullscreen */
    if (sel && !sel->isfullscreen)
        setfloating(sel, !sel->isfloating);
}

void togglefullscreen(const Arg* arg)
{
    Client* sel = focustop(selmon);
    if (sel)
        setfullscreen(sel, !sel->isfullscreen);
}

void unmapnotify(struct wl_listener* listener, void* data)
{
    /* Called when the surface is unmapped, and should no longer be shown. */
    Client* c = wl_container_of(listener, c, unmap);
    if (c == grabc) {
        cursor_mode = CurNormal;
        grabc = NULL;
    }

    if (client_is_unmanaged(c)) {
        if (c == exclusive_focus) {
            exclusive_focus = NULL;
            focusclient(focustop(selmon), 1);
        }
    } else {
        wl_list_remove(&c->link);
        setmon(c, NULL, 0);
        wl_list_remove(&c->flink);
    }

    /* The wl_surface can outlive the mapping (e.g. a client holding an idle
     * inhibitor on it), so drop the back-pointer to the scene tree we are
     * about to free instead of leaving it dangling. mapnotify() sets it again
     * on the next map. */
    if (client_surface(c))
        client_surface(c)->data = NULL;
    wlr_scene_node_destroy(&c->scene->node);
#ifdef TITLEBAR
    c->title = NULL;
#endif
#ifdef INTEGRATED_BACKGROUND
    c->blur = NULL;
    c->blurbuf = NULL;
#ifdef TITLEBAR
    c->titleblur = NULL;
    c->titleblurbuf = NULL;
#endif
#endif
#ifdef TITLEBAR
    bufpooldrop(c->titlepool, LENGTH(c->titlepool));
    c->titlebufw = 0;
#endif
    drawbars();
    motionnotify(0, NULL, 0, 0, 0, 0);
}

void updatetitle(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, set_title);
    if (c == focustop(c->mon))
        drawbars();
}

void urgent(struct wl_listener* listener, void* data)
{
    struct wlr_xdg_activation_v1_request_activate_event* event = data;
    Client* c = NULL;
    toplevel_from_wlr_surface(event->surface, &c, NULL);
    if (!c || c == focustop(selmon))
        return;

    c->isurgent = 1;
    drawbars();

    if (client_surface(c)->mapped)
        setbordercolor(c, SchemeUrg);
}
