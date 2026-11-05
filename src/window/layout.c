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

    Client* client;
    wl_list_for_each(client, &clients, link) {
        if (client->mon == monitor) {
            /* client_map_wait_done() shows a client held back by
             * client_handle_map() */
            wlr_scene_node_set_enabled(&client->scene->node,
                                       VISIBLEON(client, monitor) && !client->mapwait);
            /* re-enable content layout_tabbed() may have hidden for a non-top
             * tab */
            wlr_scene_node_set_enabled(&client->scene_surface->node, 1);
            client_set_suspended(client, !VISIBLEON(client, monitor));
        }
    }

    wlr_scene_node_set_enabled(&monitor->fullscreen_bg->node,
                               (client = client_top(monitor)) && client->isfullscreen);

    snprintf(monitor->ltsymbol, LENGTH(monitor->ltsymbol), "%s",
             monitor->lt[monitor->sellt]->symbol);

    /* Keep floating clients (except fullscreen and unmanaged) above the tiles
     */
    wl_list_for_each(client, &clients, link) {
        if (client->mon != monitor || client->scene->node.parent == layers[LyrFS])
            continue;

        wlr_scene_node_reparent(&client->scene->node,
                                client->isfloating ? layers[LyrFloat] : client->scene->node.parent);
    }

    monitor->lt[monitor->sellt]->arrange(monitor);
    input_motion(0, nullptr, 0, 0, 0, 0);
    check_idle_inhibitor(nullptr);
    input_warp_cursor(client_top(selected_monitor));
}

void layout_focus_stack(const Arg* arg)
{
    /* Focus the next or previous client (in tiling order) on selected_monitor
     */
    Client *client, *sel = client_top(selected_monitor);
    if (!sel || (sel->isfullscreen && !client_has_children(sel)))
        return;
    if (arg->i > 0) {
        wl_list_for_each(client, &sel->link, link) {
            if (&client->link == &clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(client, selected_monitor))
                break; /* found it */
        }
    } else {
        wl_list_for_each_reverse(client, &sel->link, link) {
            if (&client->link == &clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(client, selected_monitor))
                break; /* found it */
        }
    }
    /* If only one client is visible on selected_monitor, then client == sel */
    client_focus(client, 1);
}

void layout_inc_master(const Arg* arg)
{
    if (!arg || !selected_monitor)
        return;
    selected_monitor->nmaster = MAX(selected_monitor->nmaster + arg->i, 0);
    layout_arrange(selected_monitor);
}

void layout_move_stack(const Arg* arg)
{
    Client *client, *sel = client_top(selected_monitor);

    if (!sel || wl_list_length(&clients) <= 1)
        return;

    if (arg->i > 0) {
        wl_list_for_each(client, &sel->link, link) {
            if (&client->link == &clients) {
                client = wl_container_of(&clients, client, link);
                break; /* wrap past the sentinel node */
            }
            if (VISIBLEON(client, selected_monitor))
                break; /* found it */
        }
    } else {
        wl_list_for_each_reverse(client, &sel->link, link) {
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

    wl_list_remove(&sel->link);
    wl_list_insert(&client->link, &sel->link);
    layout_arrange(selected_monitor);
}

void layout_resize_height(const Arg* arg)
{
    Client* client = client_top(selected_monitor);

    if (!client || !client->isfloating)
        return;
    client_resize(client,
                  (struct wlr_box){ .x = client->geom.x,
                                    .y = client->geom.y,
                                    .width = client->geom.width,
                                    .height = MAX(1, client->geom.height + arg->i) },
                  1);
}

void layout_resize_width(const Arg* arg)
{
    Client* client = client_top(selected_monitor);

    if (!client)
        return;
    if (!client->isfloating) {
        /* A tiled client has no free geometry, so widen/narrow the master
         * area instead - g0wm's equivalent of resizing a sway split. */
        layout_set_master_factor(&(Arg){ .f = arg->i > 0 ? +0.05f : -0.05f });
        return;
    }
    client_resize(client,
                  (struct wlr_box){ .x = client->geom.x,
                                    .y = client->geom.y,
                                    .width = MAX(1, client->geom.width + arg->i),
                                    .height = client->geom.height },
                  1);
}

void layout_set(const Arg* arg)
{
    if (!selected_monitor)
        return;
    if (!arg || !arg->v || arg->v != selected_monitor->lt[selected_monitor->sellt])
        selected_monitor->sellt ^= 1;
    if (arg && arg->v)
        selected_monitor->lt[selected_monitor->sellt] = (Layout*)arg->v;
    snprintf(selected_monitor->ltsymbol, LENGTH(selected_monitor->ltsymbol), "%s",
             selected_monitor->lt[selected_monitor->sellt]->symbol);
    layout_arrange(selected_monitor);
    bar_draw_selected();
}

/* arg > 1.0 will set mfact absolutely */
void layout_set_master_factor(const Arg* arg)
{
    if (!arg || !selected_monitor)
        return;
    float new_factor = arg->f < 1.0f ? arg->f + selected_monitor->mfact : arg->f - 1.0f;
    if (new_factor < 0.1 || new_factor > 0.9)
        return;
    selected_monitor->mfact = new_factor;
    layout_arrange(selected_monitor);
}

/* tag next to set in direction i, -1 at the ends */
static int layout_tag_beside(uint32_t set, int step)
{
    if (!(set & TAGMASK))
        return -1;
    int target = step > 0 ? (int)stdc_bit_width(set & TAGMASK) - 1 : (int)stdc_trailing_zeros(set);
    target += step;
    return target < 0 || target >= (int)tag_count ? -1 : target;
}

void layout_shift_tag(const Arg* arg)
{
    Client* sel = client_top(selected_monitor);
    if (!sel)
        return;
    const int target = layout_tag_beside(sel->tags, arg->i);
    if (target < 0)
        return;
    layout_tag(&(Arg){ .ui = 1u << target });
    layout_view(&(Arg){ .ui = 1u << target });
}

void layout_shift_view(const Arg* arg)
{
    if (!selected_monitor)
        return;
    const int target =
        layout_tag_beside(selected_monitor->tagset[selected_monitor->seltags], arg->i);
    if (target < 0)
        return;
    layout_view(&(Arg){ .ui = 1u << target });
}

/* Exchanges two clients in the tiling order, which is all a layout looks at. */
void layout_swap_clients(Client* first, Client* second)
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

void layout_tag(const Arg* arg)
{
    Client* sel = client_top(selected_monitor);
    if (!sel || (arg->ui & TAGMASK) == 0)
        return;

    sel->tags = arg->ui & TAGMASK;
    client_focus(client_top(selected_monitor), 1);
    layout_arrange(selected_monitor);
    bar_draw_all();
}

/* i3/sway-style tabbed layout: the group behaves as a single window, and the
 * clients' title bars are packed into its one title row as tabs. */
void layout_tabbed(Monitor* monitor)
{
    unsigned int gaps_enabled = monitor->gaps;
    int tiled_count = 0;

    Client* client;
    wl_list_for_each(client, &clients, link)
        if (VISIBLEON(client, monitor) && !client->isfloating && !client->isfullscreen)
            tiled_count++;
    if (tiled_count == 0)
        return;
    if (smart_gaps == tiled_count)
        gaps_enabled = 0;

    struct wlr_box area;
    area.x = monitor->w.x + (int)(gap_size * gaps_enabled);
    area.y = monitor->w.y + (int)(gap_size * gaps_enabled);
    area.width = monitor->w.width - 2 * (int)(gap_size * gaps_enabled);
    area.height = monitor->w.height - 2 * (int)(gap_size * gaps_enabled);

    Client* top = layout_tab_top(monitor);
    wl_list_for_each(client, &clients, link) {
        if (!VISIBLEON(client, monitor) || client->isfloating || client->isfullscreen)
            continue;
        client_resize(client, area, 0);
        /* only the top tab's contents are drawn; scene_surface rather than
         * scene, since the title bars (the tabs) hang off the latter */
        wlr_scene_node_set_enabled(&client->scene_surface->node, client == top);
    }
    snprintf(monitor->ltsymbol, LENGTH(monitor->ltsymbol), "|%d|", tiled_count);
    if (top)
        wlr_scene_node_raise_to_top(&top->scene->node);
}

Client* layout_tab_top(Monitor* monitor)
{
    Client* client;
    wl_list_for_each(client, &focus_stack, flink)
        if (VISIBLEON(client, monitor) && !client->isfloating && !client->isfullscreen)
            return client;
    return nullptr;
}

static unsigned int layout_tag_index(uint32_t tagset)
{
    return tagset ? stdc_trailing_zeros(tagset) : 0;
}

void layout_tile(Monitor* monitor)
{
    unsigned int gaps_enabled = monitor->gaps;
    int tiled_count = 0;
    Client* client;

    wl_list_for_each(client, &clients, link)
        if (VISIBLEON(client, monitor) && !client->isfloating && !client->isfullscreen)
            tiled_count++;
    if (tiled_count == 0)
        return;
    if (smart_gaps == tiled_count)
        gaps_enabled = 0;

    unsigned int master_width;
    if (tiled_count > monitor->nmaster)
        master_width =
            monitor->nmaster
                ? (int)roundf((monitor->w.width + gap_size * gaps_enabled) * monitor->mfact)
                : 0;
    else
        master_width = monitor->w.width;
    int index = 0;
    int master_y = gap_size * gaps_enabled;
    int stack_y = master_y;
    wl_list_for_each(client, &clients, link) {
        if (!VISIBLEON(client, monitor) || client->isfloating || client->isfullscreen)
            continue;
        if (index < monitor->nmaster) {
            const unsigned int remaining = MIN(tiled_count, monitor->nmaster) - index;
            const int height = MAX(1, ((int)monitor->w.height - master_y -
                                       (int)(gap_size * gaps_enabled * remaining)) /
                                          (int)remaining);
            client_resize(client,
                          (struct wlr_box){ .x = monitor->w.x + gap_size * gaps_enabled,
                                            .y = monitor->w.y + master_y,
                                            .width = master_width - 2 * gap_size * gaps_enabled,
                                            .height = height },
                          0);
            master_y += client->geom.height + gap_size * gaps_enabled;
        } else {
            const unsigned int remaining = tiled_count - index;
            const int height = MAX(
                1, ((int)monitor->w.height - stack_y - (int)(gap_size * gaps_enabled * remaining)) /
                       (int)remaining);
            client_resize(client,
                          (struct wlr_box){ .x = monitor->w.x + master_width,
                                            .y = monitor->w.y + stack_y,
                                            .width = monitor->w.width - master_width -
                                                     gap_size * gaps_enabled,
                                            .height = height },
                          0);
            stack_y += client->geom.height + gap_size * gaps_enabled;
        }
        index++;
    }
}

void layout_toggle_gaps(const Arg*)
{
    selected_monitor->gaps = !selected_monitor->gaps;
    layout_arrange(selected_monitor);
}

/* Switches to the tabbed layout passed in arg, or back to the layout that was
 * selected before it if we are already tabbed - layout_set(nullptr) flips lt[]
 * back to the other slot, which still holds it.
 *
 * Passing nullptr rather than &(Arg){0}: Arg is a union, so that initialiser
 * only covers its first member (int i) and leaves the rest of ->v as whatever
 * was on the stack, which layout_set() then stores as the layout pointer. */
void layout_toggle_tabbed(const Arg* arg)
{
    if (!selected_monitor || !arg || !arg->v)
        return;
    layout_set(selected_monitor->lt[selected_monitor->sellt]->arrange == layout_tabbed ? nullptr
                                                                                       : arg);
}

void layout_toggle_tag(const Arg* arg)
{
    Client* sel = client_top(selected_monitor);
    uint32_t newtags;
    if (!sel || !(newtags = sel->tags ^ (arg->ui & TAGMASK)))
        return;

    sel->tags = newtags;
    client_focus(client_top(selected_monitor), 1);
    layout_arrange(selected_monitor);
    bar_draw_all();
}

void layout_toggle_view(const Arg* arg)
{
    uint32_t newtagset;
    if (!(newtagset = selected_monitor ? selected_monitor->tagset[selected_monitor->seltags] ^
                                             (arg->ui & TAGMASK)
                                       : 0))
        return;

    selected_monitor->tagset[selected_monitor->seltags] = newtagset;
    client_focus(client_top(selected_monitor), 1);
    layout_arrange(selected_monitor);
    bar_draw_all();
}

void layout_view(const Arg* arg)
{

    if (!selected_monitor ||
        (arg->ui & TAGMASK) == selected_monitor->tagset[selected_monitor->seltags])
        return;

    unsigned int i = layout_tag_index(selected_monitor->tagset[selected_monitor->seltags]);
    selected_monitor->taglt[i][0] = selected_monitor->lt[0];
    selected_monitor->taglt[i][1] = selected_monitor->lt[1];
    selected_monitor->tagsellt[i] = selected_monitor->sellt;

    selected_monitor->seltags ^= 1; /* toggle sel tagset */
    if (arg->ui & TAGMASK)
        selected_monitor->tagset[selected_monitor->seltags] = arg->ui & TAGMASK;

    i = layout_tag_index(selected_monitor->tagset[selected_monitor->seltags]);
    selected_monitor->lt[0] = selected_monitor->taglt[i][0];
    selected_monitor->lt[1] = selected_monitor->taglt[i][1];
    selected_monitor->sellt = selected_monitor->tagsellt[i];

    client_focus(client_top(selected_monitor), 1);
    layout_arrange(selected_monitor);
    bar_draw_all();
}

void layout_zoom(const Arg*)
{
    Client *client, *sel = client_top(selected_monitor);

    if (!sel || !selected_monitor || sel->isfloating)
        return;

    /* Search for the first tiled window that is not sel, marking sel as
     * NULL if we pass it along the way */
    wl_list_for_each(client, &clients, link) {
        if (VISIBLEON(client, selected_monitor) && !client->isfloating) {
            if (client != sel)
                break;
            sel = nullptr;
        }
    }

    /* Return if no other tiled window was found */
    if (&client->link == &clients)
        return;

    /* If we passed sel, move client to the front; otherwise, move sel to the
     * front */
    if (!sel)
        sel = client;
    wl_list_remove(&sel->link);
    wl_list_insert(&clients, &sel->link);

    client_focus(sel, 1);
    layout_arrange(selected_monitor);
}
