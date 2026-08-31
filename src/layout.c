/*
 * See LICENSE file for copyright and license details.
 *
 * the tiling layouts, the tag actions and the stack order
 */
#include "g0wm.h"

/* function declarations */
static unsigned int tagindex(uint32_t tagset);

/* function implementations */
void arrange(Monitor* m)
{
    Client* c;

    if (!m->wlr_output->enabled)
        return;

    wl_list_for_each(c, &clients, link)
    {
        if (c->mon == m) {
            wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m));
            /* re-enable content tabbed() may have hidden for a non-top tab */
            wlr_scene_node_set_enabled(&c->scene_surface->node, 1);
            client_set_suspended(c, !VISIBLEON(c, m));
        }
    }

    wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
                               (c = focustop(m)) && c->isfullscreen);

    snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "%s", m->lt[m->sellt]->symbol);

    /* We move all clients (except fullscreen and unmanaged) to LyrTile while
     * in floating layout to avoid "real" floating clients be always on top */
    wl_list_for_each(c, &clients, link)
    {
        if (c->mon != m || c->scene->node.parent == layers[LyrFS])
            continue;

        wlr_scene_node_reparent(&c->scene->node,
                                (!m->lt[m->sellt]->arrange && c->isfloating)
                                    ? layers[LyrTile]
                                : (m->lt[m->sellt]->arrange && c->isfloating)
                                    ? layers[LyrFloat]
                                    : c->scene->node.parent);
    }

    if (m->lt[m->sellt]->arrange)
        m->lt[m->sellt]->arrange(m);
    motionnotify(0, NULL, 0, 0, 0, 0);
    checkidleinhibitor(NULL);
    warpcursor(focustop(selmon));
}

void focusstack(const Arg* arg)
{
    /* Focus the next or previous client (in tiling order) on selmon */
    Client *c, *sel = focustop(selmon);
    if (!sel || (sel->isfullscreen && !client_has_children(sel)))
        return;
    if (arg->i > 0) {
        wl_list_for_each(c, &sel->link, link)
        {
            if (&c->link == &clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(c, selmon))
                break; /* found it */
        }
    } else {
        wl_list_for_each_reverse(c, &sel->link, link)
        {
            if (&c->link == &clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(c, selmon))
                break; /* found it */
        }
    }
    /* If only one client is visible on selmon, then c == sel */
    focusclient(c, 1);
}

void incnmaster(const Arg* arg)
{
    if (!arg || !selmon)
        return;
    selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
    arrange(selmon);
}

void monocle(Monitor* m)
{
    Client* c;
    int n = 0;

    wl_list_for_each(c, &clients, link)
    {
        if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
            continue;
        resize(c, m->w, 0);
        n++;
    }
    if (n)
        snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "[%d]", n);
    if ((c = focustop(m)))
        wlr_scene_node_raise_to_top(&c->scene->node);
}

void movestack(const Arg* arg)
{
    Client *c, *sel = focustop(selmon);

    if (!sel || wl_list_length(&clients) <= 1)
        return;

    if (arg->i > 0) {
        wl_list_for_each(c, &sel->link, link)
        {
            if (&c->link == &clients) {
                c = wl_container_of(&clients, c, link);
                break; /* wrap past the sentinel node */
            }
            if (VISIBLEON(c, selmon))
                break; /* found it */
        }
    } else {
        wl_list_for_each_reverse(c, &sel->link, link)
        {
            if (&c->link == &clients) {
                c = wl_container_of(&clients, c, link);
                break; /* wrap past the sentinel node */
            }
            if (VISIBLEON(c, selmon))
                break; /* found it */
        }
        /* backup one client */
        c = wl_container_of(c->link.prev, c, link);
    }

    wl_list_remove(&sel->link);
    wl_list_insert(&c->link, &sel->link);
    arrange(selmon);
}

void resizeheight(const Arg* arg)
{
    Client* c = focustop(selmon);

    if (!c || !c->isfloating)
        return;
    resize(c,
           (struct wlr_box){ .x = c->geom.x,
                             .y = c->geom.y,
                             .width = c->geom.width,
                             .height = MAX(1, c->geom.height + arg->i) },
           1);
}

void resizewidth(const Arg* arg)
{
    Client* c = focustop(selmon);

    if (!c)
        return;
    if (!c->isfloating) {
        /* A tiled client has no free geometry, so widen/narrow the master
         * area instead - g0wm's equivalent of resizing a sway split. */
        setmfact(&(Arg){ .f = arg->i > 0 ? +0.05f : -0.05f });
        return;
    }
    resize(c,
           (struct wlr_box){ .x = c->geom.x,
                             .y = c->geom.y,
                             .width = MAX(1, c->geom.width + arg->i),
                             .height = c->geom.height },
           1);
}

void setlayout(const Arg* arg)
{
    if (!selmon)
        return;
    if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
        selmon->sellt ^= 1;
    if (arg && arg->v)
        selmon->lt[selmon->sellt] = (Layout*)arg->v;
    snprintf(selmon->ltsymbol,
             LENGTH(selmon->ltsymbol),
             "%s",
             selmon->lt[selmon->sellt]->symbol);
    arrange(selmon);
    drawselbar();
}

/* arg > 1.0 will set mfact absolutely */
void setmfact(const Arg* arg)
{
    float f;

    if (!arg || !selmon || !selmon->lt[selmon->sellt]->arrange)
        return;
    f = arg->f < 1.0f ? arg->f + selmon->mfact : arg->f - 1.0f;
    if (f < 0.1 || f > 0.9)
        return;
    selmon->mfact = f;
    arrange(selmon);
}

/* Exchanges two clients in the tiling order, which is all a layout looks at. */
void swapclients(Client* a, Client* b)
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

void tag(const Arg* arg)
{
    Client* sel = focustop(selmon);
    if (!sel || (arg->ui & TAGMASK) == 0)
        return;

    sel->tags = arg->ui & TAGMASK;
    focusclient(focustop(selmon), 1);
    arrange(selmon);
    drawbars();
}

/* i3/sway-style tabbed layout: the group behaves as a single window, and the
 * clients' title bars are packed into its one title row as tabs. */
void tabbed(Monitor* m)
{
    unsigned int e = m->gaps;
    struct wlr_box b;
    Client *c, *top;
    int n = 0;

    wl_list_for_each(c, &clients, link) if (VISIBLEON(c, m) && !c->isfloating &&
                                            !c->isfullscreen) n++;
    if (n == 0)
        return;
    if (smartgaps == n)
        e = 0;

    b.x = m->w.x + (int)(gappx * e);
    b.y = m->w.y + (int)(gappx * e);
    b.width = m->w.width - 2 * (int)(gappx * e);
    b.height = m->w.height - 2 * (int)(gappx * e);

    top = tabtop(m);
    wl_list_for_each(c, &clients, link)
    {
        if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
            continue;
        resize(c, b, 0);
        /* only the top tab's contents are drawn; scene_surface rather than
         * scene, since the title bars (the tabs) hang off the latter */
        wlr_scene_node_set_enabled(&c->scene_surface->node, c == top);
    }
    snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "|%d|", n);
    if (top)
        wlr_scene_node_raise_to_top(&top->scene->node);
}

Client* tabtop(Monitor* m)
{
    Client* c;
    wl_list_for_each(c, &fstack, flink) if (VISIBLEON(c, m) && !c->isfloating &&
                                            !c->isfullscreen) return c;
    return NULL;
}

static unsigned int tagindex(uint32_t tagset)
{
    return tagset ? (unsigned int)__builtin_ctz(tagset) : 0;
}

void tile(Monitor* m)
{
    unsigned int r, e = m->gaps, mw;
    int h, my, ty, i, n = 0;
    Client* c;

    wl_list_for_each(c, &clients, link) if (VISIBLEON(c, m) && !c->isfloating &&
                                            !c->isfullscreen) n++;
    if (n == 0)
        return;
    if (smartgaps == n)
        e = 0;

    if (n > m->nmaster)
        mw = m->nmaster ? (int)roundf((m->w.width + gappx * e) * m->mfact) : 0;
    else
        mw = m->w.width;
    i = 0;
    my = ty = gappx * e;
    wl_list_for_each(c, &clients, link)
    {
        if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
            continue;
        if (i < m->nmaster) {
            r = MIN(n, m->nmaster) - i;
            h = MAX(1, ((int)m->w.height - my - (int)(gappx * e * r)) / (int)r);
            resize(c,
                   (struct wlr_box){ .x = m->w.x + gappx * e,
                                     .y = m->w.y + my,
                                     .width = mw - 2 * gappx * e,
                                     .height = h },
                   0);
            my += c->geom.height + gappx * e;
        } else {
            r = n - i;
            h = MAX(1, ((int)m->w.height - ty - (int)(gappx * e * r)) / (int)r);
            resize(c,
                   (struct wlr_box){ .x = m->w.x + mw,
                                     .y = m->w.y + ty,
                                     .width = m->w.width - mw - gappx * e,
                                     .height = h },
                   0);
            ty += c->geom.height + gappx * e;
        }
        i++;
    }
}

void togglegaps(const Arg* arg)
{
    selmon->gaps = !selmon->gaps;
    arrange(selmon);
}

/* Switches to the tabbed layout passed in arg, or back to the layout that was
 * selected before it if we are already tabbed - setlayout(NULL) flips lt[] back
 * to the other slot, which still holds it.
 *
 * Passing NULL rather than &(Arg){0}: Arg is a union, so that initialiser only
 * covers its first member (int i) and leaves the rest of ->v as whatever was on
 * the stack, which setlayout() then stores as the layout pointer. */
void toggletabbed(const Arg* arg)
{
    if (!selmon || !arg || !arg->v)
        return;
    setlayout(selmon->lt[selmon->sellt]->arrange == tabbed ? NULL : arg);
}

void toggletag(const Arg* arg)
{
    uint32_t newtags;
    Client* sel = focustop(selmon);
    if (!sel || !(newtags = sel->tags ^ (arg->ui & TAGMASK)))
        return;

    sel->tags = newtags;
    focusclient(focustop(selmon), 1);
    arrange(selmon);
    drawbars();
}

void toggleview(const Arg* arg)
{
    uint32_t newtagset;
    if (!(newtagset =
              selmon ? selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK)
                     : 0))
        return;

    selmon->tagset[selmon->seltags] = newtagset;
    focusclient(focustop(selmon), 1);
    arrange(selmon);
    drawbars();
}

void view(const Arg* arg)
{
    unsigned int i;

    if (!selmon || (arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
        return;

    i = tagindex(selmon->tagset[selmon->seltags]);
    selmon->taglt[i][0] = selmon->lt[0];
    selmon->taglt[i][1] = selmon->lt[1];
    selmon->tagsellt[i] = selmon->sellt;

    selmon->seltags ^= 1; /* toggle sel tagset */
    if (arg->ui & TAGMASK)
        selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;

    i = tagindex(selmon->tagset[selmon->seltags]);
    selmon->lt[0] = selmon->taglt[i][0];
    selmon->lt[1] = selmon->taglt[i][1];
    selmon->sellt = selmon->tagsellt[i];

    focusclient(focustop(selmon), 1);
    arrange(selmon);
    drawbars();
}

void zoom(const Arg* arg)
{
    Client *c, *sel = focustop(selmon);

    if (!sel || !selmon || !selmon->lt[selmon->sellt]->arrange ||
        sel->isfloating)
        return;

    /* Search for the first tiled window that is not sel, marking sel as
     * NULL if we pass it along the way */
    wl_list_for_each(c, &clients, link)
    {
        if (VISIBLEON(c, selmon) && !c->isfloating) {
            if (c != sel)
                break;
            sel = NULL;
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

    focusclient(sel, 1);
    arrange(selmon);
}
