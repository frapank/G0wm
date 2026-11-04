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

constexpr int CORNER_SAMPLES =
    4; /* per axis, so sixteen coverage samples a pixel */

/* function declarations */
static uint32_t corner_color(int scheme);
static uint8_t* corner_coverage(int r, int bw);
static double corner_exponent(void);
static void corner_draw(uint32_t* px,
                        const uint8_t* cov,
                        int r,
                        int corner,
                        uint32_t color);

/* function implementations */

/* The corner follows the superellipse |x/r|^n + |y/r|^n = 1: n = 2 is the
 * circular arc, a larger n eases the curvature into the turn (squircle). */
static double corner_exponent(void)
{
    return corner_style == CornerSquircle ? 4.0 : 2.0;
}

/* The contents stay square, so the border has to be at least as thick as the
 * curve cuts into the corner along the diagonal. */
unsigned int corner_border_width(void)
{
    double bite;

    if (!corner_radius)
        return border_width;

    bite = 1.0 - pow(2.0, -1.0 / corner_exponent());
    return MAX(border_width, (unsigned int)ceil(bite * (double)corner_radius));
}

/* Radius c is drawn with: none without a border (fullscreen, unmanaged),
 * never more than half the box. */
int corner_client_radius(Client* c)
{
    if (!corner_radius || !c->bw || !c->corner[0])
        return 0;
    return MAX(0,
               MIN((int)corner_radius, MIN(c->geom.width, c->geom.height) / 2));
}

/* Radius the bar is cut to, in layout coordinates. */
int corner_bar_radius(Monitor* m)
{
    if (!corner_radius)
        return 0;
    return MAX(
        0, MIN((int)corner_radius, MIN(m->b.real_width, m->b.real_height) / 2));
}

/* Clear the four corner squares before drawing into a buffer: buffer_get()
 * hands back a used one and corner_cut() only fades what it finds there. */
void corner_clear(uint32_t* px, int w, int h, int r)
{
    for (int i = 0; i < 4 && r > 0; i++)
        for (int y = 0; y < r; y++)
            for (int x = 0; x < r; x++)
                px[(size_t)(i & 2 ? h - 1 - y : y) * (size_t)w +
                   (size_t)(i & 1 ? w - 1 - x : x)] = 0;
}

/* Cut the curve out of a buffer that already holds a drawing: the bar has no
 * border, it just fades out along it. */
void corner_cut(uint32_t* px, int w, int h, int r)
{
    uint8_t* cov;
    uint32_t a, p;
    size_t o;

    if (r <= 0)
        return;

    /* bw = r, no inner curve: the cut goes all the way through */
    cov = corner_coverage(r, r);
    for (int i = 0; i < 4; i++) {
        for (int y = 0; y < r; y++) {
            for (int x = 0; x < r; x++) {
                a = cov[(size_t)y * (size_t)r + (size_t)x];
                o = (size_t)(i & 2 ? h - 1 - y : y) * (size_t)w +
                    (size_t)(i & 1 ? w - 1 - x : x);
                p = px[o];
                px[o] = ((((p >> 24) & 0xff) * a / 255) << 24) |
                        ((((p >> 16) & 0xff) * a / 255) << 16) |
                        ((((p >> 8) & 0xff) * a / 255) << 8) |
                        ((p & 0xff) * a / 255);
            }
        }
    }
    free(cov);
}

/* Border colour of a scheme as the premultiplied ARGB the buffers hold,
 * faded like the straight borders in opacity_set_border_color(). */
static uint32_t corner_color(int scheme)
{
    uint32_t clr = colors[scheme][ColBorder];
    uint32_t a =
        (uint32_t)((float)(clr & 0xff) * opacity_decorations_current());

    return (a << 24) | ((((clr >> 24) & 0xff) * a / 255) << 16) |
           ((((clr >> 16) & 0xff) * a / 255) << 8) |
           (((clr >> 8) & 0xff) * a / 255);
}

/* Coverage of the ring between the outer curve and the inner one, a byte a
 * pixel, for the top left corner; the others are it mirrored. Sampling
 * antialiases both edges of the ring in one pass, whatever the curve. */
static uint8_t* corner_coverage(int r, int bw)
{
    uint8_t* cov = util_calloc((size_t)r * (size_t)r, 1);
    double n = corner_exponent(), ri = r - bw, sx, sy;
    int in;

    for (int y = 0; y < r; y++) {
        for (int x = 0; x < r; x++) {
            in = 0;
            for (int j = 0; j < CORNER_SAMPLES; j++) {
                for (int i = 0; i < CORNER_SAMPLES; i++) {
                    sx = ((double)r - (x + (i + 0.5) / CORNER_SAMPLES)) / r;
                    sy = ((double)r - (y + (j + 0.5) / CORNER_SAMPLES)) / r;
                    if (pow(sx, n) + pow(sy, n) > 1.0)
                        continue; /* outside the window */
                    if (ri > 0.0 &&
                        pow(sx * r / ri, n) + pow(sy * r / ri, n) <= 1.0)
                        continue; /* inside, where the contents are */
                    in++;
                }
            }
            cov[(size_t)y * (size_t)r + (size_t)x] =
                (uint8_t)(in * 255 / (CORNER_SAMPLES * CORNER_SAMPLES));
        }
    }
    return cov;
}

/* Paint one corner from that coverage. 0 is the top left, the low bits of
 * the index mirror it: 1 right, 2 down, 3 both. */
static void corner_draw(uint32_t* px,
                        const uint8_t* cov,
                        int r,
                        int corner,
                        uint32_t color)
{
    uint32_t a;
    int sx, sy;

    for (int y = 0; y < r; y++) {
        sy = corner & 2 ? r - 1 - y : y;
        for (int x = 0; x < r; x++) {
            sx = corner & 1 ? r - 1 - x : x;
            a = cov[(size_t)sy * (size_t)r + (size_t)sx];
            px[(size_t)y * (size_t)r + (size_t)x] =
                ((((color >> 24) & 0xff) * a / 255) << 24) |
                ((((color >> 16) & 0xff) * a / 255) << 16) |
                ((((color >> 8) & 0xff) * a / 255) << 8) |
                ((color & 0xff) * a / 255);
        }
    }
}

/* Size, place and paint the four corners. Called on every resize and colour
 * change, so redraw only when the radius, border or colour changed. */
void corner_draw_all(Client* c)
{
    Monitor* m = c->mon;
    Buffer* buf;
    uint8_t* cov;
    uint32_t color;
    int r = corner_client_radius(c), rp, bwp;
    int pos[4][2];

    if (!c->corner[0])
        return;
    if (!r || !m) {
        for (int i = 0; i < 4; i++)
            wlr_scene_node_set_enabled(&c->corner[i]->node, 0);
        return;
    }

    pos[0][0] = 0;
    pos[0][1] = 0;
    pos[1][0] = c->geom.width - r;
    pos[1][1] = 0;
    pos[2][0] = 0;
    pos[2][1] = c->geom.height - r;
    pos[3][0] = c->geom.width - r;
    pos[3][1] = c->geom.height - r;
    for (int i = 0; i < 4; i++) {
        wlr_scene_node_set_position(&c->corner[i]->node, pos[i][0], pos[i][1]);
        wlr_scene_buffer_set_dest_size(c->corner[i], r, r);
        wlr_scene_node_set_enabled(&c->corner[i]->node, 1);
    }

    /* drawn at output scale, like the bar */
    rp = (int)roundf((float)r * m->wlr_output->scale);
    bwp = (int)roundf((float)c->bw * m->wlr_output->scale);
    color = corner_color(c->borderscheme);
    if (rp <= 0 || (rp == c->cornerbufr && bwp == c->cornerbufbw &&
                    color == c->cornercolor))
        return;

    if (rp != c->cornerbufr)
        for (int i = 0; i < 4; i++)
            buffer_pool_drop(c->cornerpool[i], LENGTH(c->cornerpool[i]));
    c->cornerbufr = rp;
    c->cornerbufbw = bwp;
    c->cornercolor = color;

    cov = corner_coverage(rp, bwp);
    for (int i = 0; i < 4; i++) {
        if (!(buf = buffer_get(
                  c->cornerpool[i], LENGTH(c->cornerpool[i]), rp, rp)))
            continue;
        corner_draw(buf->data, cov, rp, i, color);
        wlr_scene_buffer_set_buffer(c->corner[i], &buf->base);
        wlr_buffer_unlock(&buf->base);
    }
    free(cov);
}
