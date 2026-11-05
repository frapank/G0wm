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
static int bar_title_selected(Client* client)
{
    Monitor* monitor = client->mon;

    return client == (monitor->lt[monitor->sellt]->arrange == layout_tabbed && !client->isfloating
                          ? layout_tab_top(monitor)
                          : client_top(monitor));
}

/* x of the close button, bar_width without one */
static int bar_title_close_x(Client* client, int bar_width, int* button_size)
{
    Monitor* monitor = client->mon;

    *button_size = MAX(monitor->drw->font->height * 3 / 5, 4);
    if (!title_close)
        return bar_width;
    const int button_x = bar_width - monitor->lrpad / 2 - *button_size;
    return button_x > bar_width / 2 ? button_x : bar_width;
}

/* draws the title bar of client into buffer */
static void bar_paint_title_bar(Client* client, Buffer* buffer, int for_sharing)
{
    Monitor* monitor = client->mon;
    const int buffer_width = buffer->base.width;
    const int buffer_height = buffer->base.height;

    const int is_selected = bar_title_selected(client);
    const int scheme_index = is_selected ? SchemeTitleSel : SchemeTitle;
    const int is_tab = monitor->lt[monitor->sellt]->arrange == layout_tabbed &&
                       !client->isfloating &&
                       client->titlew < client->geom.width - 2 * (int)client->bw;
    const uint32_t foreground = colors[scheme_index][ColFg];
    const char* title = for_sharing ? privacy_share_title(client) : client_get_title(client);
    int close_button_size;
    const int close_button_x = bar_title_close_x(client, buffer_width, &close_button_size);
    const int dot_size = MAX(monitor->drw->font->height / 3, 3);

    uint32_t private_dot_color;
    const int has_private_dot = bar_private_dot(client, &private_dot_color);
    const int dot_count = !!client->isfloating + has_private_dot;
    int text_start = monitor->lrpad / 2 + dot_count * (dot_size + monitor->lrpad / 3);
    if (title_center) {
        const int text_width = (int)drwl_font_getwidth(monitor->drw, title);
        text_start = MAX(
            MIN((buffer_width - text_width) / 2, close_button_x - monitor->lrpad / 2 - text_width),
            text_start);
    }

    drwl_setimage(monitor->drw, buffer->image);
    drwl_setscheme(monitor->drw,
                   for_sharing ? bar_opaque_scheme(scheme_index) : colors[scheme_index]);
    drwl_rect(monitor->drw, 0, 0, buffer_width, buffer_height, 1, 1);
    if (close_button_x > text_start)
        drwl_text(monitor->drw, 0, 0, close_button_x, buffer_height, text_start, title, 0);

    if (has_private_dot)
        bar_draw_pill(monitor, text_start - dot_size - monitor->lrpad / 3,
                      (buffer_height - dot_size) / 2, dot_size, dot_size,
                      bar_private_radius(dot_size), 0, private_dot_color);
    if (client->isfloating)
        bar_draw_pill(monitor, text_start - dot_count * (dot_size + monitor->lrpad / 3),
                      (buffer_height - dot_size) / 2, dot_size, dot_size, dot_size / 2.0, 0,
                      foreground);
    if (is_tab && client->titlex)
        bar_draw_pill(monitor, 0, buffer_height / 4, (int)MAX(monitor->wlr_output->scale, 1),
                      buffer_height - buffer_height / 2, 0, 0,
                      bar_fade(colors[SchemeTitle][ColFg]));
    if (close_button_x < buffer_width && client == bar_hovered)
        bar_draw_pill(monitor, close_button_x, (buffer_height - close_button_size) / 2,
                      close_button_size, close_button_size, close_button_size / 2.0, 0,
                      title_close_color);
}

/* Renders the client's own title bar. In the tabbed layout every client of the
 * group shares one row, so these end up drawn side by side as tabs. */
void bar_draw_title_bar(Client* client)
{
    if (!client->title)
        return;
    Monitor* monitor = client->mon;

    bar_place_title_bar(client);
    if (!monitor || !title_bar || client->isfullscreen || client->titlew <= 0 ||
        !client->scene->node.enabled) {
        wlr_scene_node_set_enabled(&client->title->node, 0);
        return;
    }

    const int buffer_width = (int)((float)client->titlew * monitor->wlr_output->scale);
    const int buffer_height = monitor->t.height;
    if (buffer_width != client->titlebufw) {
        buffer_pool_drop(client->titlepool, LENGTH(client->titlepool));
        client->titlebufw = buffer_width;
    }
    Buffer* buffer =
        buffer_get(client->titlepool, LENGTH(client->titlepool), buffer_width, buffer_height);
    if (!buffer)
        return;
    bar_paint_title_bar(client, buffer, 0);
    Buffer* share_buffer = client->isprivate
                               ? bar_share_buffer(client->sharepool, buffer_width, buffer_height)
                               : nullptr;
    if (share_buffer) {
        bar_paint_title_bar(client, share_buffer, 1);
        wlr_buffer_unlock(&share_buffer->base);
    }

    wlr_scene_node_set_enabled(&client->title->node, 1);
    wlr_scene_buffer_set_opacity(client->title, opacity_decorations_current());
    wlr_scene_buffer_set_buffer(client->title, &buffer->base);
    wlr_buffer_unlock(&buffer->base);
}

/* whether (pointer_x, pointer_y) is on client's close button */
static int bar_on_close_button(Client* client, double pointer_x, double pointer_y)
{
    Monitor* monitor = client->mon;

    if (!monitor || !client->title || !client->title->node.enabled)
        return 0;
    const double buffer_x =
        (pointer_x - client->geom.x - client->bw - client->titlex) * monitor->wlr_output->scale;
    const double buffer_y = (pointer_y - client->geom.y - client->bw) * monitor->wlr_output->scale;
    int close_button_size;
    const int close_button_x = bar_title_close_x(client, client->titlebufw, &close_button_size);
    return close_button_x < client->titlebufw && buffer_y >= 0 && buffer_y < monitor->t.height &&
           buffer_x >= close_button_x - monitor->lrpad / 2 && buffer_x < client->titlebufw;
}

/* whether (pointer_x, pointer_y) is on client's title bar */
int bar_on_title_bar(Client* client, double pointer_x, double pointer_y)
{
    Monitor* monitor = client->mon;

    if (!monitor || !client->title || !client->title->node.enabled)
        return 0;
    const double buffer_x =
        (pointer_x - client->geom.x - client->bw - client->titlex) * monitor->wlr_output->scale;
    const double buffer_y = (pointer_y - client->geom.y - client->bw) * monitor->wlr_output->scale;
    return buffer_x >= 0 && buffer_x < client->titlebufw && buffer_y >= 0 &&
           buffer_y < monitor->t.height;
}

int bar_title_click(Client* client, double pointer_x, double pointer_y)
{
    if (bar_on_close_button(client, pointer_x, pointer_y))
        client_send_close(client);
    else if (bar_on_title_bar(client, pointer_x, pointer_y))
        input_title_grab(client);
    else
        return 0;
    return 1;
}

void bar_title_hover(Client* client, double pointer_x, double pointer_y)
{
    Client* now_hovered =
        client && bar_on_close_button(client, pointer_x, pointer_y) ? client : nullptr;
    Client* previously_hovered = bar_hovered;

    if (now_hovered == previously_hovered)
        return;
    bar_hovered = now_hovered;
    /* previously_hovered may be gone */
    Client* other_client;
    wl_list_for_each(other_client, &clients, link) {
        if (other_client == previously_hovered || other_client == now_hovered)
            bar_draw_title_bar(other_client);
    }
}

/* Sizes and places the client's title bar. It spans the whole window, except
 * in the tabbed layout where each client of the group only gets its own slice
 * of the shared row - which is what turns the title bars into tabs. */
void bar_place_title_bar(Client* client)
{
    Monitor* monitor = client->mon;

    if (!client->title || !monitor)
        return;

    int slice_x = 0;
    int slice_width = client->geom.width - 2 * (int)client->bw;
    if (monitor->lt[monitor->sellt]->arrange == layout_tabbed && !client->isfloating &&
        !client->isfullscreen) {
        int tab_index = 0;
        int tab_count = 0;
        Client* other_client;

        wl_list_for_each(other_client, &clients, link) {
            if (!VISIBLEON(other_client, monitor) || other_client->isfloating ||
                other_client->isfullscreen)
                continue;
            if (other_client == client)
                tab_index = tab_count;
            tab_count++;
        }
        if (tab_count) {
            slice_x = slice_width * tab_index / tab_count;
            slice_width = slice_width * (tab_index + 1) / tab_count - slice_x;
        }
    }

    client->titlex = slice_x;
    client->titlew = slice_width;
    wlr_scene_node_set_position(&client->title->node, (int)client->bw + slice_x, (int)client->bw);
    wlr_scene_buffer_set_dest_size(client->title, MAX(slice_width, 1), bar_title_height(client));
}

int bar_title_height(Client* client)
{
    return title_bar && client->mon && !client->isfullscreen ? client->mon->t.real_height : 0;
}

/* layout_arrange() leaves floating clients alone, and no client redraws a title
 * bar, so both are done by hand here */
void bar_toggle_title_bars(const Arg*)
{

    title_bar = !title_bar;

    Monitor* monitor;
    wl_list_for_each(monitor, &monitors, link)
        layout_arrange(monitor);
    Client* client;
    wl_list_for_each(client, &clients, link) {
        if (client->mon && client->isfloating && !client->isfullscreen)
            client_resize(client, client->geom, 1);
    }
    bar_draw_all();

    wl_list_for_each(monitor, &monitors, link) {
        if (monitor->wlr_output->enabled)
            wlr_output_schedule_frame(monitor->wlr_output);
    }
}
