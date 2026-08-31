/*
 * See LICENSE file for copyright and license details.
 *
 * opacity, the blurred wallpaper and the frosted decorations
 */
#include "g0wn.h"

/* function declarations */
#ifdef INTEGRATED_BACKGROUND
static void blurbox(Client* c,
                    struct wlr_scene_buffer* node,
                    struct wlr_buffer** last,
                    int bx,
                    int by,
                    int bw,
                    int bh);
static void blurrows(uint32_t* dst, const uint32_t* src, int w, int h, int r);
static void blurshrink(uint32_t* dst, const uint32_t* src, int w, int h, int d);
static void blursrcbox(Monitor* m,
                       struct wlr_fbox* src,
                       int x,
                       int y,
                       int w,
                       int h);
static void blurtint(uint32_t* px, size_t n);
static void blurtranspose(uint32_t* dst, const uint32_t* src, int w, int h);
static void blurwallpaper(Monitor* m);
#endif /* INTEGRATED_BACKGROUND */
static int decotranslucent(void);
static void opacityrefresh(void);

/* variables */
#ifdef INTEGRATED_BACKGROUND
static int wallpaper_load_failed;
#endif /* INTEGRATED_BACKGROUND */

/* function implementations */
#ifdef INTEGRATED_BACKGROUND
/* Repoints the bar's backdrop at the strip of frosted wallpaper it sits on.
 * The bar is drawn by g0wn, so decotranslucent() decides whether it shows. */
void blurbar(Monitor* m)
{
    struct wlr_fbox src;
    Buffer* buf;
    int y, w, h;

    if (!m->barblur)
        return;

    buf = m->blurpool[0];
    y = topbar ? 0 : m->m.height - m->b.real_height;
    w = MIN(m->b.real_width, m->wallpaperw);
    h = MIN(m->b.real_height, m->wallpaperh - y);
    if (!buf || !m->scene_buffer->node.enabled || !opacity_enabled ||
        !decotranslucent() || y < 0 || w <= 0 || h <= 0) {
        wlr_scene_node_set_enabled(&m->barblur->node, 0);
        return;
    }

    blursrcbox(m, &src, 0, y, w, h);
    wlr_scene_node_set_position(&m->barblur->node, m->m.x, m->m.y + y);
    wlr_scene_buffer_set_dest_size(m->barblur, w, h);
    wlr_scene_buffer_set_source_box(m->barblur, &src);
    wlr_scene_node_set_enabled(&m->barblur->node, 1);
}

/* Points one of a client's backdrop nodes at the frosted wallpaper under the
 * box it covers, in the coordinates of the client's own tree. last is what the
 * node already holds: set_buffer drops the texture and damages the node, so
 * handing it the same buffer every frame never stops asking for another. */
static void blurbox(Client* c,
                    struct wlr_scene_buffer* node,
                    struct wlr_buffer** last,
                    int bx,
                    int by,
                    int bw,
                    int bh)
{
    struct wlr_fbox src;
    Monitor* m = c->mon;
    Buffer* buf = m ? m->blurpool[0] : NULL;
    int x, y, w, h;

    if (!node || !m)
        return;

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

    blursrcbox(m, &src, x, y, w, h);
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
void blurclient(Client* c)
{
    Monitor* m = c->mon;
#ifdef TITLEBAR
    int th, bw, x, w;
#else
    int th = 0;
#endif

    if (!c->blur)
        return;

    /* the box covers the decorations too, so either letting light through is
     * enough; a fullscreen client is drawn opaque and never does */
    if (!m || !m->blurpool[0] || c->isfullscreen || !opacity_enabled ||
        ((!c->hasopacity || c->opacity >= 1.0f) && !decotranslucent())) {
        wlr_scene_node_set_enabled(&c->blur->node, 0);
#ifdef TITLEBAR
        if (c->titleblur)
            wlr_scene_node_set_enabled(&c->titleblur->node, 0);
#endif
        return;
    }

#ifdef TITLEBAR
    th = titleheight(c);
    bw = (int)c->bw;
#endif

    if (!th || m->lt[m->sellt]->arrange != tabbed || c->isfloating) {
        blurbox(c, c->blur, &c->blurbuf, 0, 0, c->geom.width, c->geom.height);
#ifdef TITLEBAR
        if (c->titleblur)
            wlr_scene_node_set_enabled(&c->titleblur->node, 0);
#endif
        return;
    }

#ifdef TITLEBAR
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
    blurbox(c, c->titleblur, &c->titleblurbuf, x, 0, w, bw + th);

    if (c == tabtop(m))
        blurbox(c,
                c->blur,
                &c->blurbuf,
                0,
                bw + th,
                c->geom.width,
                c->geom.height - bw - th);
    else
        wlr_scene_node_set_enabled(&c->blur->node, 0);
#endif /* TITLEBAR */
}

/* One box blur along the rows: a 2r+1 window slides on a running sum, so a
 * pass costs the same whatever the radius is, and three land near a Gaussian.
 */
static void blurrows(uint32_t* dst, const uint32_t* src, int w, int h, int r)
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
static void blurshrink(uint32_t* dst, const uint32_t* src, int w, int h, int d)
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
static void blursrcbox(Monitor* m,
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
static void blurtint(uint32_t* px, size_t n)
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
static void blurtranspose(uint32_t* dst, const uint32_t* src, int w, int h)
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
static void blurwallpaper(Monitor* m)
{
    Buffer* buf;
    Client* c;
    uint32_t *a, *b, *t;
    unsigned int i;
    int w = m->wallpaperw, h = m->wallpaperh;
    int d, sw, sh, r;

    /* the bar's node is what owns the buffer, so it lets go of it first */
    if (m->barblur)
        wlr_scene_buffer_set_buffer(m->barblur, NULL);
    bufpooldrop(m->blurpool, LENGTH(m->blurpool));
    m->blurw = m->blurh = 0;

    /* the nodes pick the new buffer up next frame; the pointer they compare
     * against has to go now, or a reused address would read as unchanged */
#ifdef TITLEBAR
    wl_list_for_each(c, &clients, link) if (c->mon == m) c->blurbuf =
        c->titleblurbuf = NULL;
#else
    wl_list_for_each(c, &clients, link) if (c->mon == m) c->blurbuf = NULL;
#endif

    if (opacity_type != OpacityBlur || !m->wallpaperpool[0] || w <= 0 || h <= 0)
        return;

    /* the shrink blurs by itself, so the radius shrinks with the image; never
     * less than a quarter, as this size is what the effect costs per frame */
    d = MAX(4, MIN(8, (int)blur_radius / 4));
    sw = MAX(1, w / d);
    sh = MAX(1, h / d);
    r = MAX(1, (int)blur_radius / d);

    if (!(buf = bufget(m->blurpool, LENGTH(m->blurpool), sw, sh)))
        return;
    a = ecalloc((size_t)sw * (size_t)sh, sizeof(*a));
    b = ecalloc((size_t)sw * (size_t)sh, sizeof(*b));

    blurshrink(a, m->wallpaperpool[0]->data, w, h, d);
    for (i = 0; i < blur_passes; i++) {
        blurrows(b, a, sw, sh, r);
        t = a, a = b, b = t;
    }
    blurtranspose(b, a, sw, sh);
    t = a, a = b, b = t;
    for (i = 0; i < blur_passes; i++) {
        blurrows(b, a, sh, sw, r);
        t = a, a = b, b = t;
    }
    blurtranspose(b, a, sh, sw);
    t = a, a = b, b = t;

    blurtint(a, (size_t)sw * (size_t)sh);
    memcpy(buf->data, a, (size_t)sw * (size_t)sh * sizeof(*a));
    free(a);
    free(b);

    /* what blurbar() and blurclient() scale their crops by */
    m->blurw = sw;
    m->blurh = sh;

    /* the pool keeps no reference of its own: the bar's node is what holds
     * the buffer up, and the clients take their own lock as they pick it up */
    if (m->barblur)
        wlr_scene_buffer_set_buffer(m->barblur, &buf->base);
    wlr_buffer_unlock(&buf->base);
    blurbar(m);
}

#endif /* INTEGRATED_BACKGROUND */
/* The opacity g0wn's own drawing runs at. It rides on opacity_enabled, so the
 * one key turns the windows and the decorations off and on together. */
float decoopacity(void)
{
    return opacity_enabled ? opacity_deco : 1.0f;
}

/* Whether light gets through what g0wn draws itself. opacity_deco fades the
 * lot, and a colour in colors[] can carry an alpha of its own, which is how a
 * bar stays see-through with opacity_deco left at 1. */
static int decotranslucent(void)
{
    size_t i, j;

    if (decoopacity() < 1.0f)
        return 1;
    for (i = 0; i < LENGTH(colors); i++)
        for (j = 1; j < LENGTH(colors[i]); j++) /* 0 is the foreground */
            if ((colors[i][j] & 0xff) < 0xff)
                return 1;
    return 0;
}

#ifdef INTEGRATED_BACKGROUND
void setwallpaper(Monitor* m)
{
    GdkPixbuf* scaled;
    GError* error = NULL;
    Buffer* buf;
    int mw = m->m.width, mh = m->m.height;
    int pw, ph, sw, sh, ox, oy, x, y, rowstride, nch;
    double scalefactor;
    guchar* src;
    uint32_t* dst;

    if (!wallpaper || !*wallpaper || mw <= 0 || mh <= 0) {
        wlr_scene_buffer_set_buffer(m->wallpaper, NULL);
        m->wallpaperbuf = NULL;
        bufpooldrop(m->wallpaperpool, LENGTH(m->wallpaperpool));
        m->wallpaperw = m->wallpaperh = 0;
        blurwallpaper(m);
        return;
    }

    if (m->wallpaperpool[0] && mw == m->wallpaperw && mh == m->wallpaperh) {
        wlr_scene_buffer_set_dest_size(m->wallpaper, mw, mh);
        wlr_scene_node_set_position(&m->wallpaper->node, m->m.x, m->m.y);
        /* guarded like blurclient()'s: set_buffer throws the texture away,
         * and this path runs on every monitor layout change */
        if (m->wallpaperbuf != &m->wallpaperpool[0]->base) {
            wlr_scene_buffer_set_buffer(m->wallpaper,
                                        &m->wallpaperpool[0]->base);
            m->wallpaperbuf = &m->wallpaperpool[0]->base;
        }
        /* the frosted copy is the same size and survives with it, unless the
         * monitor went away and took it along */
        if (!m->blurpool[0])
            blurwallpaper(m);
        else
            blurbar(m);
        return;
    }

    if (!wallpaper_src && !wallpaper_load_failed &&
        !(wallpaper_src = gdk_pixbuf_new_from_file(wallpaper, &error))) {
        wlr_log(WLR_ERROR, "wallpaper: %s", error->message);
        g_error_free(error);
        wallpaper_load_failed = 1;
    }
    if (!wallpaper_src)
        return;

    pw = gdk_pixbuf_get_width(wallpaper_src);
    ph = gdk_pixbuf_get_height(wallpaper_src);
    scalefactor = MAX((double)mw / pw, (double)mh / ph);
    sw = MAX(1, (int)lround(pw * scalefactor));
    sh = MAX(1, (int)lround(ph * scalefactor));
    scaled =
        gdk_pixbuf_scale_simple(wallpaper_src, sw, sh, GDK_INTERP_BILINEAR);
    if (!scaled) {
        wlr_log(WLR_ERROR, "wallpaper: failed to scale %s", wallpaper);
        return;
    }

    ox = MIN(sw - mw, (sw - mw) / 2);
    oy = MIN(sh - mh, (sh - mh) / 2);

    bufpooldrop(m->wallpaperpool, LENGTH(m->wallpaperpool));
    if (!(buf = bufget(m->wallpaperpool, LENGTH(m->wallpaperpool), mw, mh))) {
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
    blurwallpaper(m);
}

#endif /* INTEGRATED_BACKGROUND */
/* opacity_apps lists either the apps that get opacity or the ones that do not,
 * depending on opacity_exclusion_type; an empty list covers every app. */
int opacityallowed(const char* appid)
{
    const char* const* a;

    for (a = opacity_apps; *a; a++)
        if (strstr(appid, *a))
            return !opacity_exclusion_type;
    return opacity_exclusion_type || !*opacity_apps;
}

/* opacity is (re)applied while rendering, and changing it damages nothing by
 * itself, so a frame has to be asked for everywhere */
static void opacityrefresh(void)
{
    Monitor* m;
    Client* c;

    /* a decoration is drawn by g0wn, not by a client, so nothing would come
     * back to redraw it: the bar and the title bars follow drawbars() */
    wl_list_for_each(c, &clients, link) setbordercolor(c, c->borderscheme);
    drawbars();

    wl_list_for_each(m, &mons, link)
    {
        if (m->wlr_output->enabled)
            wlr_output_schedule_frame(m->wlr_output);
    }
}

void scenebuffersetopacity(struct wlr_scene_buffer* buffer,
                           int sx,
                           int sy,
                           void* data)
{
    Client* c = data;
    /* xdg-popups hang off Client.scene, not Client.scene_surface, so this
     * never touches them */
    wlr_scene_buffer_set_opacity(
        buffer,
        c->isfullscreen || !opacity_enabled || !c->hasopacity ? 1.0f
                                                              : c->opacity);
}

/* Colours a client's border, remembering the scheme so the opacity toggle can
 * put it back on. wlroots wants a premultiplied colour, so the alpha scales
 * the other three channels with it. */
void setbordercolor(Client* c, int scheme)
{
    float color[4] = COLOR(colors[scheme][ColBorder]);
    float a = color[3] * decoopacity();

    c->borderscheme = scheme;
    color[0] *= a;
    color[1] *= a;
    color[2] *= a;
    color[3] = a;
    client_set_border_color(c, color);
}

/* arg->f is added to the opacity the focused client uses while focused */
void setopacityfocus(const Arg* arg)
{
    Client* sel = focustop(selmon);

    if (!sel)
        return;
    sel->opacity_focus =
        MIN(MAX(sel->opacity_focus + arg->f, OPACITY_MIN), 1.0f);
    sel->opacity = sel->opacity_focus;
    opacityrefresh();
}

/* the same for the opacity it falls back to once it loses focus */
void setopacityunfocus(const Arg* arg)
{
    Client* sel = focustop(selmon);

    if (!sel)
        return;
    sel->opacity_unfocus =
        MIN(MAX(sel->opacity_unfocus + arg->f, OPACITY_MIN), 1.0f);
}

void toggleopacity(const Arg* arg)
{
    opacity_enabled = !opacity_enabled;
    opacityrefresh();
}
