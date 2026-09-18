/*
 * See LICENSE file for copyright and license details.
 *
 * the tray context menu, drawn under the cursor
 */
#include "g0wm.h"

#ifdef SYSTRAY

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
} popup;

/* function implementations */

/* Radius the popup is cut to, as barcorner() is for the bar. */
static int traypopup_corner(void)
{
    if (!cornerpx)
        return 0;
    return MAX(0, MIN((int)cornerpx, MIN(popup.w, popup.h) / 2));
}

int traypopup_active(void)
{
    return popup.menu != NULL;
}

/* Hides the popup. The caller still owes the menu its menu_pick(). */
static void traypopup_hide(void)
{
    popup.menu = NULL;
    popup.n = 0;
    popup.sel = -1;
    if (popup.scene)
        wlr_scene_node_set_enabled(&popup.scene->node, 0);
    bufpooldrop(popup.pool, LENGTH(popup.pool));
    popup.bufw = popup.bufh = 0;
}

void traypopup_dismiss(void)
{
    Menu* menu = popup.menu;

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

    if (popup.menu && popup.menu != menu)
        traypopup_dismiss();

    m = xytomon(cursor->x, cursor->y);
    if (!m || !m->drw || n < 1) {
        menu_pick(menu, -1);
        return;
    }

    if (n > TRAYPOPUP_ITEMS_MAX)
        n = TRAYPOPUP_ITEMS_MAX;
    for (i = 0; i < n; i++) {
        snprintf(popup.labels[i], sizeof(popup.labels[i]), "%s", labels[i]);
        tw = (int)drwl_font_getwidth(m->drw, popup.labels[i]) + m->lrpad;
        if (tw > maxw)
            maxw = tw;
    }

    popup.menu = menu;
    popup.n = n;
    popup.sel = -1;
    /* a decoration like any other, so borderpx and cornerpx rule it too */
    popup.bd = (int)borderwidth();

    /* bar height rows, plus the border, which is drawn inside the box */
    popup.w = MIN((int)((float)maxw / m->wlr_output->scale) + 1 + 2 * popup.bd,
                  m->m.width);
    popup.h = MIN(m->b.real_height * n + 2 * popup.bd, m->m.height);

    if (popup.w <= 2 * popup.bd || popup.h <= 2 * popup.bd) {
        popup.menu = NULL;
        menu_pick(menu, -1);
        return;
    }

    /* dropped from the cursor, then pushed back inside the output */
    popup.x = MIN((int)cursor->x, m->m.x + m->m.width - popup.w);
    popup.y = MIN((int)cursor->y, m->m.y + m->m.height - popup.h);
    popup.x = MAX(popup.x, m->m.x);
    popup.y = MAX(popup.y, m->m.y);

    traypopup_draw();
}

static void traypopup_draw(void)
{
    Monitor* m;
    Buffer* buf;
    uint32_t border[3];
    int i, rowh, rowsh, bw, bh, bd, r;

    if (!popup.n || !(m = xytomon(popup.x + popup.w / 2, popup.y)) || !m->drw)
        return;

    bw = MAX(1, (int)((float)popup.w * m->wlr_output->scale));
    bh = MAX(1, (int)((float)popup.h * m->wlr_output->scale));
    /* the border has to leave at least a pixel of row behind it */
    bd = MIN((int)roundf((float)popup.bd * m->wlr_output->scale),
             MIN(bw - 1, bh - popup.n) / 2);
    bd = MAX(bd, 0);
    rowsh = bh - 2 * bd;
    rowh = MAX(1, rowsh / popup.n);
    r = (int)roundf((float)traypopup_corner() * m->wlr_output->scale);

    /* bufget() ignores the size asked for, so a resize empties the pool */
    if (bw != popup.bufw || bh != popup.bufh) {
        bufpooldrop(popup.pool, LENGTH(popup.pool));
        popup.bufw = bw;
        popup.bufh = bh;
    }
    if (!(buf = bufget(popup.pool, LENGTH(popup.pool), bw, bh)))
        return;

    if (!popup.scene &&
        !(popup.scene = wlr_scene_buffer_create(layers[LyrOverlay], NULL))) {
        wlr_buffer_unlock(&buf->base);
        return;
    }

    drwl_setimage(m->drw, buf->image);

    /* the whole buffer in the border colour, rows laid over it inset */
    border[ColFg] = border[ColBg] = border[ColBorder] =
        colors[SchemeNorm][ColBorder];
    drwl_setscheme(m->drw, border);
    drwl_rect(m->drw, 0, 0, (unsigned int)bw, (unsigned int)bh, 1, 1);

    for (i = 0; i < popup.n; i++) {
        drwl_setscheme(m->drw, colors[i == popup.sel ? SchemeSel : SchemeNorm]);
        drwl_text(m->drw,
                  bd,
                  bd + i * rowh,
                  (unsigned int)(bw - 2 * bd),
                  /* the last row takes the rounding slack */
                  (unsigned int)(i == popup.n - 1 ? rowsh - i * rowh : rowh),
                  m->lrpad / 2,
                  popup.labels[i],
                  0);
    }

    cornercut(buf->data, bw, bh, r);

    wlr_scene_buffer_set_opacity(popup.scene, decoopacity());
    wlr_scene_buffer_set_dest_size(popup.scene, popup.w, popup.h);
    wlr_scene_node_set_position(&popup.scene->node, popup.x, popup.y);
    wlr_scene_buffer_set_buffer(popup.scene, &buf->base);
    wlr_scene_node_raise_to_top(&popup.scene->node);
    wlr_scene_node_set_enabled(&popup.scene->node, 1);
    wlr_buffer_unlock(&buf->base);
}

/* Which row the cursor is over, negative when it is outside the popup. */
static int traypopup_row(double lx, double ly)
{
    int rows = popup.h - 2 * popup.bd;
    int row;

    if (!popup.n || lx < popup.x || lx >= popup.x + popup.w || ly < popup.y ||
        ly >= popup.y + popup.h)
        return -1;

    /* same inset as the drawing, so the border belongs to its own row */
    row = rows > 0 ? (int)((ly - popup.y - popup.bd) * popup.n / rows) : 0;
    return MIN(MAX(row, 0), popup.n - 1);
}

void traypopup_motion(double lx, double ly)
{
    int row = traypopup_row(lx, ly);

    if (row == popup.sel)
        return;
    popup.sel = row;
    traypopup_draw();
}

/* A click on a row takes it, anywhere else puts the menu away. */
void traypopup_click(double lx, double ly)
{
    Menu* menu = popup.menu;
    int row = traypopup_row(lx, ly);

    if (!menu)
        return;
    traypopup_hide();
    menu_pick(menu, row);
}

void traypopup_cleanup(void)
{
    traypopup_dismiss();
    if (popup.scene)
        wlr_scene_node_destroy(&popup.scene->node);
    popup.scene = NULL;
}

#endif /* SYSTRAY */
