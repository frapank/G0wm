/*
 * SPDX-FileCopyrightText: janetski
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "desktop/systray/icon.h"

#include <fcft/fcft.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <pixman.h>

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdckdint.h>
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
static uint32_t* icon_to_pixman(const uint8_t* src, int n_pixels, size_t* pix_size)
{
    uint32_t* dest = nullptr;

    *pix_size = n_pixels * sizeof(uint32_t);
    dest = malloc(*pix_size);
    if (!dest)
        return nullptr;

    for (int i = 0; i < n_pixels; i++) {
        uint8_t alpha = src[i * 4 + 0];
        uint8_t red = src[i * 4 + 1];
        uint8_t green = src[i * 4 + 2];
        uint8_t blue = src[i * 4 + 3];

        /*
         * Skip premultiplying fully opaque and fully transparent
         * pixels.
         */
        if (alpha == 0) {
            dest[i] = 0;

        } else if (alpha == 255) {
            dest[i] = ((uint32_t)alpha << 24) | ((uint32_t)red << 16) | ((uint32_t)green << 8) |
                      ((uint32_t)blue);

        } else {
            dest[i] = ((uint32_t)alpha << 24) | ((uint32_t)PREMUL_ALPHA(red, alpha) << 16) |
                      ((uint32_t)PREMUL_ALPHA(green, alpha) << 8) |
                      ((uint32_t)PREMUL_ALPHA(blue, alpha));
        }
    }

    return dest;
}

Icon* icon_create(const uint8_t* buf, int width, int height, int size)
{
    Icon* icon = nullptr;

    int n_pixels;
    pixman_image_t* img = nullptr;
    size_t pixbuf_size;
    uint32_t* buf_pixman = nullptr;
    uint8_t* buf_orig = nullptr;

    n_pixels = size / 4;

    icon = calloc(1, sizeof(Icon));
    buf_orig = malloc(size);
    buf_pixman = icon_to_pixman(buf, n_pixels, &pixbuf_size);
    if (!icon || !buf_orig || !buf_pixman)
        goto fail;

    img = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height, buf_pixman, width * 4);
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
    return nullptr;
}

/* Sizes an icon theme keeps raster icons under, best first. */
constexpr int icon_sizes[] = { 24, 32, 22, 48, 64, 16, 128, 256 };

[[gnu::format(printf, 3, 4)]] static int icon_try(char* out, size_t outsz, const char* fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    int length = vsnprintf(out, outsz, fmt, args);
    va_end(args);

    return length > 0 && (size_t)length < outsz && access(out, R_OK) == 0;
}

/*
 * Finds the file behind an IconName, there being no icon theme library here.
 * ponytail: hicolor only, add the index.theme walk if an item needs it.
 */
static int icon_file(char* out, size_t outsz, const char* name, const char* themepath)
{
    const char* datahome = getenv("XDG_DATA_HOME");
    const char* datadirs = getenv("XDG_DATA_DIRS");
    const char* home = getenv("HOME");

    /* some items hand out a path where the spec asks for a name */
    if (*name == '/')
        return icon_try(out, outsz, "%s", name);

    if (themepath && *themepath) {
        for (size_t i = 0; i < LENGTH(icon_sizes); i++)
            if (icon_try(out, outsz, "%s/hicolor/%dx%d/apps/%s.png", themepath, icon_sizes[i],
                         icon_sizes[i], name))
                return 1;
        if (icon_try(out, outsz, "%s/%s.png", themepath, name) ||
            icon_try(out, outsz, "%s/%s.svg", themepath, name) ||
            icon_try(out, outsz, "%s/%s.xpm", themepath, name))
            return 1;
    }

    int base_length;
    char bases[512];
    if (datahome && *datahome)
        base_length = snprintf(bases, sizeof(bases), "%s", datahome);
    else
        base_length = snprintf(bases, sizeof(bases), "%s/.local/share", home ? home : "");
    if (base_length < 0 || (size_t)base_length >= sizeof(bases))
        return 0;
    if (snprintf(bases + base_length, sizeof(bases) - base_length, ":%s",
                 datadirs && *datadirs ? datadirs : "/usr/local/share:/usr/share") >=
        (int)(sizeof(bases) - base_length)) {
        return 0;
    }

    char* saveptr;
    for (char* base = strtok_r(bases, ":", &saveptr); base;
         base = strtok_r(nullptr, ":", &saveptr)) {
        for (size_t i = 0; i < LENGTH(icon_sizes); i++)
            if (icon_try(out, outsz, "%s/icons/hicolor/%dx%d/apps/%s.png", base, icon_sizes[i],
                         icon_sizes[i], name))
                return 1;
        if (icon_try(out, outsz, "%s/icons/hicolor/scalable/apps/%s.svg", base, name))
            return 1;
        if (icon_try(out, outsz, "%s/pixmaps/%s.png", base, name) ||
            icon_try(out, outsz, "%s/pixmaps/%s.svg", base, name) ||
            icon_try(out, outsz, "%s/pixmaps/%s.xpm", base, name))
            return 1;
    }

    return 0;
}

static Icon* icon_from_pixbuf(GdkPixbuf* pixbuf)
{
    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    const int channels = gdk_pixbuf_get_n_channels(pixbuf);
    const guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);
    int size;

    if (width <= 0 || height <= 0 || channels < 3 || ckd_mul(&size, width, height) ||
        ckd_mul(&size, size, 4))
        return nullptr;
    uint8_t* argb = malloc((size_t)size);
    if (!argb)
        return nullptr;

    size_t offset = 0;
    for (int y = 0; y < height; y++) {
        const guchar* source_row = pixels + (size_t)y * rowstride;
        for (int x = 0; x < width; x++, source_row += channels, offset += 4) {
            argb[offset + 0] = channels == 4 ? source_row[3] : 0xff;
            argb[offset + 1] = source_row[0];
            argb[offset + 2] = source_row[1];
            argb[offset + 3] = source_row[2];
        }
    }

    Icon* icon = icon_create(argb, width, height, size);
    free(argb);
    return icon;
}

/* Builds an icon from IconName (or a path). */
Icon* icon_create_from_name(const char* name, const char* themepath, int size)
{
    GError* error = nullptr;
    char path[PATH_MAX];

    if (!name || !*name || !icon_file(path, sizeof(path), name, themepath))
        return nullptr;

    GdkPixbuf* pixbuf;

    /* decoded at tray size, the pixman filter samples where this averages */
    if (size > 0)
        pixbuf = gdk_pixbuf_new_from_file_at_scale(path, size, size, TRUE, &error);
    else
        pixbuf = gdk_pixbuf_new_from_file(path, &error);
    if (!pixbuf) {
        fprintf(stderr, "g0wm: %s: %s\n", path, error->message);
        g_error_free(error);
        return nullptr;
    }

    Icon* icon = icon_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);
    return icon;
}

/* 8 bit rgb(a) rows, scaled down to fit size (0 keeps it) */
Icon* icon_create_from_data(const uint8_t* data,
                            int width,
                            int height,
                            int rowstride,
                            int alpha,
                            int size)
{
    int scaled_width = width;
    int scaled_height = height;

    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, alpha, 8, width, height,
                                                 rowstride, nullptr, nullptr);
    if (!pixbuf)
        return nullptr;
    if (size > 0 && (scaled_width > size || scaled_height > size)) {
        if (scaled_width >= scaled_height) {
            scaled_height =
                scaled_height * size / scaled_width > 0 ? scaled_height * size / scaled_width : 1;
            scaled_width = size;
        } else {
            scaled_width =
                scaled_width * size / scaled_height > 0 ? scaled_width * size / scaled_height : 1;
            scaled_height = size;
        }
        GdkPixbuf* scaled =
            gdk_pixbuf_scale_simple(pixbuf, scaled_width, scaled_height, GDK_INTERP_BILINEAR);
        g_object_unref(pixbuf);
        if (!(pixbuf = scaled))
            return nullptr;
    }

    Icon* icon = icon_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);
    return icon;
}

void icon_destroy(Icon* icon)
{
    if (icon->img)
        pixman_image_unref(icon->img);
    free(icon->buf_orig);
    free(icon->buf_pixman);
    free(icon);
}

FallbackIcon* icon_create_fallback(const char* appname, int, struct fcft_font* font)
{
    char initial;

    if ((unsigned char)appname[0] > 127) {
        /* first character is not ascii */
        initial = '?';
    } else {
        initial = toupper(*appname);
    }

    const struct fcft_glyph* glyph =
        fcft_rasterize_char_utf32(font, initial, FCFT_SUBPIXEL_DEFAULT);
    if (!glyph)
        return nullptr;

    return glyph;
}

int icon_resize_image(pixman_image_t* image, int new_width, int new_height)
{
    int src_width = pixman_image_get_width(image);
    int src_height = pixman_image_get_height(image);

    if (src_width == new_width && src_height == new_height)
        return 0;

    pixman_fixed_t scale_x = pixman_double_to_fixed((double)src_width / new_width);
    pixman_fixed_t scale_y = pixman_double_to_fixed((double)src_height / new_height);

    pixman_transform_t transform;
    pixman_transform_init_scale(&transform, scale_x, scale_y);
    if (!pixman_image_set_filter(image, PIXMAN_FILTER_BEST, nullptr, 0) ||
        !pixman_image_set_transform(image, &transform)) {
        return -1;
    }

    return 0;
}
