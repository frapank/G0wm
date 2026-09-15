/*
 * See LICENSE file for copyright and license details.
 *
 * rounded corners: each corner is a small buffer of its own, the straight
 * borders stop where it begins
 */
#include "g0wm.h"

#define CORNER_SAMPLES 4 /* per axis, so sixteen coverage samples a pixel */

/* function declarations */
static uint32_t cornercolor(int scheme);
static uint8_t* cornercoverage(int r, int bw);
static double cornerexp(void);
static void drawcorner(uint32_t* px,
                       const uint8_t* cov,
                       int r,
                       int corner,
                       uint32_t color);

/* function implementations */

/* The corner follows the superellipse |x/r|^n + |y/r|^n = 1: n = 2 is the
 * circular arc, a larger n eases the curvature into the turn (squircle). */
static double cornerexp(void)
{
    return cornerstyle == CornerSquircle ? 4.0 : 2.0;
}

/* The contents stay square, so the border has to be at least as thick as the
 * curve cuts into the corner along the diagonal. */
unsigned int borderwidth(void)
{
    double bite;

    if (!cornerpx)
        return borderpx;

    bite = 1.0 - pow(2.0, -1.0 / cornerexp());
    return MAX(borderpx, (unsigned int)ceil(bite * (double)cornerpx));
}

/* Radius c is drawn with: none without a border (fullscreen, unmanaged),
 * never more than half the box. */
int cornerradius(Client* c)
{
    if (!cornerpx || !c->bw || !c->corner[0])
        return 0;
    return MAX(0, MIN((int)cornerpx, MIN(c->geom.width, c->geom.height) / 2));
}

/* Radius the bar is cut to, in layout coordinates. */
int barcorner(Monitor* m)
{
    if (!cornerpx)
        return 0;
    return MAX(0,
               MIN((int)cornerpx, MIN(m->b.real_width, m->b.real_height) / 2));
}

/* Clear the four corner squares before drawing into a buffer: bufget() hands
 * back a used one and cornercut() only fades what it finds there. */
void cornerclear(uint32_t* px, int w, int h, int r)
{
    int i, x, y;

    for (i = 0; i < 4 && r > 0; i++)
        for (y = 0; y < r; y++)
            for (x = 0; x < r; x++)
                px[(size_t)(i & 2 ? h - 1 - y : y) * (size_t)w +
                   (size_t)(i & 1 ? w - 1 - x : x)] = 0;
}

/* Cut the curve out of a buffer that already holds a drawing: the bar has no
 * border, it just fades out along it. */
void cornercut(uint32_t* px, int w, int h, int r)
{
    uint8_t* cov;
    uint32_t a, p;
    size_t o;
    int i, x, y;

    if (r <= 0)
        return;

    /* bw = r, no inner curve: the cut goes all the way through */
    cov = cornercoverage(r, r);
    for (i = 0; i < 4; i++) {
        for (y = 0; y < r; y++) {
            for (x = 0; x < r; x++) {
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
 * faded like the straight borders in setbordercolor(). */
static uint32_t cornercolor(int scheme)
{
    uint32_t clr = colors[scheme][ColBorder];
    uint32_t a = (uint32_t)((float)(clr & 0xff) * decoopacity());

    return (a << 24) | ((((clr >> 24) & 0xff) * a / 255) << 16) |
           ((((clr >> 16) & 0xff) * a / 255) << 8) |
           (((clr >> 8) & 0xff) * a / 255);
}

/* Coverage of the ring between the outer curve and the inner one, a byte a
 * pixel, for the top left corner; the others are it mirrored. Sampling
 * antialiases both edges of the ring in one pass, whatever the curve. */
static uint8_t* cornercoverage(int r, int bw)
{
    uint8_t* cov = ecalloc((size_t)r * (size_t)r, 1);
    double n = cornerexp(), ri = r - bw, sx, sy;
    int x, y, i, j, in;

    for (y = 0; y < r; y++) {
        for (x = 0; x < r; x++) {
            in = 0;
            for (j = 0; j < CORNER_SAMPLES; j++) {
                for (i = 0; i < CORNER_SAMPLES; i++) {
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
static void drawcorner(uint32_t* px,
                       const uint8_t* cov,
                       int r,
                       int corner,
                       uint32_t color)
{
    uint32_t a;
    int x, y, sx, sy;

    for (y = 0; y < r; y++) {
        sy = corner & 2 ? r - 1 - y : y;
        for (x = 0; x < r; x++) {
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
void drawcorners(Client* c)
{
    Monitor* m = c->mon;
    Buffer* buf;
    uint8_t* cov;
    uint32_t color;
    int i, r = cornerradius(c), rp, bwp;
    int pos[4][2];

    if (!c->corner[0])
        return;
    if (!r || !m) {
        for (i = 0; i < 4; i++)
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
    for (i = 0; i < 4; i++) {
        wlr_scene_node_set_position(&c->corner[i]->node, pos[i][0], pos[i][1]);
        wlr_scene_buffer_set_dest_size(c->corner[i], r, r);
        wlr_scene_node_set_enabled(&c->corner[i]->node, 1);
    }

    /* drawn at output scale, like the bar */
    rp = (int)roundf((float)r * m->wlr_output->scale);
    bwp = (int)roundf((float)c->bw * m->wlr_output->scale);
    color = cornercolor(c->borderscheme);
    if (rp <= 0 || (rp == c->cornerbufr && bwp == c->cornerbufbw &&
                    color == c->cornercolor))
        return;

    if (rp != c->cornerbufr)
        for (i = 0; i < 4; i++)
            bufpooldrop(c->cornerpool[i], LENGTH(c->cornerpool[i]));
    c->cornerbufr = rp;
    c->cornerbufbw = bwp;
    c->cornercolor = color;

    cov = cornercoverage(rp, bwp);
    for (i = 0; i < 4; i++) {
        if (!(buf = bufget(c->cornerpool[i], LENGTH(c->cornerpool[i]), rp, rp)))
            continue;
        drawcorner(buf->data, cov, rp, i, color);
        wlr_scene_buffer_set_buffer(c->corner[i], &buf->base);
        wlr_buffer_unlock(&buf->base);
    }
    free(cov);
}
