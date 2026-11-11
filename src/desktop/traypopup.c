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

#include "core/g0wm.h"
#include "core/settings.h"
#include "desktop/systray/menu.h"
#include "desktop/traypopup.h"
#include "output/monitor.h"
#include "render/buffer.h"
#include "render/corner.h"
#include "render/opacity.h"

/* Rows the tray menu shows at once, past which the tail is dropped. */
constexpr int TRAYPOPUP_ITEMS_MAX = 64;

/* function declarations */
static void traypopup_draw(void);
static int traypopup_row(double layout_x, double layout_y);

/* variables */
static struct TrayPopupState {
    struct wlr_scene_buffer* scene;
    RenderBuffer* buffer_pool[2];
    char labels[TRAYPOPUP_ITEMS_MAX][MENU_LABEL_MAX];
    TrayMenu* menu;                        /* the menu waiting for its one menu_pick() */
    int item_count, hovered_index;         /* negative hovered_index means no row */
    int x, y, width, height;               /* logical, like a client's geometry */
    int border_width;                      /* drawn inside width and height */
    int buffer_width_px, buffer_height_px; /* what the pool was last sized for, in device pixels */
} traypopup_state;

/* function implementations */

/* Radius the popup is cut to, as corner_bar_radius() is for the bar. */
static int traypopup_corner(void)
{
    if (!corner_radius)
        return 0;
    return MAX(0, MIN((int)corner_radius, MIN(traypopup_state.width, traypopup_state.height) / 2));
}

int traypopup_active(void)
{
    return traypopup_state.menu != nullptr;
}

/* Hides the popup. The caller still owes the menu its menu_pick(). */
static void traypopup_hide(void)
{
    traypopup_state.menu = nullptr;
    traypopup_state.item_count = 0;
    traypopup_state.hovered_index = -1;
    if (traypopup_state.scene)
        wlr_scene_node_set_enabled(&traypopup_state.scene->node, 0);
    buffer_pool_drop(traypopup_state.buffer_pool, LENGTH(traypopup_state.buffer_pool));
    traypopup_state.buffer_width_px = traypopup_state.buffer_height_px = 0;
}

void traypopup_dismiss(void)
{
    TrayMenu* menu = traypopup_state.menu;

    if (!menu)
        return;
    traypopup_hide();
    menu_pick(menu, -1);
}

/* Lays a level out at the cursor. A new menu cancels the one it replaces. */
void traypopup_present(const char* const* labels, int label_count, TrayMenu* menu)
{
    int max_width = 0;

    if (traypopup_state.menu && traypopup_state.menu != menu)
        traypopup_dismiss();

    Monitor* monitor = monitor_at(cursor->x, cursor->y);
    if (!monitor || !monitor->draw_context || label_count < 1) {
        menu_pick(menu, -1);
        return;
    }

    if (label_count > TRAYPOPUP_ITEMS_MAX)
        label_count = TRAYPOPUP_ITEMS_MAX;
    for (int i = 0; i < label_count; i++) {
        snprintf(traypopup_state.labels[i], sizeof(traypopup_state.labels[i]), "%s", labels[i]);
        const int label_width =
            (int)drwl_font_getwidth(monitor->draw_context, traypopup_state.labels[i]) +
            monitor->horizontal_padding;
        if (label_width > max_width)
            max_width = label_width;
    }

    traypopup_state.menu = menu;
    traypopup_state.item_count = label_count;
    traypopup_state.hovered_index = -1;
    /* a decoration like any other, so border_width and corner_radius rule it
     * too */
    traypopup_state.border_width = (int)corner_border_width();

    /* bar height rows, plus the border, which is drawn inside the box */
    traypopup_state.width = MIN((int)((float)max_width / monitor->wlr_output->scale) + 1 +
                                    2 * traypopup_state.border_width,
                                monitor->geometry.width);
    traypopup_state.height =
        MIN(monitor->bar_metrics.logical_height * label_count + 2 * traypopup_state.border_width,
            monitor->geometry.height);

    if (traypopup_state.width <= 2 * traypopup_state.border_width ||
        traypopup_state.height <= 2 * traypopup_state.border_width) {
        traypopup_state.menu = nullptr;
        menu_pick(menu, -1);
        return;
    }

    /* dropped from the cursor, then pushed back inside the output */
    traypopup_state.x =
        MIN((int)cursor->x, monitor->geometry.x + monitor->geometry.width - traypopup_state.width);
    traypopup_state.y = MIN((int)cursor->y, monitor->geometry.y + monitor->geometry.height -
                                                traypopup_state.height);
    traypopup_state.x = MAX(traypopup_state.x, monitor->geometry.x);
    traypopup_state.y = MAX(traypopup_state.y, monitor->geometry.y);

    traypopup_draw();
}

static void traypopup_draw(void)
{
    if (!traypopup_state.item_count)
        return;
    Monitor* monitor = monitor_at(traypopup_state.x + traypopup_state.width / 2, traypopup_state.y);
    if (!monitor || !monitor->draw_context)
        return;

    int buffer_width = MAX(1, (int)((float)traypopup_state.width * monitor->wlr_output->scale));
    int buffer_height = MAX(1, (int)((float)traypopup_state.height * monitor->wlr_output->scale));
    /* the border has to leave at least a pixel of row behind it */
    int border_inset =
        MIN((int)roundf((float)traypopup_state.border_width * monitor->wlr_output->scale),
            MIN(buffer_width - 1, buffer_height - traypopup_state.item_count) / 2);
    border_inset = MAX(border_inset, 0);
    int rowsh = buffer_height - 2 * border_inset;
    int rowh = MAX(1, rowsh / traypopup_state.item_count);
    int rounding = (int)roundf((float)traypopup_corner() * monitor->wlr_output->scale);

    /* buffer_get() ignores the size asked for, so a resize empties the pool */
    if (buffer_width != traypopup_state.buffer_width_px ||
        buffer_height != traypopup_state.buffer_height_px) {
        buffer_pool_drop(traypopup_state.buffer_pool, LENGTH(traypopup_state.buffer_pool));
        traypopup_state.buffer_width_px = buffer_width;
        traypopup_state.buffer_height_px = buffer_height;
    }
    RenderBuffer* buffer =
        buffer_get(traypopup_state.buffer_pool, LENGTH(traypopup_state.buffer_pool), buffer_width,
                   buffer_height);
    if (!buffer)
        return;

    if (!traypopup_state.scene &&
        !(traypopup_state.scene = wlr_scene_buffer_create(layers[SCENE_LAYER_OVERLAY], nullptr))) {
        wlr_buffer_unlock(&buffer->base);
        return;
    }

    drwl_setimage(monitor->draw_context, buffer->image);

    /* the whole buffer in the border colour, rows laid over it inset */
    uint32_t border_scheme[3];
    border_scheme[ColFg] = border_scheme[ColBg] = border_scheme[ColBorder] =
        colors[COLOR_SCHEME_NORMAL][ColBorder];
    drwl_setscheme(monitor->draw_context, border_scheme);
    drwl_rect(monitor->draw_context, 0, 0, (unsigned int)buffer_width, (unsigned int)buffer_height,
              1, 1);

    for (int i = 0; i < traypopup_state.item_count; i++) {
        drwl_setscheme(monitor->draw_context,
                       colors[i == traypopup_state.hovered_index ? COLOR_SCHEME_SELECTED
                                                                 : COLOR_SCHEME_NORMAL]);
        drwl_text(monitor->draw_context, border_inset, border_inset + i * rowh,
                  (unsigned int)(buffer_width - 2 * border_inset),
                  /* the last row takes the rounding slack */
                  (unsigned int)(i == traypopup_state.item_count - 1 ? rowsh - i * rowh : rowh),
                  monitor->horizontal_padding / 2, traypopup_state.labels[i], 0);
    }

    corner_cut(buffer->pixels, buffer_width, buffer_height, rounding);

    wlr_scene_buffer_set_opacity(traypopup_state.scene, opacity_decorations_current());
    wlr_scene_buffer_set_dest_size(traypopup_state.scene, traypopup_state.width,
                                   traypopup_state.height);
    wlr_scene_node_set_position(&traypopup_state.scene->node, traypopup_state.x, traypopup_state.y);
    wlr_scene_buffer_set_buffer(traypopup_state.scene, &buffer->base);
    wlr_scene_node_raise_to_top(&traypopup_state.scene->node);
    wlr_scene_node_set_enabled(&traypopup_state.scene->node, 1);
    wlr_buffer_unlock(&buffer->base);
}

/* Which row the cursor is over, negative when it is outside the popup. */
static int traypopup_row(double layout_x, double layout_y)
{
    const int rows = traypopup_state.height - 2 * traypopup_state.border_width;

    if (!traypopup_state.item_count || layout_x < traypopup_state.x ||
        layout_x >= traypopup_state.x + traypopup_state.width || layout_y < traypopup_state.y ||
        layout_y >= traypopup_state.y + traypopup_state.height)
        return -1;

    /* same inset as the drawing, so the border belongs to its own row */
    const int row = rows > 0 ? (int)((layout_y - traypopup_state.y - traypopup_state.border_width) *
                                     traypopup_state.item_count / rows)
                             : 0;
    return MIN(MAX(row, 0), traypopup_state.item_count - 1);
}

void traypopup_motion(double layout_x, double layout_y)
{
    int row = traypopup_row(layout_x, layout_y);

    if (row == traypopup_state.hovered_index)
        return;
    traypopup_state.hovered_index = row;
    traypopup_draw();
}

/* A click on a row takes it, anywhere else puts the menu away. */
void traypopup_click(double layout_x, double layout_y)
{
    TrayMenu* menu = traypopup_state.menu;
    int row = traypopup_row(layout_x, layout_y);

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
