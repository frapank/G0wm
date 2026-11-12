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
static WindowClient* bar_hovered; /* whose close button is under the pointer */

/* function implementations */
/* the top tab, or the focused client */
static int bar_title_selected(WindowClient* client)
{
    Monitor* monitor = client->monitor;

    return client == (monitor->layouts[monitor->selected_layout_index]->arrange == layout_tabbed &&
                              !client->is_floating
                          ? layout_tab_top(monitor)
                          : client_top(monitor));
}

/* x of the close button, bar_width without one */
static int bar_title_close_x(WindowClient* client, int bar_width, int* button_size)
{
    Monitor* monitor = client->monitor;

    *button_size = MAX(monitor->draw_context->font->height * 3 / 5, 4);
    if (!settings->windows.title_close)
        return bar_width;
    const int button_x = bar_width - monitor->horizontal_padding / 2 - *button_size;
    return button_x > bar_width / 2 ? button_x : bar_width;
}

/* draws the title bar of client into buffer */
static void bar_paint_title_bar(WindowClient* client, RenderBuffer* buffer, int for_sharing)
{
    Monitor* monitor = client->monitor;
    const int buffer_width = buffer->base.width;
    const int buffer_height = buffer->base.height;

    const int is_selected = bar_title_selected(client);
    const int scheme_index = is_selected ? COLOR_SCHEME_TITLE_SELECTED : COLOR_SCHEME_TITLE;
    const int is_tab = monitor->layouts[monitor->selected_layout_index]->arrange == layout_tabbed &&
                       !client->is_floating &&
                       client->title_width < client->geometry.width - 2 * (int)client->border_width;
    const uint32_t foreground = settings->look.colors[scheme_index][ColFg];
    const char* title = for_sharing ? privacy_share_title(client) : client_get_title(client);
    int close_button_size;
    const int close_button_x = bar_title_close_x(client, buffer_width, &close_button_size);
    const int dot_size = MAX(monitor->draw_context->font->height / 3, 3);

    uint32_t private_dot_color;
    const int has_private_dot = bar_private_dot(client, &private_dot_color);
    const int dot_count = !!client->is_floating + has_private_dot;
    int text_start =
        monitor->horizontal_padding / 2 + dot_count * (dot_size + monitor->horizontal_padding / 3);
    if (settings->windows.title_center) {
        const int text_width = (int)drwl_font_getwidth(monitor->draw_context, title);
        text_start = MAX(MIN((buffer_width - text_width) / 2,
                             close_button_x - monitor->horizontal_padding / 2 - text_width),
                         text_start);
    }

    drwl_setimage(monitor->draw_context, buffer->image);
    drwl_setscheme(monitor->draw_context, for_sharing ? bar_opaque_scheme(scheme_index)
                                                      : settings->look.colors[scheme_index]);
    if (close_button_x > text_start) {
        drwl_text(monitor->draw_context, 0, 0, close_button_x, buffer_height, text_start, title, 0);
        if (close_button_x < buffer_width)
            drwl_rect(monitor->draw_context, close_button_x, 0, buffer_width - close_button_x,
                      buffer_height, 1, 1);
    } else {
        drwl_rect(monitor->draw_context, 0, 0, buffer_width, buffer_height, 1, 1);
    }

    if (has_private_dot)
        bar_draw_pill(monitor, text_start - dot_size - monitor->horizontal_padding / 3,
                      (buffer_height - dot_size) / 2, dot_size, dot_size,
                      bar_private_radius(dot_size), 0, private_dot_color);
    if (client->is_floating)
        bar_draw_pill(
            monitor, text_start - dot_count * (dot_size + monitor->horizontal_padding / 3),
            (buffer_height - dot_size) / 2, dot_size, dot_size, dot_size / 2.0, 0, foreground);
    if (is_tab && client->title_x)
        bar_draw_pill(monitor, 0, buffer_height / 4, (int)MAX(monitor->wlr_output->scale, 1),
                      buffer_height - buffer_height / 2, 0, 0,
                      bar_fade(settings->look.colors[COLOR_SCHEME_TITLE][ColFg]));
    if (close_button_x < buffer_width && client == bar_hovered)
        bar_draw_pill(monitor, close_button_x, (buffer_height - close_button_size) / 2,
                      close_button_size, close_button_size, close_button_size / 2.0, 0,
                      settings->windows.title_close_color);
}

/* Renders the client's own title bar. In the tabbed layout every client of the
 * group shares one row, so these end up drawn side by side as tabs. */
void bar_draw_title_bar(WindowClient* client)
{
    if (!client->title_scene_buffer)
        return;
    Monitor* monitor = client->monitor;

    bar_place_title_bar(client);
    if (!monitor || !settings->windows.title_bar || client->is_fullscreen ||
        client->title_width <= 0 || !client->scene->node.enabled) {
        wlr_scene_node_set_enabled(&client->title_scene_buffer->node, 0);
        return;
    }

    const int buffer_width = (int)((float)client->title_width * monitor->wlr_output->scale);
    const int buffer_height = monitor->titlebar_metrics.height_px;
    if (buffer_width != client->title_buffer_width_px) {
        buffer_pool_drop(client->title_buffer_pool, LENGTH(client->title_buffer_pool));
        client->title_buffer_width_px = buffer_width;
    }
    RenderBuffer* buffer = buffer_get(client->title_buffer_pool, LENGTH(client->title_buffer_pool),
                                      buffer_width, buffer_height);
    if (!buffer)
        return;
    bar_paint_title_bar(client, buffer, 0);
    RenderBuffer* share_buffer = client->is_private ? bar_share_buffer(client->capture_buffer_pool,
                                                                       buffer_width, buffer_height)
                                                    : nullptr;
    if (share_buffer) {
        bar_paint_title_bar(client, share_buffer, 1);
        wlr_buffer_unlock(&share_buffer->base);
    }

    wlr_scene_node_set_enabled(&client->title_scene_buffer->node, 1);
    wlr_scene_buffer_set_opacity(client->title_scene_buffer, opacity_decorations_current());
    buffer_apply(client->title_scene_buffer, buffer);
    wlr_buffer_unlock(&buffer->base);
}

/* whether (pointer_x, pointer_y) is on client's close button */
static int bar_on_close_button(WindowClient* client, double pointer_x, double pointer_y)
{
    Monitor* monitor = client->monitor;

    if (!monitor || !client->title_scene_buffer || !client->title_scene_buffer->node.enabled)
        return 0;
    const double buffer_x =
        (pointer_x - client->geometry.x - client->border_width - client->title_x) *
        monitor->wlr_output->scale;
    const double buffer_y =
        (pointer_y - client->geometry.y - client->border_width) * monitor->wlr_output->scale;
    int close_button_size;
    const int close_button_x =
        bar_title_close_x(client, client->title_buffer_width_px, &close_button_size);
    return close_button_x < client->title_buffer_width_px && buffer_y >= 0 &&
           buffer_y < monitor->titlebar_metrics.height_px &&
           buffer_x >= close_button_x - monitor->horizontal_padding / 2 &&
           buffer_x < client->title_buffer_width_px;
}

/* whether (pointer_x, pointer_y) is on client's title bar */
int bar_on_title_bar(WindowClient* client, double pointer_x, double pointer_y)
{
    Monitor* monitor = client->monitor;

    if (!monitor || !client->title_scene_buffer || !client->title_scene_buffer->node.enabled)
        return 0;
    const double buffer_x =
        (pointer_x - client->geometry.x - client->border_width - client->title_x) *
        monitor->wlr_output->scale;
    const double buffer_y =
        (pointer_y - client->geometry.y - client->border_width) * monitor->wlr_output->scale;
    return buffer_x >= 0 && buffer_x < client->title_buffer_width_px && buffer_y >= 0 &&
           buffer_y < monitor->titlebar_metrics.height_px;
}

int bar_title_click(WindowClient* client, double pointer_x, double pointer_y)
{
    if (bar_on_close_button(client, pointer_x, pointer_y))
        client_send_close(client);
    else if (bar_on_title_bar(client, pointer_x, pointer_y))
        input_title_grab(client);
    else
        return 0;
    return 1;
}

void bar_title_hover(WindowClient* client, double pointer_x, double pointer_y)
{
    WindowClient* now_hovered =
        client && bar_on_close_button(client, pointer_x, pointer_y) ? client : nullptr;
    WindowClient* previously_hovered = bar_hovered;

    if (now_hovered == previously_hovered)
        return;
    bar_hovered = now_hovered;
    /* previously_hovered may be gone */
    WindowClient* other_client;
    wl_list_for_each(other_client, &clients, link) {
        if (other_client == previously_hovered || other_client == now_hovered)
            bar_draw_title_bar(other_client);
    }
}

/* Sizes and places the client's title bar. It spans the whole window, except
 * in the tabbed layout where each client of the group only gets its own slice
 * of the shared row - which is what turns the title bars into tabs. */
void bar_place_title_bar(WindowClient* client)
{
    Monitor* monitor = client->monitor;

    if (!client->title_scene_buffer || !monitor)
        return;

    int slice_x = 0;
    int slice_width = client->geometry.width - 2 * (int)client->border_width;
    if (monitor->layouts[monitor->selected_layout_index]->arrange == layout_tabbed &&
        !client->is_floating && !client->is_fullscreen) {
        int tab_index = 0;
        int tab_count = 0;
        WindowClient* other_client;

        wl_list_for_each(other_client, &clients, link) {
            if (!VISIBLEON(other_client, monitor) || other_client->is_floating ||
                other_client->is_fullscreen)
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

    client->title_x = slice_x;
    client->title_width = slice_width;
    wlr_scene_node_set_position(&client->title_scene_buffer->node,
                                (int)client->border_width + slice_x, (int)client->border_width);
    wlr_scene_buffer_set_dest_size(client->title_scene_buffer, MAX(slice_width, 1),
                                   bar_title_height(client));
}

int bar_title_height(WindowClient* client)
{
    return settings->windows.title_bar && client->monitor && !client->is_fullscreen
               ? client->monitor->titlebar_metrics.logical_height
               : 0;
}

/* layout_arrange() leaves floating clients alone, and no client redraws a title
 * bar, so both are done by hand here */
void bar_toggle_title_bars(const ActionArg*)
{

    settings->windows.title_bar = !settings->windows.title_bar;

    Monitor* monitor;
    wl_list_for_each(monitor, &monitors, link)
        layout_arrange(monitor);
    WindowClient* client;
    wl_list_for_each(client, &clients, link) {
        if (client->monitor && client->is_floating && !client->is_fullscreen)
            client_resize(client, client->geometry, 1);
    }
    bar_draw_all();

    wl_list_for_each(monitor, &monitors, link) {
        if (monitor->wlr_output->enabled)
            wlr_output_schedule_frame(monitor->wlr_output);
    }
}
