/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_BUFFER_H
#define G0WM_BUFFER_H

#include <drwl.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_buffer.h>

typedef struct {
    struct wlr_buffer base;
    struct wl_listener release;
    bool busy;
    Img* image;
    uint32_t data[];
} Buffer;

[[nodiscard]] Buffer* buffer_get(Buffer** pool,
                                 size_t pool_length,
                                 int width,
                                 int height);
void buffer_pool_drop(Buffer** pool, size_t pool_length);

#endif
