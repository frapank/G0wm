/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_MONITOR_H
#define G0WM_MONITOR_H

#include <drwl.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/util/box.h>

#include "core/g0wm.h"
#include "desktop/systray/tray.h"
#include "render/buffer.h"
#include "window/layout.h"

struct LayerSurface {
    /* Must keep this field first */
    unsigned int type; /* SURFACE_TYPE_LAYER_SHELL */

    Monitor* monitor;
    struct wlr_scene_tree* scene;
    struct wlr_scene_tree* popup_tree;
    struct wlr_scene_layer_surface_v1* scene_layer;
    struct wl_list link;
    int mapped;
    struct wlr_layer_surface_v1* layer_surface;

    struct wl_listener destroy;
    struct wl_listener unmap;
    struct wl_listener surface_commit;
};

struct Monitor {
    struct wl_list link;
    struct wlr_output* wlr_output;
    struct wlr_scene_output* scene_output;
    struct wlr_scene_buffer* bar_scene_buffer; /* bar buffer */
#ifdef INTEGRATED_BACKGROUND
    struct wlr_scene_buffer* wallpaper_scene_buffer; /* wallpaper buffer */
    struct wlr_buffer* wallpaper_buffer;            /* buffer last attached to the wallpaper node */
    struct wlr_scene_buffer* bar_blur_scene_buffer; /* the bar's own frosted backdrop */
#endif
    struct wlr_scene_rect* fullscreen_background; /* See monitor_handle_new_output() for info */
    struct wl_listener frame;
    struct wl_listener destroy;
    struct wl_listener request_state;
    struct wl_listener destroy_lock_surface;
    struct wlr_session_lock_surface_v1* lock_surface;
    struct wlr_box geometry; /* monitor area, layout-relative */
    struct BarMetrics {
        int width_px, height_px;
        int logical_width, logical_height; /* compositor logical coordinates */
        int content_width_px;              /* free box shared by the window title and the
                                            * notification, 0 when it doesn't fit */
        float scale;
    } bar_metrics; /* bar area */
    struct TitlebarMetrics {
        int height_px;
        int logical_height; /* compositor logical coordinates */
    } titlebar_metrics;     /* per-client title bar */
    SystemTray* tray;
    struct wlr_box usable_area; /* window area, layout-relative */
    struct wl_list layers[4];   /* LayerSurface.link */
    const WindowLayout* layouts[2];
    const WindowLayout* tag_layouts[32][2];
    unsigned int tag_layout_indices[32];
    int gaps_enabled;
    unsigned int selected_tagset_index;
    unsigned int selected_layout_index;
    uint32_t tagsets[2];
    float master_factor;
    int gamma_lut_changed;
    int master_count;
    char layout_symbol[16];
    int asleep;
    int auto_vrr_state;                   /* 1 turned on by monitor_game_mode(), -1 refused */
    float zoom_factor;                    /* magnifier level, 1 is off */
    struct wlr_swapchain* zoom_swapchain; /* the frames, rendered unzoomed */
    struct wlr_buffer* zoom_buffer;       /* the latest of them */
    struct wlr_texture* zoom_texture;     /* texture backed by zoom_buffer */
    struct wlr_fbox zoom_source_box;      /* the part of it on show */
    struct wlr_buffer* capture_buffer;
    Drwl* draw_context;
    RenderBuffer* bar_buffer_pool[2];
    RenderBuffer* capture_buffer_pool[1];
    int capture_x, capture_width;
#ifdef INTEGRATED_BACKGROUND
    RenderBuffer* wallpaper_buffer_pool[1];
    RenderBuffer* blur_buffer_pool[1];           /* blurred wallpaper buffers */
    int blur_width_px, blur_height_px;           /* cached blur dimensions */
    int wallpaper_width_px, wallpaper_height_px; /* cached wallpaper dimensions */
#endif
    int horizontal_padding;
};

void monitor_arrange_layers(Monitor* monitor);
void monitor_handle_new_layer_surface(struct wl_listener*, void* data);
void monitor_handle_new_output(struct wl_listener*, void* data);
void monitor_focus(const ActionArg* argument);
void monitor_handle_config_apply(struct wl_listener*, void* data);
void monitor_handle_config_test(struct wl_listener*, void* data);
void monitor_handle_power_set_mode(struct wl_listener*, void* data);
void monitor_reload_rules(void);
void monitor_send_client(const ActionArg* argument);
void monitor_update_layout(struct wl_listener*, void*);
Monitor* monitor_at(double x, double y);

#endif
