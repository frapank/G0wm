/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the wlr_buffer pool the bar and the title bars draw into
 */
#include <drwl.h>
#include <libdrm/drm_fourcc.h>
#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>

#include "buffer.h"
#include "g0wm.h"
#include "util.h"

/* function declarations */
static bool bufdatabegin(struct wlr_buffer* buffer,
                         uint32_t flags,
                         void** data,
                         uint32_t* format,
                         size_t* stride);
static void bufdataend(struct wlr_buffer* buffer);
static void bufdestroy(struct wlr_buffer* buffer);
static void bufrelease(struct wl_listener* listener, void* data);

/* variables */
static const struct wlr_buffer_impl buffer_impl = {
    .destroy = bufdestroy,
    .begin_data_ptr_access = bufdatabegin,
    .end_data_ptr_access = bufdataend,
};

/* function implementations */
static void bufdestroy(struct wlr_buffer* wlr_buffer)
{
    Buffer* buf = wl_container_of(wlr_buffer, buf, base);
    if (buf->busy)
        wl_list_remove(&buf->release.link);
    drwl_image_destroy(buf->image);
    free(buf);
}

static bool bufdatabegin(struct wlr_buffer* wlr_buffer,
                         uint32_t flags,
                         void** data,
                         uint32_t* format,
                         size_t* stride)
{
    Buffer* buf = wl_container_of(wlr_buffer, buf, base);

    if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
        return false;

    *data = buf->data;
    *stride = wlr_buffer->width * 4;
    *format = DRM_FORMAT_ARGB8888;

    return true;
}

static void bufdataend(struct wlr_buffer*) {}

/* Grabs a free width*height buffer from pool, allocating it on first use. The
 * bar and every client's title bar own a pool, as their sizes differ. */
Buffer* bufget(Buffer** pool, size_t poollen, int width, int height)
{
    size_t i, len;
    Buffer* buf = nullptr;

    for (i = 0; i < poollen; i++) {
        if (pool[i]) {
            if (pool[i]->busy)
                continue;
            buf = pool[i];
            break;
        }

        if (ckd_mul(&len, (size_t)width, (size_t)height) ||
            ckd_mul(&len, len, 4) || ckd_add(&len, len, sizeof(Buffer)))
            return nullptr;
        buf = ecalloc(1, len);
        buf->image = drwl_image_create(nullptr, width, height, buf->data);
        wlr_buffer_init(&buf->base, &buffer_impl, width, height);
        pool[i] = buf;
        break;
    }
    if (!buf)
        return nullptr;

    buf->busy = true;
    LISTEN(&buf->base.events.release, &buf->release, bufrelease);
    wlr_buffer_lock(&buf->base);
    return buf;
}

void bufpooldrop(Buffer** pool, size_t poollen)
{
    size_t i;

    for (i = 0; i < poollen; i++)
        if (pool[i]) {
            wlr_buffer_drop(&pool[i]->base);
            pool[i] = nullptr;
        }
}

static void bufrelease(struct wl_listener* listener, void*)
{
    Buffer* buf = wl_container_of(listener, buf, release);
    buf->busy = false;
    wl_list_remove(&buf->release.link);
}
