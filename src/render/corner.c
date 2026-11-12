/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rounded corners: each corner is a small buffer of its own, the straight
 * borders stop where it begins
 */
#include <drwl.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "output/monitor.h"
#include "render/buffer.h"
#include "render/corner.h"
#include "render/opacity.h"
#include "window/client.h"

constexpr int CORNER_SAMPLES = 4; /* per axis, so sixteen coverage samples a pixel */

/* function declarations */
static uint32_t corner_color(int scheme);
static const uint8_t* corner_coverage(int radius, int border_thickness);
static double corner_exponent(void);
static double corner_power(double value, double exponent);
static void corner_draw(uint32_t* pixels,
                        const uint8_t* coverage,
                        int radius,
                        int corner,
                        uint32_t color);

struct CornerMask {
    int radius, border_thickness;
    double exponent;
    uint8_t* pixels;
};
static struct CornerMask corner_masks[16];
static size_t corner_mask_next;

void corner_finish(void)
{
    for (size_t i = 0; i < LENGTH(corner_masks); i++) {
        free(corner_masks[i].pixels);
        corner_masks[i] = (struct CornerMask){ 0 };
    }
    corner_mask_next = 0;
}

/* function implementations */

/* The corner follows the superellipse |x/r|^n + |y/r|^n = 1: n = 2 is the
 * circular arc, a larger n eases the curvature into the turn (squircle). */
static double corner_exponent(void)
{
    return settings->windows.corner_style == CORNER_STYLE_SQUIRCLE ? 4.0 : 2.0;
}

static double corner_power(double value, double exponent)
{
    const double square = value * value;
    return exponent == 2.0 ? square : square * square;
}

/* The contents stay square, so the border has to be at least as thick as the
 * curve cuts into the corner along the diagonal. */
unsigned int corner_border_width(void)
{

    if (!settings->windows.corner_radius)
        return settings->windows.border_width;

    double bite = 1.0 - pow(2.0, -1.0 / corner_exponent());
    return MAX(settings->windows.border_width,
               (unsigned int)ceil(bite * (double)settings->windows.corner_radius));
}

/* Radius c is drawn with: none without a border (fullscreen, unmanaged),
 * never more than half the box. */
int corner_client_radius(WindowClient* client)
{
    if (!settings->windows.corner_radius || !client->border_width || !client->corner_buffers[0])
        return 0;
    return MAX(0, MIN((int)settings->windows.corner_radius,
                      MIN(client->geometry.width, client->geometry.height) / 2));
}

/* Radius the bar is cut to, in layout coordinates. */
int corner_bar_radius(Monitor* monitor)
{
    if (!settings->windows.corner_radius)
        return 0;
    return MAX(
        0, MIN((int)settings->windows.corner_radius,
               MIN(monitor->bar_metrics.logical_width, monitor->bar_metrics.logical_height) / 2));
}

/* Clear the four corner squares before drawing into a buffer: buffer_get()
 * hands back a used one and corner_cut() only fades what it finds there. */
void corner_clear(uint32_t* pixels, int width, int height, int radius)
{
    for (int corner = 0; corner < 4 && radius > 0; corner++)
        for (int y = 0; y < radius; y++)
            for (int x = 0; x < radius; x++)
                pixels[(size_t)(corner & 2 ? height - 1 - y : y) * (size_t)width +
                       (size_t)(corner & 1 ? width - 1 - x : x)] = 0;
}

/* Cut the curve out of a buffer that already holds a drawing: the bar has no
 * border, it just fades out along it. */
void corner_cut(uint32_t* pixels, int width, int height, int radius)
{
    if (radius <= 0)
        return;

    /* bw = r, no inner curve: the cut goes all the way through */
    const uint8_t* coverage = corner_coverage(radius, radius);
    for (int corner = 0; corner < 4; corner++) {
        for (int y = 0; y < radius; y++) {
            for (int x = 0; x < radius; x++) {
                const uint32_t alpha = coverage[(size_t)y * (size_t)radius + (size_t)x];
                const size_t offset = (size_t)(corner & 2 ? height - 1 - y : y) * (size_t)width +
                                      (size_t)(corner & 1 ? width - 1 - x : x);
                const uint32_t pixel = pixels[offset];
                pixels[offset] = ((((pixel >> 24) & 0xff) * alpha / 255) << 24) |
                                 ((((pixel >> 16) & 0xff) * alpha / 255) << 16) |
                                 ((((pixel >> 8) & 0xff) * alpha / 255) << 8) |
                                 ((pixel & 0xff) * alpha / 255);
            }
        }
    }
}

/* Border colour of a scheme as the premultiplied ARGB the buffers hold,
 * faded like the straight borders in opacity_set_border_color(). */
static uint32_t corner_color(int scheme)
{
    uint32_t border = settings->look.colors[scheme][ColBorder];
    uint32_t alpha = (uint32_t)((float)(border & 0xff) * opacity_decorations_current());

    return (alpha << 24) | ((((border >> 24) & 0xff) * alpha / 255) << 16) |
           ((((border >> 16) & 0xff) * alpha / 255) << 8) | (((border >> 8) & 0xff) * alpha / 255);
}

/* Coverage of the ring between the outer curve and the inner one, a byte a
 * pixel, for the top left corner; the others are it mirrored. Sampling
 * antialiases both edges of the ring in one pass, whatever the curve. */
static const uint8_t* corner_coverage(int radius, int border_thickness)
{
    const double exponent = corner_exponent();
    for (size_t i = 0; i < LENGTH(corner_masks); i++) {
        const struct CornerMask* mask = &corner_masks[i];
        if (mask->pixels && mask->radius == radius && mask->border_thickness == border_thickness &&
            mask->exponent == exponent)
            return mask->pixels;
    }
    uint8_t* coverage = util_calloc((size_t)radius * (size_t)radius, 1);
    const double inner_radius = radius - border_thickness;

    for (int y = 0; y < radius; y++) {
        for (int x = 0; x < radius; x++) {
            int covered_samples = 0;
            for (int j = 0; j < CORNER_SAMPLES; j++) {
                for (int i = 0; i < CORNER_SAMPLES; i++) {
                    const double sample_x =
                        ((double)radius - (x + (i + 0.5) / CORNER_SAMPLES)) / radius;
                    const double sample_y =
                        ((double)radius - (y + (j + 0.5) / CORNER_SAMPLES)) / radius;
                    if (corner_power(sample_x, exponent) + corner_power(sample_y, exponent) > 1.0)
                        continue; /* outside the window */
                    if (inner_radius > 0.0 &&
                        corner_power(sample_x * radius / inner_radius, exponent) +
                                corner_power(sample_y * radius / inner_radius, exponent) <=
                            1.0)
                        continue; /* inside, where the contents are */
                    covered_samples++;
                }
            }
            coverage[(size_t)y * (size_t)radius + (size_t)x] =
                (uint8_t)(covered_samples * 255 / (CORNER_SAMPLES * CORNER_SAMPLES));
        }
    }
    struct CornerMask* mask = &corner_masks[corner_mask_next++ % LENGTH(corner_masks)];
    free(mask->pixels);
    *mask = (struct CornerMask){ radius, border_thickness, exponent, coverage };
    return coverage;
}

/* Paint one corner from that coverage. 0 is the top left, the low bits of
 * the index mirror it: 1 right, 2 down, 3 both. */
static void corner_draw(uint32_t* pixels,
                        const uint8_t* coverage,
                        int radius,
                        int corner,
                        uint32_t color)
{

    for (int y = 0; y < radius; y++) {
        const int source_y = corner & 2 ? radius - 1 - y : y;
        for (int x = 0; x < radius; x++) {
            const int source_x = corner & 1 ? radius - 1 - x : x;
            const uint32_t alpha = coverage[(size_t)source_y * (size_t)radius + (size_t)source_x];
            pixels[(size_t)y * (size_t)radius + (size_t)x] =
                ((((color >> 24) & 0xff) * alpha / 255) << 24) |
                ((((color >> 16) & 0xff) * alpha / 255) << 16) |
                ((((color >> 8) & 0xff) * alpha / 255) << 8) | ((color & 0xff) * alpha / 255);
        }
    }
}

/* Size, place and paint the four corners. Called on every resize and colour
 * change, so redraw only when the radius, border or colour changed. */
void corner_draw_all(WindowClient* client)
{
    Monitor* monitor = client->monitor;
    const int radius = corner_client_radius(client);
    int pos[4][2];

    if (!client->corner_buffers[0])
        return;
    if (!radius || !monitor) {
        for (int i = 0; i < 4; i++)
            wlr_scene_node_set_enabled(&client->corner_buffers[i]->node, 0);
        return;
    }

    pos[0][0] = 0;
    pos[0][1] = 0;
    pos[1][0] = client->geometry.width - radius;
    pos[1][1] = 0;
    pos[2][0] = 0;
    pos[2][1] = client->geometry.height - radius;
    pos[3][0] = client->geometry.width - radius;
    pos[3][1] = client->geometry.height - radius;
    for (int i = 0; i < 4; i++) {
        wlr_scene_node_set_position(&client->corner_buffers[i]->node, pos[i][0], pos[i][1]);
        wlr_scene_buffer_set_dest_size(client->corner_buffers[i], radius, radius);
        wlr_scene_node_set_enabled(&client->corner_buffers[i]->node, 1);
    }

    /* drawn at output scale, like the bar */
    const int scaled_radius = (int)roundf((float)radius * monitor->wlr_output->scale);
    const int scaled_border = (int)roundf((float)client->border_width * monitor->wlr_output->scale);
    uint32_t color = corner_color(client->border_scheme);
    if (scaled_radius <= 0 || (scaled_radius == client->cached_corner_radius &&
                               scaled_border == client->cached_corner_border_width &&
                               color == client->cached_corner_color))
        return;

    if (scaled_radius != client->cached_corner_radius)
        for (int i = 0; i < 4; i++)
            buffer_pool_drop(client->corner_buffer_pool[i], LENGTH(client->corner_buffer_pool[i]));
    client->cached_corner_radius = scaled_radius;
    client->cached_corner_border_width = scaled_border;
    client->cached_corner_color = color;

    const uint8_t* cov = corner_coverage(scaled_radius, scaled_border);
    for (int i = 0; i < 4; i++) {
        RenderBuffer* buffer =
            buffer_get(client->corner_buffer_pool[i], LENGTH(client->corner_buffer_pool[i]),
                       scaled_radius, scaled_radius);
        if (!buffer)
            continue;
        corner_draw(buffer->pixels, cov, scaled_radius, i, color);
        buffer_apply(client->corner_buffers[i], buffer);
        wlr_buffer_unlock(&buffer->base);
    }
}
