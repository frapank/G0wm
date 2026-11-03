/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the tray context menu, drawn under the cursor
 */
#include <drwl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

#include "buffer.h"
#include "corner.h"
#include "g0wm.h"
#include "monitor.h"
#include "opacity.h"
#include "settings.h"
#include "systray/menu.h"
#include "traypopup.h"

/* Rows the tray menu shows at once, past which the tail is dropped. */
constexpr int TRAYPOPUP_ITEMS_MAX = 64;

/* function declarations */
static void traypopup_draw(void);
static int traypopup_row(double lx, double ly);

/* variables */
static struct {
    struct wlr_scene_buffer* scene;
    Buffer* pool[2];
    char labels[TRAYPOPUP_ITEMS_MAX][MENU_LABEL_MAX];
    Menu* menu;     /* the menu waiting for its one menu_pick() */
    int n, sel;     /* entries, and the row hovered, sel is negative for none */
    int x, y, w, h; /* logical, like a client's geometry */
    int bd;         /* border thickness, drawn inside w and h */
    int bufw, bufh; /* what the pool was last sized for, in device pixels */
} traypopup_state;

/* function implementations */

/* Radius the popup is cut to, as corner_bar_radius() is for the bar. */
static int traypopup_corner(void)
{
    if (!corner_radius)
        return 0;
    return MAX(
        0,
        MIN((int)corner_radius, MIN(traypopup_state.w, traypopup_state.h) / 2));
}

int traypopup_active(void)
{
    return traypopup_state.menu != nullptr;
}

/* Hides the popup. The caller still owes the menu its menu_pick(). */
static void traypopup_hide(void)
{
    traypopup_state.menu = nullptr;
    traypopup_state.n = 0;
    traypopup_state.sel = -1;
    if (traypopup_state.scene)
        wlr_scene_node_set_enabled(&traypopup_state.scene->node, 0);
    buffer_pool_drop(traypopup_state.pool, LENGTH(traypopup_state.pool));
    traypopup_state.bufw = traypopup_state.bufh = 0;
}

void traypopup_dismiss(void)
{
    Menu* menu = traypopup_state.menu;

    if (!menu)
        return;
    traypopup_hide();
    menu_pick(menu, -1);
}

/* Lays a level out at the cursor. A new menu cancels the one it replaces. */
void traypopup_present(const char* const* labels, int n, Menu* menu)
{
    Monitor* m;
    int i, tw, maxw = 0;

    if (traypopup_state.menu && traypopup_state.menu != menu)
        traypopup_dismiss();

    m = monitor_at(cursor->x, cursor->y);
    if (!m || !m->drw || n < 1) {
        menu_pick(menu, -1);
        return;
    }

    if (n > TRAYPOPUP_ITEMS_MAX)
        n = TRAYPOPUP_ITEMS_MAX;
    for (i = 0; i < n; i++) {
        snprintf(traypopup_state.labels[i],
                 sizeof(traypopup_state.labels[i]),
                 "%s",
                 labels[i]);
        tw = (int)drwl_font_getwidth(m->drw, traypopup_state.labels[i]) +
             m->lrpad;
        if (tw > maxw)
            maxw = tw;
    }

    traypopup_state.menu = menu;
    traypopup_state.n = n;
    traypopup_state.sel = -1;
    /* a decoration like any other, so border_width and corner_radius rule it
     * too */
    traypopup_state.bd = (int)corner_border_width();

    /* bar height rows, plus the border, which is drawn inside the box */
    traypopup_state.w = MIN((int)((float)maxw / m->wlr_output->scale) + 1 +
                                2 * traypopup_state.bd,
                            m->m.width);
    traypopup_state.h =
        MIN(m->b.real_height * n + 2 * traypopup_state.bd, m->m.height);

    if (traypopup_state.w <= 2 * traypopup_state.bd ||
        traypopup_state.h <= 2 * traypopup_state.bd) {
        traypopup_state.menu = nullptr;
        menu_pick(menu, -1);
        return;
    }

    /* dropped from the cursor, then pushed back inside the output */
    traypopup_state.x =
        MIN((int)cursor->x, m->m.x + m->m.width - traypopup_state.w);
    traypopup_state.y =
        MIN((int)cursor->y, m->m.y + m->m.height - traypopup_state.h);
    traypopup_state.x = MAX(traypopup_state.x, m->m.x);
    traypopup_state.y = MAX(traypopup_state.y, m->m.y);

    traypopup_draw();
}

static void traypopup_draw(void)
{
    Monitor* m;
    Buffer* buf;
    uint32_t border[3];
    int i, rowh, rowsh, bw, bh, bd, r;

    if (!traypopup_state.n ||
        !(m = monitor_at(traypopup_state.x + traypopup_state.w / 2,
                         traypopup_state.y)) ||
        !m->drw)
        return;

    bw = MAX(1, (int)((float)traypopup_state.w * m->wlr_output->scale));
    bh = MAX(1, (int)((float)traypopup_state.h * m->wlr_output->scale));
    /* the border has to leave at least a pixel of row behind it */
    bd = MIN((int)roundf((float)traypopup_state.bd * m->wlr_output->scale),
             MIN(bw - 1, bh - traypopup_state.n) / 2);
    bd = MAX(bd, 0);
    rowsh = bh - 2 * bd;
    rowh = MAX(1, rowsh / traypopup_state.n);
    r = (int)roundf((float)traypopup_corner() * m->wlr_output->scale);

    /* buffer_get() ignores the size asked for, so a resize empties the pool */
    if (bw != traypopup_state.bufw || bh != traypopup_state.bufh) {
        buffer_pool_drop(traypopup_state.pool, LENGTH(traypopup_state.pool));
        traypopup_state.bufw = bw;
        traypopup_state.bufh = bh;
    }
    if (!(buf = buffer_get(
              traypopup_state.pool, LENGTH(traypopup_state.pool), bw, bh)))
        return;

    if (!traypopup_state.scene &&
        !(traypopup_state.scene =
              wlr_scene_buffer_create(layers[LyrOverlay], nullptr))) {
        wlr_buffer_unlock(&buf->base);
        return;
    }

    drwl_setimage(m->drw, buf->image);

    /* the whole buffer in the border colour, rows laid over it inset */
    border[ColFg] = border[ColBg] = border[ColBorder] =
        colors[SchemeNorm][ColBorder];
    drwl_setscheme(m->drw, border);
    drwl_rect(m->drw, 0, 0, (unsigned int)bw, (unsigned int)bh, 1, 1);

    for (i = 0; i < traypopup_state.n; i++) {
        drwl_setscheme(
            m->drw, colors[i == traypopup_state.sel ? SchemeSel : SchemeNorm]);
        drwl_text(m->drw,
                  bd,
                  bd + i * rowh,
                  (unsigned int)(bw - 2 * bd),
                  /* the last row takes the rounding slack */
                  (unsigned int)(i == traypopup_state.n - 1 ? rowsh - i * rowh
                                                            : rowh),
                  m->lrpad / 2,
                  traypopup_state.labels[i],
                  0);
    }

    corner_cut(buf->data, bw, bh, r);

    wlr_scene_buffer_set_opacity(traypopup_state.scene,
                                 opacity_decorations_current());
    wlr_scene_buffer_set_dest_size(
        traypopup_state.scene, traypopup_state.w, traypopup_state.h);
    wlr_scene_node_set_position(
        &traypopup_state.scene->node, traypopup_state.x, traypopup_state.y);
    wlr_scene_buffer_set_buffer(traypopup_state.scene, &buf->base);
    wlr_scene_node_raise_to_top(&traypopup_state.scene->node);
    wlr_scene_node_set_enabled(&traypopup_state.scene->node, 1);
    wlr_buffer_unlock(&buf->base);
}

/* Which row the cursor is over, negative when it is outside the popup. */
static int traypopup_row(double lx, double ly)
{
    int rows = traypopup_state.h - 2 * traypopup_state.bd;
    int row;

    if (!traypopup_state.n || lx < traypopup_state.x ||
        lx >= traypopup_state.x + traypopup_state.w || ly < traypopup_state.y ||
        ly >= traypopup_state.y + traypopup_state.h)
        return -1;

    /* same inset as the drawing, so the border belongs to its own row */
    row = rows > 0 ? (int)((ly - traypopup_state.y - traypopup_state.bd) *
                           traypopup_state.n / rows)
                   : 0;
    return MIN(MAX(row, 0), traypopup_state.n - 1);
}

void traypopup_motion(double lx, double ly)
{
    int row = traypopup_row(lx, ly);

    if (row == traypopup_state.sel)
        return;
    traypopup_state.sel = row;
    traypopup_draw();
}

/* A click on a row takes it, anywhere else puts the menu away. */
void traypopup_click(double lx, double ly)
{
    Menu* menu = traypopup_state.menu;
    int row = traypopup_row(lx, ly);

    if (!menu)
        return;
    traypopup_hide();
    menu_pick(menu, row);
}

void traypopup_cleanup(void)
{
    traypopup_dismiss();
    if (traypopup_state.scene)
        wlr_scene_node_destroy(&traypopup_state.scene->node);
    traypopup_state.scene = nullptr;
}
