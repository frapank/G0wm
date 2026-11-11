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
#include "desktop/systray/icon.h"
#include "output/monitor.h"
#include "output/privacy.h"
#include "render/buffer.h"
#include "render/corner.h"
#include "window/client.h"
#include "window/toplevel.h"

constexpr int MAXBOXES = 64;

typedef struct {
    struct wlr_ext_image_capture_source_v1_interface wrapped_interface;
    const struct wlr_ext_image_capture_source_v1_interface* original_interface;
    struct wlr_ext_image_capture_source_v1* source;
    struct wlr_output* output;
    WindowClient* client;
    struct wl_listener destroy;
} PrivacyCaptureSource;

/* black box, or buf drawn over it */
typedef struct {
    struct wlr_box buffer_box;
    RenderBuffer* placeholder_buffer;
} PrivacyMask;

typedef struct {
    struct wlr_screencopy_frame_v1* frame;
    struct wlr_buffer* buffer;
    struct wlr_box buffer_box;
} ScreencopyFrame;

/* function declarations */
static int privacy_black_out(struct wlr_buffer* buffer, PrivacyMask* patches, int patch_count);
static void privacy_copy_frame(struct wlr_ext_image_capture_source_v1* source,
                               struct wlr_ext_image_copy_capture_frame_v1* frame,
                               struct wlr_ext_image_capture_source_v1_frame_event* event);
static pixman_format_code_t privacy_pixman_format(uint32_t format);
static int privacy_private_boxes(Monitor* monitor, PrivacyMask* masks);
static RenderBuffer* privacy_draw_placeholder(WindowClient* client,
                                              int width,
                                              int height,
                                              int radius);
static int privacy_to_buffer(Monitor* monitor, struct wlr_box box, struct wlr_box* out_box);
static void privacy_shield(struct wlr_ext_image_capture_source_v1* source,
                           struct wlr_output* output,
                           WindowClient* client);
static void privacy_handle_shield_destroy(struct wl_listener* listener, void*);
static void privacy_wipe(struct wlr_buffer* buffer);

/* variables */
static struct wl_array privacy_copies;
static PrivacyMask privacy_masks[MAXBOXES];
static int privacy_mask_count;
static RasterIcon* privacy_icon;
static int privacy_icon_size = -1;
static Fnt* privacy_font;
static float privacy_font_scale;

/* function implementations */

static int privacy_black_out(struct wlr_buffer* buffer, PrivacyMask* patches, int patch_count)
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
            if (!wlr_box_intersection(&clipped_box, &patches[i].buffer_box, &buffer_box))
                continue;
            if (!patches[i].placeholder_buffer)
                pixman_image_fill_rectangles(PIXMAN_OP_SRC, image, &black, 1,
                                             &(pixman_rectangle16_t){ clipped_box.x, clipped_box.y,
                                                                      clipped_box.width,
                                                                      clipped_box.height });
            else
                pixman_image_composite32(
                    PIXMAN_OP_OVER, patches[i].placeholder_buffer->image, nullptr, image, 0, 0, 0,
                    0, patches[i].buffer_box.x, patches[i].buffer_box.y,
                    MIN(patches[i].buffer_box.width, patches[i].placeholder_buffer->base.width),
                    MIN(patches[i].buffer_box.height, patches[i].placeholder_buffer->base.height));
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
        if (!wlr_box_intersection(&clipped_box, &patches[i].buffer_box, &buffer_box))
            continue;
        if (!patches[i].placeholder_buffer ||
            !(textures[texture_count] =
                  wlr_texture_from_buffer(renderer, &patches[i].placeholder_buffer->base))) {
            wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                                               .box = clipped_box, .color = { 0, 0, 0, 1 } });
            continue;
        }
        const double scale_x = (double)textures[texture_count]->width / patches[i].buffer_box.width;
        const double scale_y =
            (double)textures[texture_count]->height / patches[i].buffer_box.height;
        wlr_render_pass_add_texture(
            pass, &(struct wlr_render_texture_options){
                      .texture = textures[texture_count++],
                      .src_box = { (clipped_box.x - patches[i].buffer_box.x) * scale_x,
                                   (clipped_box.y - patches[i].buffer_box.y) * scale_y,
                                   clipped_box.width * scale_x, clipped_box.height * scale_y },
                      .dst_box = clipped_box });
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
    PrivacyCaptureSource* shield = wl_container_of(source->impl, shield, wrapped_interface);
    PrivacyMask patches[MAXBOXES];
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (shield->client) {
        if (!shield->client->is_private) {
            shield->original_interface->copy_frame(source, frame, event);
            return;
        }
        /* a toplevel source contains the surface, without its rounded border */
        patches[0] = (PrivacyMask){ { 0, 0, frame->buffer->width, frame->buffer->height },
                                    privacy_draw_placeholder(shield->client, frame->buffer->width,
                                                             frame->buffer->height, 0) };
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
        shield->original_interface->copy_frame(source, frame, event);
        return;
    }
    if (!monitor->capture_buffer ||
        !wlr_ext_image_copy_capture_frame_v1_copy_buffer(frame, monitor->capture_buffer,
                                                         renderer) ||
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

    monitor->capture_buffer = nullptr;
    ScreencopyFrame* capture_frame;
    PrivacyMask patches[MAXBOXES];
    wl_array_for_each(capture_frame, &privacy_copies) {
        int frame_gone = 1;
        struct wlr_screencopy_frame_v1* frame;
        wl_list_for_each(frame, &screencopy_manager->frames, link) {
            if (frame == capture_frame->frame)
                frame_gone = 0;
        }
        if (frame_gone) {
            for (int i = 0; i < privacy_mask_count; i++) {
                patches[i] = privacy_masks[i];
                patches[i].buffer_box.x -= capture_frame->buffer_box.x;
                patches[i].buffer_box.y -= capture_frame->buffer_box.y;
            }
            if (!privacy_black_out(capture_frame->buffer, patches, privacy_mask_count))
                privacy_wipe(capture_frame->buffer);
        }
        wlr_buffer_unlock(capture_frame->buffer);
    }
    privacy_copies.size = 0;
}

int privacy_before_commit(Monitor* monitor, struct wlr_output_state* state)
{
    monitor->capture_buffer = state->buffer;
    privacy_copies.size = 0;
    if (!screencopy_manager || wl_list_empty(&screencopy_manager->frames) ||
        !(privacy_mask_count = privacy_private_boxes(monitor, privacy_masks)))
        return 1;
    struct wlr_screencopy_frame_v1* frame;
    wl_list_for_each(frame, &screencopy_manager->frames, link) {
        if (frame->output != monitor->wlr_output || !frame->buffer)
            continue;
        ScreencopyFrame* capture_frame = wl_array_add(&privacy_copies, sizeof(*capture_frame));
        if (!capture_frame) {
            privacy_after_commit(monitor);
            return 0;
        }
        /* ready must not be flushed before privacy_after_commit() */
        wl_client_flush(wl_resource_get_client(frame->resource));
        *capture_frame = (ScreencopyFrame){ frame, wlr_buffer_lock(frame->buffer), frame->box };
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
    privacy_reload();
}

/* also forget a failed load, so replacing the file and reloading retries it */
void privacy_reload(void)
{
    if (privacy_icon)
        icon_destroy(privacy_icon);
    privacy_icon = nullptr;
    privacy_icon_size = -1;
    if (privacy_font)
        drwl_font_destroy(privacy_font);
    privacy_font = nullptr;
    privacy_font_scale = 0;
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

/* opaque inside the curve: blending must never reveal the window underneath */
static RenderBuffer* privacy_draw_placeholder(WindowClient* client,
                                              int width,
                                              int height,
                                              int radius)
{
    Monitor* monitor = client->monitor;
    if (!monitor || width <= 0 || height <= 0)
        return nullptr;
    RenderBuffer** pool = client->privacy_buffer_pool;
    if (*pool && ((*pool)->base.width != width || (*pool)->base.height != height))
        buffer_pool_drop(pool, 1);
    RenderBuffer* buffer = buffer_get(pool, 1, width, height);
    if (!buffer)
        return nullptr;

    char text[256];
    privacy_placeholder(text, sizeof text,
                        client->private_text ? client->private_text : settings->privacy.text,
                        client_get_app_id(client));
    uint32_t scheme[] = {
        (client->private_foreground ? client->private_foreground : settings->privacy.foreground) |
            0xff,
        (client->private_background ? client->private_background : settings->privacy.background) |
            0xff,
        0,
    };
    const float font_scale = monitor->wlr_output->scale * settings->privacy.text_scale;
    if (font_scale != privacy_font_scale) {
        if (privacy_font)
            drwl_font_destroy(privacy_font);
        char attributes[32];
        snprintf(attributes, sizeof attributes, "dpi=%.2f", 96. * font_scale);
        privacy_font =
            drwl_font_create(nullptr, settings->look.font_count, settings->look.fonts, attributes);
        privacy_font_scale = font_scale;
    }
    Drwl drw = *monitor->draw_context;
    if (privacy_font)
        drwl_setfont(&drw, privacy_font);
    drwl_setimage(&drw, buffer->image);
    drwl_setscheme(&drw, scheme);
    drwl_rect(&drw, 0, 0, width, height, 1, 1);
    const int text_height = *text ? MIN(height, (int)drw.font->height) : 0;
    int gap = text_height ? text_height / 2 : 0;
    const int image_size =
        (int)fmin((double)settings->privacy.image_size * monitor->wlr_output->scale,
                  MIN(width, MAX(0, height - text_height - gap)));
    if (image_size != privacy_icon_size) {
        if (privacy_icon)
            icon_destroy(privacy_icon);
        privacy_icon = nullptr;
        privacy_icon_size = image_size;
        if (settings->privacy.image && *settings->privacy.image && image_size > 0)
            privacy_icon = icon_create_from_name(settings->privacy.image, nullptr, image_size);
    }
    const int image_width = privacy_icon ? pixman_image_get_width(privacy_icon->image) : 0;
    const int image_height = privacy_icon ? pixman_image_get_height(privacy_icon->image) : 0;
    if (!image_height)
        gap = 0;
    const int top = (height - image_height - gap - text_height) / 2;
    if (privacy_icon)
        pixman_image_composite32(PIXMAN_OP_OVER, privacy_icon->image, nullptr, buffer->image, 0, 0,
                                 0, 0, (width - image_width) / 2, top, image_width, image_height);
    if (text_height) {
        const int padding = MAX(0, (width - (int)drwl_font_getwidth(&drw, text)) / 2);
        drwl_text(&drw, 0, top + image_height + gap, width, text_height, padding, text, 0);
    }
    corner_cut(buffer->pixels, width, height, MIN(radius, MIN(width, height) / 2));
    wlr_buffer_unlock(&buffer->base);
    return buffer;
}

/* areas of m hidden from screen sharing, in buffer pixels */
static int privacy_private_boxes(Monitor* monitor, PrivacyMask* masks)
{
    struct wlr_box boxes[3];
    RenderBuffer* shared_buffers[3];
    WindowClient* client;
    int patch_count = 0;

    wl_list_for_each(client, &clients, link) {
        if (!client->is_private || !client->scene->node.enabled)
            continue;
        int box_count = 0;
        const int title_height = bar_title_height(client);
        if (client->scene_surface->node.enabled) {
            boxes[box_count] = client->geometry;
            shared_buffers[box_count++] = nullptr;
        }
        int x;
        int y;
        if (client->title_scene_buffer && client->title_scene_buffer->node.enabled &&
            wlr_scene_node_coords(&client->title_scene_buffer->node, &x, &y)) {
            /* the scene drops the title buffer once it is a texture */
            RenderBuffer* share_buffer = client->capture_buffer_pool[0];
            boxes[box_count] = (struct wlr_box){ x, y, client->title_width, title_height };
            shared_buffers[box_count++] =
                share_buffer && client->monitor &&
                        share_buffer->base.width == client->title_buffer_width_px &&
                        share_buffer->base.height == client->monitor->titlebar_metrics.height_px
                    ? share_buffer
                    : nullptr;
        }
        for (int i = 0; i < box_count; i++) {
            if (patch_count == MAXBOXES)
                goto full;
            switch (privacy_to_buffer(monitor, boxes[i], &masks[patch_count].buffer_box)) {
                case 1:
                    if (!i && client->scene_surface->node.enabled)
                        shared_buffers[i] = privacy_draw_placeholder(
                            client, masks[patch_count].buffer_box.width,
                            masks[patch_count].buffer_box.height,
                            (int)roundf((float)corner_client_radius(client) *
                                        monitor->wlr_output->scale));
                    masks[patch_count++].placeholder_buffer = shared_buffers[i];
                    break;
                case -1:
                    masks[patch_count++].placeholder_buffer = nullptr;
                    break;
            }
        }
    }

    RenderBuffer* share_buffer = monitor->capture_buffer_pool[0];
    if (monitor->capture_width && share_buffer &&
        share_buffer->base.width == monitor->capture_width &&
        share_buffer->base.height == monitor->bar_metrics.height_px &&
        monitor->bar_scene_buffer->node.enabled) {
        if (patch_count == MAXBOXES)
            goto full;
        const double scale =
            (double)monitor->bar_metrics.logical_width / monitor->bar_metrics.width_px;
        boxes[0] = (struct wlr_box){ monitor->bar_scene_buffer->node.x +
                                         (int)floor(monitor->capture_x * scale),
                                     monitor->bar_scene_buffer->node.y,
                                     (int)ceil(monitor->capture_width * scale),
                                     monitor->bar_metrics.logical_height };
        switch (privacy_to_buffer(monitor, boxes[0], &masks[patch_count].buffer_box)) {
            case 1:
                masks[patch_count++].placeholder_buffer = share_buffer;
                break;
            case -1:
                masks[patch_count++].placeholder_buffer = nullptr;
                break;
        }
    }
    return patch_count;

full:
    masks[0] =
        (PrivacyMask){ { 0, 0, monitor->wlr_output->width, monitor->wlr_output->height }, nullptr };
    return 1;
}

static void privacy_shield(struct wlr_ext_image_capture_source_v1* source,
                           struct wlr_output* output,
                           WindowClient* client)
{
    PrivacyCaptureSource* shield;

    if (source->impl->copy_frame == privacy_copy_frame ||
        !(shield = util_calloc(1, sizeof(*shield))))
        return;
    shield->original_interface = source->impl;
    shield->wrapped_interface = *source->impl;
    shield->wrapped_interface.copy_frame = privacy_copy_frame;
    shield->source = source;
    shield->output = output;
    shield->client = client;
    source->impl = &shield->wrapped_interface;
    LISTEN(&source->events.destroy, &shield->destroy, privacy_handle_shield_destroy);
}

void privacy_shield_client(WindowClient* client)
{
    if (client->capture_source)
        privacy_shield(client->capture_source, nullptr, client);
}

/* the source is still used after this signal */
static void privacy_handle_shield_destroy(struct wl_listener* listener, void*)
{
    PrivacyCaptureSource* shield = wl_container_of(listener, shield, destroy);

    shield->source->impl = shield->original_interface;
    wl_list_remove(&shield->destroy.link);
    free(shield);
}

/* title that others get to see */
const char* privacy_share_title(WindowClient* client)
{
    static char buf[256];

    if (!client->is_private)
        return client_get_title(client);
    privacy_placeholder(buf, sizeof(buf), settings->windows.private_title,
                        client_get_app_id(client));
    return buf;
}

/* b in buffer pixels, 0 when off m, -1 when rotated or zoomed */
static int privacy_to_buffer(Monitor* monitor, struct wlr_box box, struct wlr_box* out_box)
{
    struct wlr_output* output = monitor->wlr_output;
    float scale = output->scale;
    int transformed_width;
    int transformed_height;

    wlr_output_transformed_resolution(output, &transformed_width, &transformed_height);
    struct wlr_box output_geometry =
        (struct wlr_box){ 0, 0, transformed_width, transformed_height };
    int left = (int)floor((box.x - monitor->geometry.x) * scale);
    int top = (int)floor((box.y - monitor->geometry.y) * scale);
    int right = (int)ceil((box.x + box.width - monitor->geometry.x) * scale);
    int bottom = (int)ceil((box.y + box.height - monitor->geometry.y) * scale);
    box = (struct wlr_box){ left, top, right - left, bottom - top };
    struct wlr_box clip;
    if (!wlr_box_intersection(&clip, &box, &output_geometry))
        return 0;
    if (output->transform == WL_OUTPUT_TRANSFORM_NORMAL &&
        !(monitor->zoom_factor > 1 && monitor->zoom_texture)) {
        *out_box = box;
        return 1;
    }
    wlr_box_transform(&box, &clip, wlr_output_transform_invert(output->transform),
                      transformed_width, transformed_height);
    if (monitor->zoom_factor > 1 && monitor->zoom_texture) {
        left = (int)floor((box.x - monitor->zoom_source_box.x) * monitor->zoom_factor);
        top = (int)floor((box.y - monitor->zoom_source_box.y) * monitor->zoom_factor);
        right = (int)ceil((box.x + box.width - monitor->zoom_source_box.x) * monitor->zoom_factor);
        bottom =
            (int)ceil((box.y + box.height - monitor->zoom_source_box.y) * monitor->zoom_factor);
        box = (struct wlr_box){ left, top, right - left, bottom - top };
    }
    *out_box = box;
    return -1;
}

void privacy_toggle(const ActionArg*)
{
    WindowClient* selected_client = client_top(selected_monitor);

    if (!selected_client)
        return;
    selected_client->is_private = !selected_client->is_private;
    toplevel_update(selected_client);
    privacy_damage();
    bar_draw_all();
}

static void privacy_wipe(struct wlr_buffer* buffer)
{
    uint32_t format;
    size_t stride;
    void* data;

    if (wlr_buffer_begin_data_ptr_access(buffer, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &data, &format,
                                         &stride)) {
        memset(data, 0, stride * (size_t)buffer->height);
        wlr_buffer_end_data_ptr_access(buffer);
        return;
    }
    struct wlr_render_pass* pass;
    if (!(pass = wlr_renderer_begin_buffer_pass(renderer, buffer, nullptr))) {
        wlr_log(WLR_ERROR, "privacy: cannot black out a screencopy frame");
        return;
    }
    wlr_render_pass_add_rect(
        pass, &(struct wlr_render_rect_options){ .box = { 0, 0, buffer->width, buffer->height },
                                                 .color = { 0, 0, 0, 1 } });
    if (!wlr_render_pass_submit(pass))
        wlr_log(WLR_ERROR, "privacy: cannot black out a screencopy frame");
}
