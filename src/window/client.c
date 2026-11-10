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

#include "core/activation.h"
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

constexpr int MAPWAIT_MS = 200; /* longest a new client may take to fit its tile */

/* function declarations */
static void client_apply_bounds(Client* client, struct wlr_box* bbox);
static void client_apply_rules(Client* client);
static void client_handle_commit(struct wl_listener* listener, void*);
static void client_handle_popup_commit(struct wl_listener* listener, void* data);
static void client_handle_decoration_destroy(struct wl_listener* listener, void*);
static void client_handle_request_maximize(struct wl_listener* listener, void*);
static int client_map_wait_done(void* data);
static Client* client_modal_top(Client* client);
static void client_handle_request_decoration_mode(struct wl_listener* listener, void*);
static void client_set_urgent(Client* client);

/* function implementations */
void client_activate(Client* client)
{
    if (!client->mon || !client_surface(client)->mapped)
        return;
    selected_monitor = client->mon;
    if (!VISIBLEON(client, client->mon))
        layout_view(&(Arg){ .ui = client->tags });
    client_focus(client, 1);
}

static void client_apply_bounds(Client* client, struct wlr_box* bbox)
{
    /* set minimum possible */
    client->geom.width = MAX(1 + 2 * (int)client->bw, client->geom.width);
    client->geom.height = MAX(1 + 2 * (int)client->bw, client->geom.height);

    if (client->geom.x >= bbox->x + bbox->width)
        client->geom.x = bbox->x + bbox->width - client->geom.width;
    if (client->geom.y >= bbox->y + bbox->height)
        client->geom.y = bbox->y + bbox->height - client->geom.height;
    if (client->geom.x + client->geom.width <= bbox->x)
        client->geom.x = bbox->x;
    if (client->geom.y + client->geom.height <= bbox->y)
        client->geom.y = bbox->y;
}

static void client_apply_rules(Client* client)
{
    /* rule matching */
    client->private_text = nullptr;
    client->private_background = client->private_foreground = 0;
    uint32_t newtags = 0;
    Monitor* monitor = selected_monitor;

    const char* appid = client_get_app_id(client);
    const char* title = client_get_title(client);

    for (const Rule* rule = rules; rule < rules + rule_count; rule++) {
        if ((!rule->title || strstr(title, rule->title)) &&
            (!rule->id || strstr(appid, rule->id))) {
            client->isfloating = rule->isfloating;
            if (rule->isprivate >= 0)
                client->isprivate = rule->isprivate;
            if (rule->private_text)
                client->private_text = rule->private_text;
            if (rule->private_background)
                client->private_background = rule->private_background;
            if (rule->private_foreground)
                client->private_foreground = rule->private_foreground;
            if (rule->opacity_focus > 0)
                client->opacity_focus = rule->opacity_focus;
            if (rule->opacity_unfocus > 0)
                client->opacity = client->opacity_unfocus = rule->opacity_unfocus;
            newtags |= rule->tags;
            int monitor_index = 0;
            Monitor* candidate;
            wl_list_for_each(candidate, &monitors, link) {
                if (rule->monitor == monitor_index++)
                    monitor = candidate;
            }
        }
    }

    client->isfloating |= client_is_float_type(client);
    client_set_monitor(client, monitor, newtags);
}

static void client_handle_commit(struct wl_listener* listener, void*)
{
    Client* client = wl_container_of(listener, client, commit);

    if (client->surface.xdg->initial_commit) {
        /*
         * Get the monitor this client will be rendered on
         * Note that if the user set a rule in which the client is placed on
         * a different monitor based on its title, this will likely select
         * a wrong monitor.
         */
        client_apply_rules(client);
        if (client->mon) {
            client_set_scale(client_surface(client), client->mon->wlr_output->scale);
        }
        client_set_monitor(client, nullptr,
                           0); /* Make sure to reapply rules in client_handle_map() */

        wlr_xdg_toplevel_set_wm_capabilities(client->surface.xdg->toplevel,
                                             WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
        if (client->decoration)
            client_handle_request_decoration_mode(&client->set_decoration_mode, client->decoration);
        wlr_xdg_toplevel_set_size(client->surface.xdg->toplevel, 0, 0);
        return;
    }

    client_resize(client, client->geom, (client->isfloating && !client->isfullscreen));

    /* mark a pending resize as completed */
    if (client->resize && client->resize <= client->surface.xdg->current.configure_serial)
        client->resize = 0;

    if (client->mapwait && !client->resize)
        client_map_wait_done(client);
}

static void client_handle_popup_commit(struct wl_listener* listener, void* data)
{
    struct wlr_surface* surface = data;
    struct wlr_xdg_popup* popup = wlr_xdg_popup_try_from_wlr_surface(surface);
    LayerSurface* layer_surface = nullptr;
    Client* client = nullptr;
    int type = -1;

    if (!popup->base->initial_commit)
        return;

    type = client_from_surface(popup->base->surface, &client, &layer_surface);
    if (!popup->parent || type < 0)
        return;
    /* The parent may have been unmapped in the meantime, dropping its scene
     * tree; there is nothing to parent the popup to. */
    if (!popup->parent->data) {
        wlr_xdg_popup_destroy(popup);
        return;
    }
    popup->base->surface->data = wlr_scene_xdg_surface_create(popup->parent->data, popup->base);
    if ((layer_surface && !layer_surface->mon) || (client && !client->mon)) {
        wlr_xdg_popup_destroy(popup);
        return;
    }
    struct wlr_box box = type == LayerShell ? layer_surface->mon->m : client->mon->w;
    box.x -= (type == LayerShell ? layer_surface->scene->node.x : client->geom.x);
    box.y -= (type == LayerShell ? layer_surface->scene->node.y : client->geom.y);
    wlr_xdg_popup_unconstrain_from_box(popup, &box);
    wl_list_remove(&listener->link);
    free(listener);
}

void client_handle_new_decoration(struct wl_listener*, void* data)
{
    struct wlr_xdg_toplevel_decoration_v1* deco = data;
    Client* client = deco->toplevel->base->data;
    client->decoration = deco;

    LISTEN(&deco->events.request_mode, &client->set_decoration_mode,
           client_handle_request_decoration_mode);
    LISTEN(&deco->events.destroy, &client->destroy_decoration, client_handle_decoration_destroy);

    client_handle_request_decoration_mode(&client->set_decoration_mode, deco);
}

void client_handle_new_toplevel(struct wl_listener*, void* data)
{
    /* This event is raised when a client creates a new toplevel (application
     * window). */
    struct wlr_xdg_toplevel* toplevel = data;
    Client* client = nullptr;

    /* Allocate a Client for this surface */
    client = toplevel->base->data = util_calloc(1, sizeof(*client));
    client->surface.xdg = toplevel->base;
    client->bw = corner_border_width();
    client->opacity = client->opacity_unfocus = opacity_unfocus;
    client->opacity_focus = opacity_focus;
    client->isprivate = always_private;

    LISTEN(&toplevel->base->surface->events.commit, &client->commit, client_handle_commit);
    LISTEN(&toplevel->base->surface->events.map, &client->map, client_handle_map);
    LISTEN(&toplevel->base->surface->events.unmap, &client->unmap, client_handle_unmap);
    LISTEN(&toplevel->events.destroy, &client->destroy, client_handle_destroy);
    LISTEN(&toplevel->events.request_fullscreen, &client->fullscreen,
           client_handle_request_fullscreen);
    LISTEN(&toplevel->events.request_maximize, &client->maximize, client_handle_request_maximize);
    LISTEN(&toplevel->events.set_title, &client->set_title, client_handle_set_title);
}

void client_handle_new_popup(struct wl_listener*, void* data)
{
    /* This event is raised when a client (either xdg-shell or layer-shell)
     * creates a new popup. */
    struct wlr_xdg_popup* popup = data;
    LISTEN_STATIC(&popup->base->surface->events.commit, client_handle_popup_commit);
}

static void client_handle_decoration_destroy(struct wl_listener* listener, void*)
{
    Client* client = wl_container_of(listener, client, destroy_decoration);

    wl_list_remove(&client->destroy_decoration.link);
    wl_list_remove(&client->set_decoration_mode.link);
}

void client_handle_destroy(struct wl_listener* listener, void*)
{
    /* Called when the xdg_toplevel is destroyed. */
    Client* client = wl_container_of(listener, client, destroy);
    wl_list_remove(&client->destroy.link);
    wl_list_remove(&client->set_title.link);
    wl_list_remove(&client->fullscreen.link);
#ifdef XWAYLAND
    if (client->type != XDGShell) {
        wl_list_remove(&client->activate.link);
        wl_list_remove(&client->associate.link);
        wl_list_remove(&client->configure.link);
        wl_list_remove(&client->dissociate.link);
        wl_list_remove(&client->set_hints.link);
        wl_list_remove(&client->minimize.link);
    } else
#endif
    {
        wl_list_remove(&client->commit.link);
        wl_list_remove(&client->map.link);
        wl_list_remove(&client->unmap.link);
        wl_list_remove(&client->maximize.link);
    }
    free(client);
}

void client_focus(Client* client, int lift)
{
    struct wlr_surface* old = seat->keyboard_state.focused_surface;
    Client* old_c = nullptr;
    LayerSurface* old_l = nullptr;

    if (lock_active)
        return;

    if (client && !client_is_unmanaged(client))
        client = client_modal_top(client);

    /* Warp cursor to center of client if it is outside */
    if (lift)
        input_warp_cursor(client);

    /* Raise client in stacking order if requested */
    if (client && lift)
        wlr_scene_node_raise_to_top(&client->scene->node);

    if (client && client_surface(client) == old)
        return;

    int old_client_type;
    if ((old_client_type = client_from_surface(old, &old_c, &old_l)) == XDGShell) {
        struct wlr_xdg_popup* popup;
        struct wlr_xdg_popup* tmp;
        wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
            wlr_xdg_popup_destroy(popup);
    }

    /* Put the new client atop the focus stack and select its monitor */
    if (client && !client_is_unmanaged(client)) {
        wl_list_remove(&client->flink);
        wl_list_insert(&focus_stack, &client->flink);
        selected_monitor = client->mon;
        client->isurgent = 0;
        client->opacity = client->opacity_focus;

        /* Don't change border color if there is an exclusive focus or we are
         * handling a drag operation */
        if (!exclusive_focus && !seat->drag)
            opacity_set_border_color(client, SchemeSel);

        /* layout_tabbed() otherwise only reruns on layout_arrange() */
        if (client->mon && client->mon->lt[client->mon->sellt]->arrange == layout_tabbed &&
            !client->isfloating && !client->isfullscreen)
            layout_tabbed(client->mon);
    }

    int unused_lx;
    int unused_ly;
    /* Deactivate old client if focus is changing */
    if (old && (!client || client_surface(client) != old)) {
        /* If an overlay is focused, don't focus or activate the client,
         * but only update its position in focus_stack to render its border with
         * its color and focus it after the overlay is closed. */
        if (old_client_type == LayerShell &&
            wlr_scene_node_coords(&old_l->scene->node, &unused_lx, &unused_ly) &&
            old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
            return;
        } else if (old_c && old_c == exclusive_focus && client_wants_focus(old_c)) {
            return;
            /* Don't deactivate old client if the new one wants focus, as this
             * causes issues with winecfg and probably other clients */
        } else if (old_c && !client_is_unmanaged(old_c) &&
                   (!client || !client_wants_focus(client))) {
            opacity_set_border_color(old_c, SchemeNorm);
            client_activate_surface(old, 0);
            old_c->opacity = old_c->opacity_unfocus;
        }
    }
    bar_draw_all();

    if (!client) {
        /* With no client, all we have left is to clear focus */
        toplevel_focus(nullptr);
        wlr_seat_keyboard_notify_clear_focus(seat);
        return;
    }

    /* Change cursor surface */
    input_motion(0, nullptr, 0, 0, 0, 0);

    /* Have a client, so focus its top-level wlr_surface */
    client_notify_enter(client_surface(client), wlr_seat_get_keyboard(seat));

    /* Activate the new client */
    client_activate_surface(client_surface(client), 1);
    toplevel_focus(client);
}

/* the last focused client visible on m */
Client* client_top(Monitor* monitor)
{
    Client* client;
    wl_list_for_each(client, &focus_stack, flink) {
        if (VISIBLEON(client, monitor))
            return client;
    }
    return nullptr;
}

void client_handle_request_fullscreen(struct wl_listener* listener, void*)
{
    Client* client = wl_container_of(listener, client, fullscreen);
    client_set_fullscreen(client, client_wants_fullscreen(client));
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
    Client* parent = nullptr;
    Client* client = wl_container_of(listener, client, map);

    /* Create scene tree for this client and its border */
    client->scene = client_surface(client)->data = wlr_scene_tree_create(layers[LyrTile]);
    /* Enabled later by a call to layout_arrange() */
    wlr_scene_node_set_enabled(&client->scene->node, client_is_unmanaged(client));
    client->scene_surface =
        client->type == XDGShell
            ? wlr_scene_xdg_surface_create(client->scene, client->surface.xdg)
            : wlr_scene_subsurface_tree_create(client->scene, client_surface(client));
    client->scene->node.data = client->scene_surface->node.data = client;

    client_get_geometry(client, &client->geom);

    /* Handle unmanaged clients first so we can return prior create borders */
    if (client_is_unmanaged(client)) {
        /* Unmanaged clients always are floating */
        wlr_scene_node_reparent(&client->scene->node, layers[LyrFloat]);
        wlr_scene_node_set_position(&client->scene->node, client->geom.x, client->geom.y);
        client_set_size(client, client->geom.width, client->geom.height);
        if (client_wants_focus(client)) {
            client_focus(client, 1);
            exclusive_focus = client;
        }
        goto unset_fullscreen;
    }

    for (int i = 0; i < 4; i++) {
        client->border[i] =
            wlr_scene_rect_create(client->scene, 0, 0, (float[]){ 0.0f, 0.0f, 0.0f, 0.0f });
        client->border[i]->node.data = client;
        /* above the borders, which stop short of the curve */
        client->corner[i] = wlr_scene_buffer_create(client->scene, nullptr);
        client->corner[i]->node.data = client;
        wlr_scene_node_set_enabled(&client->corner[i]->node, 0);
    }
    /* the colour comes from here, once all four exist */
    opacity_set_border_color(client, client->isurgent ? SchemeUrg : SchemeNorm);

    client->title = wlr_scene_buffer_create(client->scene, nullptr);
    client->title->node.data = client;
    wlr_scene_node_set_enabled(&client->title->node, 0);

#ifdef INTEGRATED_BACKGROUND
    /* the frosted backdrop goes under the lot: the surface, which is what it
     * shows through, and the decorations, which opacity_decorations may fade
     * too */
    client->blur = wlr_scene_buffer_create(client->scene, nullptr);
    client->blur->node.data = client;
    wlr_scene_node_set_enabled(&client->blur->node, 0);
    wlr_scene_node_lower_to_bottom(&client->blur->node);

    /* backs the title bar alone, which is what tabs need */
    client->titleblur = wlr_scene_buffer_create(client->scene, nullptr);
    client->titleblur->node.data = client;
    wlr_scene_node_set_enabled(&client->titleblur->node, 0);
    wlr_scene_node_lower_to_bottom(&client->titleblur->node);
#endif

    /* Initialize client geometry with room for border */
    client_set_tiled(client, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
    client->geom.width += 2 * client->bw;
    client->geom.height += 2 * client->bw;

    /* Insert this client into client lists. */
    wl_list_insert(clients.prev, &client->link); /* attach at the bottom of the stack */
    wl_list_insert(&focus_stack, &client->flink);

    /* Set initial monitor, tags, floating status, and focus:
     * we always consider floating, clients that have parent and thus
     * we set the same tags and monitor as its parent.
     * If there is no parent, apply rules */
    if ((parent = client_get_parent(client))) {
        client->isfloating = 1;
        client_set_monitor(client, parent->mon, parent->tags);
    } else {
        client_apply_rules(client);
    }

    const int title_height = bar_title_height(client);
    if (title_height > 0 && client->isfloating) {
        client->geom.height += title_height;
        client->prev.height += title_height;
        client->prev.y -= title_height / 2;
        client_resize(client, client->geom, client->isfloating && !client->isfullscreen);
    }

    /* the first buffer has the client's own size, not its tile's: stay hidden
     * until the resize lands or MAPWAIT_MS passes */
    if (client->resize) {
        client->mapwait = wl_event_loop_add_timer(event_loop, client_map_wait_done, client);
        wl_event_source_timer_update(client->mapwait, MAPWAIT_MS);
        wlr_scene_node_set_enabled(&client->scene->node, 0);
    }

    bar_draw_all();
    toplevel_map(client);

unset_fullscreen:
    Monitor* monitor = client->mon ? client->mon : monitor_at(client->geom.x, client->geom.y);
    Client* other_client;
    wl_list_for_each(other_client, &clients, link) {
        if (other_client != client && other_client != parent && other_client->isfullscreen &&
            monitor == other_client->mon && (other_client->tags & client->tags))
            client_set_fullscreen(other_client, 0);
    }
}

static int client_map_wait_done(void* data)
{
    Client* client = data;

    wl_event_source_remove(client->mapwait);
    client->mapwait = nullptr;
    if (client->mon) {
        wlr_scene_node_set_enabled(&client->scene->node, VISIBLEON(client, client->mon));
        /* bar_draw_title_bar() skipped the title bar while the client was
         * hidden */
        bar_draw(client->mon);
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
    Client* client = wl_container_of(listener, client, maximize);
    if (client->surface.xdg->initialized &&
        wl_resource_get_version(client->surface.xdg->toplevel->resource) <
            XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
        wlr_xdg_surface_schedule_configure(client->surface.xdg);
}

/* innermost modal dialog of c, or c */
static Client* client_modal_top(Client* client)
{
    Client* candidate;
    wl_list_for_each(candidate, &focus_stack, flink) {
        if (candidate == client || client_is_x11(candidate) ||
            !VISIBLEON(candidate, candidate->mon) || client_get_parent(candidate) != client)
            continue;
        struct wlr_xdg_dialog_v1* dialog =
            wlr_xdg_dialog_v1_try_from_wlr_xdg_toplevel(candidate->surface.xdg->toplevel);
        if (dialog && dialog->modal)
            return client_modal_top(candidate);
    }
    return client;
}

static void client_handle_request_decoration_mode(struct wl_listener* listener, void*)
{
    Client* client = wl_container_of(listener, client, set_decoration_mode);
    if (client->surface.xdg->initialized)
        wlr_xdg_toplevel_decoration_v1_set_mode(client->decoration,
                                                WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void client_resize(Client* client, struct wlr_box geo, int interact)
{

    if (!client->mon || !client_surface(client)->mapped)
        return;

    const int title_height = bar_title_height(client);

    struct wlr_box* bbox = interact ? &screen_geometry : &client->mon->w;

    client_set_bounds(client, geo.width, geo.height);
    client->geom = geo;
    client_apply_bounds(client, bbox);

    /* Update scene-graph, including borders */
    wlr_scene_node_set_position(&client->scene->node, client->geom.x, client->geom.y);
    wlr_scene_node_set_position(&client->scene_surface->node, client->bw,
                                client->bw + title_height);
    /* the borders run between the corners, which take r off each edge */
    const int rounding = corner_client_radius(client);
    const int vertical_inset = MAX(rounding, (int)client->bw);
    wlr_scene_rect_set_size(client->border[0], MAX(client->geom.width - 2 * rounding, 0),
                            client->bw);
    wlr_scene_rect_set_size(client->border[1], MAX(client->geom.width - 2 * rounding, 0),
                            client->bw);
    wlr_scene_rect_set_size(client->border[2], client->bw,
                            MAX(client->geom.height - 2 * vertical_inset, 0));
    wlr_scene_rect_set_size(client->border[3], client->bw,
                            MAX(client->geom.height - 2 * vertical_inset, 0));
    wlr_scene_node_set_position(&client->border[0]->node, rounding, 0);
    wlr_scene_node_set_position(&client->border[1]->node, rounding,
                                client->geom.height - client->bw);
    wlr_scene_node_set_position(&client->border[2]->node, 0, vertical_inset);
    wlr_scene_node_set_position(&client->border[3]->node, client->geom.width - client->bw,
                                vertical_inset);
    corner_draw_all(client);

    /* this is a no-op if size hasn't changed */
    client->resize =
        client_set_size(client, MAX(1, client->geom.width - 2 * (int)client->bw),
                        MAX(1, client->geom.height - 2 * (int)client->bw - title_height));
    struct wlr_box clip;
    client_get_clip(client, &clip);
    clip.height -= title_height;
    wlr_scene_subsurface_tree_set_clip(&client->scene_surface->node, &clip);

    bar_place_title_bar(client);
    textinput_arrange();
}

void client_handle_ring_bell(struct wl_listener*, void* data)
{
    struct wlr_xdg_system_bell_v1_ring_event* event = data;
    Client* client = nullptr;

    if (event->surface)
        client_from_surface(event->surface, &client, nullptr);
    if (!client || client == client_top(selected_monitor))
        return;
    client_set_urgent(client);
}

void client_set_floating(Client* client, int floating)
{
    Client* parent = client_get_parent(client);
    client->isfloating = floating;
    if (!client->mon || !client_surface(client)->mapped)
        return;
    wlr_scene_node_reparent(&client->scene->node,
                            layers[client->isfullscreen || (parent && parent->isfullscreen) ? LyrFS
                                   : client->isfloating ? LyrFloat
                                                        : LyrTile]);
    layout_arrange(client->mon);
    bar_draw_all();
}

void client_set_fullscreen(Client* client, int fullscreen)
{
    client->isfullscreen = fullscreen;
    toplevel_update(client);
    if (!client->mon || !client_surface(client)->mapped)
        return;
    client->bw = fullscreen ? 0 : corner_border_width();
    client_send_fullscreen(client, fullscreen);
    wlr_scene_node_reparent(&client->scene->node, layers[client->isfullscreen ? LyrFS
                                                         : client->isfloating ? LyrFloat
                                                                              : LyrTile]);

    if (fullscreen) {
        client->prev = client->geom;
        client_resize(client, client->mon->m, 0);
    } else {
        /* restore previous size instead of arrange for floating windows since
         * client positions are set by the user and cannot be recalculated */
        client_resize(client, client->prev, 0);
    }
    layout_arrange(client->mon);
    bar_draw_all();
}

void client_set_monitor(Client* client, Monitor* monitor, uint32_t newtags)
{
    Monitor* oldmon = client->mon;

    if (oldmon == monitor)
        return;
    client->mon = monitor;
    client->prev = client->geom;

    /* Scene graph sends surface leave/enter events on move and resize */
    if (oldmon)
        layout_arrange(oldmon);
    if (monitor) {
        /* Make sure window actually overlaps with the monitor */
        client_resize(client, client->geom, 0);
        client->tags = newtags
                           ? newtags
                           : monitor->tagset[monitor->seltags]; /* assign tags of target monitor */
        client->prev.x = (monitor->w.width - client->prev.width) / 2 + monitor->m.x;
        client->prev.y = (monitor->w.height - client->prev.height) / 2 + monitor->m.y;
        client_set_fullscreen(
            client, client->isfullscreen); /* This will call layout_arrange(client->mon) */
        client_set_floating(client, client->isfloating);
    }
    toplevel_update(client);
    client_focus(client_top(selected_monitor), 1);
}

static void client_set_urgent(Client* client)
{
    client->isurgent = 1;
    bar_draw_all();

    if (client_surface(client)->mapped)
        opacity_set_border_color(client, SchemeUrg);
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
    Client* client = wl_container_of(listener, client, unmap);

    toplevel_unmap(client);
    if (client->mapwait) {
        wl_event_source_remove(client->mapwait);
        client->mapwait = nullptr;
    }
    if (client == input_grab_client) {
        input_cursor_mode = CurNormal;
        input_grab_client = nullptr;
    }

    if (client_is_unmanaged(client)) {
        if (client == exclusive_focus) {
            exclusive_focus = nullptr;
            client_focus(client_top(selected_monitor), 1);
        }
    } else {
        wl_list_remove(&client->link);
        client_set_monitor(client, nullptr, 0);
        wl_list_remove(&client->flink);
    }

    /* The wl_surface can outlive the mapping (e.g. a client holding an idle
     * inhibitor on it), so drop the back-pointer to the scene tree we are
     * about to free instead of leaving it dangling. client_handle_map() sets it
     * again on the next map. */
    if (client_surface(client))
        client_surface(client)->data = nullptr;
    wlr_scene_node_destroy(&client->scene->node);
    client->title = nullptr;
#ifdef INTEGRATED_BACKGROUND
    client->blur = nullptr;
    client->blurbuf = nullptr;
    client->titleblur = nullptr;
    client->titleblurbuf = nullptr;
#endif
    buffer_pool_drop(client->titlepool, LENGTH(client->titlepool));
    buffer_pool_drop(client->sharepool, LENGTH(client->sharepool));
    buffer_pool_drop(client->privatepool, LENGTH(client->privatepool));
    client->titlebufw = 0;
    for (int i = 0; i < 4; i++) {
        client->corner[i] = nullptr;
        buffer_pool_drop(client->cornerpool[i], LENGTH(client->cornerpool[i]));
    }
    client->cornerbufr = 0;
    bar_draw_all();
    input_motion(0, nullptr, 0, 0, 0, 0);
}

void client_handle_set_title(struct wl_listener* listener, void*)
{
    Client* client = wl_container_of(listener, client, set_title);
    toplevel_update(client);
    if (client == client_top(client->mon))
        bar_draw_all();
}

void client_handle_request_activate(struct wl_listener*, void* data)
{
    struct wlr_xdg_activation_v1_request_activate_event* event = data;
    Client* client = nullptr;
    client_from_surface(event->surface, &client, nullptr);
    if (!client || client == client_top(selected_monitor))
        return;

    /* the user clicked a notification: focus instead of marking urgent */
    if (activation_token_ours(event->token) && client->mon && client_surface(client)->mapped) {
        client_activate(client);
        return;
    }

    client_set_urgent(client);
}
