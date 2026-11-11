/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * outputs, their layout and the layer-shell surfaces on them
 */
#include <drwl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_content_type_v1.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/bar/bar.h"
#include "desktop/systray/tray.h"
#include "input/pointer.h"
#include "input/tablet.h"
#include "output/lock.h"
#include "output/magnifier.h"
#include "output/monitor.h"
#include "output/privacy.h"
#include "render/buffer.h"
#include "render/opacity.h"
#include "window/client.h"
#include "window/layout.h"

/* function declarations */
static void monitor_apply_rules(Monitor* monitor, struct wlr_output_state* state);
static void monitor_arrange_layer(Monitor* monitor,
                                  struct wl_list* list,
                                  struct wlr_box* usable_area,
                                  int exclusive);
static void monitor_handle_destroy(struct wl_listener* listener, void*);
static void monitor_close(Monitor* monitor);
static void monitor_handle_layer_commit(struct wl_listener* listener, void*);
static void monitor_handle_layer_destroy(struct wl_listener* listener, void*);
static Monitor* monitor_in_direction(enum wlr_direction direction);
static WindowClient* monitor_fullscreen_client(Monitor* monitor);
static void monitor_game_mode(Monitor* monitor, struct wlr_output_state* state);
static void monitor_apply_or_test_config(struct wlr_output_configuration_v1* config, int test);
static void monitor_handle_frame(struct wl_listener* listener, void*);
static void monitor_handle_request_state(struct wl_listener*, void* data);
static void monitor_handle_layer_unmap(struct wl_listener* listener, void*);

/* variables */
/* Map from ZWLR_LAYER_SHELL_* constants to Lyr* enum */
constexpr int monitor_layer_map[] = { SCENE_LAYER_BACKGROUND, SCENE_LAYER_BOTTOM, SCENE_LAYER_TOP,
                                      SCENE_LAYER_OVERLAY };
static_assert(LENGTH(monitor_layer_map) == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY + 1);

/* function implementations */
static void monitor_arrange_layer(Monitor* monitor,
                                  struct wl_list* list,
                                  struct wlr_box* usable_area,
                                  int exclusive)
{
    struct wlr_box full_area = monitor->geometry;

    LayerSurface* layer;
    wl_list_for_each(layer, list, link) {
        struct wlr_layer_surface_v1* layer_surface = layer->layer_surface;

        if (!layer_surface->initialized)
            continue;

        if (exclusive != (layer_surface->current.exclusive_zone > 0))
            continue;

        wlr_scene_layer_surface_v1_configure(layer->scene_layer, &full_area, usable_area);
        wlr_scene_node_set_position(&layer->popup_tree->node, layer->scene->node.x,
                                    layer->scene->node.y);
    }
}

void monitor_arrange_layers(Monitor* monitor)
{
    struct wlr_box usable_area = monitor->geometry;
    uint32_t layers_above_shell[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
    };
    if (!monitor->wlr_output->enabled)
        return;

    if (monitor->bar_scene_buffer->node.enabled) {
        /* the padding goes with the bar, so tiles keep the same gap */
        usable_area.height -= monitor->bar_metrics.logical_height + (int)settings->bar.padding;
        usable_area.y += settings->bar.top
                             ? monitor->bar_metrics.logical_height + (int)settings->bar.padding
                             : 0;
    }

    /* Arrange exclusive surfaces from top->bottom */
    for (int i = 3; i >= 0; i--)
        monitor_arrange_layer(monitor, &monitor->layers[i], &usable_area, 1);

    if (!wlr_box_equal(&usable_area, &monitor->usable_area)) {
        monitor->usable_area = usable_area;
        layout_arrange(monitor);
    }

    /* Arrange non-exclusive surfaces from top->bottom */
    for (int i = 3; i >= 0; i--)
        monitor_arrange_layer(monitor, &monitor->layers[i], &usable_area, 0);

    LayerSurface* layer_surface;
    /* Find topmost keyboard interactive layer, if such a layer exists */
    for (int i = 0; i < (int)LENGTH(layers_above_shell); i++) {
        wl_list_for_each_reverse(layer_surface, &monitor->layers[layers_above_shell[i]], link) {
            if (lock_active || !layer_surface->layer_surface->current.keyboard_interactive ||
                !layer_surface->mapped)
                continue;
            /* Deactivate the focused client. */
            client_focus(nullptr, 0);
            exclusive_focus = layer_surface;
            client_notify_enter(layer_surface->layer_surface->surface, wlr_seat_get_keyboard(seat));
            return;
        }
    }
}

static void monitor_handle_destroy(struct wl_listener* listener, void*)
{
    Monitor* monitor = wl_container_of(listener, monitor, destroy);
    LayerSurface* layer_surface;
    LayerSurface* next_layer_surface;

    /* monitor->layers[i] are intentionally not unlinked */
    for (size_t i = 0; i < LENGTH(monitor->layers); i++) {
        wl_list_for_each_safe(layer_surface, next_layer_surface, &monitor->layers[i], link)
            wlr_layer_surface_v1_destroy(layer_surface->layer_surface);
    }

    for (size_t i = 0; i < LENGTH(monitor->bar_buffer_pool); i++)
        wlr_buffer_drop(&monitor->bar_buffer_pool[i]->base);
    buffer_pool_drop(monitor->capture_buffer_pool, LENGTH(monitor->capture_buffer_pool));
#ifdef INTEGRATED_BACKGROUND
    buffer_pool_drop(monitor->wallpaper_buffer_pool, LENGTH(monitor->wallpaper_buffer_pool));
    buffer_pool_drop(monitor->blur_buffer_pool, LENGTH(monitor->blur_buffer_pool));
#endif

    if (monitor->tray)
        tray_destroy(monitor->tray);

    magnifier_drop(monitor);

    drwl_setimage(monitor->draw_context, nullptr);
    drwl_destroy(monitor->draw_context);

    wl_list_remove(&monitor->destroy.link);
    wl_list_remove(&monitor->frame.link);
    wl_list_remove(&monitor->link);
    wl_list_remove(&monitor->request_state.link);
    if (monitor->lock_surface)
        lock_handle_surface_destroy(&monitor->destroy_lock_surface, nullptr);
    monitor->wlr_output->data = nullptr;
    wlr_output_layout_remove(output_layout, monitor->wlr_output);
    wlr_scene_output_destroy(monitor->scene_output);

    monitor_close(monitor);
    wlr_scene_node_destroy(&monitor->fullscreen_background->node);
    wlr_scene_node_destroy(&monitor->bar_scene_buffer->node);
#ifdef INTEGRATED_BACKGROUND
    wlr_scene_node_destroy(&monitor->wallpaper_scene_buffer->node);
    wlr_scene_node_destroy(&monitor->bar_blur_scene_buffer->node);
#endif
    free(monitor);
}

static void monitor_close(Monitor* monitor)
{
    /* update selected_monitor if needed and
     * move closed monitor's clients to the focused one */
    int i = 0, nmons = wl_list_length(&monitors);
    if (!nmons) {
        selected_monitor = nullptr;
    } else if (monitor == selected_monitor) {
        do /* don't switch to disabled monitors */
            selected_monitor = wl_container_of(monitors.next, selected_monitor, link);
        while (!selected_monitor->wlr_output->enabled && i++ < nmons);

        if (!selected_monitor->wlr_output->enabled)
            selected_monitor = nullptr;
    }

    WindowClient* client;
    wl_list_for_each(client, &clients, link) {
        if (client->is_floating && client->geometry.x > monitor->geometry.width)
            client_resize(client,
                          (struct wlr_box){ .x = client->geometry.x - monitor->usable_area.width,
                                            .y = client->geometry.y,
                                            .width = client->geometry.width,
                                            .height = client->geometry.height },
                          0);
        if (client->monitor == monitor)
            client_set_monitor(client, selected_monitor, client->tags);
    }
    client_focus(client_top(selected_monitor), 1);
    bar_draw_all();
}

static void monitor_handle_layer_commit(struct wl_listener* listener, void*)
{
    LayerSurface* layer = wl_container_of(listener, layer, surface_commit);
    struct wlr_layer_surface_v1* layer_surface = layer->layer_surface;
    struct wlr_scene_tree* scene_layer = layers[monitor_layer_map[layer_surface->current.layer]];
    struct wlr_layer_surface_v1_state old_state;

    if (layer->layer_surface->initial_commit) {
        client_set_scale(layer_surface->surface, layer->monitor->wlr_output->scale);

        /* Temporarily set the layer's current state to pending
         * so that we can easily arrange it */
        old_state = layer->layer_surface->current;
        layer->layer_surface->current = layer->layer_surface->pending;
        monitor_arrange_layers(layer->monitor);
        layer->layer_surface->current = old_state;
        return;
    }

    if (layer_surface->current.committed == 0 && layer->mapped == layer_surface->surface->mapped)
        return;
    layer->mapped = layer_surface->surface->mapped;

    if (scene_layer != layer->scene->node.parent) {
        wlr_scene_node_reparent(&layer->scene->node, scene_layer);
        wl_list_remove(&layer->link);
        wl_list_insert(&layer->monitor->layers[layer_surface->current.layer], &layer->link);
        wlr_scene_node_reparent(&layer->popup_tree->node,
                                (layer_surface->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP
                                     ? layers[SCENE_LAYER_TOP]
                                     : scene_layer));
    }

    monitor_arrange_layers(layer->monitor);
}

void monitor_handle_new_layer_surface(struct wl_listener*, void* data)
{
    struct wlr_layer_surface_v1* layer_surface = data;
    struct wlr_surface* surface = layer_surface->surface;
    struct wlr_scene_tree* scene_layer = layers[monitor_layer_map[layer_surface->pending.layer]];

    if (!layer_surface->output &&
        !(layer_surface->output = selected_monitor ? selected_monitor->wlr_output : nullptr)) {
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }

    LayerSurface* layer = layer_surface->data = util_calloc(1, sizeof(*layer));
    layer->type = SURFACE_TYPE_LAYER_SHELL;
    LISTEN(&surface->events.commit, &layer->surface_commit, monitor_handle_layer_commit);
    LISTEN(&surface->events.unmap, &layer->unmap, monitor_handle_layer_unmap);
    LISTEN(&layer_surface->events.destroy, &layer->destroy, monitor_handle_layer_destroy);

    layer->layer_surface = layer_surface;
    layer->monitor = layer_surface->output->data;
    layer->scene_layer = wlr_scene_layer_surface_v1_create(scene_layer, layer_surface);
    layer->scene = layer->scene_layer->tree;
    layer->popup_tree = surface->data = wlr_scene_tree_create(
        layer_surface->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[SCENE_LAYER_TOP]
                                                                     : scene_layer);
    layer->scene->node.data = layer->popup_tree->node.data = layer;

    wl_list_insert(&layer->monitor->layers[layer_surface->pending.layer], &layer->link);
    wlr_surface_send_enter(surface, layer_surface->output);
}

/* the first matching rule onto the monitor and the state about to be
 * committed; also the path a reload takes */
static void monitor_apply_rules(Monitor* monitor, struct wlr_output_state* state)
{
    struct wlr_output* wlr_output = monitor->wlr_output;
    struct wlr_output_mode* mode = nullptr;

    monitor->gaps_enabled = settings->windows.gaps;
    for (const MonitorRule* rule = settings->rules.monitor_rules;
         rule < settings->rules.monitor_rules + settings->rules.monitor_rule_count; rule++) {
        if (rule->name && !strstr(wlr_output->name, rule->name))
            continue;
        monitor->geometry.x = rule->x;
        monitor->geometry.y = rule->y;
        monitor->master_factor = rule->master_factor;
        monitor->master_count = rule->master_count;
        monitor->layouts[0] = rule->layout;
        monitor->layouts[1] = &settings->rules.layouts[settings->rules.layout_count > 1 &&
                                                       rule->layout != &settings->rules.layouts[1]];
        snprintf(monitor->layout_symbol, LENGTH(monitor->layout_symbol), "%s",
                 monitor->layouts[monitor->selected_layout_index]->symbol);
        for (size_t i = 0; i < LENGTH(monitor->tag_layouts); i++) {
            monitor->tag_layouts[i][0] = monitor->layouts[0];
            monitor->tag_layouts[i][1] = monitor->layouts[1];
        }
        wlr_output_state_set_scale(state, rule->scale);
        wlr_output_state_set_transform(state, rule->transform);
        /* Load (or reuse, if already cached) the xcursor theme at this
         * monitor's scale so the cursor renders crisp and sized
         * relative to cursor_size * scale, not just cursor_size. */
        wlr_xcursor_manager_load(cursor_manager, rule->scale);
        if (rule->width && rule->height) {
            struct wlr_output_mode* candidate_mode;
            int32_t wanted_refresh = rule->refresh_hz * 1000;
            int32_t best_diff = INT32_MAX;
            wl_list_for_each(candidate_mode, &wlr_output->modes, link) {
                int32_t diff = abs(candidate_mode->refresh - wanted_refresh);
                if (candidate_mode->width == rule->width &&
                    candidate_mode->height == rule->height && diff < best_diff) {
                    mode = candidate_mode;
                    best_diff = diff;
                }
            }
            if (!mode)
                wlr_log(WLR_ERROR, "no %dx%d mode found on %s, using preferred mode", rule->width,
                        rule->height, wlr_output->name);
        }
        break;
    }

    /* The mode is a tuple of (width, height, refresh rate), and each
     * monitor supports only a specific set of modes. Use the mode matched
     * against the rule above, falling back to the preferred mode when the
     * rule didn't request one (or it wasn't found). */
    wlr_output_state_set_mode(state, mode ? mode : wlr_output_preferred_mode(wlr_output));
}

/* the monitor_rules again, over monitors that already exist */
void monitor_reload_rules(void)
{
    struct wlr_output_state state;
    Monitor* monitor;

    wl_list_for_each(monitor, &monitors, link) {
        if (!monitor->wlr_output->enabled)
            continue;
        wlr_output_state_init(&state);
        monitor_apply_rules(monitor, &state);
        /* before the commit: a mode change runs monitor_update_layout(), which
         * would overwrite the rule's monitor->m.x/y with the old layout position */
        if (monitor->geometry.x == -1 && monitor->geometry.y == -1)
            wlr_output_layout_add_auto(output_layout, monitor->wlr_output);
        else
            wlr_output_layout_add(output_layout, monitor->wlr_output, monitor->geometry.x,
                                  monitor->geometry.y);

        /* a mode or a scale the output cannot take would blank it */
        if (wlr_output_test_state(monitor->wlr_output, &state))
            wlr_output_commit_state(monitor->wlr_output, &state);
        wlr_output_state_finish(&state);

        /* a scale of 0 is what a scale change looks like to bar_update(), and
         * the bar has to be its new height before anything is arranged */
        monitor->bar_metrics.scale = 0;
        bar_update(monitor);
    }
}

void monitor_handle_new_output(struct wl_listener*, void* data)
{
    /* This event is raised by the backend when a new output (aka a display or
     * monitor) becomes available. */
    struct wlr_output* wlr_output = data;

    if (!wlr_output_init_render(wlr_output, allocator, renderer))
        return;

    Monitor* monitor = wlr_output->data = util_calloc(1, sizeof(*monitor));
    monitor->wlr_output = wlr_output;

    for (size_t i = 0; i < LENGTH(monitor->layers); i++)
        wl_list_init(&monitor->layers[i]);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    monitor->tagsets[0] = monitor->tagsets[1] = 1;
    monitor->zoom_factor = 1;
    monitor_apply_rules(monitor, &state);

    /* Set up event listeners */
    LISTEN(&wlr_output->events.frame, &monitor->frame, monitor_handle_frame);
    LISTEN(&wlr_output->events.destroy, &monitor->destroy, monitor_handle_destroy);
    LISTEN(&wlr_output->events.request_state, &monitor->request_state,
           monitor_handle_request_state);

    wlr_output_state_set_enabled(&state, 1);
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    if (!(monitor->draw_context = drwl_create()))
        util_die("failed to create drwl context");

#ifdef INTEGRATED_BACKGROUND
    /* created before the bar so that it stays underneath it */
    monitor->bar_blur_scene_buffer = wlr_scene_buffer_create(layers[SCENE_LAYER_BOTTOM], nullptr);
    wlr_scene_node_set_enabled(&monitor->bar_blur_scene_buffer->node, 0);
#endif
    monitor->bar_scene_buffer = wlr_scene_buffer_create(layers[SCENE_LAYER_BOTTOM], nullptr);
    monitor->bar_scene_buffer->point_accepts_input = bar_accepts_input;
#ifdef INTEGRATED_BACKGROUND
    monitor->wallpaper_scene_buffer =
        wlr_scene_buffer_create(layers[SCENE_LAYER_BACKGROUND], nullptr);
#endif
    bar_update(monitor);

    wl_list_insert(&monitors, &monitor->link);
    bar_draw_all();

    /* The xdg-protocol specifies:
     *
     * If the fullscreened surface is not opaque, the compositor must make
     * sure that other screen content not part of the same surface tree (made
     * up of subsurfaces, popups or similarly coupled surfaces) are not
     * visible below the fullscreened surface.
     *
     */
    /* monitor_update_layout() will resize and set correct position */
    monitor->fullscreen_background = wlr_scene_rect_create(layers[SCENE_LAYER_FULLSCREEN], 0, 0,
                                                           settings->look.fullscreen_color);
    wlr_scene_node_set_enabled(&monitor->fullscreen_background->node, 0);

    /* Adds this to the output layout in the order it was configured.
     *
     * The output layout utility automatically adds a wl_output global to the
     * display, which Wayland clients can see to find out information about the
     * output (such as DPI, scale factor, manufacturer, etc).
     */
    monitor->scene_output = wlr_scene_output_create(scene, wlr_output);
    if (monitor->geometry.x == -1 && monitor->geometry.y == -1)
        wlr_output_layout_add_auto(output_layout, wlr_output);
    else
        wlr_output_layout_add(output_layout, wlr_output, monitor->geometry.x, monitor->geometry.y);
}

static void monitor_handle_layer_destroy(struct wl_listener* listener, void*)
{
    LayerSurface* layer_surface = wl_container_of(listener, layer_surface, destroy);

    wl_list_remove(&layer_surface->link);
    wl_list_remove(&layer_surface->destroy.link);
    wl_list_remove(&layer_surface->unmap.link);
    wl_list_remove(&layer_surface->surface_commit.link);
    /* Same as in client_handle_unmap(): the wl_surface may outlive the layer
     * surface, don't leave it pointing at the freed popup tree. */
    if (layer_surface->layer_surface->surface)
        layer_surface->layer_surface->surface->data = nullptr;
    wlr_scene_node_destroy(&layer_surface->scene->node);
    wlr_scene_node_destroy(&layer_surface->popup_tree->node);
    free(layer_surface);
}

static Monitor* monitor_in_direction(enum wlr_direction direction)
{
    if (!wlr_output_layout_get(output_layout, selected_monitor->wlr_output))
        return selected_monitor;
    struct wlr_output* next;
    if ((next = wlr_output_layout_adjacent_output(
             output_layout, direction, selected_monitor->wlr_output, selected_monitor->geometry.x,
             selected_monitor->geometry.y)))
        return next->data;
    if ((next = wlr_output_layout_farthest_output(
             output_layout, direction ^ (WLR_DIRECTION_LEFT | WLR_DIRECTION_RIGHT),
             selected_monitor->wlr_output, selected_monitor->geometry.x,
             selected_monitor->geometry.y)))
        return next->data;
    return selected_monitor;
}

void monitor_focus(const ActionArg* argument)
{
    int i = 0, nmons = wl_list_length(&monitors);
    if (nmons) {
        do /* don't switch to disabled monitors */
            selected_monitor = monitor_in_direction(argument->integer);
        while (!selected_monitor->wlr_output->enabled && i++ < nmons);
    }
    client_focus(client_top(selected_monitor), 1);
}

static WindowClient* monitor_fullscreen_client(Monitor* monitor)
{

    if (lock_active)
        return nullptr;
    WindowClient* client;
    wl_list_for_each(client, &focus_stack, focus_link) {
        if (VISIBLEON(client, monitor) && client->is_fullscreen && client_surface(client))
            return client;
    }
    return nullptr;
}

static void monitor_game_mode(Monitor* monitor, struct wlr_output_state* state)
{
    WindowClient* client = monitor_fullscreen_client(monitor);
    struct wlr_output* output = monitor->wlr_output;
    enum wp_content_type_v1_type type = WP_CONTENT_TYPE_V1_TYPE_NONE;

    if (client)
        type = wlr_surface_get_content_type_v1(content_type_manager, client_surface(client));
    int vrr = settings->misc.auto_vrr &&
              (type == WP_CONTENT_TYPE_V1_TYPE_GAME || type == WP_CONTENT_TYPE_V1_TYPE_VIDEO);

    /* never undo a wlr-randr choice */
    if (vrr && !monitor->auto_vrr_state && output->adaptive_sync_supported &&
        output->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_DISABLED) {
        wlr_output_state_set_adaptive_sync_enabled(state, true);
        if (!wlr_output_test_state(output, state)) {
            state->committed &= ~WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED;
            monitor->auto_vrr_state = -1; /* don't retry every frame */
        }
    } else if (!vrr && monitor->auto_vrr_state > 0) {
        wlr_output_state_set_adaptive_sync_enabled(state, false);
    } else if (!vrr) {
        monitor->auto_vrr_state = 0;
    }

    /* needs direct scanout, which the magnifier holds back */
    state->tearing_page_flip =
        settings->misc.allow_tearing && client && monitor->zoom_factor <= 1 &&
        wlr_tearing_control_manager_v1_surface_hint_from_surface(tearing_manager,
                                                                 client_surface(client)) ==
            WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC;
    if (state->tearing_page_flip && !wlr_output_test_state(output, state))
        state->tearing_page_flip = false;
}

void monitor_handle_config_apply(struct wl_listener*, void* data)
{
    struct wlr_output_configuration_v1* config = data;
    monitor_apply_or_test_config(config, 0);
}

static void monitor_apply_or_test_config(struct wlr_output_configuration_v1* config, int test)
{
    /*
     * Called when a client such as wlr-randr requests a change in output
     * configuration. This is only one way that the layout can be changed,
     * so any Monitor information should be updated by monitor_update_layout()
     * after an output_layout.change event, not here.
     */
    struct wlr_output_configuration_head_v1* config_head;
    int ok = 1;

    wl_list_for_each(config_head, &config->heads, link) {
        struct wlr_output* wlr_output = config_head->state.output;
        Monitor* monitor = wlr_output->data;
        struct wlr_output_state state;

        /* Ensure displays previously disabled by wlr-output-power-management-v1
         * are properly handled*/
        monitor->asleep = 0;
        monitor->auto_vrr_state = 0;

        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, config_head->state.enabled);
        if (!config_head->state.enabled)
            goto apply_or_test;

        if (config_head->state.mode)
            wlr_output_state_set_mode(&state, config_head->state.mode);
        else
            wlr_output_state_set_custom_mode(&state, config_head->state.custom_mode.width,
                                             config_head->state.custom_mode.height,
                                             config_head->state.custom_mode.refresh);

        wlr_output_state_set_transform(&state, config_head->state.transform);
        wlr_output_state_set_scale(&state, config_head->state.scale);
        wlr_output_state_set_adaptive_sync_enabled(&state,
                                                   config_head->state.adaptive_sync_enabled);

    apply_or_test:
        ok &= test ? wlr_output_test_state(wlr_output, &state)
                   : wlr_output_commit_state(wlr_output, &state);

        /* Don't move monitors if position wouldn't change. This avoids
         * wlroots marking the output as manually configured.
         * wlr_output_layout_add does not like disabled outputs */
        if (!test && wlr_output->enabled &&
            (monitor->geometry.x != config_head->state.x ||
             monitor->geometry.y != config_head->state.y))
            wlr_output_layout_add(output_layout, wlr_output, config_head->state.x,
                                  config_head->state.y);

        wlr_output_state_finish(&state);
    }

    if (ok)
        wlr_output_configuration_v1_send_succeeded(config);
    else
        wlr_output_configuration_v1_send_failed(config);
    wlr_output_configuration_v1_destroy(config);

    /* https://codeberg.org/dwl/dwl/issues/577 */
    monitor_update_layout(nullptr, nullptr);
}

void monitor_handle_config_test(struct wl_listener*, void* data)
{
    struct wlr_output_configuration_v1* config = data;
    monitor_apply_or_test_config(config, 1);
}

void monitor_handle_power_set_mode(struct wl_listener*, void* data)
{
    struct wlr_output_power_v1_set_mode_event* event = data;
    struct wlr_output_state state = {};
    Monitor* monitor = event->output->data;

    if (!monitor)
        return;

    monitor->gamma_lut_changed = 1; /* Reapply gamma LUT when re-enabling the output */
    wlr_output_state_set_enabled(&state, event->mode);
    if (!wlr_output_commit_state(monitor->wlr_output, &state))
        fprintf(stderr, "g0wm: failed to %s output %s\n", event->mode ? "enable" : "disable",
                monitor->wlr_output->name);

    /* Track what the output actually is rather than what we asked for: a
     * refused commit would otherwise leave us believing a live output is
     * asleep, and nothing would ever schedule a frame for it again. */
    monitor->asleep = !monitor->wlr_output->enabled;
    if (monitor->wlr_output->enabled)
        wlr_output_schedule_frame(monitor->wlr_output);
    monitor_update_layout(nullptr, nullptr);
}

static void monitor_handle_frame(struct wl_listener* listener, void*)
{
    /* This function is called every time an output is ready to display a frame,
     * generally at the output's refresh rate (e.g. 60Hz). */
    Monitor* monitor = wl_container_of(listener, monitor, frame);
    WindowClient* client;
    struct wlr_output_state pending;
    struct timespec now;

    wlr_output_state_init(&pending);

    /* Render if no XDG clients have an outstanding resize and are visible on
     * this monitor. */
    wl_list_for_each(client, &clients, link) {
        if (!client_is_rendered_on_monitor(client, monitor))
            continue;
        /* done here rather than on focus changes so that buffers a client
         * adds later (subsurfaces, videos) are covered too */
        wlr_scene_node_for_each_buffer(&client->scene_surface->node, opacity_set_scene_buffer,
                                       client);
#ifdef INTEGRATED_BACKGROUND
        opacity_blur_client(client);
#endif
        /* a tab below the top one is not drawn and gets no frame callbacks,
         * so it never acks */
        if (client->pending_resize_serial && !client->is_floating &&
            client->scene_surface->node.enabled && !client_is_stopped(client))
            goto skip;
    }

    if (monitor->zoom_factor > 1 || monitor->zoom_swapchain) {
        if (!magnifier_frame(monitor, &pending))
            goto skip;
    } else if (!wlr_scene_output_needs_frame(monitor->scene_output) ||
               !wlr_scene_output_build_state(monitor->scene_output, &pending, nullptr)) {
        goto skip;
    }
    monitor_game_mode(monitor, &pending);

    if (!privacy_before_commit(monitor, &pending))
        goto skip;
    if (wlr_output_commit_state(monitor->wlr_output, &pending) &&
        (pending.committed & WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED))
        monitor->auto_vrr_state = pending.adaptive_sync_enabled;
    privacy_after_commit(monitor);

skip:
    /* Let clients know a frame has been rendered */
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(monitor->scene_output, &now);

    /* a client held back by client_handle_map() still needs frame callbacks */
    wl_list_for_each(client, &clients, link)
        if (client->monitor == monitor && client_surface(client)->mapped &&
            ((client->is_fullscreen && !VISIBLEON(client, monitor)) || client->map_wait_timer))
            wlr_surface_send_frame_done(client_surface(client), &now);

    wlr_output_state_finish(&pending);
}

static void monitor_handle_request_state(struct wl_listener*, void* data)
{
    struct wlr_output_event_request_state* event = data;
    wlr_output_commit_state(event->output, event->state);
    monitor_update_layout(nullptr, nullptr);
}

void monitor_send_client(const ActionArg* argument)
{
    WindowClient* selected_client = client_top(selected_monitor);
    if (selected_client)
        client_set_monitor(selected_client, monitor_in_direction(argument->integer), 0);
}

static void monitor_handle_layer_unmap(struct wl_listener* listener, void*)
{
    LayerSurface* layer_surface = wl_container_of(listener, layer_surface, unmap);

    layer_surface->mapped = 0;
    wlr_scene_node_set_enabled(&layer_surface->scene->node, 0);
    if (layer_surface == exclusive_focus)
        exclusive_focus = nullptr;
    if (layer_surface->layer_surface->output &&
        (layer_surface->monitor = layer_surface->layer_surface->output->data))
        monitor_arrange_layers(layer_surface->monitor);
    if (layer_surface->layer_surface->surface == seat->keyboard_state.focused_surface)
        client_focus(client_top(selected_monitor), 1);
    input_motion(0, nullptr, 0, 0, 0, 0);
}

void monitor_update_layout(struct wl_listener*, void*)
{
    /*
     * Called whenever the output layout changes: adding or removing a
     * monitor, changing an output's mode or position, etc. This is where
     * the change officially happens and we update geometry, window
     * positions, focus, and the stored configuration in wlroots'
     * output-manager implementation.
     */
    struct wlr_output_configuration_v1* config = wlr_output_configuration_v1_create();
    struct wlr_output_configuration_head_v1* config_head;
    Monitor* monitor;

    /* First remove from the layout the disabled monitors */
    wl_list_for_each(monitor, &monitors, link) {
        if (monitor->wlr_output->enabled || monitor->asleep)
            continue;
        config_head = wlr_output_configuration_head_v1_create(config, monitor->wlr_output);
        config_head->state.enabled = 0;
        /* Remove this output from the layout to avoid cursor enter inside it */
        wlr_output_layout_remove(output_layout, monitor->wlr_output);
        monitor_close(monitor);
        monitor->geometry = monitor->usable_area = (struct wlr_box){};
    }
    /* Insert outputs that need to */
    wl_list_for_each(monitor, &monitors, link) {
        if (monitor->wlr_output->enabled &&
            !wlr_output_layout_get(output_layout, monitor->wlr_output))
            wlr_output_layout_add_auto(output_layout, monitor->wlr_output);
    }

    /* Now that we update the output layout we can get its box */
    wlr_output_layout_get_box(output_layout, nullptr, &screen_geometry);

    wlr_scene_node_set_position(&root_background->node, screen_geometry.x, screen_geometry.y);
    wlr_scene_rect_set_size(root_background, screen_geometry.width, screen_geometry.height);

    /* Make sure the clients are hidden when g0wm is lock_active */
    wlr_scene_node_set_position(&locked_background->node, screen_geometry.x, screen_geometry.y);
    wlr_scene_rect_set_size(locked_background, screen_geometry.width, screen_geometry.height);

    WindowClient* client;
    wl_list_for_each(monitor, &monitors, link) {
        if (!monitor->wlr_output->enabled)
            continue;
        config_head = wlr_output_configuration_head_v1_create(config, monitor->wlr_output);

        /* Get the effective monitor geometry to use for surfaces */
        wlr_output_layout_get_box(output_layout, monitor->wlr_output, &monitor->geometry);
        monitor->usable_area = monitor->geometry;
        wlr_scene_output_set_position(monitor->scene_output, monitor->geometry.x,
                                      monitor->geometry.y);
#ifdef INTEGRATED_BACKGROUND
        opacity_set_wallpaper(monitor);
#endif

        wlr_scene_node_set_position(&monitor->fullscreen_background->node, monitor->geometry.x,
                                    monitor->geometry.y);
        wlr_scene_rect_set_size(monitor->fullscreen_background, monitor->geometry.width,
                                monitor->geometry.height);

        if (monitor->lock_surface) {
            struct wlr_scene_tree* scene_tree = monitor->lock_surface->surface->data;
            wlr_scene_node_set_position(&scene_tree->node, monitor->geometry.x,
                                        monitor->geometry.y);
            wlr_session_lock_surface_v1_configure(monitor->lock_surface, monitor->geometry.width,
                                                  monitor->geometry.height);
        }

        /* Calculate the effective monitor geometry to use for clients */
        monitor_arrange_layers(monitor);
        /* Don't move clients to the left output when plugging monitors */
        layout_arrange(monitor);
        /* make sure fullscreen clients have the right size */
        if ((client = client_top(monitor)) && client->is_fullscreen)
            client_resize(client, monitor->geometry, 0);

        /* Try to re-set the gamma LUT when updating monitors,
         * it's only really needed when enabling a disabled output, but meh. */
        monitor->gamma_lut_changed = 1;

        config_head->state.x = monitor->geometry.x;
        config_head->state.y = monitor->geometry.y;

        if (!selected_monitor) {
            selected_monitor = monitor;
        }
    }

    tablet_remap();

    if (selected_monitor && selected_monitor->wlr_output->enabled) {
        wl_list_for_each(client, &clients, link) {
            if (!client->monitor && client_surface(client)->mapped)
                client_set_monitor(client, selected_monitor, client->tags);
        }
        client_focus(client_top(selected_monitor), 1);
        if (selected_monitor->lock_surface) {
            client_notify_enter(selected_monitor->lock_surface->surface,
                                wlr_seat_get_keyboard(seat));
            client_activate_surface(selected_monitor->lock_surface->surface, 1);
        }
    }

    if (bar_status_text[0] == '\0')
        strncpy(bar_status_text, "g0wm-" VERSION, sizeof(bar_status_text));
    wl_list_for_each(monitor, &monitors, link) {
        bar_update(monitor);
        /* this very change can hand a monitor the single bar, and with it
         * the strip of height its clients are kept out of */
        monitor_arrange_layers(monitor);
        bar_draw(monitor);
    }

    /* FIXME: figure out why the cursor image is at 0,0 after turning all
     * the monitors on.
     * Move the cursor image where it used to be. It does not generate a
     * wl_pointer.motion event for the clients, it's only the image what it's
     * at the wrong position after all. */
    wlr_cursor_move(cursor, nullptr, 0, 0);

    wlr_output_manager_v1_set_configuration(output_manager, config);
}

Monitor* monitor_at(double x, double y)
{
    struct wlr_output* output = wlr_output_layout_output_at(output_layout, x, y);
    return output ? output->data : nullptr;
}
