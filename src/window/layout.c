/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: Nikita Ivanov
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the tiling layouts, the tag actions and the stack order
 */
#include <math.h>
#include <stdbit.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "desktop/bar/bar.h"
#include "input/pointer.h"
#include "output/monitor.h"
#include "window/client.h"
#include "window/layout.h"

/* function declarations */
static unsigned int layout_tag_index(uint32_t tagset);
static int layout_tag_beside(uint32_t set, int step);

/* function implementations */
void layout_arrange(Monitor* monitor)
{

    if (!monitor->wlr_output->enabled)
        return;

    WindowClient* client;
    wl_list_for_each(client, &clients, link) {
        if (client->monitor == monitor) {
            /* client_map_wait_done() shows a client held back by
             * client_handle_map() */
            wlr_scene_node_set_enabled(&client->scene->node,
                                       VISIBLEON(client, monitor) && !client->map_wait_timer);
            /* re-enable content layout_tabbed() may have hidden for a non-top
             * tab */
            wlr_scene_node_set_enabled(&client->scene_surface->node, 1);
            client_set_suspended(client, !VISIBLEON(client, monitor));
        }
    }

    wlr_scene_node_set_enabled(&monitor->fullscreen_background->node,
                               (client = client_top(monitor)) && client->is_fullscreen);

    snprintf(monitor->layout_symbol, LENGTH(monitor->layout_symbol), "%s",
             monitor->layouts[monitor->selected_layout_index]->symbol);

    /* Keep floating clients (except fullscreen and unmanaged) above the tiles
     */
    wl_list_for_each(client, &clients, link) {
        if (client->monitor != monitor ||
            client->scene->node.parent == layers[SCENE_LAYER_FULLSCREEN])
            continue;

        wlr_scene_node_reparent(&client->scene->node, client->is_floating
                                                          ? layers[SCENE_LAYER_FLOATING]
                                                          : client->scene->node.parent);
    }

    monitor->layouts[monitor->selected_layout_index]->arrange(monitor);
    input_motion(0, nullptr, 0, 0, 0, 0);
    check_idle_inhibitor(nullptr);
    input_warp_cursor(client_top(selected_monitor));
}

void layout_focus_stack(const ActionArg* argument)
{
    /* Focus the next or previous client (in tiling order) on selected_monitor
     */
    WindowClient *client, *selected_client = client_top(selected_monitor);
    if (!selected_client ||
        (selected_client->is_fullscreen && !client_has_children(selected_client)))
        return;
    if (argument->integer > 0) {
        wl_list_for_each(client, &selected_client->link, link) {
            if (&client->link == &clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(client, selected_monitor))
                break; /* found it */
        }
    } else {
        wl_list_for_each_reverse(client, &selected_client->link, link) {
            if (&client->link == &clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(client, selected_monitor))
                break; /* found it */
        }
    }
    /* If only one client is visible on selected_monitor, then client == sel */
    client_focus(client, 1);
}

void layout_inc_master(const ActionArg* argument)
{
    if (!argument || !selected_monitor)
        return;
    selected_monitor->master_count = MAX(selected_monitor->master_count + argument->integer, 0);
    layout_arrange(selected_monitor);
}

void layout_move_stack(const ActionArg* argument)
{
    WindowClient *client, *selected_client = client_top(selected_monitor);

    if (!selected_client || wl_list_length(&clients) <= 1)
        return;

    if (argument->integer > 0) {
        wl_list_for_each(client, &selected_client->link, link) {
            if (&client->link == &clients) {
                client = wl_container_of(&clients, client, link);
                break; /* wrap past the sentinel node */
            }
            if (VISIBLEON(client, selected_monitor))
                break; /* found it */
        }
    } else {
        wl_list_for_each_reverse(client, &selected_client->link, link) {
            if (&client->link == &clients) {
                client = wl_container_of(&clients, client, link);
                break; /* wrap past the sentinel node */
            }
            if (VISIBLEON(client, selected_monitor))
                break; /* found it */
        }
        /* backup one client */
        client = wl_container_of(client->link.prev, client, link);
    }

    wl_list_remove(&selected_client->link);
    wl_list_insert(&client->link, &selected_client->link);
    layout_arrange(selected_monitor);
}

void layout_resize_height(const ActionArg* argument)
{
    WindowClient* client = client_top(selected_monitor);

    if (!client || !client->is_floating)
        return;
    client_resize(client,
                  (struct wlr_box){ .x = client->geometry.x,
                                    .y = client->geometry.y,
                                    .width = client->geometry.width,
                                    .height = MAX(1, client->geometry.height + argument->integer) },
                  1);
}

void layout_resize_width(const ActionArg* argument)
{
    WindowClient* client = client_top(selected_monitor);

    if (!client)
        return;
    if (!client->is_floating) {
        /* A tiled client has no free geometry, so widen/narrow the master
         * area instead - g0wm's equivalent of resizing a sway split. */
        layout_set_master_factor(
            &(ActionArg){ .floating = argument->integer > 0 ? +0.05f : -0.05f });
        return;
    }
    client_resize(client,
                  (struct wlr_box){ .x = client->geometry.x,
                                    .y = client->geometry.y,
                                    .width = MAX(1, client->geometry.width + argument->integer),
                                    .height = client->geometry.height },
                  1);
}

void layout_set(const ActionArg* argument)
{
    if (!selected_monitor)
        return;
    if (!argument || !argument->pointer ||
        argument->pointer != selected_monitor->layouts[selected_monitor->selected_layout_index])
        selected_monitor->selected_layout_index ^= 1;
    if (argument && argument->pointer)
        selected_monitor->layouts[selected_monitor->selected_layout_index] =
            (WindowLayout*)argument->pointer;
    snprintf(selected_monitor->layout_symbol, LENGTH(selected_monitor->layout_symbol), "%s",
             selected_monitor->layouts[selected_monitor->selected_layout_index]->symbol);
    layout_arrange(selected_monitor);
    bar_draw_selected();
}

/* An argument above 1.0 sets master_factor absolutely. */
void layout_set_master_factor(const ActionArg* argument)
{
    if (!argument || !selected_monitor)
        return;
    float new_factor = argument->floating < 1.0f
                           ? argument->floating + selected_monitor->master_factor
                           : argument->floating - 1.0f;
    if (new_factor < 0.1 || new_factor > 0.9)
        return;
    selected_monitor->master_factor = new_factor;
    layout_arrange(selected_monitor);
}

/* tag next to set in direction i, -1 at the ends */
static int layout_tag_beside(uint32_t set, int step)
{
    if (!(set & TAGMASK))
        return -1;
    int target = step > 0 ? (int)stdc_bit_width(set & TAGMASK) - 1 : (int)stdc_trailing_zeros(set);
    target += step;
    return target < 0 || target >= (int)settings->look.tag_count ? -1 : target;
}

void layout_shift_tag(const ActionArg* argument)
{
    WindowClient* selected_client = client_top(selected_monitor);
    if (!selected_client)
        return;
    const int target = layout_tag_beside(selected_client->tags, argument->integer);
    if (target < 0)
        return;
    layout_tag(&(ActionArg){ .unsigned_integer = 1u << target });
    layout_view(&(ActionArg){ .unsigned_integer = 1u << target });
}

void layout_shift_view(const ActionArg* argument)
{
    if (!selected_monitor)
        return;
    const int target = layout_tag_beside(
        selected_monitor->tagsets[selected_monitor->selected_tagset_index], argument->integer);
    if (target < 0)
        return;
    layout_view(&(ActionArg){ .unsigned_integer = 1u << target });
}

/* Exchanges two clients in the tiling order, which is all a layout looks at. */
void layout_swap_clients(WindowClient* first, WindowClient* second)
{
    struct wl_list* first_prev = first->link.prev;
    struct wl_list* second_prev = second->link.prev;

    if (first_prev == &second->link) {
        wl_list_remove(&first->link);
        wl_list_insert(second_prev, &first->link);
    } else if (second_prev == &first->link) {
        wl_list_remove(&second->link);
        wl_list_insert(first_prev, &second->link);
    } else {
        wl_list_remove(&first->link);
        wl_list_remove(&second->link);
        wl_list_insert(second_prev, &first->link);
        wl_list_insert(first_prev, &second->link);
    }
}

void layout_tag(const ActionArg* argument)
{
    WindowClient* selected_client = client_top(selected_monitor);
    if (!selected_client || (argument->unsigned_integer & TAGMASK) == 0)
        return;

    selected_client->tags = argument->unsigned_integer & TAGMASK;
    client_focus(client_top(selected_monitor), 1);
    layout_arrange(selected_monitor);
    bar_draw_all();
}

/* i3/sway-style tabbed layout: the group behaves as a single window, and the
 * clients' title bars are packed into its one title row as tabs. */
void layout_tabbed(Monitor* monitor)
{
    unsigned int gaps_enabled = monitor->gaps_enabled;
    int tiled_count = 0;

    WindowClient* client;
    wl_list_for_each(client, &clients, link)
        if (VISIBLEON(client, monitor) && !client->is_floating && !client->is_fullscreen)
            tiled_count++;
    if (tiled_count == 0)
        return;
    if (settings->windows.smart_gaps == tiled_count)
        gaps_enabled = 0;

    struct wlr_box area;
    area.x = monitor->usable_area.x + (int)(settings->windows.gap_size * gaps_enabled);
    area.y = monitor->usable_area.y + (int)(settings->windows.gap_size * gaps_enabled);
    area.width = monitor->usable_area.width - 2 * (int)(settings->windows.gap_size * gaps_enabled);
    area.height =
        monitor->usable_area.height - 2 * (int)(settings->windows.gap_size * gaps_enabled);

    WindowClient* top_client = layout_tab_top(monitor);
    wl_list_for_each(client, &clients, link) {
        if (!VISIBLEON(client, monitor) || client->is_floating || client->is_fullscreen)
            continue;
        client_resize(client, area, 0);
        /* only the top tab's contents are drawn; scene_surface rather than
         * scene, since the title bars (the tabs) hang off the latter */
        wlr_scene_node_set_enabled(&client->scene_surface->node, client == top_client);
    }
    snprintf(monitor->layout_symbol, LENGTH(monitor->layout_symbol), "|%d|", tiled_count);
    if (top_client)
        wlr_scene_node_raise_to_top(&top_client->scene->node);
}

WindowClient* layout_tab_top(Monitor* monitor)
{
    WindowClient* client;
    wl_list_for_each(client, &focus_stack, focus_link)
        if (VISIBLEON(client, monitor) && !client->is_floating && !client->is_fullscreen)
            return client;
    return nullptr;
}

static unsigned int layout_tag_index(uint32_t tagset)
{
    return tagset ? stdc_trailing_zeros(tagset) : 0;
}

void layout_tile(Monitor* monitor)
{
    unsigned int gaps_enabled = monitor->gaps_enabled;
    int tiled_count = 0;
    WindowClient* client;

    wl_list_for_each(client, &clients, link)
        if (VISIBLEON(client, monitor) && !client->is_floating && !client->is_fullscreen)
            tiled_count++;
    if (tiled_count == 0)
        return;
    if (settings->windows.smart_gaps == tiled_count)
        gaps_enabled = 0;

    unsigned int master_width;
    if (tiled_count > monitor->master_count)
        master_width = monitor->master_count
                           ? (int)roundf((monitor->usable_area.width +
                                          settings->windows.gap_size * gaps_enabled) *
                                         monitor->master_factor)
                           : 0;
    else
        master_width = monitor->usable_area.width;
    int index = 0;
    int master_y = settings->windows.gap_size * gaps_enabled;
    int stack_y = master_y;
    wl_list_for_each(client, &clients, link) {
        if (!VISIBLEON(client, monitor) || client->is_floating || client->is_fullscreen)
            continue;
        if (index < monitor->master_count) {
            const unsigned int remaining = MIN(tiled_count, monitor->master_count) - index;
            const int height =
                MAX(1, ((int)monitor->usable_area.height - master_y -
                        (int)(settings->windows.gap_size * gaps_enabled * remaining)) /
                           (int)remaining);
            client_resize(
                client,
                (struct wlr_box){
                    .x = monitor->usable_area.x + settings->windows.gap_size * gaps_enabled,
                    .y = monitor->usable_area.y + master_y,
                    .width = master_width - 2 * settings->windows.gap_size * gaps_enabled,
                    .height = height },
                0);
            master_y += client->geometry.height + settings->windows.gap_size * gaps_enabled;
        } else {
            const unsigned int remaining = tiled_count - index;
            const int height =
                MAX(1, ((int)monitor->usable_area.height - stack_y -
                        (int)(settings->windows.gap_size * gaps_enabled * remaining)) /
                           (int)remaining);
            client_resize(client,
                          (struct wlr_box){ .x = monitor->usable_area.x + master_width,
                                            .y = monitor->usable_area.y + stack_y,
                                            .width = monitor->usable_area.width - master_width -
                                                     settings->windows.gap_size * gaps_enabled,
                                            .height = height },
                          0);
            stack_y += client->geometry.height + settings->windows.gap_size * gaps_enabled;
        }
        index++;
    }
}

void layout_toggle_gaps(const ActionArg*)
{
    selected_monitor->gaps_enabled = !selected_monitor->gaps_enabled;
    layout_arrange(selected_monitor);
}

/* Switches to the tabbed layout passed in arg, or back to the layout that was
 * selected before it if we are already tabbed - layout_set(nullptr) flips lt[]
 * back to the other slot, which still holds it.
 *
 * Passing nullptr rather than &(ActionArg){0}: ActionArg is a union, so that initialiser
 * only covers its first member (int i) and leaves the rest of ->v as whatever
 * was on the stack, which layout_set() then stores as the layout pointer. */
void layout_toggle_tabbed(const ActionArg* argument)
{
    if (!selected_monitor || !argument || !argument->pointer)
        return;
    layout_set(selected_monitor->layouts[selected_monitor->selected_layout_index]->arrange ==
                       layout_tabbed
                   ? nullptr
                   : argument);
}

void layout_toggle_tag(const ActionArg* argument)
{
    WindowClient* selected_client = client_top(selected_monitor);
    uint32_t newtags;
    if (!selected_client ||
        !(newtags = selected_client->tags ^ (argument->unsigned_integer & TAGMASK)))
        return;

    selected_client->tags = newtags;
    client_focus(client_top(selected_monitor), 1);
    layout_arrange(selected_monitor);
    bar_draw_all();
}

void layout_toggle_view(const ActionArg* argument)
{
    uint32_t newtagset;
    if (!(newtagset = selected_monitor
                          ? selected_monitor->tagsets[selected_monitor->selected_tagset_index] ^
                                (argument->unsigned_integer & TAGMASK)
                          : 0))
        return;

    selected_monitor->tagsets[selected_monitor->selected_tagset_index] = newtagset;
    client_focus(client_top(selected_monitor), 1);
    layout_arrange(selected_monitor);
    bar_draw_all();
}

void layout_view(const ActionArg* argument)
{

    if (!selected_monitor || (argument->unsigned_integer & TAGMASK) ==
                                 selected_monitor->tagsets[selected_monitor->selected_tagset_index])
        return;

    unsigned int i =
        layout_tag_index(selected_monitor->tagsets[selected_monitor->selected_tagset_index]);
    selected_monitor->tag_layouts[i][0] = selected_monitor->layouts[0];
    selected_monitor->tag_layouts[i][1] = selected_monitor->layouts[1];
    selected_monitor->tag_layout_indices[i] = selected_monitor->selected_layout_index;

    selected_monitor->selected_tagset_index ^= 1; /* toggle sel tagsets */
    if (argument->unsigned_integer & TAGMASK)
        selected_monitor->tagsets[selected_monitor->selected_tagset_index] =
            argument->unsigned_integer & TAGMASK;

    i = layout_tag_index(selected_monitor->tagsets[selected_monitor->selected_tagset_index]);
    selected_monitor->layouts[0] = selected_monitor->tag_layouts[i][0];
    selected_monitor->layouts[1] = selected_monitor->tag_layouts[i][1];
    selected_monitor->selected_layout_index = selected_monitor->tag_layout_indices[i];

    client_focus(client_top(selected_monitor), 1);
    layout_arrange(selected_monitor);
    bar_draw_all();
}

void layout_zoom(const ActionArg*)
{
    WindowClient *client, *selected_client = client_top(selected_monitor);

    if (!selected_client || !selected_monitor || selected_client->is_floating)
        return;

    /* Search for the first tiled window that is not sel, marking sel as
     * NULL if we pass it along the way */
    wl_list_for_each(client, &clients, link) {
        if (VISIBLEON(client, selected_monitor) && !client->is_floating) {
            if (client != selected_client)
                break;
            selected_client = nullptr;
        }
    }

    /* Return if no other tiled window was found */
    if (&client->link == &clients)
        return;

    /* If we passed sel, move client to the front; otherwise, move sel to the
     * front */
    if (!selected_client)
        selected_client = client;
    wl_list_remove(&selected_client->link);
    wl_list_insert(&clients, &selected_client->link);

    client_focus(selected_client, 1);
    layout_arrange(selected_monitor);
}
