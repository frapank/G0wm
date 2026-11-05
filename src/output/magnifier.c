/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * magnifier: zooms a monitor in around the pointer. While zoomed the scene is
 * rendered unzoomed into buffers of our own, and the part on show is drawn
 * from there, scaled, onto the output.
 */
#include <pixman.h>
#include <wlr/render/pass.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include <wlr/util/transform.h>

#include "core/g0wm.h"
#include "output/magnifier.h"
#include "output/monitor.h"

constexpr float ZOOMMAX = 16.0f;

/* function declarations */
static void magnifier_damage_whole(struct wlr_output* output, struct wlr_output_state* state);

/* function implementations */
void magnifier_drop(Monitor* monitor)
{
    wlr_texture_destroy(monitor->zoomtex);
    wlr_buffer_unlock(monitor->zoombuf);
    wlr_swapchain_destroy(monitor->zoomchain);
    monitor->zoomtex = nullptr;
    monitor->zoombuf = nullptr;
    monitor->zoomchain = nullptr;
}

/* The frame of a zoomed monitor, or of one just leaving the zoom. 0 when
 * there is nothing new to show. */
int magnifier_frame(Monitor* monitor, struct wlr_output_state* state)
{
    struct wlr_output* output = monitor->wlr_output;
    struct wlr_scene_output_state_options opts = {};

    if (monitor->zoom <= 1) {
        /* the output's own buffers still hold zoomed frames */
        magnifier_drop(monitor);
        wlr_damage_ring_add_whole(&monitor->scene_output->damage_ring);
        if (!wlr_scene_output_build_state(monitor->scene_output, state, nullptr))
            return 0;
        magnifier_damage_whole(output, state);
        return 1;
    }

    if (!wlr_output_configure_primary_swapchain(output, state, &output->swapchain))
        return 0;
    if (!monitor->zoomchain || monitor->zoomchain->width != output->width ||
        monitor->zoomchain->height != output->height) {
        magnifier_drop(monitor);
        /* ponytail: the output's format, taken to be one the renderer can
         * sample too; intersect with its texture formats if one is not */
        if (!(monitor->zoomchain = wlr_swapchain_create(allocator, output->width, output->height,
                                                        &output->swapchain->format)))
            return 0;
    }

    /* the part on show, worked out upright and then turned the way the
     * output's buffers are. It is placed in proportion to the pointer, which
     * so stays over what it is over unzoomed: input needs no remapping. */
    int transformed_width;
    int transformed_height;
    wlr_output_transformed_resolution(output, &transformed_width, &transformed_height);
    const double pointer_fraction_x = (cursor->x - monitor->m.x) / monitor->m.width;
    const double pointer_fraction_y = (cursor->y - monitor->m.y) / monitor->m.height;
    struct wlr_fbox src;
    src.width = transformed_width / monitor->zoom;
    src.height = transformed_height / monitor->zoom;
    src.x = (pointer_fraction_x < 0   ? 0
             : pointer_fraction_x > 1 ? 1
                                      : pointer_fraction_x) *
            (transformed_width - src.width);
    src.y = (pointer_fraction_y < 0   ? 0
             : pointer_fraction_y > 1 ? 1
                                      : pointer_fraction_y) *
            (transformed_height - src.height);
    wlr_fbox_transform(&src, &src, wlr_output_transform_invert(output->transform),
                       transformed_width, transformed_height);

    if (monitor->zoomtex && !wlr_scene_output_needs_frame(monitor->scene_output)) {
        if (wlr_fbox_equal(&src, &monitor->zoomsrc))
            return 0;
    } else {
        opts.swapchain = monitor->zoomchain;
        if (!wlr_scene_output_build_state(monitor->scene_output, state, &opts) || !state->buffer)
            return 0;
        wlr_texture_destroy(monitor->zoomtex);
        wlr_buffer_unlock(monitor->zoombuf);
        monitor->zoombuf = wlr_buffer_lock(state->buffer);
        if (!(monitor->zoomtex = wlr_texture_from_buffer(renderer, monitor->zoombuf))) {
            wlr_log(WLR_ERROR, "magnifier: cannot draw from the frame");
            return 0;
        }
        /* they belong to the frame, not to the zoomed copy made below */
        state->committed &= ~(WLR_OUTPUT_STATE_WAIT_TIMELINE | WLR_OUTPUT_STATE_SIGNAL_TIMELINE);
    }
    monitor->zoomsrc = src;

    struct wlr_render_pass* pass = wlr_output_begin_render_pass(output, state, nullptr);
    if (!pass)
        return 0;
    wlr_render_pass_add_texture(pass,
                                &(struct wlr_render_texture_options){
                                    .texture = monitor->zoomtex,
                                    .src_box = src,
                                    .dst_box = { .width = output->width, .height = output->height },
                                    .filter_mode = WLR_SCALE_FILTER_BILINEAR,
                                });
    if (!wlr_render_pass_submit(pass))
        return 0;
    magnifier_damage_whole(output, state);
    return 1;
}

void magnifier_magnify(const Arg* arg)
{
    Monitor* monitor = monitor_at(cursor->x, cursor->y);

    if (!monitor && !(monitor = selected_monitor))
        return;

    /* a step and its opposite cancel out: +0.25 is x1.25, -0.25 is /1.25 */
    float zoom_level;
    if (arg->f == 0)
        zoom_level = 1;
    else if (arg->f > 0)
        zoom_level = monitor->zoom * (1 + arg->f);
    else
        zoom_level = monitor->zoom / (1 - arg->f);
    magnifier_zoom(monitor, zoom_level);
}

void magnifier_zoom(Monitor* monitor, float zoom_level)
{
    zoom_level = zoom_level < 1.01f ? 1 : zoom_level > ZOOMMAX ? ZOOMMAX : zoom_level;
    if (zoom_level == monitor->zoom)
        return;

    /* while zoomed the pointer is drawn into the frame, to be zoomed with it,
     * and no window may go straight to the screen past the zoom */
    if ((zoom_level > 1) != (monitor->zoom > 1)) {
        wlr_output_lock_software_cursors(monitor->wlr_output, zoom_level > 1);
        wlr_output_lock_attach_render(monitor->wlr_output, zoom_level > 1);
    }
    monitor->zoom = zoom_level;
    wlr_output_schedule_frame(monitor->wlr_output);
}

static void magnifier_damage_whole(struct wlr_output* output, struct wlr_output_state* state)
{
    pixman_region32_t damage;

    pixman_region32_init_rect(&damage, 0, 0, output->width, output->height);
    wlr_output_state_set_damage(state, &damage);
    pixman_region32_fini(&damage);
}
