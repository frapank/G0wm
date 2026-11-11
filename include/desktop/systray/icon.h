/*
 * SPDX-FileCopyrightText: janetski
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_SYSTRAY_ICON_H
#define G0WM_SYSTRAY_ICON_H

#include <fcft/fcft.h>
#include <pixman.h>

#include <stddef.h>
#include <stdint.h>

typedef const struct fcft_glyph FallbackIconGlyph;

typedef struct {
    pixman_image_t* image;
    uint32_t* premultiplied_pixels;
    uint8_t* original_pixels;
    size_t original_size_bytes;
    size_t premultiplied_size_bytes;
} RasterIcon;

[[nodiscard]] RasterIcon* icon_create(const uint8_t* buf, int width, int height, int size);
[[nodiscard]] RasterIcon* icon_create_from_name(const char* name, const char* theme_path, int size);
[[nodiscard]] RasterIcon* icon_create_from_data(const uint8_t* data,
                                                int width,
                                                int height,
                                                int rowstride,
                                                int alpha,
                                                int size);
[[nodiscard]] FallbackIconGlyph* icon_create_fallback(const char* appname,
                                                      int,
                                                      struct fcft_font* font);
void icon_destroy(RasterIcon* icon);
[[nodiscard]] int icon_resize_image(pixman_image_t* image, int new_width, int new_height);

#endif
