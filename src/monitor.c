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

#include "bar.h"
#include "buffer.h"
#include "client.h"
#include "g0wm.h"
#include "input.h"
#include "layout.h"
#include "lock.h"
#include "magnifier.h"
#include "monitor.h"
#include "opacity.h"
#include "privacy.h"
#include "settings.h"
#include "systray/tray.h"
#include "tablet.h"
#include "util.h"

/* function declarations */
static void monitor_apply_rules(Monitor* m, struct wlr_output_state* state);
static void monitor_arrange_layer(Monitor* m,
                                  struct wl_list* list,
                                  struct wlr_box* usable_area,
                                  int exclusive);
static void monitor_handle_destroy(struct wl_listener* listener, void* data);
static void monitor_close(Monitor* m);
static void monitor_handle_layer_commit(struct wl_listener* listener,
                                        void* data);
static void monitor_handle_layer_destroy(struct wl_listener* listener,
                                         void* data);
static Monitor* monitor_in_direction(enum wlr_direction dir);
static Client* monitor_fullscreen_client(Monitor* m);
static void monitor_game_mode(Monitor* m, struct wlr_output_state* state);
static void monitor_apply_or_test_config(
    struct wlr_output_configuration_v1* config,
    int test);
static void monitor_handle_frame(struct wl_listener* listener, void* data);
static void monitor_handle_request_state(struct wl_listener* listener,
                                         void* data);
static void monitor_handle_layer_unmap(struct wl_listener* listener,
                                       void* data);

/* variables */
/* Map from ZWLR_LAYER_SHELL_* constants to Lyr* enum */
constexpr int monitor_layer_map[] = { LyrBg, LyrBottom, LyrTop, LyrOverlay };
static_assert(LENGTH(monitor_layer_map) ==
              ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY + 1);

/* function implementations */
static void monitor_arrange_layer(Monitor* m,
                                  struct wl_list* list,
                                  struct wlr_box* usable_area,
                                  int exclusive)
{
    LayerSurface* l;
    struct wlr_box full_area = m->m;

    wl_list_for_each(l, list, link)
    {
        struct wlr_layer_surface_v1* layer_surface = l->layer_surface;

        if (!layer_surface->initialized)
            continue;

        if (exclusive != (layer_surface->current.exclusive_zone > 0))
            continue;

        wlr_scene_layer_surface_v1_configure(
            l->scene_layer, &full_area, usable_area);
        wlr_scene_node_set_position(
            &l->popups->node, l->scene->node.x, l->scene->node.y);
    }
}

void monitor_arrange_layers(Monitor* m)
{
    int i;
    struct wlr_box usable_area = m->m;
    LayerSurface* l;
    uint32_t layers_above_shell[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
    };
    if (!m->wlr_output->enabled)
        return;

    if (m->scene_buffer->node.enabled) {
        /* the padding goes with the bar, so tiles keep the same gap */
        usable_area.height -= m->b.real_height + (int)bar_padding;
        usable_area.y += bar_top ? m->b.real_height + (int)bar_padding : 0;
    }

    /* Arrange exclusive surfaces from top->bottom */
    for (i = 3; i >= 0; i--)
        monitor_arrange_layer(m, &m->layers[i], &usable_area, 1);

    if (!wlr_box_equal(&usable_area, &m->w)) {
        m->w = usable_area;
        layout_arrange(m);
    }

    /* Arrange non-exclusive surfaces from top->bottom */
    for (i = 3; i >= 0; i--)
        monitor_arrange_layer(m, &m->layers[i], &usable_area, 0);

    /* Find topmost keyboard interactive layer, if such a layer exists */
    for (i = 0; i < (int)LENGTH(layers_above_shell); i++) {
        wl_list_for_each_reverse(l, &m->layers[layers_above_shell[i]], link)
        {
            if (lock_active ||
                !l->layer_surface->current.keyboard_interactive || !l->mapped)
                continue;
            /* Deactivate the focused client. */
            client_focus(nullptr, 0);
            exclusive_focus = l;
            client_notify_enter(l->layer_surface->surface,
                                wlr_seat_get_keyboard(seat));
            return;
        }
    }
}

static void monitor_handle_destroy(struct wl_listener* listener, void*)
{
    Monitor* m = wl_container_of(listener, m, destroy);
    LayerSurface *l, *tmp;
    size_t i;

    /* m->layers[i] are intentionally not unlinked */
    for (i = 0; i < LENGTH(m->layers); i++) {
        wl_list_for_each_safe(l, tmp, &m->layers[i], link)
            wlr_layer_surface_v1_destroy(l->layer_surface);
    }

    for (i = 0; i < LENGTH(m->pool); i++)
        wlr_buffer_drop(&m->pool[i]->base);
    buffer_pool_drop(m->sharepool, LENGTH(m->sharepool));
#ifdef INTEGRATED_BACKGROUND
    buffer_pool_drop(m->wallpaperpool, LENGTH(m->wallpaperpool));
    buffer_pool_drop(m->blurpool, LENGTH(m->blurpool));
#endif

    if (m->tray)
        tray_destroy(m->tray);

    magnifier_drop(m);

    drwl_setimage(m->drw, nullptr);
    drwl_destroy(m->drw);

    wl_list_remove(&m->destroy.link);
    wl_list_remove(&m->frame.link);
    wl_list_remove(&m->link);
    wl_list_remove(&m->request_state.link);
    if (m->lock_surface)
        lock_handle_surface_destroy(&m->destroy_lock_surface, nullptr);
    m->wlr_output->data = nullptr;
    wlr_output_layout_remove(output_layout, m->wlr_output);
    wlr_scene_output_destroy(m->scene_output);

    monitor_close(m);
    wlr_scene_node_destroy(&m->fullscreen_bg->node);
    wlr_scene_node_destroy(&m->scene_buffer->node);
#ifdef INTEGRATED_BACKGROUND
    wlr_scene_node_destroy(&m->wallpaper->node);
    wlr_scene_node_destroy(&m->barblur->node);
#endif
    free(m);
}

static void monitor_close(Monitor* m)
{
    /* update selected_monitor if needed and
     * move closed monitor's clients to the focused one */
    Client* c;
    int i = 0, nmons = wl_list_length(&monitors);
    if (!nmons) {
        selected_monitor = nullptr;
    } else if (m == selected_monitor) {
        do /* don't switch to disabled monitors */
            selected_monitor =
                wl_container_of(monitors.next, selected_monitor, link);
        while (!selected_monitor->wlr_output->enabled && i++ < nmons);

        if (!selected_monitor->wlr_output->enabled)
            selected_monitor = nullptr;
    }

    wl_list_for_each(c, &clients, link)
    {
        if (c->isfloating && c->geom.x > m->m.width)
            client_resize(c,
                          (struct wlr_box){ .x = c->geom.x - m->w.width,
                                            .y = c->geom.y,
                                            .width = c->geom.width,
                                            .height = c->geom.height },
                          0);
        if (c->mon == m)
            client_set_monitor(c, selected_monitor, c->tags);
    }
    client_focus(client_top(selected_monitor), 1);
    bar_draw_all();
}

static void monitor_handle_layer_commit(struct wl_listener* listener, void*)
{
    LayerSurface* l = wl_container_of(listener, l, surface_commit);
    struct wlr_layer_surface_v1* layer_surface = l->layer_surface;
    struct wlr_scene_tree* scene_layer =
        layers[monitor_layer_map[layer_surface->current.layer]];
    struct wlr_layer_surface_v1_state old_state;

    if (l->layer_surface->initial_commit) {
        client_set_scale(layer_surface->surface, l->mon->wlr_output->scale);

        /* Temporarily set the layer's current state to pending
         * so that we can easily arrange it */
        old_state = l->layer_surface->current;
        l->layer_surface->current = l->layer_surface->pending;
        monitor_arrange_layers(l->mon);
        l->layer_surface->current = old_state;
        return;
    }

    if (layer_surface->current.committed == 0 &&
        l->mapped == layer_surface->surface->mapped)
        return;
    l->mapped = layer_surface->surface->mapped;

    if (scene_layer != l->scene->node.parent) {
        wlr_scene_node_reparent(&l->scene->node, scene_layer);
        wl_list_remove(&l->link);
        wl_list_insert(&l->mon->layers[layer_surface->current.layer], &l->link);
        wlr_scene_node_reparent(
            &l->popups->node,
            (layer_surface->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP
                 ? layers[LyrTop]
                 : scene_layer));
    }

    monitor_arrange_layers(l->mon);
}

void monitor_handle_new_layer_surface(struct wl_listener*, void* data)
{
    struct wlr_layer_surface_v1* layer_surface = data;
    LayerSurface* l;
    struct wlr_surface* surface = layer_surface->surface;
    struct wlr_scene_tree* scene_layer =
        layers[monitor_layer_map[layer_surface->pending.layer]];

    if (!layer_surface->output &&
        !(layer_surface->output =
              selected_monitor ? selected_monitor->wlr_output : nullptr)) {
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }

    l = layer_surface->data = util_calloc(1, sizeof(*l));
    l->type = LayerShell;
    LISTEN(&surface->events.commit,
           &l->surface_commit,
           monitor_handle_layer_commit);
    LISTEN(&surface->events.unmap, &l->unmap, monitor_handle_layer_unmap);
    LISTEN(&layer_surface->events.destroy,
           &l->destroy,
           monitor_handle_layer_destroy);

    l->layer_surface = layer_surface;
    l->mon = layer_surface->output->data;
    l->scene_layer =
        wlr_scene_layer_surface_v1_create(scene_layer, layer_surface);
    l->scene = l->scene_layer->tree;
    l->popups = surface->data = wlr_scene_tree_create(
        layer_surface->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP
            ? layers[LyrTop]
            : scene_layer);
    l->scene->node.data = l->popups->node.data = l;

    wl_list_insert(&l->mon->layers[layer_surface->pending.layer], &l->link);
    wlr_surface_send_enter(surface, layer_surface->output);
}

/* the first matching rule onto the monitor and the state about to be
 * committed; also the path a reload takes */
static void monitor_apply_rules(Monitor* m, struct wlr_output_state* state)
{
    struct wlr_output* wlr_output = m->wlr_output;
    struct wlr_output_mode* mode = nullptr;
    const MonitorRule* r;
    size_t i;

    m->gaps = gaps;
    for (r = monitor_rules; r < monitor_rules + monitor_rule_count; r++) {
        if (r->name && !strstr(wlr_output->name, r->name))
            continue;
        m->m.x = r->x;
        m->m.y = r->y;
        m->mfact = r->mfact;
        m->nmaster = r->nmaster;
        m->lt[0] = r->lt;
        m->lt[1] = &layouts[layout_count > 1 && r->lt != &layouts[1]];
        snprintf(
            m->ltsymbol, LENGTH(m->ltsymbol), "%s", m->lt[m->sellt]->symbol);
        for (i = 0; i < LENGTH(m->taglt); i++) {
            m->taglt[i][0] = m->lt[0];
            m->taglt[i][1] = m->lt[1];
        }
        wlr_output_state_set_scale(state, r->scale);
        wlr_output_state_set_transform(state, r->rr);
        /* Load (or reuse, if already cached) the xcursor theme at this
         * monitor's scale so the cursor renders crisp and sized
         * relative to cursor_size * scale, not just cursor_size. */
        wlr_xcursor_manager_load(cursor_manager, r->scale);
        if (r->width && r->height) {
            struct wlr_output_mode* m2;
            int32_t want = r->refresh * 1000, best_diff = INT32_MAX;
            wl_list_for_each(m2, &wlr_output->modes, link)
            {
                int32_t diff = abs(m2->refresh - want);
                if (m2->width == r->width && m2->height == r->height &&
                    diff < best_diff) {
                    mode = m2;
                    best_diff = diff;
                }
            }
            if (!mode)
                wlr_log(WLR_ERROR,
                        "no %dx%d mode found on %s, using preferred mode",
                        r->width,
                        r->height,
                        wlr_output->name);
        }
        break;
    }

    /* The mode is a tuple of (width, height, refresh rate), and each
     * monitor supports only a specific set of modes. Use the mode matched
     * against the rule above, falling back to the preferred mode when the
     * rule didn't request one (or it wasn't found). */
    wlr_output_state_set_mode(
        state, mode ? mode : wlr_output_preferred_mode(wlr_output));
}

/* the monitor_rules again, over monitors that already exist */
void monitor_reload_rules(void)
{
    struct wlr_output_state state;
    Monitor* m;

    wl_list_for_each(m, &monitors, link)
    {
        if (!m->wlr_output->enabled)
            continue;
        wlr_output_state_init(&state);
        monitor_apply_rules(m, &state);
        /* before the commit: a mode change runs monitor_update_layout(), which
         * would overwrite the rule's m->m.x/y with the old layout position */
        if (m->m.x == -1 && m->m.y == -1)
            wlr_output_layout_add_auto(output_layout, m->wlr_output);
        else
            wlr_output_layout_add(output_layout, m->wlr_output, m->m.x, m->m.y);

        /* a mode or a scale the output cannot take would blank it */
        if (wlr_output_test_state(m->wlr_output, &state))
            wlr_output_commit_state(m->wlr_output, &state);
        wlr_output_state_finish(&state);

        /* a scale of 0 is what a scale change looks like to bar_update(), and
         * the bar has to be its new height before anything is arranged */
        m->b.scale = 0;
        bar_update(m);
    }
}

void monitor_handle_new_output(struct wl_listener*, void* data)
{
    /* This event is raised by the backend when a new output (aka a display or
     * monitor) becomes available. */
    struct wlr_output* wlr_output = data;
    struct wlr_output_state state;
    Monitor* m;
    size_t i;

    if (!wlr_output_init_render(wlr_output, allocator, renderer))
        return;

    m = wlr_output->data = util_calloc(1, sizeof(*m));
    m->wlr_output = wlr_output;

    for (i = 0; i < LENGTH(m->layers); i++)
        wl_list_init(&m->layers[i]);

    wlr_output_state_init(&state);
    m->tagset[0] = m->tagset[1] = 1;
    m->zoom = 1;
    monitor_apply_rules(m, &state);

    /* Set up event listeners */
    LISTEN(&wlr_output->events.frame, &m->frame, monitor_handle_frame);
    LISTEN(&wlr_output->events.destroy, &m->destroy, monitor_handle_destroy);
    LISTEN(&wlr_output->events.request_state,
           &m->request_state,
           monitor_handle_request_state);

    wlr_output_state_set_enabled(&state, 1);
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    if (!(m->drw = drwl_create()))
        util_die("failed to create drwl context");

#ifdef INTEGRATED_BACKGROUND
    /* created before the bar so that it stays underneath it */
    m->barblur = wlr_scene_buffer_create(layers[LyrBottom], nullptr);
    wlr_scene_node_set_enabled(&m->barblur->node, 0);
#endif
    m->scene_buffer = wlr_scene_buffer_create(layers[LyrBottom], nullptr);
    m->scene_buffer->point_accepts_input = bar_accepts_input;
#ifdef INTEGRATED_BACKGROUND
    m->wallpaper = wlr_scene_buffer_create(layers[LyrBg], nullptr);
#endif
    bar_update(m);

    wl_list_insert(&monitors, &m->link);
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
    m->fullscreen_bg =
        wlr_scene_rect_create(layers[LyrFS], 0, 0, fullscreen_color);
    wlr_scene_node_set_enabled(&m->fullscreen_bg->node, 0);

    /* Adds this to the output layout in the order it was configured.
     *
     * The output layout utility automatically adds a wl_output global to the
     * display, which Wayland clients can see to find out information about the
     * output (such as DPI, scale factor, manufacturer, etc).
     */
    m->scene_output = wlr_scene_output_create(scene, wlr_output);
    if (m->m.x == -1 && m->m.y == -1)
        wlr_output_layout_add_auto(output_layout, wlr_output);
    else
        wlr_output_layout_add(output_layout, wlr_output, m->m.x, m->m.y);
}

static void monitor_handle_layer_destroy(struct wl_listener* listener, void*)
{
    LayerSurface* l = wl_container_of(listener, l, destroy);

    wl_list_remove(&l->link);
    wl_list_remove(&l->destroy.link);
    wl_list_remove(&l->unmap.link);
    wl_list_remove(&l->surface_commit.link);
    /* Same as in client_handle_unmap(): the wl_surface may outlive the layer
     * surface, don't leave it pointing at the freed popup tree. */
    if (l->layer_surface->surface)
        l->layer_surface->surface->data = nullptr;
    wlr_scene_node_destroy(&l->scene->node);
    wlr_scene_node_destroy(&l->popups->node);
    free(l);
}

static Monitor* monitor_in_direction(enum wlr_direction dir)
{
    struct wlr_output* next;
    if (!wlr_output_layout_get(output_layout, selected_monitor->wlr_output))
        return selected_monitor;
    if ((next = wlr_output_layout_adjacent_output(output_layout,
                                                  dir,
                                                  selected_monitor->wlr_output,
                                                  selected_monitor->m.x,
                                                  selected_monitor->m.y)))
        return next->data;
    if ((next = wlr_output_layout_farthest_output(
             output_layout,
             dir ^ (WLR_DIRECTION_LEFT | WLR_DIRECTION_RIGHT),
             selected_monitor->wlr_output,
             selected_monitor->m.x,
             selected_monitor->m.y)))
        return next->data;
    return selected_monitor;
}

void monitor_focus(const Arg* arg)
{
    int i = 0, nmons = wl_list_length(&monitors);
    if (nmons) {
        do /* don't switch to disabled monitors */
            selected_monitor = monitor_in_direction(arg->i);
        while (!selected_monitor->wlr_output->enabled && i++ < nmons);
    }
    client_focus(client_top(selected_monitor), 1);
}

static Client* monitor_fullscreen_client(Monitor* m)
{
    Client* c;

    if (lock_active)
        return nullptr;
    wl_list_for_each(c, &focus_stack, flink)
    {
        if (VISIBLEON(c, m) && c->isfullscreen && client_surface(c))
            return c;
    }
    return nullptr;
}

static void monitor_game_mode(Monitor* m, struct wlr_output_state* state)
{
    Client* c = monitor_fullscreen_client(m);
    struct wlr_output* o = m->wlr_output;
    enum wp_content_type_v1_type type = WP_CONTENT_TYPE_V1_TYPE_NONE;
    int vrr;

    if (c)
        type = wlr_surface_get_content_type_v1(content_type_manager,
                                               client_surface(c));
    vrr = auto_vrr && (type == WP_CONTENT_TYPE_V1_TYPE_GAME ||
                       type == WP_CONTENT_TYPE_V1_TYPE_VIDEO);

    /* never undo a wlr-randr choice */
    if (vrr && !m->autovrr && o->adaptive_sync_supported &&
        o->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_DISABLED) {
        wlr_output_state_set_adaptive_sync_enabled(state, true);
        if (!wlr_output_test_state(o, state)) {
            state->committed &= ~WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED;
            m->autovrr = -1; /* don't retry every frame */
        }
    } else if (!vrr && m->autovrr > 0) {
        wlr_output_state_set_adaptive_sync_enabled(state, false);
    } else if (!vrr) {
        m->autovrr = 0;
    }

    /* needs direct scanout, which the magnifier holds back */
    state->tearing_page_flip =
        allow_tearing && c && m->zoom <= 1 &&
        wlr_tearing_control_manager_v1_surface_hint_from_surface(
            tearing_manager, client_surface(c)) ==
            WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC;
    if (state->tearing_page_flip && !wlr_output_test_state(o, state))
        state->tearing_page_flip = false;
}

void monitor_handle_config_apply(struct wl_listener*, void* data)
{
    struct wlr_output_configuration_v1* config = data;
    monitor_apply_or_test_config(config, 0);
}

static void monitor_apply_or_test_config(
    struct wlr_output_configuration_v1* config,
    int test)
{
    /*
     * Called when a client such as wlr-randr requests a change in output
     * configuration. This is only one way that the layout can be changed,
     * so any Monitor information should be updated by monitor_update_layout()
     * after an output_layout.change event, not here.
     */
    struct wlr_output_configuration_head_v1* config_head;
    int ok = 1;

    wl_list_for_each(config_head, &config->heads, link)
    {
        struct wlr_output* wlr_output = config_head->state.output;
        Monitor* m = wlr_output->data;
        struct wlr_output_state state;

        /* Ensure displays previously disabled by wlr-output-power-management-v1
         * are properly handled*/
        m->asleep = 0;
        m->autovrr = 0;

        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, config_head->state.enabled);
        if (!config_head->state.enabled)
            goto apply_or_test;

        if (config_head->state.mode)
            wlr_output_state_set_mode(&state, config_head->state.mode);
        else
            wlr_output_state_set_custom_mode(
                &state,
                config_head->state.custom_mode.width,
                config_head->state.custom_mode.height,
                config_head->state.custom_mode.refresh);

        wlr_output_state_set_transform(&state, config_head->state.transform);
        wlr_output_state_set_scale(&state, config_head->state.scale);
        wlr_output_state_set_adaptive_sync_enabled(
            &state, config_head->state.adaptive_sync_enabled);

    apply_or_test:
        ok &= test ? wlr_output_test_state(wlr_output, &state)
                   : wlr_output_commit_state(wlr_output, &state);

        /* Don't move monitors if position wouldn't change. This avoids
         * wlroots marking the output as manually configured.
         * wlr_output_layout_add does not like disabled outputs */
        if (!test && wlr_output->enabled &&
            (m->m.x != config_head->state.x || m->m.y != config_head->state.y))
            wlr_output_layout_add(output_layout,
                                  wlr_output,
                                  config_head->state.x,
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
    Monitor* m = event->output->data;

    if (!m)
        return;

    m->gamma_lut_changed =
        1; /* Reapply gamma LUT when re-enabling the output */
    wlr_output_state_set_enabled(&state, event->mode);
    if (!wlr_output_commit_state(m->wlr_output, &state))
        fprintf(stderr,
                "g0wm: failed to %s output %s\n",
                event->mode ? "enable" : "disable",
                m->wlr_output->name);

    /* Track what the output actually is rather than what we asked for: a
     * refused commit would otherwise leave us believing a live output is
     * asleep, and nothing would ever schedule a frame for it again. */
    m->asleep = !m->wlr_output->enabled;
    if (m->wlr_output->enabled)
        wlr_output_schedule_frame(m->wlr_output);
    monitor_update_layout(nullptr, nullptr);
}

static void monitor_handle_frame(struct wl_listener* listener, void*)
{
    /* This function is called every time an output is ready to display a frame,
     * generally at the output's refresh rate (e.g. 60Hz). */
    Monitor* m = wl_container_of(listener, m, frame);
    Client* c;
    struct wlr_output_state pending;
    struct timespec now;

    wlr_output_state_init(&pending);

    /* Render if no XDG clients have an outstanding resize and are visible on
     * this monitor. */
    wl_list_for_each(c, &clients, link)
    {
        if (!client_is_rendered_on_monitor(c, m))
            continue;
        /* done here rather than on focus changes so that buffers a client
         * adds later (subsurfaces, videos) are covered too */
        wlr_scene_node_for_each_buffer(
            &c->scene_surface->node, opacity_set_scene_buffer, c);
#ifdef INTEGRATED_BACKGROUND
        opacity_blur_client(c);
#endif
        /* a tab below the top one is not drawn and gets no frame callbacks,
         * so it never acks */
        if (c->resize && !c->isfloating && c->scene_surface->node.enabled &&
            !client_is_stopped(c))
            goto skip;
    }

    if (m->zoom > 1 || m->zoomchain) {
        if (!magnifier_frame(m, &pending))
            goto skip;
    } else if (!wlr_scene_output_needs_frame(m->scene_output) ||
               !wlr_scene_output_build_state(
                   m->scene_output, &pending, nullptr)) {
        goto skip;
    }
    monitor_game_mode(m, &pending);
    if (!privacy_before_commit(m, &pending))
        goto skip;
    if (wlr_output_commit_state(m->wlr_output, &pending) &&
        (pending.committed & WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED))
        m->autovrr = pending.adaptive_sync_enabled;
    privacy_after_commit(m);

skip:
    /* Let clients know a frame has been rendered */
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(m->scene_output, &now);

    /* a client held back by client_handle_map() still needs frame callbacks */
    wl_list_for_each(
        c, &clients, link) if (c->mon == m && client_surface(c)->mapped &&
                               ((c->isfullscreen && !VISIBLEON(c, m)) ||
                                c->mapwait))
        wlr_surface_send_frame_done(client_surface(c), &now);

    wlr_output_state_finish(&pending);
}

static void monitor_handle_request_state(struct wl_listener*, void* data)
{
    struct wlr_output_event_request_state* event = data;
    wlr_output_commit_state(event->output, event->state);
    monitor_update_layout(nullptr, nullptr);
}

void monitor_send_client(const Arg* arg)
{
    Client* sel = client_top(selected_monitor);
    if (sel)
        client_set_monitor(sel, monitor_in_direction(arg->i), 0);
}

static void monitor_handle_layer_unmap(struct wl_listener* listener, void*)
{
    LayerSurface* l = wl_container_of(listener, l, unmap);

    l->mapped = 0;
    wlr_scene_node_set_enabled(&l->scene->node, 0);
    if (l == exclusive_focus)
        exclusive_focus = nullptr;
    if (l->layer_surface->output && (l->mon = l->layer_surface->output->data))
        monitor_arrange_layers(l->mon);
    if (l->layer_surface->surface == seat->keyboard_state.focused_surface)
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
    struct wlr_output_configuration_v1* config =
        wlr_output_configuration_v1_create();
    Client* c;
    struct wlr_output_configuration_head_v1* config_head;
    Monitor* m;

    /* First remove from the layout the disabled monitors */
    wl_list_for_each(m, &monitors, link)
    {
        if (m->wlr_output->enabled || m->asleep)
            continue;
        config_head =
            wlr_output_configuration_head_v1_create(config, m->wlr_output);
        config_head->state.enabled = 0;
        /* Remove this output from the layout to avoid cursor enter inside it */
        wlr_output_layout_remove(output_layout, m->wlr_output);
        monitor_close(m);
        m->m = m->w = (struct wlr_box){};
    }
    /* Insert outputs that need to */
    wl_list_for_each(m, &monitors, link)
    {
        if (m->wlr_output->enabled &&
            !wlr_output_layout_get(output_layout, m->wlr_output))
            wlr_output_layout_add_auto(output_layout, m->wlr_output);
    }

    /* Now that we update the output layout we can get its box */
    wlr_output_layout_get_box(output_layout, nullptr, &screen_geometry);

    wlr_scene_node_set_position(
        &root_background->node, screen_geometry.x, screen_geometry.y);
    wlr_scene_rect_set_size(
        root_background, screen_geometry.width, screen_geometry.height);

    /* Make sure the clients are hidden when g0wm is lock_active */
    wlr_scene_node_set_position(
        &locked_background->node, screen_geometry.x, screen_geometry.y);
    wlr_scene_rect_set_size(
        locked_background, screen_geometry.width, screen_geometry.height);

    wl_list_for_each(m, &monitors, link)
    {
        if (!m->wlr_output->enabled)
            continue;
        config_head =
            wlr_output_configuration_head_v1_create(config, m->wlr_output);

        /* Get the effective monitor geometry to use for surfaces */
        wlr_output_layout_get_box(output_layout, m->wlr_output, &m->m);
        m->w = m->m;
        wlr_scene_output_set_position(m->scene_output, m->m.x, m->m.y);
#ifdef INTEGRATED_BACKGROUND
        opacity_set_wallpaper(m);
#endif

        wlr_scene_node_set_position(&m->fullscreen_bg->node, m->m.x, m->m.y);
        wlr_scene_rect_set_size(m->fullscreen_bg, m->m.width, m->m.height);

        if (m->lock_surface) {
            struct wlr_scene_tree* scene_tree = m->lock_surface->surface->data;
            wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
            wlr_session_lock_surface_v1_configure(
                m->lock_surface, m->m.width, m->m.height);
        }

        /* Calculate the effective monitor geometry to use for clients */
        monitor_arrange_layers(m);
        /* Don't move clients to the left output when plugging monitors */
        layout_arrange(m);
        /* make sure fullscreen clients have the right size */
        if ((c = client_top(m)) && c->isfullscreen)
            client_resize(c, m->m, 0);

        /* Try to re-set the gamma LUT when updating monitors,
         * it's only really needed when enabling a disabled output, but meh. */
        m->gamma_lut_changed = 1;

        config_head->state.x = m->m.x;
        config_head->state.y = m->m.y;

        if (!selected_monitor) {
            selected_monitor = m;
        }
    }

    tablet_remap();

    if (selected_monitor && selected_monitor->wlr_output->enabled) {
        wl_list_for_each(c, &clients, link)
        {
            if (!c->mon && client_surface(c)->mapped)
                client_set_monitor(c, selected_monitor, c->tags);
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
    wl_list_for_each(m, &monitors, link)
    {
        bar_update(m);
        /* this very change can hand a monitor the single bar, and with it
         * the strip of height its clients are kept out of */
        monitor_arrange_layers(m);
        bar_draw(m);
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
    struct wlr_output* o = wlr_output_layout_output_at(output_layout, x, y);
    return o ? o->data : nullptr;
}
