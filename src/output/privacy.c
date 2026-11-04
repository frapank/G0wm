/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * private windows, masked in screen capture but not on screen
 */
#include <libdrm/drm_fourcc.h>
#include <math.h>
#include <pixman.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/interfaces/wlr_ext_image_capture_source_v1.h>
#include <wlr/render/pass.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include <wlr/util/transform.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/bar/bar.h"
#include "desktop/bar/title.h"
#include "output/monitor.h"
#include "output/privacy.h"
#include "render/buffer.h"
#include "window/client.h"
#include "window/toplevel.h"

constexpr int MAXBOXES = 64;

typedef struct {
    struct wlr_ext_image_capture_source_v1_interface impl;
    const struct wlr_ext_image_capture_source_v1_interface* orig;
    struct wlr_ext_image_capture_source_v1* source;
    struct wlr_output* output;
    Client* client;
    struct wl_listener destroy;
} Shield;

/* black box, or buf drawn over it */
typedef struct {
    struct wlr_box box;
    Buffer* buf;
} Patch;

typedef struct {
    struct wlr_screencopy_frame_v1* frame;
    struct wlr_buffer* buf;
    struct wlr_box box;
} Copy;

/* function declarations */
static int privacy_black_out(struct wlr_buffer* buf, Patch* p, int n);
static void privacy_copy_frame(
    struct wlr_ext_image_capture_source_v1* source,
    struct wlr_ext_image_copy_capture_frame_v1* frame,
    struct wlr_ext_image_capture_source_v1_frame_event* ev);
static pixman_format_code_t privacy_pixman_format(uint32_t format);
static int privacy_private_boxes(Monitor* m, Patch* out);
static int privacy_to_buffer(Monitor* m, struct wlr_box b, struct wlr_box* out);
static void privacy_shield(struct wlr_ext_image_capture_source_v1* source,
                           struct wlr_output* output,
                           Client* c);
static void privacy_handle_shield_destroy(struct wl_listener* listener,
                                          void* data);
static void privacy_wipe(struct wlr_buffer* buf);

/* variables */
static struct wl_array privacy_copies;
static Patch privacy_masks[MAXBOXES];
static int privacy_mask_count;

/* function implementations */

static int privacy_black_out(struct wlr_buffer* buf, Patch* p, int n)
{
    static constexpr pixman_color_t black = { 0, 0, 0, 0xffff };
    struct wlr_box all = { 0, 0, buf->width, buf->height }, b;
    struct wlr_render_pass* pass;
    struct wlr_texture* tex[MAXBOXES];
    pixman_format_code_t fmt;
    pixman_image_t* img;
    uint32_t format;
    size_t stride;
    void* data;
    double sx, sy;
    int ntex = 0, ok;

    if (wlr_buffer_begin_data_ptr_access(
            buf, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &data, &format, &stride)) {
        if (!(fmt = privacy_pixman_format(format)) ||
            !(img = pixman_image_create_bits_no_clear(
                  fmt, buf->width, buf->height, data, (int)stride))) {
            wlr_buffer_end_data_ptr_access(buf);
            return 0;
        }
        for (int i = 0; i < n; i++) {
            if (!wlr_box_intersection(&b, &p[i].box, &all))
                continue;
            if (!p[i].buf)
                pixman_image_fill_rectangles(
                    PIXMAN_OP_SRC,
                    img,
                    &black,
                    1,
                    &(pixman_rectangle16_t){ b.x, b.y, b.width, b.height });
            else
                pixman_image_composite32(
                    PIXMAN_OP_SRC,
                    p[i].buf->image,
                    nullptr,
                    img,
                    0,
                    0,
                    0,
                    0,
                    p[i].box.x,
                    p[i].box.y,
                    MIN(p[i].box.width, p[i].buf->base.width),
                    MIN(p[i].box.height, p[i].buf->base.height));
        }
        pixman_image_unref(img);
        wlr_buffer_end_data_ptr_access(buf);
        return 1;
    }

    if (!(pass = wlr_renderer_begin_buffer_pass(renderer, buf, nullptr)))
        return 0;
    for (int i = 0; i < n; i++) {
        if (!wlr_box_intersection(&b, &p[i].box, &all))
            continue;
        if (!p[i].buf ||
            !(tex[ntex] = wlr_texture_from_buffer(renderer, &p[i].buf->base))) {
            wlr_render_pass_add_rect(pass,
                                     &(struct wlr_render_rect_options){
                                         .box = b, .color = { 0, 0, 0, 1 } });
            continue;
        }
        sx = (double)tex[ntex]->width / p[i].box.width;
        sy = (double)tex[ntex]->height / p[i].box.height;
        wlr_render_pass_add_texture(
            pass,
            &(struct wlr_render_texture_options){
                .texture = tex[ntex++],
                .src_box = { (b.x - p[i].box.x) * sx,
                             (b.y - p[i].box.y) * sy,
                             b.width * sx,
                             b.height * sy },
                .dst_box = b,
                .blend_mode = WLR_RENDER_BLEND_MODE_NONE });
    }
    /* in use until the pass is submitted */
    ok = wlr_render_pass_submit(pass);
    while (ntex)
        wlr_texture_destroy(tex[--ntex]);
    return ok;
}

/* fail the frame rather than leak it */
static void privacy_copy_frame(
    struct wlr_ext_image_capture_source_v1* source,
    struct wlr_ext_image_copy_capture_frame_v1* frame,
    struct wlr_ext_image_capture_source_v1_frame_event* ev)
{
    Shield* s = wl_container_of(source->impl, s, impl);
    Patch b[MAXBOXES];
    struct timespec now;
    Monitor* m;
    int n;

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (s->client) {
        if (!s->client->isprivate) {
            s->orig->copy_frame(source, frame, ev);
            return;
        }
        b[0] = (Patch){ { 0, 0, frame->buffer->width, frame->buffer->height },
                        nullptr };
        if (!privacy_black_out(frame->buffer, b, 1))
            goto fail;
        wlr_ext_image_copy_capture_frame_v1_ready(
            frame, WL_OUTPUT_TRANSFORM_NORMAL, &now);
        return;
    }

    if (!(m = s->output->data))
        goto fail;
    if (!(n = privacy_private_boxes(m, b))) {
        s->orig->copy_frame(source, frame, ev);
        return;
    }
    if (!m->shown ||
        !wlr_ext_image_copy_capture_frame_v1_copy_buffer(
            frame, m->shown, renderer) ||
        !privacy_black_out(frame->buffer, b, n))
        goto fail;
    wlr_ext_image_copy_capture_frame_v1_ready(
        frame, s->output->transform, &now);
    return;

fail:
    wlr_ext_image_copy_capture_frame_v1_fail(
        frame, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN);
}

static pixman_format_code_t privacy_pixman_format(uint32_t format)
{
    switch (format) {
        case DRM_FORMAT_ARGB8888:
            return PIXMAN_a8r8g8b8;
        case DRM_FORMAT_XRGB8888:
            return PIXMAN_x8r8g8b8;
        case DRM_FORMAT_ABGR8888:
            return PIXMAN_a8b8g8r8;
        case DRM_FORMAT_XBGR8888:
            return PIXMAN_x8b8g8r8;
        case DRM_FORMAT_ARGB2101010:
            return PIXMAN_a2r10g10b10;
        case DRM_FORMAT_XRGB2101010:
            return PIXMAN_x2r10g10b10;
        case DRM_FORMAT_ABGR2101010:
            return PIXMAN_a2b10g10r10;
        case DRM_FORMAT_XBGR2101010:
            return PIXMAN_x2b10g10r10;
        default:
            return 0;
    }
}

/* screencopy copies during the commit: mask before ready is sent */
void privacy_after_commit(Monitor* m)
{
    struct wlr_screencopy_frame_v1* f;
    Patch b[MAXBOXES];
    Copy* cp;
    int gone;

    m->shown = nullptr;
    wl_array_for_each(cp, &privacy_copies)
    {
        gone = 1;
        wl_list_for_each(f, &screencopy_manager->frames, link)
        {
            if (f == cp->frame)
                gone = 0;
        }
        if (gone) {
            for (int i = 0; i < privacy_mask_count; i++) {
                b[i] = privacy_masks[i];
                b[i].box.x -= cp->box.x;
                b[i].box.y -= cp->box.y;
            }
            if (!privacy_black_out(cp->buf, b, privacy_mask_count))
                privacy_wipe(cp->buf);
        }
        wlr_buffer_unlock(cp->buf);
    }
    privacy_copies.size = 0;
}

int privacy_before_commit(Monitor* m, struct wlr_output_state* state)
{
    struct wlr_screencopy_frame_v1* f;
    Copy* cp;

    m->shown = state->buffer;
    privacy_copies.size = 0;
    if (!screencopy_manager ||
        !(privacy_mask_count = privacy_private_boxes(m, privacy_masks)))
        return 1;
    wl_list_for_each(f, &screencopy_manager->frames, link)
    {
        if (f->output != m->wlr_output || !f->buffer)
            continue;
        if (!(cp = wl_array_add(&privacy_copies, sizeof(*cp)))) {
            privacy_after_commit(m);
            return 0;
        }
        /* ready must not be flushed before privacy_after_commit() */
        wl_client_flush(wl_resource_get_client(f->resource));
        *cp = (Copy){ f, wlr_buffer_lock(f->buffer), f->box };
    }
    return 1;
}

void privacy_handle_new_capture_session(struct wl_listener*, void* data)
{
    struct wlr_ext_image_copy_capture_session_v1* session = data;
    struct wlr_output* o =
        wlr_output_try_from_ext_image_capture_source_v1(session->source);

    if (o)
        privacy_shield(session->source, o, nullptr);
}

void privacy_damage(void)
{
    Monitor* m;

    wl_list_for_each(m, &monitors, link)
    {
        wlr_damage_ring_add_whole(&m->scene_output->damage_ring);
        wlr_output_schedule_frame(m->wlr_output);
    }
}

void privacy_finish(void)
{
    wl_array_release(&privacy_copies);
}

/* areas of m hidden from screen sharing, in buffer pixels */
static int privacy_private_boxes(Monitor* m, Patch* out)
{
    struct wlr_box b[3];
    Buffer* share[3];
    Buffer* sp;
    Client* c;
    double k;
    int n = 0, nb, t, x, y;

    wl_list_for_each(c, &clients, link)
    {
        if (!c->isprivate || !c->scene->node.enabled)
            continue;
        nb = 0;
        t = bar_title_height(c);
        if (c->scene_surface->node.enabled) {
            b[nb] = (struct wlr_box){ c->geom.x + (int)c->bw,
                                      c->geom.y + (int)c->bw + t,
                                      c->geom.width - 2 * (int)c->bw,
                                      c->geom.height - 2 * (int)c->bw - t };
            share[nb++] = nullptr;
        }
        if (c->title && c->title->node.enabled &&
            wlr_scene_node_coords(&c->title->node, &x, &y)) {
            /* the scene drops the title buffer once it is a texture */
            sp = c->sharepool[0];
            b[nb] = (struct wlr_box){ x, y, c->titlew, t };
            share[nb++] = sp && c->mon && sp->base.width == c->titlebufw &&
                                  sp->base.height == c->mon->t.height
                              ? sp
                              : nullptr;
        }
        for (int i = 0; i < nb; i++) {
            if (n == MAXBOXES)
                goto full;
            switch (privacy_to_buffer(m, b[i], &out[n].box)) {
                case 1:
                    out[n++].buf = share[i];
                    break;
                case -1:
                    out[n++].buf = nullptr;
                    break;
            }
        }
    }

    sp = m->sharepool[0];
    if (m->sharew && sp && sp->base.width == m->sharew &&
        sp->base.height == m->b.height && m->scene_buffer->node.enabled) {
        if (n == MAXBOXES)
            goto full;
        k = (double)m->b.real_width / m->b.width;
        b[0] = (struct wlr_box){ m->scene_buffer->node.x +
                                     (int)floor(m->sharex * k),
                                 m->scene_buffer->node.y,
                                 (int)ceil(m->sharew * k),
                                 m->b.real_height };
        switch (privacy_to_buffer(m, b[0], &out[n].box)) {
            case 1:
                out[n++].buf = sp;
                break;
            case -1:
                out[n++].buf = nullptr;
                break;
        }
    }
    return n;

full:
    out[0] = (Patch){ { 0, 0, m->wlr_output->width, m->wlr_output->height },
                      nullptr };
    return 1;
}

static void privacy_shield(struct wlr_ext_image_capture_source_v1* source,
                           struct wlr_output* output,
                           Client* c)
{
    Shield* s;

    if (source->impl->copy_frame == privacy_copy_frame ||
        !(s = util_calloc(1, sizeof(*s))))
        return;
    s->orig = source->impl;
    s->impl = *source->impl;
    s->impl.copy_frame = privacy_copy_frame;
    s->source = source;
    s->output = output;
    s->client = c;
    source->impl = &s->impl;
    LISTEN(&source->events.destroy, &s->destroy, privacy_handle_shield_destroy);
}

void privacy_shield_client(Client* c)
{
    if (c->capture)
        privacy_shield(c->capture, nullptr, c);
}

/* the source is still used after this signal */
static void privacy_handle_shield_destroy(struct wl_listener* listener, void*)
{
    Shield* s = wl_container_of(listener, s, destroy);

    s->source->impl = s->orig;
    wl_list_remove(&s->destroy.link);
    free(s);
}

/* title that others get to see */
const char* privacy_share_title(Client* c)
{
    static char buf[256];
    size_t n = 0;

    if (!c->isprivate)
        return client_get_title(c);
    for (const char* s = private_title; *s && n < sizeof(buf) - 1; s++) {
        if (!strncmp(s, "{app}", 5)) {
            n += snprintf(buf + n, sizeof(buf) - n, "%s", client_get_app_id(c));
            n = MIN(n, sizeof(buf) - 1);
            s += 4;
        } else {
            buf[n++] = *s;
        }
    }
    buf[n] = '\0';
    return buf;
}

/* b in buffer pixels, 0 when off m, -1 when rotated or zoomed */
static int privacy_to_buffer(Monitor* m, struct wlr_box b, struct wlr_box* out)
{
    struct wlr_output* o = m->wlr_output;
    struct wlr_box all, clip;
    float sc = o->scale;
    int tw, th, x0, y0, x1, y1;

    wlr_output_transformed_resolution(o, &tw, &th);
    all = (struct wlr_box){ 0, 0, tw, th };
    x0 = (int)floor((b.x - m->m.x) * sc);
    y0 = (int)floor((b.y - m->m.y) * sc);
    x1 = (int)ceil((b.x + b.width - m->m.x) * sc);
    y1 = (int)ceil((b.y + b.height - m->m.y) * sc);
    b = (struct wlr_box){ x0, y0, x1 - x0, y1 - y0 };
    if (!wlr_box_intersection(&clip, &b, &all))
        return 0;
    if (o->transform == WL_OUTPUT_TRANSFORM_NORMAL &&
        !(m->zoom > 1 && m->zoomtex)) {
        *out = b;
        return 1;
    }
    wlr_box_transform(
        &b, &clip, wlr_output_transform_invert(o->transform), tw, th);
    if (m->zoom > 1 && m->zoomtex) {
        x0 = (int)floor((b.x - m->zoomsrc.x) * m->zoom);
        y0 = (int)floor((b.y - m->zoomsrc.y) * m->zoom);
        x1 = (int)ceil((b.x + b.width - m->zoomsrc.x) * m->zoom);
        y1 = (int)ceil((b.y + b.height - m->zoomsrc.y) * m->zoom);
        b = (struct wlr_box){ x0, y0, x1 - x0, y1 - y0 };
    }
    *out = b;
    return -1;
}

void privacy_toggle(const Arg*)
{
    Client* sel = client_top(selected_monitor);

    if (!sel)
        return;
    sel->isprivate = !sel->isprivate;
    toplevel_update(sel);
    privacy_damage();
    bar_draw_all();
}

static void privacy_wipe(struct wlr_buffer* buf)
{
    struct wlr_render_pass* pass;
    uint32_t format;
    size_t stride;
    void* data;

    if (wlr_buffer_begin_data_ptr_access(
            buf, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &data, &format, &stride)) {
        memset(data, 0, stride * (size_t)buf->height);
        wlr_buffer_end_data_ptr_access(buf);
        return;
    }
    if (!(pass = wlr_renderer_begin_buffer_pass(renderer, buf, nullptr))) {
        wlr_log(WLR_ERROR, "privacy: cannot black out a screencopy frame");
        return;
    }
    wlr_render_pass_add_rect(pass,
                             &(struct wlr_render_rect_options){
                                 .box = { 0, 0, buf->width, buf->height },
                                 .color = { 0, 0, 0, 1 } });
    if (!wlr_render_pass_submit(pass))
        wlr_log(WLR_ERROR, "privacy: cannot black out a screencopy frame");
}
