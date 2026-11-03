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

#include "bar.h"
#include "buffer.h"
#include "client.h"
#include "corner.h"
#include "g0wm.h"
#include "layout.h"
#include "monitor.h"
#include "opacity.h"
#include "settings.h"
#include "util.h"

constexpr float OPACITY_MIN = 0.1f; /* floor for the opacity keybindings */

/* function declarations */
#ifdef INTEGRATED_BACKGROUND
static void opacity_blur_box(Client* c,
                             struct wlr_scene_buffer* node,
                             struct wlr_buffer** last,
                             int bx,
                             int by,
                             int bw,
                             int bh);
static void opacity_blur_rows(uint32_t* dst,
                              const uint32_t* src,
                              int w,
                              int h,
                              int r);
static void opacity_blur_shrink(uint32_t* dst,
                                const uint32_t* src,
                                int w,
                                int h,
                                int d);
static void opacity_blur_source_box(Monitor* m,
                                    struct wlr_fbox* src,
                                    int x,
                                    int y,
                                    int w,
                                    int h);
static void opacity_blur_tint(uint32_t* px, size_t n);
static void opacity_blur_transpose(uint32_t* dst,
                                   const uint32_t* src,
                                   int w,
                                   int h);
static void opacity_blur_wallpaper(Monitor* m);
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
void opacity_blur_bar(Monitor* m)
{
    struct wlr_fbox src;
    Buffer* buf;
    int x, y, w, h;
    /* the backdrop is square, keep it off the corners a rounded bar drops */
    int r = corner_bar_radius(m);

    if (!m->barblur)
        return;

    buf = m->blurpool[0];
    x = (int)bar_padding + r;
    y = bar_top ? (int)bar_padding
                : m->m.height - m->b.real_height - (int)bar_padding;
    w = MIN(m->b.real_width - 2 * r, m->wallpaperw - x);
    h = MIN(m->b.real_height, m->wallpaperh - y);
    if (!buf || !m->scene_buffer->node.enabled || !opacity_enabled ||
        !opacity_decorations_translucent() || y < 0 || w <= 0 || h <= 0) {
        wlr_scene_node_set_enabled(&m->barblur->node, 0);
        return;
    }

    opacity_blur_source_box(m, &src, x, y, w, h);
    wlr_scene_node_set_position(&m->barblur->node, m->m.x + x, m->m.y + y);
    wlr_scene_buffer_set_dest_size(m->barblur, w, h);
    wlr_scene_buffer_set_source_box(m->barblur, &src);
    wlr_scene_node_set_enabled(&m->barblur->node, 1);
}

/* Points one of a client's backdrop nodes at the frosted wallpaper under the
 * box it covers, in the coordinates of the client's own tree. last is what the
 * node already holds: set_buffer drops the texture and damages the node, so
 * handing it the same buffer every frame never stops asking for another. */
static void opacity_blur_box(Client* c,
                             struct wlr_scene_buffer* node,
                             struct wlr_buffer** last,
                             int bx,
                             int by,
                             int bw,
                             int bh)
{
    struct wlr_fbox src;
    Monitor* m = c->mon;
    Buffer* buf = m ? m->blurpool[0] : nullptr;
    int x, y, w, h, x1, y1;

    if (!node || !m)
        return;

    /* same for a rounded client: shrink the box to the contents, or the
     * square backdrop fills the corners back in */
    if (corner_client_radius(c)) {
        x1 = MIN(bx + bw, c->geom.width - (int)c->bw);
        y1 = MIN(by + bh, c->geom.height - (int)c->bw);
        bx = MAX(bx, (int)c->bw);
        by = MAX(by, (int)c->bw);
        bw = x1 - bx;
        bh = y1 - by;
        if (bw <= 0 || bh <= 0) {
            wlr_scene_node_set_enabled(&node->node, 0);
            return;
        }
    }

    /* the wallpaper covers the monitor pixel for pixel, so the crop is just
     * the box in monitor-local coordinates, clipped to it */
    x = MAX(0, c->geom.x - m->m.x + bx);
    y = MAX(0, c->geom.y - m->m.y + by);
    w = MIN(c->geom.x - m->m.x + bx + bw, m->wallpaperw) - x;
    h = MIN(c->geom.y - m->m.y + by + bh, m->wallpaperh) - y;
    if (!buf || w <= 0 || h <= 0) {
        wlr_scene_node_set_enabled(&node->node, 0);
        return;
    }

    opacity_blur_source_box(m, &src, x, y, w, h);
    /* the node hangs off the client's tree, hence the offset back into it */
    wlr_scene_node_set_position(
        &node->node, x - (c->geom.x - m->m.x), y - (c->geom.y - m->m.y));
    wlr_scene_buffer_set_dest_size(node, w, h);
    wlr_scene_buffer_set_source_box(node, &src);
    if (*last != &buf->base) {
        wlr_scene_buffer_set_buffer(node, &buf->base);
        *last = &buf->base;
    }
    wlr_scene_node_set_enabled(&node->node, 1);
}

/* Points a client's backdrop at the frosted wallpaper it covers. Done while
 * rendering, so nothing that moves a window has to keep the two in step. */
void opacity_blur_client(Client* c)
{
    Monitor* m = c->mon;
    int th, bw, x, w;

    if (!c->blur)
        return;

    /* the box covers the decorations too, so either letting light through is
     * enough; a fullscreen client is drawn opaque and never does */
    if (!m || !m->blurpool[0] || c->isfullscreen || !opacity_enabled ||
        (c->opacity >= 1.0f && !opacity_decorations_translucent())) {
        wlr_scene_node_set_enabled(&c->blur->node, 0);
        if (c->titleblur)
            wlr_scene_node_set_enabled(&c->titleblur->node, 0);
        return;
    }

    th = bar_title_height(c);
    bw = (int)c->bw;

    if (!th || m->lt[m->sellt]->arrange != layout_tabbed || c->isfloating) {
        opacity_blur_box(
            c, c->blur, &c->blurbuf, 0, 0, c->geom.width, c->geom.height);
        if (c->titleblur)
            wlr_scene_node_set_enabled(&c->titleblur->node, 0);
        return;
    }

    /* Tabs share one box and the ones under the top are drawn in lower trees,
     * so a backdrop over the whole box buries their title bars. Each tab backs
     * its own slice of the row, the top one backs the body below it. */
    x = bw + c->titlex;
    w = c->titlew;
    if (c->titlex == 0) { /* first tab, take the corner in */
        x = 0;
        w += bw;
    }
    if (c->titlex + c->titlew == c->geom.width - 2 * bw)
        w += bw; /* last tab, same */
    opacity_blur_box(c, c->titleblur, &c->titleblurbuf, x, 0, w, bw + th);

    if (c == layout_tab_top(m))
        opacity_blur_box(c,
                         c->blur,
                         &c->blurbuf,
                         0,
                         bw + th,
                         c->geom.width,
                         c->geom.height - bw - th);
    else
        wlr_scene_node_set_enabled(&c->blur->node, 0);
}

/* One box blur along the rows: a 2r+1 window slides on a running sum, so a
 * pass costs the same whatever the radius is, and three land near a Gaussian.
 */
static void opacity_blur_rows(uint32_t* dst,
                              const uint32_t* src,
                              int w,
                              int h,
                              int r)
{
    int x, y, i;

    for (y = 0; y < h; y++) {
        const uint32_t* s = src + (size_t)y * (size_t)w;
        uint32_t* d = dst + (size_t)y * (size_t)w;
        uint32_t sr = 0, sg = 0, sb = 0, n = 0;

        for (i = 0; i <= r && i < w; i++) {
            sr += (s[i] >> 16) & 0xff;
            sg += (s[i] >> 8) & 0xff;
            sb += s[i] & 0xff;
            n++;
        }
        for (x = 0; x < w; x++) {
            d[x] = 0xff000000u | ((sr / n) << 16) | ((sg / n) << 8) | (sb / n);
            if (x - r >= 0) {
                sr -= (s[x - r] >> 16) & 0xff;
                sg -= (s[x - r] >> 8) & 0xff;
                sb -= s[x - r] & 0xff;
                n--;
            }
            if (x + r + 1 < w) {
                sr += (s[x + r + 1] >> 16) & 0xff;
                sg += (s[x + r + 1] >> 8) & 0xff;
                sb += s[x + r + 1] & 0xff;
                n++;
            }
        }
    }
}

/* Averages every d*d block down to one pixel; the blur then runs on the
 * small image, which is what keeps a radius this wide affordable. */
static void opacity_blur_shrink(uint32_t* dst,
                                const uint32_t* src,
                                int w,
                                int h,
                                int d)
{
    int sw = MAX(1, w / d), sh = MAX(1, h / d);
    int x, y, i, j;

    for (y = 0; y < sh; y++)
        for (x = 0; x < sw; x++) {
            uint32_t sr = 0, sg = 0, sb = 0, n = 0;

            for (j = y * d; j < MIN(h, (y + 1) * d); j++)
                for (i = x * d; i < MIN(w, (x + 1) * d); i++) {
                    uint32_t px = src[(size_t)j * (size_t)w + (size_t)i];

                    sr += (px >> 16) & 0xff;
                    sg += (px >> 8) & 0xff;
                    sb += px & 0xff;
                    n++;
                }
            dst[(size_t)y * (size_t)sw + (size_t)x] =
                n ? 0xff000000u | ((sr / n) << 16) | ((sg / n) << 8) | (sb / n)
                  : 0xff000000u;
        }
}

/* Maps a monitor-local rectangle onto the smaller frosted buffer. The crop
 * stays in floating point so it does not snap to that buffer's grid. */
static void opacity_blur_source_box(Monitor* m,
                                    struct wlr_fbox* src,
                                    int x,
                                    int y,
                                    int w,
                                    int h)
{
    double sx = (double)m->blurw / (double)m->wallpaperw;
    double sy = (double)m->blurh / (double)m->wallpaperh;

    src->x = x * sx;
    src->y = y * sy;
    src->width = w * sx;
    src->height = h * sy;
}

/* A blur this wide leaves grey mush, so the saturation is pushed back up:
 * that is what makes it read as glass rather than as a smudge. */
static void opacity_blur_tint(uint32_t* px, size_t n)
{
    size_t i;

    if (blur_saturation == 1.0f && blur_brightness == 1.0f)
        return;

    for (i = 0; i < n; i++) {
        float r = (float)((px[i] >> 16) & 0xff);
        float g = (float)((px[i] >> 8) & 0xff);
        float b = (float)(px[i] & 0xff);
        float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;

        r = (l + (r - l) * blur_saturation) * blur_brightness;
        g = (l + (g - l) * blur_saturation) * blur_brightness;
        b = (l + (b - l) * blur_saturation) * blur_brightness;
        px[i] = 0xff000000u |
                ((uint32_t)(MAX(0.0f, MIN(255.0f, r)) + 0.5f) << 16) |
                ((uint32_t)(MAX(0.0f, MIN(255.0f, g)) + 0.5f) << 8) |
                (uint32_t)(MAX(0.0f, MIN(255.0f, b)) + 0.5f);
    }
}

/* Turns a w*h image on its side, so that blurring its rows blurs the columns
 * of the original and one row pass covers both directions. */
static void opacity_blur_transpose(uint32_t* dst,
                                   const uint32_t* src,
                                   int w,
                                   int h)
{
    int x, y;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            dst[(size_t)x * (size_t)h + (size_t)y] =
                src[(size_t)y * (size_t)w + (size_t)x];
}

/* Builds the frosted copy OpacityBlur shows through everything transparent,
 * once per wallpaper rather than per frame. It stays at 1/d of the monitor
 * and the GPU scales it back up: a backdrop is never occluded away, so a
 * full-size copy would cross the bus on every frame anything moved. */
static void opacity_blur_wallpaper(Monitor* m)
{
    Buffer* buf;
    Client* c;
    uint32_t *a, *b, *t;
    unsigned int i;
    int w = m->wallpaperw, h = m->wallpaperh;
    int d, sw, sh, r;

    /* the bar's node is what owns the buffer, so it lets go of it first */
    if (m->barblur)
        wlr_scene_buffer_set_buffer(m->barblur, nullptr);
    buffer_pool_drop(m->blurpool, LENGTH(m->blurpool));
    m->blurw = m->blurh = 0;

    /* the nodes pick the new buffer up next frame; the pointer they compare
     * against has to go now, or a reused address would read as unchanged */
    wl_list_for_each(c, &clients, link) if (c->mon == m) c->blurbuf =
        c->titleblurbuf = nullptr;

    if (opacity_type != OpacityBlur || !m->wallpaperpool[0] || w <= 0 || h <= 0)
        return;

    /* the shrink blurs by itself, so the radius shrinks with the image; never
     * less than a quarter, as this size is what the effect costs per frame */
    d = MAX(4, MIN(8, (int)blur_radius / 4));
    sw = MAX(1, w / d);
    sh = MAX(1, h / d);
    r = MAX(1, (int)blur_radius / d);

    if (!(buf = buffer_get(m->blurpool, LENGTH(m->blurpool), sw, sh)))
        return;
    a = util_calloc((size_t)sw * (size_t)sh, sizeof(*a));
    b = util_calloc((size_t)sw * (size_t)sh, sizeof(*b));

    opacity_blur_shrink(a, m->wallpaperpool[0]->data, w, h, d);
    for (i = 0; i < blur_passes; i++) {
        opacity_blur_rows(b, a, sw, sh, r);
        t = a, a = b, b = t;
    }
    opacity_blur_transpose(b, a, sw, sh);
    t = a, a = b, b = t;
    for (i = 0; i < blur_passes; i++) {
        opacity_blur_rows(b, a, sh, sw, r);
        t = a, a = b, b = t;
    }
    opacity_blur_transpose(b, a, sh, sw);
    t = a, a = b, b = t;

    opacity_blur_tint(a, (size_t)sw * (size_t)sh);
    memcpy(buf->data, a, (size_t)sw * (size_t)sh * sizeof(*a));
    free(a);
    free(b);

    /* what opacity_blur_bar() and opacity_blur_client() scale their crops by */
    m->blurw = sw;
    m->blurh = sh;

    /* the pool keeps no reference of its own: the bar's node is what holds
     * the buffer up, and the clients take their own lock as they pick it up */
    if (m->barblur)
        wlr_scene_buffer_set_buffer(m->barblur, &buf->base);
    wlr_buffer_unlock(&buf->base);
    opacity_blur_bar(m);
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
    size_t i, j;

    if (opacity_decorations_current() < 1.0f)
        return 1;
    for (i = 0; i < LENGTH(colors); i++)
        for (j = 1; j < LENGTH(colors[i]); j++) /* 0 is the foreground */
            if ((colors[i][j] & 0xff) < 0xff)
                return 1;
    return 0;
}

#ifdef INTEGRATED_BACKGROUND
void opacity_set_wallpaper(Monitor* m)
{
    GdkPixbuf* scaled;
    GError* error = nullptr;
    Buffer* buf;
    int mw = m->m.width, mh = m->m.height;
    int pw, ph, sw, sh, ox, oy, x, y, rowstride, nch;
    double scalefactor;
    guchar* src;
    uint32_t* dst;

    if (!wallpaper || !*wallpaper || mw <= 0 || mh <= 0) {
        wlr_scene_buffer_set_buffer(m->wallpaper, nullptr);
        m->wallpaperbuf = nullptr;
        buffer_pool_drop(m->wallpaperpool, LENGTH(m->wallpaperpool));
        m->wallpaperw = m->wallpaperh = 0;
        opacity_blur_wallpaper(m);
        return;
    }

    if (m->wallpaperpool[0] && mw == m->wallpaperw && mh == m->wallpaperh) {
        wlr_scene_buffer_set_dest_size(m->wallpaper, mw, mh);
        wlr_scene_node_set_position(&m->wallpaper->node, m->m.x, m->m.y);
        /* guarded like opacity_blur_client()'s: set_buffer throws the texture
         * away, and this path runs on every monitor layout change */
        if (m->wallpaperbuf != &m->wallpaperpool[0]->base) {
            wlr_scene_buffer_set_buffer(m->wallpaper,
                                        &m->wallpaperpool[0]->base);
            m->wallpaperbuf = &m->wallpaperpool[0]->base;
        }
        /* the frosted copy is the same size and survives with it, unless the
         * monitor went away and took it along */
        if (!m->blurpool[0])
            opacity_blur_wallpaper(m);
        else
            opacity_blur_bar(m);
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

    pw = gdk_pixbuf_get_width(wallpaper_source);
    ph = gdk_pixbuf_get_height(wallpaper_source);
    scalefactor = MAX((double)mw / pw, (double)mh / ph);
    sw = MAX(1, (int)lround(pw * scalefactor));
    sh = MAX(1, (int)lround(ph * scalefactor));
    scaled =
        gdk_pixbuf_scale_simple(wallpaper_source, sw, sh, GDK_INTERP_BILINEAR);
    if (!scaled) {
        wlr_log(WLR_ERROR, "wallpaper: failed to scale %s", wallpaper);
        return;
    }

    ox = MIN(sw - mw, (sw - mw) / 2);
    oy = MIN(sh - mh, (sh - mh) / 2);

    buffer_pool_drop(m->wallpaperpool, LENGTH(m->wallpaperpool));
    if (!(buf =
              buffer_get(m->wallpaperpool, LENGTH(m->wallpaperpool), mw, mh))) {
        g_object_unref(scaled);
        return;
    }

    src = gdk_pixbuf_get_pixels(scaled);
    rowstride = gdk_pixbuf_get_rowstride(scaled);
    nch = gdk_pixbuf_get_n_channels(scaled);
    dst = buf->data;
    for (y = 0; y < mh; y++) {
        guchar* srow = src + (size_t)(y + oy) * rowstride + (size_t)ox * nch;
        for (x = 0; x < mw; x++, srow += nch, dst++)
            *dst = (0xffu << 24) | ((uint32_t)srow[0] << 16) |
                   ((uint32_t)srow[1] << 8) | (uint32_t)srow[2];
    }
    g_object_unref(scaled);
    m->wallpaperw = mw;
    m->wallpaperh = mh;

    wlr_scene_buffer_set_dest_size(m->wallpaper, mw, mh);
    wlr_scene_node_set_position(&m->wallpaper->node, m->m.x, m->m.y);
    wlr_scene_buffer_set_buffer(m->wallpaper, &buf->base);
    m->wallpaperbuf = &buf->base;
    wlr_buffer_unlock(&buf->base);
    opacity_blur_wallpaper(m);
}

#endif /* INTEGRATED_BACKGROUND */
/* opacity is (re)applied while rendering, and changing it damages nothing by
 * itself, so a frame has to be asked for everywhere */
static void opacity_refresh(void)
{
    Monitor* m;
    Client* c;

    /* a decoration is drawn by g0wm, not by a client, so nothing would come
     * back to redraw it: the bar and the title bars follow bar_draw_all() */
    wl_list_for_each(c, &clients, link)
        opacity_set_border_color(c, c->borderscheme);
    bar_draw_all();

    wl_list_for_each(m, &monitors, link)
    {
        if (m->wlr_output->enabled)
            wlr_output_schedule_frame(m->wlr_output);
    }
}

void opacity_set_scene_buffer(struct wlr_scene_buffer* buffer,
                              int,
                              int,
                              void* data)
{
    Client* c = data;
    /* xdg-popups hang off Client.scene, not Client.scene_surface, so this
     * never touches them */
    wlr_scene_buffer_set_opacity(
        buffer, c->isfullscreen || !opacity_enabled ? 1.0f : c->opacity);
}

/* Colours a client's border, remembering the scheme so the opacity toggle can
 * put it back on. wlroots wants a premultiplied colour, so the alpha scales
 * the other three channels with it. */
void opacity_set_border_color(Client* c, int scheme)
{
    float color[4] = COLOR(colors[scheme][ColBorder]);
    float a = color[3] * opacity_decorations_current();

    c->borderscheme = scheme;
    color[0] *= a;
    color[1] *= a;
    color[2] *= a;
    color[3] = a;
    client_set_border_color(c, color);
    corner_draw_all(c);
}

/* arg->f is added to the opacity the focused client uses while focused */
void opacity_change_focused(const Arg* arg)
{
    Client* sel = client_top(selected_monitor);

    if (!sel)
        return;
    sel->opacity_focus =
        MIN(MAX(sel->opacity_focus + arg->f, OPACITY_MIN), 1.0f);
    sel->opacity = sel->opacity_focus;
    opacity_refresh();
}

/* the same for the opacity it falls back to once it loses focus */
void opacity_change_unfocused(const Arg* arg)
{
    Client* sel = client_top(selected_monitor);

    if (!sel)
        return;
    sel->opacity_unfocus =
        MIN(MAX(sel->opacity_unfocus + arg->f, OPACITY_MIN), 1.0f);
}

/* the wallpaper is decoded once and the frosted copy cached with it, so a
 * reload has to throw both away before anything redraws */
void opacity_reload(void)
{
#ifdef INTEGRATED_BACKGROUND
    Monitor* m;

    if (wallpaper_source) {
        g_object_unref(wallpaper_source);
        wallpaper_source = nullptr;
    }
    opacity_wallpaper_failed = 0;
    wl_list_for_each(m, &monitors, link)
    {
        buffer_pool_drop(m->wallpaperpool, LENGTH(m->wallpaperpool));
        buffer_pool_drop(m->blurpool, LENGTH(m->blurpool));
        m->wallpaperbuf = nullptr;
        m->wallpaperw = m->wallpaperh = 0;
        m->blurw = m->blurh = 0;
        opacity_set_wallpaper(m);
    }
#endif
    opacity_refresh();
}

void opacity_toggle(const Arg*)
{
    opacity_enabled = !opacity_enabled;
    opacity_refresh();
}
