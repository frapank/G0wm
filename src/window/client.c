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
static void client_apply_bounds(WindowClient* client, struct wlr_box* bbox);
static void client_apply_rules(WindowClient* client);
static void client_handle_commit(struct wl_listener* listener, void*);
static void client_handle_popup_commit(struct wl_listener* listener, void* data);
static void client_handle_decoration_destroy(struct wl_listener* listener, void*);
static void client_handle_request_maximize(struct wl_listener* listener, void*);
static int client_map_wait_done(void* data);
static WindowClient* client_modal_top(WindowClient* client);
static void client_handle_request_decoration_mode(struct wl_listener* listener, void*);
static void client_set_urgent(WindowClient* client);

/* function implementations */
void client_activate(WindowClient* client)
{
    if (!client->monitor || !client_surface(client)->mapped)
        return;
    selected_monitor = client->monitor;
    if (!VISIBLEON(client, client->monitor))
        layout_view(&(ActionArg){ .unsigned_integer = client->tags });
    client_focus(client, 1);
}

static void client_apply_bounds(WindowClient* client, struct wlr_box* bbox)
{
    /* set minimum possible */
    client->geometry.width = MAX(1 + 2 * (int)client->border_width, client->geometry.width);
    client->geometry.height = MAX(1 + 2 * (int)client->border_width, client->geometry.height);

    if (client->geometry.x >= bbox->x + bbox->width)
        client->geometry.x = bbox->x + bbox->width - client->geometry.width;
    if (client->geometry.y >= bbox->y + bbox->height)
        client->geometry.y = bbox->y + bbox->height - client->geometry.height;
    if (client->geometry.x + client->geometry.width <= bbox->x)
        client->geometry.x = bbox->x;
    if (client->geometry.y + client->geometry.height <= bbox->y)
        client->geometry.y = bbox->y;
}

static void client_apply_rules(WindowClient* client)
{
    /* rule matching */
    free(client->private_text);
    client->private_text = nullptr;
    client->private_background = client->private_foreground = 0;
    uint32_t newtags = 0;
    Monitor* monitor = selected_monitor;

    const char* app_id = client_get_app_id(client);
    const char* title = client_get_title(client);

    for (const ClientRule* rule = settings->rules.client_rules;
         rule < settings->rules.client_rules + settings->rules.client_rule_count; rule++) {
        if ((!rule->title || strstr(title, rule->title)) &&
            (!rule->app_id || strstr(app_id, rule->app_id))) {
            client->is_floating = rule->is_floating;
            if (rule->is_private >= 0)
                client->is_private = rule->is_private;
            if (rule->private_text) {
                char* text = strdup(rule->private_text);
                if (!text)
                    util_die("cannot copy private text");
                free(client->private_text);
                client->private_text = text;
            }
            if (rule->private_background)
                client->private_background = rule->private_background;
            if (rule->private_foreground)
                client->private_foreground = rule->private_foreground;
            if (rule->focused_opacity > 0)
                client->focused_opacity = rule->focused_opacity;
            if (rule->unfocused_opacity > 0)
                client->opacity = client->unfocused_opacity = rule->unfocused_opacity;
            newtags |= rule->tags;
            int monitor_index = 0;
            Monitor* candidate;
            wl_list_for_each(candidate, &monitors, link) {
                if (rule->monitor_index == monitor_index++)
                    monitor = candidate;
            }
        }
    }

    client->is_floating |= client_is_float_type(client);
    client_set_monitor(client, monitor, newtags);
}

static void client_handle_commit(struct wl_listener* listener, void*)
{
    WindowClient* client = wl_container_of(listener, client, commit);

    if (client->surface.xdg->initial_commit) {
        /*
         * Get the monitor this client will be rendered on
         * Note that if the user set a rule in which the client is placed on
         * a different monitor based on its title, this will likely select
         * a wrong monitor.
         */
        client_apply_rules(client);
        if (client->monitor) {
            client_set_scale(client_surface(client), client->monitor->wlr_output->scale);
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

    client_resize(client, client->geometry, (client->is_floating && !client->is_fullscreen));

    /* mark a pending resize as completed */
    if (client->pending_resize_serial &&
        client->pending_resize_serial <= client->surface.xdg->current.configure_serial)
        client->pending_resize_serial = 0;

    if (client->map_wait_timer && !client->pending_resize_serial)
        client_map_wait_done(client);
}

static void client_handle_popup_commit(struct wl_listener* listener, void* data)
{
    struct wlr_surface* surface = data;
    struct wlr_xdg_popup* popup = wlr_xdg_popup_try_from_wlr_surface(surface);
    LayerSurface* layer_surface = nullptr;
    WindowClient* client = nullptr;
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
    if ((layer_surface && !layer_surface->monitor) || (client && !client->monitor)) {
        wlr_xdg_popup_destroy(popup);
        return;
    }
    struct wlr_box box = type == SURFACE_TYPE_LAYER_SHELL ? layer_surface->monitor->geometry
                                                          : client->monitor->usable_area;
    box.x -= (type == SURFACE_TYPE_LAYER_SHELL ? layer_surface->scene->node.x : client->geometry.x);
    box.y -= (type == SURFACE_TYPE_LAYER_SHELL ? layer_surface->scene->node.y : client->geometry.y);
    wlr_xdg_popup_unconstrain_from_box(popup, &box);
    wl_list_remove(&listener->link);
    free(listener);
}

void client_handle_new_decoration(struct wl_listener*, void* data)
{
    struct wlr_xdg_toplevel_decoration_v1* deco = data;
    WindowClient* client = deco->toplevel->base->data;
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
    WindowClient* client = nullptr;

    /* Allocate a WindowClient for this surface */
    client = toplevel->base->data = util_calloc(1, sizeof(*client));
    client->surface.xdg = toplevel->base;
    client->border_width = corner_border_width();
    client->opacity = client->unfocused_opacity = settings->opacity.unfocus;
    client->focused_opacity = settings->opacity.focus;
    client->is_private = settings->windows.always_private;

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
    WindowClient* client = wl_container_of(listener, client, destroy_decoration);

    wl_list_remove(&client->destroy_decoration.link);
    wl_list_remove(&client->set_decoration_mode.link);
}

void client_handle_destroy(struct wl_listener* listener, void*)
{
    /* Called when the xdg_toplevel is destroyed. */
    WindowClient* client = wl_container_of(listener, client, destroy);
    wl_list_remove(&client->destroy.link);
    wl_list_remove(&client->set_title.link);
    wl_list_remove(&client->fullscreen.link);
#ifdef XWAYLAND
    if (client->type != SURFACE_TYPE_XDG_SHELL) {
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
    free(client->private_text);
    free(client->last_title);
    free(client);
}

void client_focus(WindowClient* client, int lift)
{
    struct wlr_surface* previous_surface = seat->keyboard_state.focused_surface;
    WindowClient* old_c = nullptr;
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

    if (client && client_surface(client) == previous_surface)
        return;

    int old_client_type;
    if ((old_client_type = client_from_surface(previous_surface, &old_c, &old_l)) ==
        SURFACE_TYPE_XDG_SHELL) {
        struct wlr_xdg_popup* popup;
        struct wlr_xdg_popup* tmp;
        wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
            wlr_xdg_popup_destroy(popup);
    }

    /* Put the new client atop the focus stack and select its monitor */
    if (client && !client_is_unmanaged(client)) {
        wl_list_remove(&client->focus_link);
        wl_list_insert(&focus_stack, &client->focus_link);
        selected_monitor = client->monitor;
        client->is_urgent = 0;
        client->opacity = client->focused_opacity;

        /* Don't change border color if there is an exclusive focus or we are
         * handling a drag operation */
        if (!exclusive_focus && !seat->drag)
            opacity_set_border_color(client, COLOR_SCHEME_SELECTED);

        /* layout_tabbed() otherwise only reruns on layout_arrange() */
        if (client->monitor &&
            client->monitor->layouts[client->monitor->selected_layout_index]->arrange ==
                layout_tabbed &&
            !client->is_floating && !client->is_fullscreen)
            layout_tabbed(client->monitor);
    }

    int unused_lx;
    int unused_ly;
    /* Deactivate old client if focus is changing */
    if (previous_surface && (!client || client_surface(client) != previous_surface)) {
        /* If an overlay is focused, don't focus or activate the client,
         * but only update its position in focus_stack to render its border with
         * its color and focus it after the overlay is closed. */
        if (old_client_type == SURFACE_TYPE_LAYER_SHELL &&
            wlr_scene_node_coords(&old_l->scene->node, &unused_lx, &unused_ly) &&
            old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
            return;
        } else if (old_c && old_c == exclusive_focus && client_wants_focus(old_c)) {
            return;
            /* Don't deactivate old client if the new one wants focus, as this
             * causes issues with winecfg and probably other clients */
        } else if (old_c && !client_is_unmanaged(old_c) &&
                   (!client || !client_wants_focus(client))) {
            opacity_set_border_color(old_c, COLOR_SCHEME_NORMAL);
            client_activate_surface(previous_surface, 0);
            old_c->opacity = old_c->unfocused_opacity;
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
WindowClient* client_top(Monitor* monitor)
{
    WindowClient* client;
    wl_list_for_each(client, &focus_stack, focus_link) {
        if (VISIBLEON(client, monitor))
            return client;
    }
    return nullptr;
}

void client_handle_request_fullscreen(struct wl_listener* listener, void*)
{
    WindowClient* client = wl_container_of(listener, client, fullscreen);
    client_set_fullscreen(client, client_wants_fullscreen(client));
}

void client_kill(const ActionArg*)
{
    WindowClient* selected_client = client_top(selected_monitor);
    if (selected_client)
        client_send_close(selected_client);
}

void client_handle_map(struct wl_listener* listener, void*)
{
    /* Called when the surface is mapped, or ready to display on-screen. */
    WindowClient* parent = nullptr;
    WindowClient* client = wl_container_of(listener, client, map);

    /* Create scene tree for this client and its border */
    client->scene = client_surface(client)->data = wlr_scene_tree_create(layers[SCENE_LAYER_TILED]);
    /* Enabled later by a call to layout_arrange() */
    wlr_scene_node_set_enabled(&client->scene->node, client_is_unmanaged(client));
    client->scene_surface =
        client->type == SURFACE_TYPE_XDG_SHELL
            ? wlr_scene_xdg_surface_create(client->scene, client->surface.xdg)
            : wlr_scene_subsurface_tree_create(client->scene, client_surface(client));
    client->scene->node.data = client->scene_surface->node.data = client;

    client_get_geometry(client, &client->geometry);

    /* Handle unmanaged clients first so we can return prior create borders */
    if (client_is_unmanaged(client)) {
        /* Unmanaged clients always are floating */
        wlr_scene_node_reparent(&client->scene->node, layers[SCENE_LAYER_FLOATING]);
        wlr_scene_node_set_position(&client->scene->node, client->geometry.x, client->geometry.y);
        client_set_size(client, client->geometry.width, client->geometry.height);
        if (client_wants_focus(client)) {
            client_focus(client, 1);
            exclusive_focus = client;
        }
        goto unset_fullscreen;
    }

    for (int i = 0; i < 4; i++) {
        client->border_rects[i] =
            wlr_scene_rect_create(client->scene, 0, 0, (float[]){ 0.0f, 0.0f, 0.0f, 0.0f });
        client->border_rects[i]->node.data = client;
        /* above the borders, which stop short of the curve */
        client->corner_buffers[i] = wlr_scene_buffer_create(client->scene, nullptr);
        client->corner_buffers[i]->node.data = client;
        wlr_scene_node_set_enabled(&client->corner_buffers[i]->node, 0);
    }
    /* the colour comes from here, once all four exist */
    opacity_set_border_color(client, client->is_urgent ? COLOR_SCHEME_URGENT : COLOR_SCHEME_NORMAL);

    client->title_scene_buffer = wlr_scene_buffer_create(client->scene, nullptr);
    client->title_scene_buffer->node.data = client;
    wlr_scene_node_set_enabled(&client->title_scene_buffer->node, 0);

#ifdef INTEGRATED_BACKGROUND
    /* the frosted backdrop goes under the lot: the surface, which is what it
     * shows through, and the decorations, which opacity.decorations may fade
     * too */
    client->blur_scene_buffer = wlr_scene_buffer_create(client->scene, nullptr);
    client->blur_scene_buffer->node.data = client;
    wlr_scene_node_set_enabled(&client->blur_scene_buffer->node, 0);
    wlr_scene_node_lower_to_bottom(&client->blur_scene_buffer->node);

    /* backs the title bar alone, which is what tabs need */
    client->title_blur_scene_buffer = wlr_scene_buffer_create(client->scene, nullptr);
    client->title_blur_scene_buffer->node.data = client;
    wlr_scene_node_set_enabled(&client->title_blur_scene_buffer->node, 0);
    wlr_scene_node_lower_to_bottom(&client->title_blur_scene_buffer->node);
#endif

    /* Initialize client geometry with room for border */
    client_set_tiled(client, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
    client->geometry.width += 2 * client->border_width;
    client->geometry.height += 2 * client->border_width;

    /* Insert this client into client lists. */
    wl_list_insert(clients.prev, &client->link); /* attach at the bottom of the stack */
    wl_list_insert(&focus_stack, &client->focus_link);

    /* Set initial monitor, tags, floating status, and focus:
     * we always consider floating, clients that have parent and thus
     * we set the same tags and monitor as its parent.
     * If there is no parent, apply rules */
    if ((parent = client_get_parent(client))) {
        client->is_floating = 1;
        client_set_monitor(client, parent->monitor, parent->tags);
    } else {
        client_apply_rules(client);
    }

    const int title_height = bar_title_height(client);
    if (title_height > 0 && client->is_floating) {
        client->geometry.height += title_height;
        client->restore_geometry.height += title_height;
        client->restore_geometry.y -= title_height / 2;
        client_resize(client, client->geometry, client->is_floating && !client->is_fullscreen);
    }

    /* the first buffer has the client's own size, not its tile's: stay hidden
     * until the resize lands or MAPWAIT_MS passes */
    if (client->pending_resize_serial) {
        client->map_wait_timer = wl_event_loop_add_timer(event_loop, client_map_wait_done, client);
        wl_event_source_timer_update(client->map_wait_timer, MAPWAIT_MS);
        wlr_scene_node_set_enabled(&client->scene->node, 0);
    }

    bar_draw_all();
    toplevel_map(client);

unset_fullscreen:
    Monitor* monitor =
        client->monitor ? client->monitor : monitor_at(client->geometry.x, client->geometry.y);
    WindowClient* other_client;
    wl_list_for_each(other_client, &clients, link) {
        if (other_client != client && other_client != parent && other_client->is_fullscreen &&
            monitor == other_client->monitor && (other_client->tags & client->tags))
            client_set_fullscreen(other_client, 0);
    }
}

static int client_map_wait_done(void* data)
{
    WindowClient* client = data;

    wl_event_source_remove(client->map_wait_timer);
    client->map_wait_timer = nullptr;
    if (client->monitor) {
        wlr_scene_node_set_enabled(&client->scene->node, VISIBLEON(client, client->monitor));
        /* bar_draw_title_bar() skipped the title bar while the client was
         * hidden */
        bar_draw(client->monitor);
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
    WindowClient* client = wl_container_of(listener, client, maximize);
    if (client->surface.xdg->initialized &&
        wl_resource_get_version(client->surface.xdg->toplevel->resource) <
            XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
        wlr_xdg_surface_schedule_configure(client->surface.xdg);
}

/* innermost modal dialog of c, or c */
static WindowClient* client_modal_top(WindowClient* client)
{
    WindowClient* candidate;
    wl_list_for_each(candidate, &focus_stack, focus_link) {
        if (candidate == client || client_is_x11(candidate) ||
            !VISIBLEON(candidate, candidate->monitor) || client_get_parent(candidate) != client)
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
    WindowClient* client = wl_container_of(listener, client, set_decoration_mode);
    if (client->surface.xdg->initialized)
        wlr_xdg_toplevel_decoration_v1_set_mode(client->decoration,
                                                WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void client_resize(WindowClient* client, struct wlr_box geometry, int interact)
{

    if (!client->monitor || !client_surface(client)->mapped)
        return;

    const int title_height = bar_title_height(client);

    struct wlr_box* bbox = interact ? &screen_geometry : &client->monitor->usable_area;

    client_set_bounds(client, geometry.width, geometry.height);
    client->geometry = geometry;
    client_apply_bounds(client, bbox);

    /* Update scene-graph, including borders */
    wlr_scene_node_set_position(&client->scene->node, client->geometry.x, client->geometry.y);
    wlr_scene_node_set_position(&client->scene_surface->node, client->border_width,
                                client->border_width + title_height);
    /* the borders run between the corners, which take r off each edge */
    const int rounding = corner_client_radius(client);
    const int vertical_inset = MAX(rounding, (int)client->border_width);
    wlr_scene_rect_set_size(client->border_rects[0], MAX(client->geometry.width - 2 * rounding, 0),
                            client->border_width);
    wlr_scene_rect_set_size(client->border_rects[1], MAX(client->geometry.width - 2 * rounding, 0),
                            client->border_width);
    wlr_scene_rect_set_size(client->border_rects[2], client->border_width,
                            MAX(client->geometry.height - 2 * vertical_inset, 0));
    wlr_scene_rect_set_size(client->border_rects[3], client->border_width,
                            MAX(client->geometry.height - 2 * vertical_inset, 0));
    wlr_scene_node_set_position(&client->border_rects[0]->node, rounding, 0);
    wlr_scene_node_set_position(&client->border_rects[1]->node, rounding,
                                client->geometry.height - client->border_width);
    wlr_scene_node_set_position(&client->border_rects[2]->node, 0, vertical_inset);
    wlr_scene_node_set_position(&client->border_rects[3]->node,
                                client->geometry.width - client->border_width, vertical_inset);
    corner_draw_all(client);

    /* this is a no-op if size hasn't changed */
    client->pending_resize_serial = client_set_size(
        client, MAX(1, client->geometry.width - 2 * (int)client->border_width),
        MAX(1, client->geometry.height - 2 * (int)client->border_width - title_height));
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
    WindowClient* client = nullptr;

    if (event->surface)
        client_from_surface(event->surface, &client, nullptr);
    if (!client || client == client_top(selected_monitor))
        return;
    client_set_urgent(client);
}

void client_set_floating(WindowClient* client, int floating)
{
    WindowClient* parent = client_get_parent(client);
    client->is_floating = floating;
    if (!client->monitor || !client_surface(client)->mapped)
        return;
    wlr_scene_node_reparent(
        &client->scene->node,
        layers[client->is_fullscreen || (parent && parent->is_fullscreen) ? SCENE_LAYER_FULLSCREEN
               : client->is_floating                                      ? SCENE_LAYER_FLOATING
                                                                          : SCENE_LAYER_TILED]);
    layout_arrange(client->monitor);
    bar_draw_all();
}

void client_set_fullscreen(WindowClient* client, int fullscreen)
{
    client->is_fullscreen = fullscreen;
    toplevel_update(client);
    if (!client->monitor || !client_surface(client)->mapped)
        return;
    client->border_width = fullscreen ? 0 : corner_border_width();
    client_send_fullscreen(client, fullscreen);
    wlr_scene_node_reparent(&client->scene->node,
                            layers[client->is_fullscreen ? SCENE_LAYER_FULLSCREEN
                                   : client->is_floating ? SCENE_LAYER_FLOATING
                                                         : SCENE_LAYER_TILED]);

    if (fullscreen) {
        client->restore_geometry = client->geometry;
        client_resize(client, client->monitor->geometry, 0);
    } else {
        /* restore previous size instead of arrange for floating windows since
         * client positions are set by the user and cannot be recalculated */
        client_resize(client, client->restore_geometry, 0);
    }
    layout_arrange(client->monitor);
    bar_draw_all();
}

void client_set_monitor(WindowClient* client, Monitor* monitor, uint32_t newtags)
{
    Monitor* oldmon = client->monitor;

    if (oldmon == monitor)
        return;
    client->monitor = monitor;
    client->restore_geometry = client->geometry;

    /* Scene graph sends surface leave/enter events on move and resize */
    if (oldmon)
        layout_arrange(oldmon);
    if (monitor) {
        /* Make sure window actually overlaps with the monitor */
        client_resize(client, client->geometry, 0);
        client->tags =
            newtags
                ? newtags
                : monitor
                      ->tagsets[monitor->selected_tagset_index]; /* assign tags of target monitor */
        client->restore_geometry.x =
            (monitor->usable_area.width - client->restore_geometry.width) / 2 + monitor->geometry.x;
        client->restore_geometry.y =
            (monitor->usable_area.height - client->restore_geometry.height) / 2 +
            monitor->geometry.y;
        client_set_fullscreen(
            client, client->is_fullscreen); /* This will call layout_arrange(client->monitor) */
        client_set_floating(client, client->is_floating);
    }
    toplevel_update(client);
    client_focus(client_top(selected_monitor), 1);
}

static void client_set_urgent(WindowClient* client)
{
    client->is_urgent = 1;
    bar_draw_all();

    if (client_surface(client)->mapped)
        opacity_set_border_color(client, COLOR_SCHEME_URGENT);
}

void client_toggle_floating(const ActionArg*)
{
    WindowClient* selected_client = client_top(selected_monitor);
    /* return if fullscreen */
    if (selected_client && !selected_client->is_fullscreen)
        client_set_floating(selected_client, !selected_client->is_floating);
}

void client_toggle_fullscreen(const ActionArg*)
{
    WindowClient* selected_client = client_top(selected_monitor);
    if (selected_client)
        client_set_fullscreen(selected_client, !selected_client->is_fullscreen);
}

void client_handle_unmap(struct wl_listener* listener, void*)
{
    /* Called when the surface is unmapped, and should no longer be shown. */
    WindowClient* client = wl_container_of(listener, client, unmap);

    toplevel_unmap(client);
    if (client->map_wait_timer) {
        wl_event_source_remove(client->map_wait_timer);
        client->map_wait_timer = nullptr;
    }
    if (client == input_grab_client) {
        input_cursor_mode = CURSOR_MODE_NORMAL;
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
        wl_list_remove(&client->focus_link);
    }

    /* The wl_surface can outlive the mapping (e.g. a client holding an idle
     * inhibitor on it), so drop the back-pointer to the scene tree we are
     * about to free instead of leaving it dangling. client_handle_map() sets it
     * again on the next map. */
    if (client_surface(client))
        client_surface(client)->data = nullptr;
    wlr_scene_node_destroy(&client->scene->node);
    client->title_scene_buffer = nullptr;
#ifdef INTEGRATED_BACKGROUND
    client->blur_scene_buffer = nullptr;
    client->blur_buffer = nullptr;
    client->title_blur_scene_buffer = nullptr;
    client->title_blur_buffer = nullptr;
#endif
    buffer_pool_drop(client->title_buffer_pool, LENGTH(client->title_buffer_pool));
    buffer_pool_drop(client->capture_buffer_pool, LENGTH(client->capture_buffer_pool));
    buffer_pool_drop(client->privacy_buffer_pool, LENGTH(client->privacy_buffer_pool));
    client->title_buffer_width_px = 0;
    for (int i = 0; i < 4; i++) {
        client->corner_buffers[i] = nullptr;
        buffer_pool_drop(client->corner_buffer_pool[i], LENGTH(client->corner_buffer_pool[i]));
    }
    client->cached_corner_radius = 0;
    bar_draw_all();
    input_motion(0, nullptr, 0, 0, 0, 0);
}

void client_handle_set_title(struct wl_listener* listener, void*)
{
    WindowClient* client = wl_container_of(listener, client, set_title);
    const char* title = client_get_title(client);
    if (client->last_title && !strcmp(client->last_title, title))
        return;
    char* copy = strdup(title);
    if (!copy)
        util_die("failed to copy window title");
    free(client->last_title);
    client->last_title = copy;
    toplevel_update(client);
    if (!client->scene || !client->scene->node.enabled)
        return;
    client->title_update_pending = true;
    bar_schedule_title_update(client->monitor);
    if (settings->bar.single_monitor && client->monitor == selected_monitor &&
        client == client_top(client->monitor))
        bar_schedule_title_update(bar_monitor());
}

void client_handle_request_activate(struct wl_listener*, void* data)
{
    struct wlr_xdg_activation_v1_request_activate_event* event = data;
    WindowClient* client = nullptr;
    client_from_surface(event->surface, &client, nullptr);
    if (!client || client == client_top(selected_monitor))
        return;

    /* the user clicked a notification: focus instead of marking urgent */
    if (activation_token_ours(event->token) && client->monitor && client_surface(client)->mapped) {
        client_activate(client);
        return;
    }

    client_set_urgent(client);
}
