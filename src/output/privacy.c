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
static int privacy_black_out(struct wlr_buffer* buffer, Patch* patches, int patch_count);
static void privacy_copy_frame(struct wlr_ext_image_capture_source_v1* source,
                               struct wlr_ext_image_copy_capture_frame_v1* frame,
                               struct wlr_ext_image_capture_source_v1_frame_event* event);
static pixman_format_code_t privacy_pixman_format(uint32_t format);
static int privacy_private_boxes(Monitor* monitor, Patch* out);
static int privacy_to_buffer(Monitor* monitor, struct wlr_box box, struct wlr_box* out);
static void privacy_shield(struct wlr_ext_image_capture_source_v1* source,
                           struct wlr_output* output,
                           Client* client);
static void privacy_handle_shield_destroy(struct wl_listener* listener, void*);
static void privacy_wipe(struct wlr_buffer* buf);

/* variables */
static struct wl_array privacy_copies;
static Patch privacy_masks[MAXBOXES];
static int privacy_mask_count;

/* function implementations */

static int privacy_black_out(struct wlr_buffer* buffer, Patch* patches, int patch_count)
{
    static constexpr pixman_color_t black = { 0, 0, 0, 0xffff };
    const struct wlr_box buffer_box = { 0, 0, buffer->width, buffer->height };
    struct wlr_box clipped_box;
    uint32_t format;
    size_t stride;
    void* data;

    if (wlr_buffer_begin_data_ptr_access(buffer, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &data, &format,
                                         &stride)) {
        const pixman_format_code_t pixman_format = privacy_pixman_format(format);
        pixman_image_t* image =
            pixman_format ? pixman_image_create_bits_no_clear(pixman_format, buffer->width,
                                                              buffer->height, data, (int)stride)
                          : nullptr;
        if (!image) {
            wlr_buffer_end_data_ptr_access(buffer);
            return 0;
        }
        for (int i = 0; i < patch_count; i++) {
            if (!wlr_box_intersection(&clipped_box, &patches[i].box, &buffer_box))
                continue;
            if (!patches[i].buf)
                pixman_image_fill_rectangles(PIXMAN_OP_SRC, image, &black, 1,
                                             &(pixman_rectangle16_t){ clipped_box.x, clipped_box.y,
                                                                      clipped_box.width,
                                                                      clipped_box.height });
            else
                pixman_image_composite32(PIXMAN_OP_SRC, patches[i].buf->image, nullptr, image, 0, 0,
                                         0, 0, patches[i].box.x, patches[i].box.y,
                                         MIN(patches[i].box.width, patches[i].buf->base.width),
                                         MIN(patches[i].box.height, patches[i].buf->base.height));
        }
        pixman_image_unref(image);
        wlr_buffer_end_data_ptr_access(buffer);
        return 1;
    }

    struct wlr_render_pass* pass = wlr_renderer_begin_buffer_pass(renderer, buffer, nullptr);
    if (!pass)
        return 0;
    int texture_count = 0;
    struct wlr_texture* textures[MAXBOXES];
    for (int i = 0; i < patch_count; i++) {
        if (!wlr_box_intersection(&clipped_box, &patches[i].box, &buffer_box))
            continue;
        if (!patches[i].buf ||
            !(textures[texture_count] = wlr_texture_from_buffer(renderer, &patches[i].buf->base))) {
            wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                                               .box = clipped_box, .color = { 0, 0, 0, 1 } });
            continue;
        }
        const double scale_x = (double)textures[texture_count]->width / patches[i].box.width;
        const double scale_y = (double)textures[texture_count]->height / patches[i].box.height;
        wlr_render_pass_add_texture(
            pass, &(struct wlr_render_texture_options){
                      .texture = textures[texture_count++],
                      .src_box = { (clipped_box.x - patches[i].box.x) * scale_x,
                                   (clipped_box.y - patches[i].box.y) * scale_y,
                                   clipped_box.width * scale_x, clipped_box.height * scale_y },
                      .dst_box = clipped_box,
                      .blend_mode = WLR_RENDER_BLEND_MODE_NONE });
    }
    /* in use until the pass is submitted */
    const int submitted = wlr_render_pass_submit(pass);
    while (texture_count)
        wlr_texture_destroy(textures[--texture_count]);
    return submitted;
}

/* fail the frame rather than leak it */
static void privacy_copy_frame(struct wlr_ext_image_capture_source_v1* source,
                               struct wlr_ext_image_copy_capture_frame_v1* frame,
                               struct wlr_ext_image_capture_source_v1_frame_event* event)
{
    Shield* shield = wl_container_of(source->impl, shield, impl);
    Patch patches[MAXBOXES];
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (shield->client) {
        if (!shield->client->isprivate) {
            shield->orig->copy_frame(source, frame, event);
            return;
        }
        patches[0] = (Patch){ { 0, 0, frame->buffer->width, frame->buffer->height }, nullptr };
        if (!privacy_black_out(frame->buffer, patches, 1))
            goto fail;
        wlr_ext_image_copy_capture_frame_v1_ready(frame, WL_OUTPUT_TRANSFORM_NORMAL, &now);
        return;
    }

    Monitor* monitor = shield->output->data;
    if (!monitor)
        goto fail;
    const int patch_count = privacy_private_boxes(monitor, patches);
    if (!patch_count) {
        shield->orig->copy_frame(source, frame, event);
        return;
    }
    if (!monitor->shown ||
        !wlr_ext_image_copy_capture_frame_v1_copy_buffer(frame, monitor->shown, renderer) ||
        !privacy_black_out(frame->buffer, patches, patch_count))
        goto fail;
    wlr_ext_image_copy_capture_frame_v1_ready(frame, shield->output->transform, &now);
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
void privacy_after_commit(Monitor* monitor)
{

    monitor->shown = nullptr;
    Copy* copy;
    Patch patches[MAXBOXES];
    wl_array_for_each(copy, &privacy_copies) {
        int frame_gone = 1;
        struct wlr_screencopy_frame_v1* frame;
        wl_list_for_each(frame, &screencopy_manager->frames, link) {
            if (frame == copy->frame)
                frame_gone = 0;
        }
        if (frame_gone) {
            for (int i = 0; i < privacy_mask_count; i++) {
                patches[i] = privacy_masks[i];
                patches[i].box.x -= copy->box.x;
                patches[i].box.y -= copy->box.y;
            }
            if (!privacy_black_out(copy->buf, patches, privacy_mask_count))
                privacy_wipe(copy->buf);
        }
        wlr_buffer_unlock(copy->buf);
    }
    privacy_copies.size = 0;
}

int privacy_before_commit(Monitor* monitor, struct wlr_output_state* state)
{
    monitor->shown = state->buffer;
    privacy_copies.size = 0;
    if (!screencopy_manager ||
        !(privacy_mask_count = privacy_private_boxes(monitor, privacy_masks)))
        return 1;
    struct wlr_screencopy_frame_v1* frame;
    wl_list_for_each(frame, &screencopy_manager->frames, link) {
        if (frame->output != monitor->wlr_output || !frame->buffer)
            continue;
        Copy* copy = wl_array_add(&privacy_copies, sizeof(*copy));
        if (!copy) {
            privacy_after_commit(monitor);
            return 0;
        }
        /* ready must not be flushed before privacy_after_commit() */
        wl_client_flush(wl_resource_get_client(frame->resource));
        *copy = (Copy){ frame, wlr_buffer_lock(frame->buffer), frame->box };
    }
    return 1;
}

void privacy_handle_new_capture_session(struct wl_listener*, void* data)
{
    struct wlr_ext_image_copy_capture_session_v1* session = data;
    struct wlr_output* output = wlr_output_try_from_ext_image_capture_source_v1(session->source);

    if (output)
        privacy_shield(session->source, output, nullptr);
}

void privacy_damage(void)
{
    Monitor* monitor;

    wl_list_for_each(monitor, &monitors, link) {
        wlr_damage_ring_add_whole(&monitor->scene_output->damage_ring);
        wlr_output_schedule_frame(monitor->wlr_output);
    }
}

void privacy_finish(void)
{
    wl_array_release(&privacy_copies);
}

/* template with {app} replaced, an unset one gives an empty text */
void privacy_placeholder(char* buf, size_t size, const char* template, const char* app)
{
    size_t length = 0;

    for (; template && *template && length < size - 1; template ++) {
        if (!strncmp(template, "{app}", 5)) {
            length += snprintf(buf + length, size - length, "%s", app);
            length = MIN(length, size - 1);
            template += 4;
        } else {
            buf[length++] = *template;
        }
    }
    buf[length] = '\0';
}

/* areas of m hidden from screen sharing, in buffer pixels */
static int privacy_private_boxes(Monitor* monitor, Patch* out)
{
    struct wlr_box boxes[3];
    Buffer* shared_buffers[3];
    Client* client;
    int patch_count = 0;

    wl_list_for_each(client, &clients, link) {
        if (!client->isprivate || !client->scene->node.enabled)
            continue;
        int box_count = 0;
        const int title_height = bar_title_height(client);
        if (client->scene_surface->node.enabled) {
            boxes[box_count] =
                (struct wlr_box){ client->geom.x + (int)client->bw,
                                  client->geom.y + (int)client->bw + title_height,
                                  client->geom.width - 2 * (int)client->bw,
                                  client->geom.height - 2 * (int)client->bw - title_height };
            shared_buffers[box_count++] = nullptr;
        }
        int x;
        int y;
        if (client->title && client->title->node.enabled &&
            wlr_scene_node_coords(&client->title->node, &x, &y)) {
            /* the scene drops the title buffer once it is a texture */
            Buffer* share_buffer = client->sharepool[0];
            boxes[box_count] = (struct wlr_box){ x, y, client->titlew, title_height };
            shared_buffers[box_count++] = share_buffer && client->mon &&
                                                  share_buffer->base.width == client->titlebufw &&
                                                  share_buffer->base.height == client->mon->t.height
                                              ? share_buffer
                                              : nullptr;
        }
        for (int i = 0; i < box_count; i++) {
            if (patch_count == MAXBOXES)
                goto full;
            switch (privacy_to_buffer(monitor, boxes[i], &out[patch_count].box)) {
                case 1:
                    out[patch_count++].buf = shared_buffers[i];
                    break;
                case -1:
                    out[patch_count++].buf = nullptr;
                    break;
            }
        }
    }

    Buffer* share_buffer = monitor->sharepool[0];
    if (monitor->sharew && share_buffer && share_buffer->base.width == monitor->sharew &&
        share_buffer->base.height == monitor->b.height && monitor->scene_buffer->node.enabled) {
        if (patch_count == MAXBOXES)
            goto full;
        const double scale = (double)monitor->b.real_width / monitor->b.width;
        boxes[0] =
            (struct wlr_box){ monitor->scene_buffer->node.x + (int)floor(monitor->sharex * scale),
                              monitor->scene_buffer->node.y, (int)ceil(monitor->sharew * scale),
                              monitor->b.real_height };
        switch (privacy_to_buffer(monitor, boxes[0], &out[patch_count].box)) {
            case 1:
                out[patch_count++].buf = share_buffer;
                break;
            case -1:
                out[patch_count++].buf = nullptr;
                break;
        }
    }
    return patch_count;

full:
    out[0] = (Patch){ { 0, 0, monitor->wlr_output->width, monitor->wlr_output->height }, nullptr };
    return 1;
}

static void privacy_shield(struct wlr_ext_image_capture_source_v1* source,
                           struct wlr_output* output,
                           Client* client)
{
    Shield* shield;

    if (source->impl->copy_frame == privacy_copy_frame ||
        !(shield = util_calloc(1, sizeof(*shield))))
        return;
    shield->orig = source->impl;
    shield->impl = *source->impl;
    shield->impl.copy_frame = privacy_copy_frame;
    shield->source = source;
    shield->output = output;
    shield->client = client;
    source->impl = &shield->impl;
    LISTEN(&source->events.destroy, &shield->destroy, privacy_handle_shield_destroy);
}

void privacy_shield_client(Client* client)
{
    if (client->capture)
        privacy_shield(client->capture, nullptr, client);
}

/* the source is still used after this signal */
static void privacy_handle_shield_destroy(struct wl_listener* listener, void*)
{
    Shield* shield = wl_container_of(listener, shield, destroy);

    shield->source->impl = shield->orig;
    wl_list_remove(&shield->destroy.link);
    free(shield);
}

/* title that others get to see */
const char* privacy_share_title(Client* client)
{
    static char buf[256];

    if (!client->isprivate)
        return client_get_title(client);
    privacy_placeholder(buf, sizeof(buf), private_title, client_get_app_id(client));
    return buf;
}

/* b in buffer pixels, 0 when off m, -1 when rotated or zoomed */
static int privacy_to_buffer(Monitor* monitor, struct wlr_box box, struct wlr_box* out)
{
    struct wlr_output* output = monitor->wlr_output;
    float scale = output->scale;
    int transformed_width;
    int transformed_height;

    wlr_output_transformed_resolution(output, &transformed_width, &transformed_height);
    struct wlr_box all = (struct wlr_box){ 0, 0, transformed_width, transformed_height };
    int left = (int)floor((box.x - monitor->m.x) * scale);
    int top = (int)floor((box.y - monitor->m.y) * scale);
    int right = (int)ceil((box.x + box.width - monitor->m.x) * scale);
    int bottom = (int)ceil((box.y + box.height - monitor->m.y) * scale);
    box = (struct wlr_box){ left, top, right - left, bottom - top };
    struct wlr_box clip;
    if (!wlr_box_intersection(&clip, &box, &all))
        return 0;
    if (output->transform == WL_OUTPUT_TRANSFORM_NORMAL &&
        !(monitor->zoom > 1 && monitor->zoomtex)) {
        *out = box;
        return 1;
    }
    wlr_box_transform(&box, &clip, wlr_output_transform_invert(output->transform),
                      transformed_width, transformed_height);
    if (monitor->zoom > 1 && monitor->zoomtex) {
        left = (int)floor((box.x - monitor->zoomsrc.x) * monitor->zoom);
        top = (int)floor((box.y - monitor->zoomsrc.y) * monitor->zoom);
        right = (int)ceil((box.x + box.width - monitor->zoomsrc.x) * monitor->zoom);
        bottom = (int)ceil((box.y + box.height - monitor->zoomsrc.y) * monitor->zoom);
        box = (struct wlr_box){ left, top, right - left, bottom - top };
    }
    *out = box;
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
    uint32_t format;
    size_t stride;
    void* data;

    if (wlr_buffer_begin_data_ptr_access(buf, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &data, &format,
                                         &stride)) {
        memset(data, 0, stride * (size_t)buf->height);
        wlr_buffer_end_data_ptr_access(buf);
        return;
    }
    struct wlr_render_pass* pass;
    if (!(pass = wlr_renderer_begin_buffer_pass(renderer, buf, nullptr))) {
        wlr_log(WLR_ERROR, "privacy: cannot black out a screencopy frame");
        return;
    }
    wlr_render_pass_add_rect(
        pass, &(struct wlr_render_rect_options){ .box = { 0, 0, buf->width, buf->height },
                                                 .color = { 0, 0, 0, 1 } });
    if (!wlr_render_pass_submit(pass))
        wlr_log(WLR_ERROR, "privacy: cannot black out a screencopy frame");
}
