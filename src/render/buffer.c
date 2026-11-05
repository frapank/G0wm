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

#include "core/g0wm.h"
#include "core/util.h"
#include "render/buffer.h"

/* function declarations */
static bool buffer_begin_data_access(struct wlr_buffer* wlr_buffer,
                                     uint32_t flags,
                                     void** data,
                                     uint32_t* format,
                                     size_t* stride);
static void buffer_end_data_access(struct wlr_buffer* buffer);
static void buffer_destroy(struct wlr_buffer* wlr_buffer);
static void buffer_handle_release(struct wl_listener* listener, void*);

/* variables */
static const struct wlr_buffer_impl buffer_impl = {
    .destroy = buffer_destroy,
    .begin_data_ptr_access = buffer_begin_data_access,
    .end_data_ptr_access = buffer_end_data_access,
};

/* function implementations */
static void buffer_destroy(struct wlr_buffer* wlr_buffer)
{
    Buffer* buffer = wl_container_of(wlr_buffer, buffer, base);
    if (buffer->busy)
        wl_list_remove(&buffer->release.link);
    drwl_image_destroy(buffer->image);
    free(buffer);
}

static bool buffer_begin_data_access(struct wlr_buffer* wlr_buffer,
                                     uint32_t flags,
                                     void** data,
                                     uint32_t* format,
                                     size_t* stride)
{
    Buffer* buffer = wl_container_of(wlr_buffer, buffer, base);

    if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
        return false;

    *data = buffer->data;
    *stride = wlr_buffer->width * 4;
    *format = DRM_FORMAT_ARGB8888;

    return true;
}

static void buffer_end_data_access(struct wlr_buffer*) {}

/* Grabs a free width*height buffer from pool, allocating it on first use. The
 * bar and every client's title bar own a pool, as their sizes differ. */
Buffer* buffer_get(Buffer** pool, size_t pool_length, int width, int height)
{
    Buffer* buffer = nullptr;

    for (size_t i = 0; i < pool_length; i++) {
        if (pool[i]) {
            if (pool[i]->busy)
                continue;
            buffer = pool[i];
            break;
        }

        size_t byte_size;
        if (ckd_mul(&byte_size, (size_t)width, (size_t)height) ||
            ckd_mul(&byte_size, byte_size, 4) || ckd_add(&byte_size, byte_size, sizeof(Buffer)))
            return nullptr;
        buffer = util_calloc(1, byte_size);
        buffer->image = drwl_image_create(nullptr, width, height, buffer->data);
        wlr_buffer_init(&buffer->base, &buffer_impl, width, height);
        pool[i] = buffer;
        break;
    }
    if (!buffer)
        return nullptr;

    buffer->busy = true;
    LISTEN(&buffer->base.events.release, &buffer->release, buffer_handle_release);
    wlr_buffer_lock(&buffer->base);
    return buffer;
}

void buffer_pool_drop(Buffer** pool, size_t pool_length)
{
    for (size_t i = 0; i < pool_length; i++)
        if (pool[i]) {
            wlr_buffer_drop(&pool[i]->base);
            pool[i] = nullptr;
        }
}

static void buffer_handle_release(struct wl_listener* listener, void*)
{
    Buffer* buffer = wl_container_of(listener, buffer, release);
    buffer->busy = false;
    wl_list_remove(&buffer->release.link);
}
