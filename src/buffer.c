/*
 * See LICENSE file for copyright and license details.
 *
 * the wlr_buffer pool the bar and the title bars draw into
 */
#include "g0wn.h"

/* function declarations */
static void bufrelease(struct wl_listener* listener, void* data);

/* variables */
static const struct wlr_buffer_impl buffer_impl = {
    .destroy = bufdestroy,
    .begin_data_ptr_access = bufdatabegin,
    .end_data_ptr_access = bufdataend,
};

/* function implementations */
void bufdestroy(struct wlr_buffer* wlr_buffer)
{
    Buffer* buf = wl_container_of(wlr_buffer, buf, base);
    if (buf->busy)
        wl_list_remove(&buf->release.link);
    drwl_image_destroy(buf->image);
    free(buf);
}

bool bufdatabegin(struct wlr_buffer* wlr_buffer,
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

void bufdataend(struct wlr_buffer* wlr_buffer) {}

/* Grabs a free width*height buffer from pool, allocating it on first use. The
 * bar and every client's title bar own a pool, as their sizes differ. */
Buffer* bufget(Buffer** pool, size_t poollen, int width, int height)
{
    size_t i;
    Buffer* buf = NULL;

    for (i = 0; i < poollen; i++) {
        if (pool[i]) {
            if (pool[i]->busy)
                continue;
            buf = pool[i];
            break;
        }

        buf = ecalloc(1, sizeof(Buffer) + ((size_t)width * 4 * (size_t)height));
        buf->image = drwl_image_create(NULL, width, height, buf->data);
        wlr_buffer_init(&buf->base, &buffer_impl, width, height);
        pool[i] = buf;
        break;
    }
    if (!buf)
        return NULL;

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
            pool[i] = NULL;
        }
}

static void bufrelease(struct wl_listener* listener, void* data)
{
    Buffer* buf = wl_container_of(listener, buf, release);
    buf->busy = false;
    wl_list_remove(&buf->release.link);
}
