/*
 * SPDX-FileCopyrightText: Hansvon
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * opacity, the blurred wallpaper and the frosted decorations
 */
#include <drwl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/bar/bar.h"
#include "desktop/bar/title.h"
#include "output/monitor.h"
#include "render/buffer.h"
#include "render/corner.h"
#include "render/opacity.h"
#include "window/client.h"
#include "window/layout.h"

constexpr float OPACITY_MIN = 0.1f; /* floor for the opacity keybindings */

/* function declarations */
#ifdef INTEGRATED_BACKGROUND
static void opacity_blur_box(Client* client,
                             struct wlr_scene_buffer* node,
                             struct wlr_buffer** last,
                             int box_x,
                             int box_y,
                             int box_width,
                             int box_height);
static void opacity_blur_rows(uint32_t* dst,
                              const uint32_t* src,
                              int width,
                              int height,
                              int radius);
static void opacity_blur_shrink(uint32_t* dst,
                                const uint32_t* src,
                                int width,
                                int height,
                                int block_size);
static void opacity_blur_source_box(Monitor* monitor,
                                    struct wlr_fbox* source_box,
                                    int x,
                                    int y,
                                    int width,
                                    int height);
static void opacity_blur_tint(uint32_t* pixels, size_t count);
static void opacity_blur_transpose(uint32_t* dst, const uint32_t* src, int width, int height);
static void opacity_blur_wallpaper(Monitor* monitor);
#endif /* INTEGRATED_BACKGROUND */
static int opacity_decorations_translucent(void);
static void opacity_refresh(void);

/* variables */
#ifdef INTEGRATED_BACKGROUND
static int opacity_wallpaper_failed;
#endif /* INTEGRATED_BACKGROUND */

/* function implementations */
#ifdef INTEGRATED_BACKGROUND
/* Repoints the bar's backdrop at the strip of frosted wallpaper it sits on.
 * The bar is drawn by g0wm, so opacity_decorations_translucent() decides
 * whether it shows. */
void opacity_blur_bar(Monitor* monitor)
{
    /* the backdrop is square, keep it off the corners a rounded bar drops */
    const int bar_rounding = corner_bar_radius(monitor);

    if (!monitor->barblur)
        return;

    Buffer* buffer = monitor->blurpool[0];
    int x = (int)bar_padding + bar_rounding;
    int y =
        bar_top ? (int)bar_padding : monitor->m.height - monitor->b.real_height - (int)bar_padding;
    int width = MIN(monitor->b.real_width - 2 * bar_rounding, monitor->wallpaperw - x);
    int height = MIN(monitor->b.real_height, monitor->wallpaperh - y);
    if (!buffer || !monitor->scene_buffer->node.enabled || !opacity_enabled ||
        !opacity_decorations_translucent() || y < 0 || width <= 0 || height <= 0) {
        wlr_scene_node_set_enabled(&monitor->barblur->node, 0);
        return;
    }

    struct wlr_fbox source_box;
    opacity_blur_source_box(monitor, &source_box, x, y, width, height);
    wlr_scene_node_set_position(&monitor->barblur->node, monitor->m.x + x, monitor->m.y + y);
    wlr_scene_buffer_set_dest_size(monitor->barblur, width, height);
    wlr_scene_buffer_set_source_box(monitor->barblur, &source_box);
    wlr_scene_node_set_enabled(&monitor->barblur->node, 1);
}

/* Points one of a client's backdrop nodes at the frosted wallpaper under the
 * box it covers, in the coordinates of the client's own tree. last is what the
 * node already holds: set_buffer drops the texture and damages the node, so
 * handing it the same buffer every frame never stops asking for another. */
static void opacity_blur_box(Client* client,
                             struct wlr_scene_buffer* node,
                             struct wlr_buffer** last,
                             int box_x,
                             int box_y,
                             int box_width,
                             int box_height)
{
    Monitor* monitor = client->mon;
    Buffer* buffer = monitor ? monitor->blurpool[0] : nullptr;

    if (!node || !monitor)
        return;

    /* same for a rounded client: shrink the box to the contents, or the
     * square backdrop fills the corners back in */
    if (corner_client_radius(client)) {
        const int right = MIN(box_x + box_width, client->geom.width - (int)client->bw);
        const int bottom = MIN(box_y + box_height, client->geom.height - (int)client->bw);
        box_x = MAX(box_x, (int)client->bw);
        box_y = MAX(box_y, (int)client->bw);
        box_width = right - box_x;
        box_height = bottom - box_y;
        if (box_width <= 0 || box_height <= 0) {
            wlr_scene_node_set_enabled(&node->node, 0);
            return;
        }
    }

    /* the wallpaper covers the monitor pixel for pixel, so the crop is just
     * the box in monitor-local coordinates, clipped to it */
    int x = MAX(0, client->geom.x - monitor->m.x + box_x);
    int y = MAX(0, client->geom.y - monitor->m.y + box_y);
    int width = MIN(client->geom.x - monitor->m.x + box_x + box_width, monitor->wallpaperw) - x;
    int height = MIN(client->geom.y - monitor->m.y + box_y + box_height, monitor->wallpaperh) - y;
    if (!buffer || width <= 0 || height <= 0) {
        wlr_scene_node_set_enabled(&node->node, 0);
        return;
    }

    struct wlr_fbox source_box;
    opacity_blur_source_box(monitor, &source_box, x, y, width, height);
    /* the node hangs off the client's tree, hence the offset back into it */
    wlr_scene_node_set_position(&node->node, x - (client->geom.x - monitor->m.x),
                                y - (client->geom.y - monitor->m.y));
    wlr_scene_buffer_set_dest_size(node, width, height);
    wlr_scene_buffer_set_source_box(node, &source_box);
    if (*last != &buffer->base) {
        wlr_scene_buffer_set_buffer(node, &buffer->base);
        *last = &buffer->base;
    }
    wlr_scene_node_set_enabled(&node->node, 1);
}

/* Points a client's backdrop at the frosted wallpaper it covers. Done while
 * rendering, so nothing that moves a window has to keep the two in step. */
void opacity_blur_client(Client* client)
{
    Monitor* monitor = client->mon;

    if (!client->blur)
        return;

    /* the box covers the decorations too, so either letting light through is
     * enough; a fullscreen client is drawn opaque and never does */
    if (!monitor || !monitor->blurpool[0] || client->isfullscreen || !opacity_enabled ||
        (client->opacity >= 1.0f && !opacity_decorations_translucent())) {
        wlr_scene_node_set_enabled(&client->blur->node, 0);
        if (client->titleblur)
            wlr_scene_node_set_enabled(&client->titleblur->node, 0);
        return;
    }

    int title_height = bar_title_height(client);
    int border = (int)client->bw;

    if (!title_height || monitor->lt[monitor->sellt]->arrange != layout_tabbed ||
        client->isfloating) {
        opacity_blur_box(client, client->blur, &client->blurbuf, 0, 0, client->geom.width,
                         client->geom.height);
        if (client->titleblur)
            wlr_scene_node_set_enabled(&client->titleblur->node, 0);
        return;
    }

    /* Tabs share one box and the ones under the top are drawn in lower trees,
     * so a backdrop over the whole box buries their title bars. Each tab backs
     * its own slice of the row, the top one backs the body below it. */
    int x = border + client->titlex;
    int tab_width = client->titlew;
    if (client->titlex == 0) { /* first tab, take the corner in */
        x = 0;
        tab_width += border;
    }
    if (client->titlex + client->titlew == client->geom.width - 2 * border)
        tab_width += border; /* last tab, same */
    opacity_blur_box(client, client->titleblur, &client->titleblurbuf, x, 0, tab_width,
                     border + title_height);

    if (client == layout_tab_top(monitor))
        opacity_blur_box(client, client->blur, &client->blurbuf, 0, border + title_height,
                         client->geom.width, client->geom.height - border - title_height);
    else
        wlr_scene_node_set_enabled(&client->blur->node, 0);
}

/* One box blur along the rows: a 2r+1 window slides on a running sum, so a
 * pass costs the same whatever the radius is, and three land near a Gaussian.
 */
static void opacity_blur_rows(uint32_t* dst, const uint32_t* src, int width, int height, int radius)
{
    for (int y = 0; y < height; y++) {
        const uint32_t* source_row = src + (size_t)y * (size_t)width;
        uint32_t* dest_row = dst + (size_t)y * (size_t)width;
        uint32_t sum_red = 0;
        uint32_t sum_green = 0;
        uint32_t sum_blue = 0;
        uint32_t window_count = 0;

        for (int i = 0; i <= radius && i < width; i++) {
            sum_red += (source_row[i] >> 16) & 0xff;
            sum_green += (source_row[i] >> 8) & 0xff;
            sum_blue += source_row[i] & 0xff;
            window_count++;
        }
        for (int x = 0; x < width; x++) {
            dest_row[x] = 0xff000000u | ((sum_red / window_count) << 16) |
                          ((sum_green / window_count) << 8) | (sum_blue / window_count);
            if (x - radius >= 0) {
                sum_red -= (source_row[x - radius] >> 16) & 0xff;
                sum_green -= (source_row[x - radius] >> 8) & 0xff;
                sum_blue -= source_row[x - radius] & 0xff;
                window_count--;
            }
            if (x + radius + 1 < width) {
                sum_red += (source_row[x + radius + 1] >> 16) & 0xff;
                sum_green += (source_row[x + radius + 1] >> 8) & 0xff;
                sum_blue += source_row[x + radius + 1] & 0xff;
                window_count++;
            }
        }
    }
}

/* Averages every d*d block down to one pixel; the blur then runs on the
 * small image, which is what keeps a radius this wide affordable. */
static void opacity_blur_shrink(uint32_t* dst,
                                const uint32_t* src,
                                int width,
                                int height,
                                int block_size)
{
    int small_width = MAX(1, width / block_size), small_height = MAX(1, height / block_size);

    for (int y = 0; y < small_height; y++)
        for (int x = 0; x < small_width; x++) {
            uint32_t sum_red = 0;
            uint32_t sum_green = 0;
            uint32_t sum_blue = 0;
            uint32_t count = 0;

            for (int j = y * block_size; j < MIN(height, (y + 1) * block_size); j++)
                for (int i = x * block_size; i < MIN(width, (x + 1) * block_size); i++) {
                    uint32_t pixel = src[(size_t)j * (size_t)width + (size_t)i];

                    sum_red += (pixel >> 16) & 0xff;
                    sum_green += (pixel >> 8) & 0xff;
                    sum_blue += pixel & 0xff;
                    count++;
                }
            dst[(size_t)y * (size_t)small_width + (size_t)x] =
                count ? 0xff000000u | ((sum_red / count) << 16) | ((sum_green / count) << 8) |
                            (sum_blue / count)
                      : 0xff000000u;
        }
}

/* Maps a monitor-local rectangle onto the smaller frosted buffer. The crop
 * stays in floating point so it does not snap to that buffer's grid. */
static void opacity_blur_source_box(Monitor* monitor,
                                    struct wlr_fbox* source_box,
                                    int x,
                                    int y,
                                    int width,
                                    int height)
{
    double scale_x = (double)monitor->blurw / (double)monitor->wallpaperw;
    double scale_y = (double)monitor->blurh / (double)monitor->wallpaperh;

    source_box->x = x * scale_x;
    source_box->y = y * scale_y;
    source_box->width = width * scale_x;
    source_box->height = height * scale_y;
}

/* A blur this wide leaves grey mush, so the saturation is pushed back up:
 * that is what makes it read as glass rather than as a smudge. */
static void opacity_blur_tint(uint32_t* pixels, size_t count)
{
    if (blur_saturation == 1.0f && blur_brightness == 1.0f)
        return;

    for (size_t i = 0; i < count; i++) {
        float red = (float)((pixels[i] >> 16) & 0xff);
        float green = (float)((pixels[i] >> 8) & 0xff);
        float blue = (float)(pixels[i] & 0xff);
        float luma = 0.2126f * red + 0.7152f * green + 0.0722f * blue;

        red = (luma + (red - luma) * blur_saturation) * blur_brightness;
        green = (luma + (green - luma) * blur_saturation) * blur_brightness;
        blue = (luma + (blue - luma) * blur_saturation) * blur_brightness;
        pixels[i] = 0xff000000u | ((uint32_t)(MAX(0.0f, MIN(255.0f, red)) + 0.5f) << 16) |
                    ((uint32_t)(MAX(0.0f, MIN(255.0f, green)) + 0.5f) << 8) |
                    (uint32_t)(MAX(0.0f, MIN(255.0f, blue)) + 0.5f);
    }
}

/* Turns a w*h image on its side, so that blurring its rows blurs the columns
 * of the original and one row pass covers both directions. */
static void opacity_blur_transpose(uint32_t* dst, const uint32_t* src, int width, int height)
{
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            dst[(size_t)x * (size_t)height + (size_t)y] =
                src[(size_t)y * (size_t)width + (size_t)x];
}

/* Builds the frosted copy OpacityBlur shows through everything transparent,
 * once per wallpaper rather than per frame. It stays at 1/d of the monitor
 * and the GPU scales it back up: a backdrop is never occluded away, so a
 * full-size copy would cross the bus on every frame anything moved. */
static void opacity_blur_wallpaper(Monitor* monitor)
{
    int width = monitor->wallpaperw;
    int height = monitor->wallpaperh;

    Buffer* buffer;
    /* the bar's node is what owns the buffer, so it lets go of it first */
    if (monitor->barblur)
        wlr_scene_buffer_set_buffer(monitor->barblur, nullptr);
    buffer_pool_drop(monitor->blurpool, LENGTH(monitor->blurpool));
    monitor->blurw = monitor->blurh = 0;

    /* the nodes pick the new buffer up next frame; the pointer they compare
     * against has to go now, or a reused address would read as unchanged */
    Client* client;
    wl_list_for_each(client, &clients, link)
        if (client->mon == monitor)
            client->blurbuf = client->titleblurbuf = nullptr;

    if (opacity_type != OpacityBlur || !monitor->wallpaperpool[0] || width <= 0 || height <= 0)
        return;

    /* the shrink blurs by itself, so the radius shrinks with the image; never
     * less than a quarter, as this size is what the effect costs per frame */
    int block_size = MAX(4, MIN(8, (int)blur_radius / 4));
    int small_width = MAX(1, width / block_size);
    int small_height = MAX(1, height / block_size);
    int pass_radius = MAX(1, (int)blur_radius / block_size);

    if (!(buffer =
              buffer_get(monitor->blurpool, LENGTH(monitor->blurpool), small_width, small_height)))
        return;
    uint32_t* current;
    current = util_calloc((size_t)small_width * (size_t)small_height, sizeof(*current));
    uint32_t* scratch = util_calloc((size_t)small_width * (size_t)small_height, sizeof(*scratch));

    opacity_blur_shrink(current, monitor->wallpaperpool[0]->data, width, height, block_size);
    uint32_t* swap;
    for (unsigned int i = 0; i < blur_passes; i++) {
        opacity_blur_rows(scratch, current, small_width, small_height, pass_radius);
        swap = current, current = scratch, scratch = swap;
    }
    opacity_blur_transpose(scratch, current, small_width, small_height);
    swap = current, current = scratch, scratch = swap;
    for (unsigned int i = 0; i < blur_passes; i++) {
        opacity_blur_rows(scratch, current, small_height, small_width, pass_radius);
        swap = current, current = scratch, scratch = swap;
    }
    opacity_blur_transpose(scratch, current, small_height, small_width);
    swap = current, current = scratch, scratch = swap;

    opacity_blur_tint(current, (size_t)small_width * (size_t)small_height);
    memcpy(buffer->data, current, (size_t)small_width * (size_t)small_height * sizeof(*current));
    free(current);
    free(scratch);

    /* what opacity_blur_bar() and opacity_blur_client() scale their crops by */
    monitor->blurw = small_width;
    monitor->blurh = small_height;

    /* the pool keeps no reference of its own: the bar's node is what holds
     * the buffer up, and the clients take their own lock as they pick it up */
    if (monitor->barblur)
        wlr_scene_buffer_set_buffer(monitor->barblur, &buffer->base);
    wlr_buffer_unlock(&buffer->base);
    opacity_blur_bar(monitor);
}

#endif /* INTEGRATED_BACKGROUND */
/* The opacity g0wm's own drawing runs at. It rides on opacity_enabled, so the
 * one key turns the windows and the decorations off and on together. */
float opacity_decorations_current(void)
{
    return opacity_enabled ? opacity_decorations : 1.0f;
}

/* Whether light gets through what g0wm draws itself. opacity_decorations fades
 * the lot, and a colour in colors[] can carry an alpha of its own, which is how
 * a bar stays see-through with opacity_decorations left at 1. */
static int opacity_decorations_translucent(void)
{
    if (opacity_decorations_current() < 1.0f)
        return 1;
    for (size_t i = 0; i < LENGTH(colors); i++)
        for (size_t j = 1; j < LENGTH(colors[i]); j++) /* 0 is the foreground */
            if ((colors[i][j] & 0xff) < 0xff)
                return 1;
    return 0;
}

#ifdef INTEGRATED_BACKGROUND
void opacity_set_wallpaper(Monitor* monitor)
{
    GError* error = nullptr;
    int monitor_width = monitor->m.width;
    int monitor_height = monitor->m.height;

    if (!wallpaper || !*wallpaper || monitor_width <= 0 || monitor_height <= 0) {
        wlr_scene_buffer_set_buffer(monitor->wallpaper, nullptr);
        monitor->wallpaperbuf = nullptr;
        buffer_pool_drop(monitor->wallpaperpool, LENGTH(monitor->wallpaperpool));
        monitor->wallpaperw = monitor->wallpaperh = 0;
        opacity_blur_wallpaper(monitor);
        return;
    }

    if (monitor->wallpaperpool[0] && monitor_width == monitor->wallpaperw &&
        monitor_height == monitor->wallpaperh) {
        wlr_scene_buffer_set_dest_size(monitor->wallpaper, monitor_width, monitor_height);
        wlr_scene_node_set_position(&monitor->wallpaper->node, monitor->m.x, monitor->m.y);
        /* guarded like opacity_blur_client()'s: set_buffer throws the texture
         * away, and this path runs on every monitor layout change */
        if (monitor->wallpaperbuf != &monitor->wallpaperpool[0]->base) {
            wlr_scene_buffer_set_buffer(monitor->wallpaper, &monitor->wallpaperpool[0]->base);
            monitor->wallpaperbuf = &monitor->wallpaperpool[0]->base;
        }
        /* the frosted copy is the same size and survives with it, unless the
         * monitor went away and took it along */
        if (!monitor->blurpool[0])
            opacity_blur_wallpaper(monitor);
        else
            opacity_blur_bar(monitor);
        return;
    }

    if (!wallpaper_source && !opacity_wallpaper_failed &&
        !(wallpaper_source = gdk_pixbuf_new_from_file(wallpaper, &error))) {
        wlr_log(WLR_ERROR, "wallpaper: %s", error->message);
        g_error_free(error);
        opacity_wallpaper_failed = 1;
    }
    if (!wallpaper_source)
        return;

    int source_width = gdk_pixbuf_get_width(wallpaper_source);
    int source_height = gdk_pixbuf_get_height(wallpaper_source);
    double scalefactor =
        MAX((double)monitor_width / source_width, (double)monitor_height / source_height);
    int scaled_width = MAX(1, (int)lround(source_width * scalefactor));
    int scaled_height = MAX(1, (int)lround(source_height * scalefactor));
    GdkPixbuf* scaled =
        gdk_pixbuf_scale_simple(wallpaper_source, scaled_width, scaled_height, GDK_INTERP_BILINEAR);
    if (!scaled) {
        wlr_log(WLR_ERROR, "wallpaper: failed to scale %s", wallpaper);
        return;
    }

    int offset_x = MIN(scaled_width - monitor_width, (scaled_width - monitor_width) / 2);
    int offset_y = MIN(scaled_height - monitor_height, (scaled_height - monitor_height) / 2);

    buffer_pool_drop(monitor->wallpaperpool, LENGTH(monitor->wallpaperpool));
    Buffer* buffer;
    if (!(buffer = buffer_get(monitor->wallpaperpool, LENGTH(monitor->wallpaperpool), monitor_width,
                              monitor_height))) {
        g_object_unref(scaled);
        return;
    }

    guchar* pixels = gdk_pixbuf_get_pixels(scaled);
    int rowstride = gdk_pixbuf_get_rowstride(scaled);
    int channels = gdk_pixbuf_get_n_channels(scaled);
    uint32_t* dest = buffer->data;
    int x;
    for (int y = 0; y < monitor_height; y++) {
        guchar* source_row =
            pixels + (size_t)(y + offset_y) * rowstride + (size_t)offset_x * channels;
        for (x = 0; x < monitor_width; x++, source_row += channels, dest++)
            *dest = (0xffu << 24) | ((uint32_t)source_row[0] << 16) |
                    ((uint32_t)source_row[1] << 8) | (uint32_t)source_row[2];
    }
    g_object_unref(scaled);
    monitor->wallpaperw = monitor_width;
    monitor->wallpaperh = monitor_height;

    wlr_scene_buffer_set_dest_size(monitor->wallpaper, monitor_width, monitor_height);
    wlr_scene_node_set_position(&monitor->wallpaper->node, monitor->m.x, monitor->m.y);
    wlr_scene_buffer_set_buffer(monitor->wallpaper, &buffer->base);
    monitor->wallpaperbuf = &buffer->base;
    wlr_buffer_unlock(&buffer->base);
    opacity_blur_wallpaper(monitor);
}

#endif /* INTEGRATED_BACKGROUND */
/* opacity is (re)applied while rendering, and changing it damages nothing by
 * itself, so a frame has to be asked for everywhere */
static void opacity_refresh(void)
{
    Client* client;

    /* a decoration is drawn by g0wm, not by a client, so nothing would come
     * back to redraw it: the bar and the title bars follow bar_draw_all() */
    wl_list_for_each(client, &clients, link)
        opacity_set_border_color(client, client->borderscheme);
    bar_draw_all();

    Monitor* monitor;
    wl_list_for_each(monitor, &monitors, link) {
        if (monitor->wlr_output->enabled)
            wlr_output_schedule_frame(monitor->wlr_output);
    }
}

void opacity_set_scene_buffer(struct wlr_scene_buffer* buffer, int, int, void* data)
{
    Client* client = data;
    /* xdg-popups hang off Client.scene, not Client.scene_surface, so this
     * never touches them */
    wlr_scene_buffer_set_opacity(buffer,
                                 client->isfullscreen || !opacity_enabled ? 1.0f : client->opacity);
}

/* Colours a client's border, remembering the scheme so the opacity toggle can
 * put it back on. wlroots wants a premultiplied colour, so the alpha scales
 * the other three channels with it. */
void opacity_set_border_color(Client* client, int scheme)
{
    float color[4] = COLOR(colors[scheme][ColBorder]);
    float alpha = color[3] * opacity_decorations_current();

    client->borderscheme = scheme;
    color[0] *= alpha;
    color[1] *= alpha;
    color[2] *= alpha;
    color[3] = alpha;
    client_set_border_color(client, color);
    corner_draw_all(client);
}

/* arg->f is added to the opacity the focused client uses while focused */
void opacity_change_focused(const Arg* arg)
{
    Client* sel = client_top(selected_monitor);

    if (!sel)
        return;
    sel->opacity_focus = MIN(MAX(sel->opacity_focus + arg->f, OPACITY_MIN), 1.0f);
    sel->opacity = sel->opacity_focus;
    opacity_refresh();
}

/* the same for the opacity it falls back to once it loses focus */
void opacity_change_unfocused(const Arg* arg)
{
    Client* sel = client_top(selected_monitor);

    if (!sel)
        return;
    sel->opacity_unfocus = MIN(MAX(sel->opacity_unfocus + arg->f, OPACITY_MIN), 1.0f);
}

/* the wallpaper is decoded once and the frosted copy cached with it, so a
 * reload has to throw both away before anything redraws */
void opacity_reload(void)
{
#ifdef INTEGRATED_BACKGROUND

    if (wallpaper_source) {
        g_object_unref(wallpaper_source);
        wallpaper_source = nullptr;
    }
    opacity_wallpaper_failed = 0;
    Monitor* monitor;
    wl_list_for_each(monitor, &monitors, link) {
        buffer_pool_drop(monitor->wallpaperpool, LENGTH(monitor->wallpaperpool));
        buffer_pool_drop(monitor->blurpool, LENGTH(monitor->blurpool));
        monitor->wallpaperbuf = nullptr;
        monitor->wallpaperw = monitor->wallpaperh = 0;
        monitor->blurw = monitor->blurh = 0;
        opacity_set_wallpaper(monitor);
    }
#endif
    opacity_refresh();
}

void opacity_toggle(const Arg*)
{
    opacity_enabled = !opacity_enabled;
    opacity_refresh();
}
