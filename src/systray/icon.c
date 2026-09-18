#include "icon.h"

#include <fcft/fcft.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <pixman.h>

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PREMUL_ALPHA(chan, alpha) (chan * alpha + 127) / 255
#define LENGTH(X) (sizeof(X) / sizeof((X)[0]))

/*
 * Converts pixels from uint8_t[4] to uint32_t and
 * straight alpha to premultiplied alpha.
 */
static uint32_t* to_pixman(const uint8_t* src, int n_pixels, size_t* pix_size)
{
    uint32_t* dest = NULL;

    *pix_size = n_pixels * sizeof(uint32_t);
    dest = malloc(*pix_size);
    if (!dest)
        return NULL;

    for (int i = 0; i < n_pixels; i++) {
        uint8_t a = src[i * 4 + 0];
        uint8_t r = src[i * 4 + 1];
        uint8_t g = src[i * 4 + 2];
        uint8_t b = src[i * 4 + 3];

        /*
         * Skip premultiplying fully opaque and fully transparent
         * pixels.
         */
        if (a == 0) {
            dest[i] = 0;

        } else if (a == 255) {
            dest[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                      ((uint32_t)g << 8) | ((uint32_t)b);

        } else {
            dest[i] = ((uint32_t)a << 24) |
                      ((uint32_t)PREMUL_ALPHA(r, a) << 16) |
                      ((uint32_t)PREMUL_ALPHA(g, a) << 8) |
                      ((uint32_t)PREMUL_ALPHA(b, a));
        }
    }

    return dest;
}

Icon* createicon(const uint8_t* buf, int width, int height, int size)
{
    Icon* icon = NULL;

    int n_pixels;
    pixman_image_t* img = NULL;
    size_t pixbuf_size;
    uint32_t* buf_pixman = NULL;
    uint8_t* buf_orig = NULL;

    n_pixels = size / 4;

    icon = calloc(1, sizeof(Icon));
    buf_orig = malloc(size);
    buf_pixman = to_pixman(buf, n_pixels, &pixbuf_size);
    if (!icon || !buf_orig || !buf_pixman)
        goto fail;

    img = pixman_image_create_bits(
        PIXMAN_a8r8g8b8, width, height, buf_pixman, width * 4);
    if (!img)
        goto fail;

    memcpy(buf_orig, buf, size);

    icon->buf_orig = buf_orig;
    icon->buf_pixman = buf_pixman;
    icon->img = img;
    icon->size_orig = size;
    icon->size_pixman = pixbuf_size;

    return icon;

fail:
    free(buf_orig);
    if (img)
        pixman_image_unref(img);
    free(buf_pixman);
    free(icon);
    return NULL;
}

/* Sizes an icon theme keeps raster icons under, best first. */
static const int iconsizes[] = { 24, 32, 22, 48, 64, 16, 128, 256 };

static int tryicon(char* out, size_t outsz, const char* fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(out, outsz, fmt, ap);
    va_end(ap);

    return n > 0 && (size_t)n < outsz && access(out, R_OK) == 0;
}

/*
 * Finds the file behind an IconName, there being no icon theme library here.
 * ponytail: hicolor only, add the index.theme walk if an item needs it.
 */
static int iconfile(char* out,
                    size_t outsz,
                    const char* name,
                    const char* themepath)
{
    const char* datahome = getenv("XDG_DATA_HOME");
    const char* datadirs = getenv("XDG_DATA_DIRS");
    const char* home = getenv("HOME");
    char bases[512], *base, *saveptr;
    size_t i;
    int n;

    /* some items hand out a path where the spec asks for a name */
    if (*name == '/')
        return tryicon(out, outsz, "%s", name);

    if (themepath && *themepath) {
        for (i = 0; i < LENGTH(iconsizes); i++)
            if (tryicon(out,
                        outsz,
                        "%s/hicolor/%dx%d/apps/%s.png",
                        themepath,
                        iconsizes[i],
                        iconsizes[i],
                        name))
                return 1;
        if (tryicon(out, outsz, "%s/%s.png", themepath, name) ||
            tryicon(out, outsz, "%s/%s.svg", themepath, name) ||
            tryicon(out, outsz, "%s/%s.xpm", themepath, name))
            return 1;
    }

    if (datahome && *datahome)
        n = snprintf(bases, sizeof(bases), "%s", datahome);
    else
        n = snprintf(bases, sizeof(bases), "%s/.local/share", home ? home : "");
    if (n < 0 || (size_t)n >= sizeof(bases))
        return 0;
    if (snprintf(bases + n,
                 sizeof(bases) - n,
                 ":%s",
                 datadirs && *datadirs ? datadirs
                                       : "/usr/local/share:/usr/share") >=
        (int)(sizeof(bases) - n)) {
        return 0;
    }

    for (base = strtok_r(bases, ":", &saveptr); base;
         base = strtok_r(NULL, ":", &saveptr)) {
        for (i = 0; i < LENGTH(iconsizes); i++)
            if (tryicon(out,
                        outsz,
                        "%s/icons/hicolor/%dx%d/apps/%s.png",
                        base,
                        iconsizes[i],
                        iconsizes[i],
                        name))
                return 1;
        if (tryicon(out,
                    outsz,
                    "%s/icons/hicolor/scalable/apps/%s.svg",
                    base,
                    name))
            return 1;
        if (tryicon(out, outsz, "%s/pixmaps/%s.png", base, name) ||
            tryicon(out, outsz, "%s/pixmaps/%s.svg", base, name) ||
            tryicon(out, outsz, "%s/pixmaps/%s.xpm", base, name))
            return 1;
    }

    return 0;
}

/* Builds an icon from IconName. gdk pixbuf rows are rgb, createicon() argb. */
Icon* createiconfromname(const char* name, const char* themepath, int size)
{
    GError* err = NULL;
    GdkPixbuf* pb;
    Icon* icon = NULL;
    char path[PATH_MAX];
    const guchar *src, *srow;
    uint8_t* argb = NULL;
    int w, h, rowstride, nch, x, y;
    size_t i = 0;

    if (!name || !*name || !iconfile(path, sizeof(path), name, themepath))
        return NULL;

    /* decoded at tray size, the pixman filter samples where this averages */
    if (size > 0)
        pb = gdk_pixbuf_new_from_file_at_scale(path, size, size, TRUE, &err);
    else
        pb = gdk_pixbuf_new_from_file(path, &err);
    if (!pb) {
        fprintf(stderr, "systray: %s: %s\n", path, err->message);
        g_error_free(err);
        return NULL;
    }

    w = gdk_pixbuf_get_width(pb);
    h = gdk_pixbuf_get_height(pb);
    rowstride = gdk_pixbuf_get_rowstride(pb);
    nch = gdk_pixbuf_get_n_channels(pb);
    src = gdk_pixbuf_get_pixels(pb);
    if (w <= 0 || h <= 0 || nch < 3 || !(argb = malloc((size_t)w * h * 4)))
        goto out;

    for (y = 0; y < h; y++) {
        srow = src + (size_t)y * rowstride;
        for (x = 0; x < w; x++, srow += nch, i += 4) {
            argb[i + 0] = nch == 4 ? srow[3] : 0xff;
            argb[i + 1] = srow[0];
            argb[i + 2] = srow[1];
            argb[i + 3] = srow[2];
        }
    }

    icon = createicon(argb, w, h, (int)((size_t)w * h * 4));

out:
    free(argb);
    g_object_unref(pb);
    return icon;
}

void destroyicon(Icon* icon)
{
    if (icon->img)
        pixman_image_unref(icon->img);
    free(icon->buf_orig);
    free(icon->buf_pixman);
    free(icon);
}

FallbackIcon* createfallbackicon(const char* appname,
                                 int fgcolor,
                                 struct fcft_font* font)
{
    const struct fcft_glyph* glyph;
    char initial;

    if ((unsigned char)appname[0] > 127) {
        /* first character is not ascii */
        initial = '?';
    } else {
        initial = toupper(*appname);
    }

    glyph = fcft_rasterize_char_utf32(font, initial, FCFT_SUBPIXEL_DEFAULT);
    if (!glyph)
        return NULL;

    return glyph;
}

int resize_image(pixman_image_t* image, int new_width, int new_height)
{
    int src_width = pixman_image_get_width(image);
    int src_height = pixman_image_get_height(image);
    pixman_transform_t transform;
    pixman_fixed_t scale_x, scale_y;

    if (src_width == new_width && src_height == new_height)
        return 0;

    scale_x = pixman_double_to_fixed((double)src_width / new_width);
    scale_y = pixman_double_to_fixed((double)src_height / new_height);

    pixman_transform_init_scale(&transform, scale_x, scale_y);
    if (!pixman_image_set_filter(image, PIXMAN_FILTER_BEST, NULL, 0) ||
        !pixman_image_set_transform(image, &transform)) {
        return -1;
    }

    return 0;
}
