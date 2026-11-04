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
static int layout_tag_beside(uint32_t set, int i);

/* function implementations */
void layout_arrange(Monitor* m)
{
    Client* c;

    if (!m->wlr_output->enabled)
        return;

    wl_list_for_each(c, &clients, link)
    {
        if (c->mon == m) {
            /* client_map_wait_done() shows a client held back by
             * client_handle_map() */
            wlr_scene_node_set_enabled(&c->scene->node,
                                       VISIBLEON(c, m) && !c->mapwait);
            /* re-enable content layout_tabbed() may have hidden for a non-top
             * tab */
            wlr_scene_node_set_enabled(&c->scene_surface->node, 1);
            client_set_suspended(c, !VISIBLEON(c, m));
        }
    }

    wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
                               (c = client_top(m)) && c->isfullscreen);

    snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "%s", m->lt[m->sellt]->symbol);

    /* Keep floating clients (except fullscreen and unmanaged) above the tiles
     */
    wl_list_for_each(c, &clients, link)
    {
        if (c->mon != m || c->scene->node.parent == layers[LyrFS])
            continue;

        wlr_scene_node_reparent(&c->scene->node,
                                c->isfloating ? layers[LyrFloat]
                                              : c->scene->node.parent);
    }

    m->lt[m->sellt]->arrange(m);
    input_motion(0, nullptr, 0, 0, 0, 0);
    check_idle_inhibitor(nullptr);
    input_warp_cursor(client_top(selected_monitor));
}

void layout_focus_stack(const Arg* arg)
{
    /* Focus the next or previous client (in tiling order) on selected_monitor
     */
    Client *c, *sel = client_top(selected_monitor);
    if (!sel || (sel->isfullscreen && !client_has_children(sel)))
        return;
    if (arg->i > 0) {
        wl_list_for_each(c, &sel->link, link)
        {
            if (&c->link == &clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(c, selected_monitor))
                break; /* found it */
        }
    } else {
        wl_list_for_each_reverse(c, &sel->link, link)
        {
            if (&c->link == &clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(c, selected_monitor))
                break; /* found it */
        }
    }
    /* If only one client is visible on selected_monitor, then c == sel */
    client_focus(c, 1);
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
    Client *c, *sel = client_top(selected_monitor);

    if (!sel || wl_list_length(&clients) <= 1)
        return;

    if (arg->i > 0) {
        wl_list_for_each(c, &sel->link, link)
        {
            if (&c->link == &clients) {
                c = wl_container_of(&clients, c, link);
                break; /* wrap past the sentinel node */
            }
            if (VISIBLEON(c, selected_monitor))
                break; /* found it */
        }
    } else {
        wl_list_for_each_reverse(c, &sel->link, link)
        {
            if (&c->link == &clients) {
                c = wl_container_of(&clients, c, link);
                break; /* wrap past the sentinel node */
            }
            if (VISIBLEON(c, selected_monitor))
                break; /* found it */
        }
        /* backup one client */
        c = wl_container_of(c->link.prev, c, link);
    }

    wl_list_remove(&sel->link);
    wl_list_insert(&c->link, &sel->link);
    layout_arrange(selected_monitor);
}

void layout_resize_height(const Arg* arg)
{
    Client* c = client_top(selected_monitor);

    if (!c || !c->isfloating)
        return;
    client_resize(c,
                  (struct wlr_box){ .x = c->geom.x,
                                    .y = c->geom.y,
                                    .width = c->geom.width,
                                    .height = MAX(1, c->geom.height + arg->i) },
                  1);
}

void layout_resize_width(const Arg* arg)
{
    Client* c = client_top(selected_monitor);

    if (!c)
        return;
    if (!c->isfloating) {
        /* A tiled client has no free geometry, so widen/narrow the master
         * area instead - g0wm's equivalent of resizing a sway split. */
        layout_set_master_factor(&(Arg){ .f = arg->i > 0 ? +0.05f : -0.05f });
        return;
    }
    client_resize(c,
                  (struct wlr_box){ .x = c->geom.x,
                                    .y = c->geom.y,
                                    .width = MAX(1, c->geom.width + arg->i),
                                    .height = c->geom.height },
                  1);
}

void layout_set(const Arg* arg)
{
    if (!selected_monitor)
        return;
    if (!arg || !arg->v ||
        arg->v != selected_monitor->lt[selected_monitor->sellt])
        selected_monitor->sellt ^= 1;
    if (arg && arg->v)
        selected_monitor->lt[selected_monitor->sellt] = (Layout*)arg->v;
    snprintf(selected_monitor->ltsymbol,
             LENGTH(selected_monitor->ltsymbol),
             "%s",
             selected_monitor->lt[selected_monitor->sellt]->symbol);
    layout_arrange(selected_monitor);
    bar_draw_selected();
}

/* arg > 1.0 will set mfact absolutely */
void layout_set_master_factor(const Arg* arg)
{
    float f;

    if (!arg || !selected_monitor)
        return;
    f = arg->f < 1.0f ? arg->f + selected_monitor->mfact : arg->f - 1.0f;
    if (f < 0.1 || f > 0.9)
        return;
    selected_monitor->mfact = f;
    layout_arrange(selected_monitor);
}

/* tag next to set in direction i, -1 at the ends */
static int layout_tag_beside(uint32_t set, int i)
{
    int t;

    if (!(set & TAGMASK))
        return -1;
    t = i > 0 ? (int)stdc_bit_width(set & TAGMASK) - 1
              : (int)stdc_trailing_zeros(set);
    t += i;
    return t < 0 || t >= (int)tag_count ? -1 : t;
}

void layout_shift_tag(const Arg* arg)
{
    Client* sel = client_top(selected_monitor);
    int t;

    if (!sel || (t = layout_tag_beside(sel->tags, arg->i)) < 0)
        return;
    layout_tag(&(Arg){ .ui = 1u << t });
    layout_view(&(Arg){ .ui = 1u << t });
}

void layout_shift_view(const Arg* arg)
{
    int t;

    if (!selected_monitor ||
        (t = layout_tag_beside(
             selected_monitor->tagset[selected_monitor->seltags], arg->i)) < 0)
        return;
    layout_view(&(Arg){ .ui = 1u << t });
}

/* Exchanges two clients in the tiling order, which is all a layout looks at. */
void layout_swap_clients(Client* a, Client* b)
{
    struct wl_list *aprev = a->link.prev, *bprev = b->link.prev;

    if (aprev == &b->link) {
        wl_list_remove(&a->link);
        wl_list_insert(bprev, &a->link);
    } else if (bprev == &a->link) {
        wl_list_remove(&b->link);
        wl_list_insert(aprev, &b->link);
    } else {
        wl_list_remove(&a->link);
        wl_list_remove(&b->link);
        wl_list_insert(bprev, &a->link);
        wl_list_insert(aprev, &b->link);
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
void layout_tabbed(Monitor* m)
{
    unsigned int e = m->gaps;
    struct wlr_box b;
    Client *c, *top;
    int n = 0;

    wl_list_for_each(c, &clients, link) if (VISIBLEON(c, m) && !c->isfloating &&
                                            !c->isfullscreen) n++;
    if (n == 0)
        return;
    if (smart_gaps == n)
        e = 0;

    b.x = m->w.x + (int)(gap_size * e);
    b.y = m->w.y + (int)(gap_size * e);
    b.width = m->w.width - 2 * (int)(gap_size * e);
    b.height = m->w.height - 2 * (int)(gap_size * e);

    top = layout_tab_top(m);
    wl_list_for_each(c, &clients, link)
    {
        if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
            continue;
        client_resize(c, b, 0);
        /* only the top tab's contents are drawn; scene_surface rather than
         * scene, since the title bars (the tabs) hang off the latter */
        wlr_scene_node_set_enabled(&c->scene_surface->node, c == top);
    }
    snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "|%d|", n);
    if (top)
        wlr_scene_node_raise_to_top(&top->scene->node);
}

Client* layout_tab_top(Monitor* m)
{
    Client* c;
    wl_list_for_each(c, &focus_stack, flink) if (VISIBLEON(c, m) &&
                                                 !c->isfloating &&
                                                 !c->isfullscreen) return c;
    return nullptr;
}

static unsigned int layout_tag_index(uint32_t tagset)
{
    return tagset ? stdc_trailing_zeros(tagset) : 0;
}

void layout_tile(Monitor* m)
{
    unsigned int r, e = m->gaps, mw;
    int h, my, ty, i, n = 0;
    Client* c;

    wl_list_for_each(c, &clients, link) if (VISIBLEON(c, m) && !c->isfloating &&
                                            !c->isfullscreen) n++;
    if (n == 0)
        return;
    if (smart_gaps == n)
        e = 0;

    if (n > m->nmaster)
        mw = m->nmaster ? (int)roundf((m->w.width + gap_size * e) * m->mfact)
                        : 0;
    else
        mw = m->w.width;
    i = 0;
    my = ty = gap_size * e;
    wl_list_for_each(c, &clients, link)
    {
        if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
            continue;
        if (i < m->nmaster) {
            r = MIN(n, m->nmaster) - i;
            h = MAX(1,
                    ((int)m->w.height - my - (int)(gap_size * e * r)) / (int)r);
            client_resize(c,
                          (struct wlr_box){ .x = m->w.x + gap_size * e,
                                            .y = m->w.y + my,
                                            .width = mw - 2 * gap_size * e,
                                            .height = h },
                          0);
            my += c->geom.height + gap_size * e;
        } else {
            r = n - i;
            h = MAX(1,
                    ((int)m->w.height - ty - (int)(gap_size * e * r)) / (int)r);
            client_resize(
                c,
                (struct wlr_box){ .x = m->w.x + mw,
                                  .y = m->w.y + ty,
                                  .width = m->w.width - mw - gap_size * e,
                                  .height = h },
                0);
            ty += c->geom.height + gap_size * e;
        }
        i++;
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
    layout_set(selected_monitor->lt[selected_monitor->sellt]->arrange ==
                       layout_tabbed
                   ? nullptr
                   : arg);
}

void layout_toggle_tag(const Arg* arg)
{
    uint32_t newtags;
    Client* sel = client_top(selected_monitor);
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
    if (!(newtagset =
              selected_monitor
                  ? selected_monitor->tagset[selected_monitor->seltags] ^
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
    unsigned int i;

    if (!selected_monitor ||
        (arg->ui & TAGMASK) ==
            selected_monitor->tagset[selected_monitor->seltags])
        return;

    i = layout_tag_index(selected_monitor->tagset[selected_monitor->seltags]);
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
    Client *c, *sel = client_top(selected_monitor);

    if (!sel || !selected_monitor || sel->isfloating)
        return;

    /* Search for the first tiled window that is not sel, marking sel as
     * NULL if we pass it along the way */
    wl_list_for_each(c, &clients, link)
    {
        if (VISIBLEON(c, selected_monitor) && !c->isfloating) {
            if (c != sel)
                break;
            sel = nullptr;
        }
    }

    /* Return if no other tiled window was found */
    if (&c->link == &clients)
        return;

    /* If we passed sel, move c to the front; otherwise, move sel to the
     * front */
    if (!sel)
        sel = c;
    wl_list_remove(&sel->link);
    wl_list_insert(&clients, &sel->link);

    client_focus(sel, 1);
    layout_arrange(selected_monitor);
}
