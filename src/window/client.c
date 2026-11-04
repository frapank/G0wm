/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * client lifecycle: mapping, geometry, focus and decorations
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_dialog_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_system_bell_v1.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/bar/bar.h"
#include "desktop/bar/title.h"
#include "input/pointer.h"
#include "input/textinput.h"
#include "output/lock.h"
#include "output/monitor.h"
#include "render/buffer.h"
#include "render/corner.h"
#include "render/opacity.h"
#include "window/client.h"
#include "window/layout.h"
#include "window/toplevel.h"
#include "xdg-shell-protocol.h"

constexpr int MAPWAIT_MS =
    200; /* longest a new client may take to fit its tile */

/* function declarations */
static void client_apply_bounds(Client* c, struct wlr_box* bbox);
static void client_apply_rules(Client* c);
static void client_handle_commit(struct wl_listener* listener, void* data);
static void client_handle_popup_commit(struct wl_listener* listener,
                                       void* data);
static void client_handle_decoration_destroy(struct wl_listener* listener,
                                             void* data);
static void client_handle_request_maximize(struct wl_listener* listener,
                                           void* data);
static int client_map_wait_done(void* data);
static Client* client_modal_top(Client* c);
static void client_handle_request_decoration_mode(struct wl_listener* listener,
                                                  void* data);
static void client_set_urgent(Client* c);

/* function implementations */
void client_activate(Client* c)
{
    if (!c->mon || !client_surface(c)->mapped)
        return;
    selected_monitor = c->mon;
    if (!VISIBLEON(c, c->mon))
        layout_view(&(Arg){ .ui = c->tags });
    client_focus(c, 1);
}

static void client_apply_bounds(Client* c, struct wlr_box* bbox)
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

static void client_apply_rules(Client* c)
{
    /* rule matching */
    const char *appid, *title;
    uint32_t newtags = 0;
    int i;
    Monitor *mon = selected_monitor, *m;

    appid = client_get_app_id(c);
    title = client_get_title(c);

    for (const Rule* r = rules; r < rules + rule_count; r++) {
        if ((!r->title || strstr(title, r->title)) &&
            (!r->id || strstr(appid, r->id))) {
            c->isfloating = r->isfloating;
            if (r->isprivate >= 0)
                c->isprivate = r->isprivate;
            if (r->opacity_focus > 0)
                c->opacity_focus = r->opacity_focus;
            if (r->opacity_unfocus > 0)
                c->opacity = c->opacity_unfocus = r->opacity_unfocus;
            newtags |= r->tags;
            i = 0;
            wl_list_for_each(m, &monitors, link)
            {
                if (r->monitor == i++)
                    mon = m;
            }
        }
    }

    c->isfloating |= client_is_float_type(c);
    client_set_monitor(c, mon, newtags);
}

static void client_handle_commit(struct wl_listener* listener, void*)
{
    Client* c = wl_container_of(listener, c, commit);

    if (c->surface.xdg->initial_commit) {
        /*
         * Get the monitor this client will be rendered on
         * Note that if the user set a rule in which the client is placed on
         * a different monitor based on its title, this will likely select
         * a wrong monitor.
         */
        client_apply_rules(c);
        if (c->mon) {
            client_set_scale(client_surface(c), c->mon->wlr_output->scale);
        }
        client_set_monitor(
            c,
            nullptr,
            0); /* Make sure to reapply rules in client_handle_map() */

        wlr_xdg_toplevel_set_wm_capabilities(
            c->surface.xdg->toplevel,
            WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
        if (c->decoration)
            client_handle_request_decoration_mode(&c->set_decoration_mode,
                                                  c->decoration);
        wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
        return;
    }

    client_resize(c, c->geom, (c->isfloating && !c->isfullscreen));

    /* mark a pending resize as completed */
    if (c->resize && c->resize <= c->surface.xdg->current.configure_serial)
        c->resize = 0;

    if (c->mapwait && !c->resize)
        client_map_wait_done(c);
}

static void client_handle_popup_commit(struct wl_listener* listener, void* data)
{
    struct wlr_surface* surface = data;
    struct wlr_xdg_popup* popup = wlr_xdg_popup_try_from_wlr_surface(surface);
    LayerSurface* l = nullptr;
    Client* c = nullptr;
    struct wlr_box box;
    int type = -1;

    if (!popup->base->initial_commit)
        return;

    type = client_from_surface(popup->base->surface, &c, &l);
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

void client_handle_new_decoration(struct wl_listener*, void* data)
{
    struct wlr_xdg_toplevel_decoration_v1* deco = data;
    Client* c = deco->toplevel->base->data;
    c->decoration = deco;

    LISTEN(&deco->events.request_mode,
           &c->set_decoration_mode,
           client_handle_request_decoration_mode);
    LISTEN(&deco->events.destroy,
           &c->destroy_decoration,
           client_handle_decoration_destroy);

    client_handle_request_decoration_mode(&c->set_decoration_mode, deco);
}

void client_handle_new_toplevel(struct wl_listener*, void* data)
{
    /* This event is raised when a client creates a new toplevel (application
     * window). */
    struct wlr_xdg_toplevel* toplevel = data;
    Client* c = nullptr;

    /* Allocate a Client for this surface */
    c = toplevel->base->data = util_calloc(1, sizeof(*c));
    c->surface.xdg = toplevel->base;
    c->bw = corner_border_width();
    c->opacity = c->opacity_unfocus = opacity_unfocus;
    c->opacity_focus = opacity_focus;
    c->isprivate = always_private;

    LISTEN(&toplevel->base->surface->events.commit,
           &c->commit,
           client_handle_commit);
    LISTEN(&toplevel->base->surface->events.map, &c->map, client_handle_map);
    LISTEN(
        &toplevel->base->surface->events.unmap, &c->unmap, client_handle_unmap);
    LISTEN(&toplevel->events.destroy, &c->destroy, client_handle_destroy);
    LISTEN(&toplevel->events.request_fullscreen,
           &c->fullscreen,
           client_handle_request_fullscreen);
    LISTEN(&toplevel->events.request_maximize,
           &c->maximize,
           client_handle_request_maximize);
    LISTEN(&toplevel->events.set_title, &c->set_title, client_handle_set_title);
}

void client_handle_new_popup(struct wl_listener*, void* data)
{
    /* This event is raised when a client (either xdg-shell or layer-shell)
     * creates a new popup. */
    struct wlr_xdg_popup* popup = data;
    LISTEN_STATIC(&popup->base->surface->events.commit,
                  client_handle_popup_commit);
}

static void client_handle_decoration_destroy(struct wl_listener* listener,
                                             void*)
{
    Client* c = wl_container_of(listener, c, destroy_decoration);

    wl_list_remove(&c->destroy_decoration.link);
    wl_list_remove(&c->set_decoration_mode.link);
}

void client_handle_destroy(struct wl_listener* listener, void*)
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
        wl_list_remove(&c->minimize.link);
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

void client_focus(Client* c, int lift)
{
    struct wlr_surface* old = seat->keyboard_state.focused_surface;
    int unused_lx, unused_ly, old_client_type;
    Client* old_c = nullptr;
    LayerSurface* old_l = nullptr;

    if (lock_active)
        return;

    if (c && !client_is_unmanaged(c))
        c = client_modal_top(c);

    /* Warp cursor to center of client if it is outside */
    if (lift)
        input_warp_cursor(c);

    /* Raise client in stacking order if requested */
    if (c && lift)
        wlr_scene_node_raise_to_top(&c->scene->node);

    if (c && client_surface(c) == old)
        return;

    if ((old_client_type = client_from_surface(old, &old_c, &old_l)) ==
        XDGShell) {
        struct wlr_xdg_popup *popup, *tmp;
        wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
            wlr_xdg_popup_destroy(popup);
    }

    /* Put the new client atop the focus stack and select its monitor */
    if (c && !client_is_unmanaged(c)) {
        wl_list_remove(&c->flink);
        wl_list_insert(&focus_stack, &c->flink);
        selected_monitor = c->mon;
        c->isurgent = 0;
        c->opacity = c->opacity_focus;

        /* Don't change border color if there is an exclusive focus or we are
         * handling a drag operation */
        if (!exclusive_focus && !seat->drag)
            opacity_set_border_color(c, SchemeSel);

        /* layout_tabbed() otherwise only reruns on layout_arrange() */
        if (c->mon && c->mon->lt[c->mon->sellt]->arrange == layout_tabbed &&
            !c->isfloating && !c->isfullscreen)
            layout_tabbed(c->mon);
    }

    /* Deactivate old client if focus is changing */
    if (old && (!c || client_surface(c) != old)) {
        /* If an overlay is focused, don't focus or activate the client,
         * but only update its position in focus_stack to render its border with
         * its color and focus it after the overlay is closed. */
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
            opacity_set_border_color(old_c, SchemeNorm);
            client_activate_surface(old, 0);
            old_c->opacity = old_c->opacity_unfocus;
        }
    }
    bar_draw_all();

    if (!c) {
        /* With no client, all we have left is to clear focus */
        toplevel_focus(nullptr);
        wlr_seat_keyboard_notify_clear_focus(seat);
        return;
    }

    /* Change cursor surface */
    input_motion(0, nullptr, 0, 0, 0, 0);

    /* Have a client, so focus its top-level wlr_surface */
    client_notify_enter(client_surface(c), wlr_seat_get_keyboard(seat));

    /* Activate the new client */
    client_activate_surface(client_surface(c), 1);
    toplevel_focus(c);
}

/* the last focused client visible on m */
Client* client_top(Monitor* m)
{
    Client* c;
    wl_list_for_each(c, &focus_stack, flink)
    {
        if (VISIBLEON(c, m))
            return c;
    }
    return nullptr;
}

void client_handle_request_fullscreen(struct wl_listener* listener, void*)
{
    Client* c = wl_container_of(listener, c, fullscreen);
    client_set_fullscreen(c, client_wants_fullscreen(c));
}

void client_kill(const Arg*)
{
    Client* sel = client_top(selected_monitor);
    if (sel)
        client_send_close(sel);
}

void client_handle_map(struct wl_listener* listener, void*)
{
    /* Called when the surface is mapped, or ready to display on-screen. */
    Client* p = nullptr;
    Client *w, *c = wl_container_of(listener, c, map);
    Monitor* m;
    int th;

    /* Create scene tree for this client and its border */
    c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrTile]);
    /* Enabled later by a call to layout_arrange() */
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
            client_focus(c, 1);
            exclusive_focus = c;
        }
        goto unset_fullscreen;
    }

    for (int i = 0; i < 4; i++) {
        c->border[i] = wlr_scene_rect_create(
            c->scene, 0, 0, (float[]){ 0.0f, 0.0f, 0.0f, 0.0f });
        c->border[i]->node.data = c;
        /* above the borders, which stop short of the curve */
        c->corner[i] = wlr_scene_buffer_create(c->scene, nullptr);
        c->corner[i]->node.data = c;
        wlr_scene_node_set_enabled(&c->corner[i]->node, 0);
    }
    /* the colour comes from here, once all four exist */
    opacity_set_border_color(c, c->isurgent ? SchemeUrg : SchemeNorm);

    c->title = wlr_scene_buffer_create(c->scene, nullptr);
    c->title->node.data = c;
    wlr_scene_node_set_enabled(&c->title->node, 0);

#ifdef INTEGRATED_BACKGROUND
    /* the frosted backdrop goes under the lot: the surface, which is what it
     * shows through, and the decorations, which opacity_decorations may fade
     * too */
    c->blur = wlr_scene_buffer_create(c->scene, nullptr);
    c->blur->node.data = c;
    wlr_scene_node_set_enabled(&c->blur->node, 0);
    wlr_scene_node_lower_to_bottom(&c->blur->node);

    /* backs the title bar alone, which is what tabs need */
    c->titleblur = wlr_scene_buffer_create(c->scene, nullptr);
    c->titleblur->node.data = c;
    wlr_scene_node_set_enabled(&c->titleblur->node, 0);
    wlr_scene_node_lower_to_bottom(&c->titleblur->node);
#endif

    /* Initialize client geometry with room for border */
    client_set_tiled(
        c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
    c->geom.width += 2 * c->bw;
    c->geom.height += 2 * c->bw;

    /* Insert this client into client lists. */
    wl_list_insert(clients.prev,
                   &c->link); /* attach at the bottom of the stack */
    wl_list_insert(&focus_stack, &c->flink);

    /* Set initial monitor, tags, floating status, and focus:
     * we always consider floating, clients that have parent and thus
     * we set the same tags and monitor as its parent.
     * If there is no parent, apply rules */
    if ((p = client_get_parent(c))) {
        c->isfloating = 1;
        client_set_monitor(c, p->mon, p->tags);
    } else {
        client_apply_rules(c);
    }

    if ((th = bar_title_height(c)) > 0 && c->isfloating) {
        c->geom.height += th;
        c->prev.height += th;
        c->prev.y -= th / 2;
        client_resize(c, c->geom, c->isfloating && !c->isfullscreen);
    }

    /* the first buffer has the client's own size, not its tile's: stay hidden
     * until the resize lands or MAPWAIT_MS passes */
    if (c->resize) {
        c->mapwait =
            wl_event_loop_add_timer(event_loop, client_map_wait_done, c);
        wl_event_source_timer_update(c->mapwait, MAPWAIT_MS);
        wlr_scene_node_set_enabled(&c->scene->node, 0);
    }

    bar_draw_all();
    toplevel_map(c);

unset_fullscreen:
    m = c->mon ? c->mon : monitor_at(c->geom.x, c->geom.y);
    wl_list_for_each(w, &clients, link)
    {
        if (w != c && w != p && w->isfullscreen && m == w->mon &&
            (w->tags & c->tags))
            client_set_fullscreen(w, 0);
    }
}

static int client_map_wait_done(void* data)
{
    Client* c = data;

    wl_event_source_remove(c->mapwait);
    c->mapwait = nullptr;
    if (c->mon) {
        wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, c->mon));
        /* bar_draw_title_bar() skipped the title bar while the client was
         * hidden */
        bar_draw(c->mon);
    }
    return 0;
}

static void client_handle_request_maximize(struct wl_listener* listener, void*)
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

/* innermost modal dialog of c, or c */
static Client* client_modal_top(Client* c)
{
    struct wlr_xdg_dialog_v1* dialog;
    Client* d;

    wl_list_for_each(d, &focus_stack, flink)
    {
        if (d == c || client_is_x11(d) || !VISIBLEON(d, d->mon) ||
            client_get_parent(d) != c)
            continue;
        dialog = wlr_xdg_dialog_v1_try_from_wlr_xdg_toplevel(
            d->surface.xdg->toplevel);
        if (dialog && dialog->modal)
            return client_modal_top(d);
    }
    return c;
}

static void client_handle_request_decoration_mode(struct wl_listener* listener,
                                                  void*)
{
    Client* c = wl_container_of(listener, c, set_decoration_mode);
    if (c->surface.xdg->initialized)
        wlr_xdg_toplevel_decoration_v1_set_mode(
            c->decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void client_resize(Client* c, struct wlr_box geo, int interact)
{
    struct wlr_box* bbox;
    struct wlr_box clip;
    int th = 0, r, v;

    if (!c->mon || !client_surface(c)->mapped)
        return;

    th = bar_title_height(c);

    bbox = interact ? &screen_geometry : &c->mon->w;

    client_set_bounds(c, geo.width, geo.height);
    c->geom = geo;
    client_apply_bounds(c, bbox);

    /* Update scene-graph, including borders */
    wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
    wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw + th);
    /* the borders run between the corners, which take r off each edge */
    r = corner_client_radius(c);
    v = MAX(r, (int)c->bw);
    wlr_scene_rect_set_size(c->border[0], MAX(c->geom.width - 2 * r, 0), c->bw);
    wlr_scene_rect_set_size(c->border[1], MAX(c->geom.width - 2 * r, 0), c->bw);
    wlr_scene_rect_set_size(
        c->border[2], c->bw, MAX(c->geom.height - 2 * v, 0));
    wlr_scene_rect_set_size(
        c->border[3], c->bw, MAX(c->geom.height - 2 * v, 0));
    wlr_scene_node_set_position(&c->border[0]->node, r, 0);
    wlr_scene_node_set_position(&c->border[1]->node, r, c->geom.height - c->bw);
    wlr_scene_node_set_position(&c->border[2]->node, 0, v);
    wlr_scene_node_set_position(&c->border[3]->node, c->geom.width - c->bw, v);
    corner_draw_all(c);

    /* this is a no-op if size hasn't changed */
    c->resize = client_set_size(c,
                                MAX(1, c->geom.width - 2 * (int)c->bw),
                                MAX(1, c->geom.height - 2 * (int)c->bw - th));
    client_get_clip(c, &clip);
    clip.height -= th;
    wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);

    bar_place_title_bar(c);
    textinput_arrange();
}

void client_handle_ring_bell(struct wl_listener*, void* data)
{
    struct wlr_xdg_system_bell_v1_ring_event* event = data;
    Client* c = nullptr;

    if (event->surface)
        client_from_surface(event->surface, &c, nullptr);
    if (!c || c == client_top(selected_monitor))
        return;
    client_set_urgent(c);
}

void client_set_floating(Client* c, int floating)
{
    Client* p = client_get_parent(c);
    c->isfloating = floating;
    if (!c->mon || !client_surface(c)->mapped)
        return;
    wlr_scene_node_reparent(
        &c->scene->node,
        layers[c->isfullscreen || (p && p->isfullscreen) ? LyrFS
               : c->isfloating                           ? LyrFloat
                                                         : LyrTile]);
    layout_arrange(c->mon);
    bar_draw_all();
}

void client_set_fullscreen(Client* c, int fullscreen)
{
    c->isfullscreen = fullscreen;
    toplevel_update(c);
    if (!c->mon || !client_surface(c)->mapped)
        return;
    c->bw = fullscreen ? 0 : corner_border_width();
    client_send_fullscreen(c, fullscreen);
    wlr_scene_node_reparent(&c->scene->node,
                            layers[c->isfullscreen ? LyrFS
                                   : c->isfloating ? LyrFloat
                                                   : LyrTile]);

    if (fullscreen) {
        c->prev = c->geom;
        client_resize(c, c->mon->m, 0);
    } else {
        /* restore previous size instead of arrange for floating windows since
         * client positions are set by the user and cannot be recalculated */
        client_resize(c, c->prev, 0);
    }
    layout_arrange(c->mon);
    bar_draw_all();
}

void client_set_monitor(Client* c, Monitor* m, uint32_t newtags)
{
    Monitor* oldmon = c->mon;

    if (oldmon == m)
        return;
    c->mon = m;
    c->prev = c->geom;

    /* Scene graph sends surface leave/enter events on move and resize */
    if (oldmon)
        layout_arrange(oldmon);
    if (m) {
        /* Make sure window actually overlaps with the monitor */
        client_resize(c, c->geom, 0);
        c->tags =
            newtags ? newtags
                    : m->tagset[m->seltags]; /* assign tags of target monitor */
        c->prev.x = (m->w.width - c->prev.width) / 2 + m->m.x;
        c->prev.y = (m->w.height - c->prev.height) / 2 + m->m.y;
        client_set_fullscreen(
            c, c->isfullscreen); /* This will call layout_arrange(c->mon) */
        client_set_floating(c, c->isfloating);
    }
    toplevel_update(c);
    client_focus(client_top(selected_monitor), 1);
}

static void client_set_urgent(Client* c)
{
    c->isurgent = 1;
    bar_draw_all();

    if (client_surface(c)->mapped)
        opacity_set_border_color(c, SchemeUrg);
}

void client_toggle_floating(const Arg*)
{
    Client* sel = client_top(selected_monitor);
    /* return if fullscreen */
    if (sel && !sel->isfullscreen)
        client_set_floating(sel, !sel->isfloating);
}

void client_toggle_fullscreen(const Arg*)
{
    Client* sel = client_top(selected_monitor);
    if (sel)
        client_set_fullscreen(sel, !sel->isfullscreen);
}

void client_handle_unmap(struct wl_listener* listener, void*)
{
    /* Called when the surface is unmapped, and should no longer be shown. */
    Client* c = wl_container_of(listener, c, unmap);

    toplevel_unmap(c);
    if (c->mapwait) {
        wl_event_source_remove(c->mapwait);
        c->mapwait = nullptr;
    }
    if (c == input_grab_client) {
        input_cursor_mode = CurNormal;
        input_grab_client = nullptr;
    }

    if (client_is_unmanaged(c)) {
        if (c == exclusive_focus) {
            exclusive_focus = nullptr;
            client_focus(client_top(selected_monitor), 1);
        }
    } else {
        wl_list_remove(&c->link);
        client_set_monitor(c, nullptr, 0);
        wl_list_remove(&c->flink);
    }

    /* The wl_surface can outlive the mapping (e.g. a client holding an idle
     * inhibitor on it), so drop the back-pointer to the scene tree we are
     * about to free instead of leaving it dangling. client_handle_map() sets it
     * again on the next map. */
    if (client_surface(c))
        client_surface(c)->data = nullptr;
    wlr_scene_node_destroy(&c->scene->node);
    c->title = nullptr;
#ifdef INTEGRATED_BACKGROUND
    c->blur = nullptr;
    c->blurbuf = nullptr;
    c->titleblur = nullptr;
    c->titleblurbuf = nullptr;
#endif
    buffer_pool_drop(c->titlepool, LENGTH(c->titlepool));
    buffer_pool_drop(c->sharepool, LENGTH(c->sharepool));
    c->titlebufw = 0;
    for (int i = 0; i < 4; i++) {
        c->corner[i] = nullptr;
        buffer_pool_drop(c->cornerpool[i], LENGTH(c->cornerpool[i]));
    }
    c->cornerbufr = 0;
    bar_draw_all();
    input_motion(0, nullptr, 0, 0, 0, 0);
}

void client_handle_set_title(struct wl_listener* listener, void*)
{
    Client* c = wl_container_of(listener, c, set_title);
    toplevel_update(c);
    if (c == client_top(c->mon))
        bar_draw_all();
}

void client_handle_request_activate(struct wl_listener*, void* data)
{
    struct wlr_xdg_activation_v1_request_activate_event* event = data;
    Client* c = nullptr;
    client_from_surface(event->surface, &c, nullptr);
    if (!c || c == client_top(selected_monitor))
        return;

    /* the user clicked a notification: focus instead of marking urgent */
    if (activation_token_ours(event->token) && c->mon &&
        client_surface(c)->mapped) {
        client_activate(c);
        return;
    }

    client_set_urgent(c);
}
