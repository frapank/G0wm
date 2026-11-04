/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the title bar of each window, with its close button
 */

#include <drwl.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "desktop/bar/bar.h"
#include "desktop/bar/title.h"
#include "input/pointer.h"
#include "internal.h"
#include "output/monitor.h"
#include "output/privacy.h"
#include "render/buffer.h"
#include "render/opacity.h"
#include "window/client.h"
#include "window/layout.h"

/* variables */
static Client* bar_hovered; /* whose close button is under the pointer */

/* function implementations */
/* the top tab, or the focused client */
static int bar_title_selected(Client* c)
{
    Monitor* m = c->mon;

    return c == (m->lt[m->sellt]->arrange == layout_tabbed && !c->isfloating
                     ? layout_tab_top(m)
                     : client_top(m));
}

/* x of the close button, w without one */
static int bar_title_close_x(Client* c, int w, int* d)
{
    Monitor* m = c->mon;
    int x;

    *d = MAX(m->drw->font->height * 3 / 5, 4);
    if (!title_close)
        return w;
    x = w - m->lrpad / 2 - *d;
    return x > w / 2 ? x : w;
}

/* draws the title bar of c into buf */
static void bar_paint_title_bar(Client* c, Buffer* buf, int share)
{
    Monitor* m = c->mon;
    const char* title;
    int w = buf->base.width, h = buf->base.height;
    int sel, scheme, tab, end, lead, tw, d, fd, pd, dots;
    uint32_t fg, pc;

    sel = bar_title_selected(c);
    scheme = sel ? SchemeTitleSel : SchemeTitle;
    tab = m->lt[m->sellt]->arrange == layout_tabbed && !c->isfloating &&
          c->titlew < c->geom.width - 2 * (int)c->bw;
    fg = colors[scheme][ColFg];
    title = share ? privacy_share_title(c) : client_get_title(c);
    end = bar_title_close_x(c, w, &d);
    fd = MAX(m->drw->font->height / 3, 3);

    pd = bar_private_dot(c, &pc);
    dots = !!c->isfloating + pd;
    lead = m->lrpad / 2 + dots * (fd + m->lrpad / 3);
    if (title_center) {
        tw = (int)drwl_font_getwidth(m->drw, title);
        lead = MAX(MIN((w - tw) / 2, end - m->lrpad / 2 - tw), lead);
    }

    drwl_setimage(m->drw, buf->image);
    drwl_setscheme(m->drw, share ? bar_opaque_scheme(scheme) : colors[scheme]);
    drwl_rect(m->drw, 0, 0, w, h, 1, 1);
    if (end > lead)
        drwl_text(m->drw, 0, 0, end, h, lead, title, 0);

    if (pd)
        bar_draw_pill(m,
                      lead - fd - m->lrpad / 3,
                      (h - fd) / 2,
                      fd,
                      fd,
                      bar_private_radius(fd),
                      0,
                      pc);
    if (c->isfloating)
        bar_draw_pill(m,
                      lead - dots * (fd + m->lrpad / 3),
                      (h - fd) / 2,
                      fd,
                      fd,
                      fd / 2.0,
                      0,
                      fg);
    if (tab && c->titlex)
        bar_draw_pill(m,
                      0,
                      h / 4,
                      (int)MAX(m->wlr_output->scale, 1),
                      h - h / 2,
                      0,
                      0,
                      bar_fade(colors[SchemeTitle][ColFg]));
    if (end < w && c == bar_hovered)
        bar_draw_pill(m, end, (h - d) / 2, d, d, d / 2.0, 0, title_close_color);
}

/* Renders the client's own title bar. In the tabbed layout every client of the
 * group shares one row, so these end up drawn side by side as tabs. */
void bar_draw_title_bar(Client* c)
{
    Monitor* m = c->mon;
    Buffer *buf, *share;
    int w, h;

    if (!c->title)
        return;
    bar_place_title_bar(c);
    if (!m || !title_bar || c->isfullscreen || c->titlew <= 0 ||
        !c->scene->node.enabled) {
        wlr_scene_node_set_enabled(&c->title->node, 0);
        return;
    }

    w = (int)((float)c->titlew * m->wlr_output->scale);
    h = m->t.height;
    if (w != c->titlebufw) {
        buffer_pool_drop(c->titlepool, LENGTH(c->titlepool));
        c->titlebufw = w;
    }
    if (!(buf = buffer_get(c->titlepool, LENGTH(c->titlepool), w, h)))
        return;
    bar_paint_title_bar(c, buf, 0);
    if (c->isprivate && (share = bar_share_buffer(c->sharepool, w, h))) {
        bar_paint_title_bar(c, share, 1);
        wlr_buffer_unlock(&share->base);
    }

    wlr_scene_node_set_enabled(&c->title->node, 1);
    wlr_scene_buffer_set_opacity(c->title, opacity_decorations_current());
    wlr_scene_buffer_set_buffer(c->title, &buf->base);
    wlr_buffer_unlock(&buf->base);
}

/* whether (x, y) is on c's close button */
static int bar_on_close_button(Client* c, double x, double y)
{
    Monitor* m = c->mon;
    int bx, d;
    double lx, ly;

    if (!m || !c->title || !c->title->node.enabled)
        return 0;
    lx = (x - c->geom.x - c->bw - c->titlex) * m->wlr_output->scale;
    ly = (y - c->geom.y - c->bw) * m->wlr_output->scale;
    bx = bar_title_close_x(c, c->titlebufw, &d);
    return bx < c->titlebufw && ly >= 0 && ly < m->t.height &&
           lx >= bx - m->lrpad / 2 && lx < c->titlebufw;
}

/* whether (x, y) is on c's title bar */
int bar_on_title_bar(Client* c, double x, double y)
{
    Monitor* m = c->mon;
    double lx, ly;

    if (!m || !c->title || !c->title->node.enabled)
        return 0;
    lx = (x - c->geom.x - c->bw - c->titlex) * m->wlr_output->scale;
    ly = (y - c->geom.y - c->bw) * m->wlr_output->scale;
    return lx >= 0 && lx < c->titlebufw && ly >= 0 && ly < m->t.height;
}

int bar_title_click(Client* c, double x, double y)
{
    if (bar_on_close_button(c, x, y))
        client_send_close(c);
    else if (bar_on_title_bar(c, x, y))
        input_title_grab(c);
    else
        return 0;
    return 1;
}

void bar_title_hover(Client* c, double x, double y)
{
    Client *h = c && bar_on_close_button(c, x, y) ? c : nullptr,
           *old = bar_hovered, *w;

    if (h == old)
        return;
    bar_hovered = h;
    /* old may be gone */
    wl_list_for_each(w, &clients, link) if (w == old || w == h)
        bar_draw_title_bar(w);
}

/* Sizes and places the client's title bar. It spans the whole window, except
 * in the tabbed layout where each client of the group only gets its own slice
 * of the shared row - which is what turns the title bars into tabs. */
void bar_place_title_bar(Client* c)
{
    Monitor* m = c->mon;
    Client* w;
    int i = 0, n = 0, x = 0, tw;

    if (!c->title || !m)
        return;

    tw = c->geom.width - 2 * (int)c->bw;
    if (m->lt[m->sellt]->arrange == layout_tabbed && !c->isfloating &&
        !c->isfullscreen) {
        wl_list_for_each(w, &clients, link)
        {
            if (!VISIBLEON(w, m) || w->isfloating || w->isfullscreen)
                continue;
            if (w == c)
                i = n;
            n++;
        }
        if (n) {
            x = tw * i / n;
            tw = tw * (i + 1) / n - x;
        }
    }

    c->titlex = x;
    c->titlew = tw;
    wlr_scene_node_set_position(&c->title->node, (int)c->bw + x, (int)c->bw);
    wlr_scene_buffer_set_dest_size(c->title, MAX(tw, 1), bar_title_height(c));
}

int bar_title_height(Client* c)
{
    return title_bar && c->mon && !c->isfullscreen ? c->mon->t.real_height : 0;
}

/* layout_arrange() leaves floating clients alone, and no client redraws a title
 * bar, so both are done by hand here */
void bar_toggle_title_bars(const Arg*)
{
    Monitor* m;
    Client* c;

    title_bar = !title_bar;

    wl_list_for_each(m, &monitors, link) layout_arrange(m);
    wl_list_for_each(c, &clients, link)
    {
        if (c->mon && c->isfloating && !c->isfullscreen)
            client_resize(c, c->geom, 1);
    }
    bar_draw_all();

    wl_list_for_each(m, &monitors, link)
    {
        if (m->wlr_output->enabled)
            wlr_output_schedule_frame(m->wlr_output);
    }
}
