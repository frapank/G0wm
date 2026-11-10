/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_CLIENT_H
#define G0WM_CLIENT_H

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <sys/wait.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>
#ifdef XWAYLAND
#include <wlr/xwayland.h>
#include <xcb/xcb_icccm.h>
#endif

#include "core/g0wm.h"
#include "output/monitor.h"
#include "render/buffer.h"
#include "xdg-shell-protocol.h"

#define VISIBLEON(C, M)                                                                            \
    ((M) && (C)->monitor == (M) && ((C)->tags & (M)->tagsets[(M)->selected_tagset_index]))

enum SurfaceType {
    SURFACE_TYPE_XDG_SHELL,
    SURFACE_TYPE_LAYER_SHELL,
    SURFACE_TYPE_X11
}; /* client types */

struct WindowClient {
    /* Must keep this field first */
    unsigned int type; /* SURFACE_TYPE_XDG_SHELL or SURFACE_TYPE_X11 */

    Monitor* monitor;
    struct wlr_scene_tree* scene;
    struct wlr_scene_rect* border_rects[4];     /* top, bottom, left, right */
    struct wlr_scene_buffer* corner_buffers[4]; /* tl, tr, bl, br */
    struct wlr_scene_tree* scene_surface;
    struct wl_list link;
    struct wl_list focus_link;
    struct wlr_box geometry;         /* layout-relative, includes border */
    struct wlr_box restore_geometry; /* layout-relative, includes border */
    struct wlr_box bounds;           /* only width and height are used */
    union ClientSurface {
        struct wlr_xdg_surface* xdg;
        struct wlr_xwayland_surface* xwayland;
    } surface;
    struct wlr_xdg_toplevel_decoration_v1* decoration;
    struct wl_listener commit;
    struct wl_listener map;
    struct wl_listener maximize;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener set_title;
    struct wl_listener fullscreen;
    struct wl_listener set_decoration_mode;
    struct wl_listener destroy_decoration;
#ifdef XWAYLAND
    struct wl_listener activate;
    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener configure;
    struct wl_listener set_hints;
    struct wl_listener minimize;
#endif
    struct wlr_scene_buffer* title_scene_buffer;
#ifdef INTEGRATED_BACKGROUND
    struct wlr_scene_buffer* blur_scene_buffer;       /* frosted wallpaper, below the whole box */
    struct wlr_buffer* blur_buffer;                   /* what blur's node was last handed */
    struct wlr_scene_buffer* title_blur_scene_buffer; /* the same, for the title bar */
    struct wlr_buffer* title_blur_buffer;
#endif
    RenderBuffer* title_buffer_pool[2];
    RenderBuffer* capture_buffer_pool[1];
    RenderBuffer* privacy_buffer_pool[1];
    int title_x, title_width;  /* title bar placement, relative to the border box */
    int title_buffer_width_px; /* allocated title buffer width */
    RenderBuffer* corner_buffer_pool[4][2];
    int cached_corner_radius, cached_corner_border_width; /* cached corner drawing parameters */
    uint32_t cached_corner_color;                         /* and the colour it was drawn with */
    unsigned int border_width;
    uint32_t tags;
    int is_floating, is_urgent, is_fullscreen, is_private;
    const char* private_text;
    uint32_t private_background, private_foreground;
    float opacity;                          /* the one in effect, focused or not */
    float focused_opacity;                  /* used while the client holds focus */
    float unfocused_opacity;                /* used while it does not */
    int border_scheme;                      /* scheme its border is drawn in, to redo it */
    uint32_t pending_resize_serial;         /* configure serial of a pending resize */
    struct wl_event_source* map_wait_timer; /* set while a new client is held back */
    struct wlr_foreign_toplevel_handle_v1* foreign_toplevel;
    struct wlr_ext_foreign_toplevel_handle_v1* ext_foreign_toplevel;
    struct wlr_ext_image_capture_source_v1* capture_source;
    struct wl_listener foreign_toplevel_activate;
    struct wl_listener foreign_toplevel_close;
    struct wl_listener foreign_toplevel_fullscreen;
};

void client_activate(WindowClient* client);
void client_handle_new_decoration(struct wl_listener*, void* data);
void client_handle_new_toplevel(struct wl_listener*, void* data);
void client_handle_new_popup(struct wl_listener*, void* data);
void client_handle_destroy(struct wl_listener* listener, void*);
void client_focus(WindowClient* client, int lift);
WindowClient* client_top(Monitor* monitor);
void client_handle_request_fullscreen(struct wl_listener* listener, void*);
void client_kill(const ActionArg*);
void client_handle_map(struct wl_listener* listener, void*);
void client_resize(WindowClient* client, struct wlr_box geometry, int interact);
void client_handle_ring_bell(struct wl_listener*, void* data);
void client_set_floating(WindowClient* client, int floating);
void client_set_fullscreen(WindowClient* client, int fullscreen);
void client_set_monitor(WindowClient* client, Monitor* monitor, uint32_t newtags);
void client_toggle_floating(const ActionArg*);
void client_toggle_fullscreen(const ActionArg*);
void client_handle_unmap(struct wl_listener* listener, void*);
void client_handle_set_title(struct wl_listener* listener, void*);
void client_handle_request_activate(struct wl_listener*, void* data);

/*
 * Attempt to consolidate unavoidable suck into one file, away from g0wm.c. This
 * file is not meant to be pretty.  We use a .h file with static inline
 * functions instead of a separate .c module, or function pointers like sway, so
 * that they will simply compile out if the chosen #defines leave them unused.
 */

/* Leave these functions first; they're used in the others */
static inline int client_is_x11([[maybe_unused]] WindowClient* client)
{
#ifdef XWAYLAND
    return client->type == SURFACE_TYPE_X11;
#endif
    return 0;
}

static inline struct wlr_surface* client_surface(WindowClient* client)
{
#ifdef XWAYLAND
    if (client_is_x11(client))
        return client->surface.xwayland->surface;
#endif
    return client->surface.xdg->surface;
}

static inline int client_from_surface(struct wlr_surface* surface,
                                      WindowClient** out_client,
                                      LayerSurface** out_layer_surface)
{
    struct wlr_xdg_surface *xdg_surface, *tmp_xdg_surface;
    struct wlr_surface* root_surface;
    struct wlr_layer_surface_v1* layer_surface;
    WindowClient* client = nullptr;
    LayerSurface* layer_surface_state = nullptr;
    int type = -1;
#ifdef XWAYLAND
    struct wlr_xwayland_surface* xsurface;
#endif

    if (!surface)
        return -1;
    root_surface = wlr_surface_get_root_surface(surface);

#ifdef XWAYLAND
    if ((xsurface = wlr_xwayland_surface_try_from_wlr_surface(root_surface))) {
        client = xsurface->data;
        type = client->type;
        goto end;
    }
#endif

    if ((layer_surface = wlr_layer_surface_v1_try_from_wlr_surface(root_surface))) {
        layer_surface_state = layer_surface->data;
        type = SURFACE_TYPE_LAYER_SHELL;
        goto end;
    }

    xdg_surface = wlr_xdg_surface_try_from_wlr_surface(root_surface);
    while (xdg_surface) {
        tmp_xdg_surface = nullptr;
        switch (xdg_surface->role) {
            case WLR_XDG_SURFACE_ROLE_POPUP:
                if (!xdg_surface->popup || !xdg_surface->popup->parent)
                    return -1;

                tmp_xdg_surface = wlr_xdg_surface_try_from_wlr_surface(xdg_surface->popup->parent);

                if (!tmp_xdg_surface)
                    return client_from_surface(xdg_surface->popup->parent, out_client,
                                               out_layer_surface);

                xdg_surface = tmp_xdg_surface;
                break;
            case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
                client = xdg_surface->data;
                type = client->type;
                goto end;
            case WLR_XDG_SURFACE_ROLE_NONE:
                return -1;
        }
    }

end:
    if (out_layer_surface)
        *out_layer_surface = layer_surface_state;
    if (out_client)
        *out_client = client;
    return type;
}

/* The others */
static inline void client_activate_surface(struct wlr_surface* surface, int activated)
{
    struct wlr_xdg_toplevel* toplevel;
#ifdef XWAYLAND
    struct wlr_xwayland_surface* xsurface;
    if ((xsurface = wlr_xwayland_surface_try_from_wlr_surface(surface))) {
        /* a game that minimized itself on losing focus (Wine) stays black
         * and paused until told it is back */
        if (activated && xsurface->minimized)
            wlr_xwayland_surface_set_minimized(xsurface, false);
        wlr_xwayland_surface_activate(xsurface, activated);
        return;
    }
#endif
    if ((toplevel = wlr_xdg_toplevel_try_from_wlr_surface(surface)))
        wlr_xdg_toplevel_set_activated(toplevel, activated);
}

static inline uint32_t client_set_bounds(WindowClient* client, int32_t width, int32_t height)
{
#ifdef XWAYLAND
    if (client_is_x11(client))
        return 0;
#endif
    if (wl_resource_get_version(client->surface.xdg->toplevel->resource) >=
            XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION &&
        width >= 0 && height >= 0 &&
        (client->bounds.width != width || client->bounds.height != height)) {
        client->bounds.width = width;
        client->bounds.height = height;
        return wlr_xdg_toplevel_set_bounds(client->surface.xdg->toplevel, width, height);
    }
    return 0;
}

static inline const char* client_get_app_id(WindowClient* client)
{
#ifdef XWAYLAND
    if (client_is_x11(client))
        return client->surface.xwayland->class ? client->surface.xwayland->class : "broken";
#endif
    return client->surface.xdg->toplevel->app_id ? client->surface.xdg->toplevel->app_id : "broken";
}

static inline void client_get_clip(WindowClient* client, struct wlr_box* clip)
{
    *clip = (struct wlr_box){
        .x = 0,
        .y = 0,
        .width = client->geometry.width - client->border_width,
        .height = client->geometry.height - client->border_width,
    };

#ifdef XWAYLAND
    if (client_is_x11(client))
        return;
#endif

    clip->x = client->surface.xdg->geometry.x;
    clip->y = client->surface.xdg->geometry.y;
}

static inline void client_get_geometry(WindowClient* client, struct wlr_box* geometry)
{
#ifdef XWAYLAND
    if (client_is_x11(client)) {
        geometry->x = client->surface.xwayland->x;
        geometry->y = client->surface.xwayland->y;
        geometry->width = client->surface.xwayland->width;
        geometry->height = client->surface.xwayland->height;
        return;
    }
#endif
    *geometry = client->surface.xdg->geometry;
}

static inline WindowClient* client_get_parent(WindowClient* client)
{
    WindowClient* parent = nullptr;
#ifdef XWAYLAND
    if (client_is_x11(client)) {
        if (client->surface.xwayland->parent)
            client_from_surface(client->surface.xwayland->parent->surface, &parent, nullptr);
        return parent;
    }
#endif
    if (client->surface.xdg->toplevel->parent)
        client_from_surface(client->surface.xdg->toplevel->parent->base->surface, &parent, nullptr);
    return parent;
}

static inline int client_has_children(WindowClient* client)
{
#ifdef XWAYLAND
    if (client_is_x11(client))
        return !wl_list_empty(&client->surface.xwayland->children);
#endif
    /* surface.xdg->link is never empty because it always contains at least the
     * surface itself. */
    return wl_list_length(&client->surface.xdg->link) > 1;
}

static inline const char* client_get_title(WindowClient* client)
{
#ifdef XWAYLAND
    if (client_is_x11(client))
        return client->surface.xwayland->title ? client->surface.xwayland->title : "broken";
#endif
    return client->surface.xdg->toplevel->title ? client->surface.xdg->toplevel->title : "broken";
}

static inline int client_is_float_type(WindowClient* client)
{
    struct wlr_xdg_toplevel* toplevel;
    struct wlr_xdg_toplevel_state state;

#ifdef XWAYLAND
    if (client_is_x11(client)) {
        struct wlr_xwayland_surface* surface = client->surface.xwayland;
        xcb_size_hints_t* size_hints = surface->size_hints;
        if (surface->modal)
            return 1;

        if (wlr_xwayland_surface_has_window_type(surface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DIALOG) ||
            wlr_xwayland_surface_has_window_type(surface, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_SPLASH) ||
            wlr_xwayland_surface_has_window_type(surface,
                                                 WLR_XWAYLAND_NET_WM_WINDOW_TYPE_TOOLBAR) ||
            wlr_xwayland_surface_has_window_type(surface,
                                                 WLR_XWAYLAND_NET_WM_WINDOW_TYPE_UTILITY)) {
            return 1;
        }

        return size_hints && size_hints->min_width > 0 && size_hints->min_height > 0 &&
               (size_hints->max_width == size_hints->min_width ||
                size_hints->max_height == size_hints->min_height);
    }
#endif

    toplevel = client->surface.xdg->toplevel;
    state = toplevel->current;
    return toplevel->parent ||
           (state.min_width != 0 && state.min_height != 0 &&
            (state.min_width == state.max_width || state.min_height == state.max_height));
}

static inline int client_is_rendered_on_monitor(WindowClient* client, Monitor* monitor)
{
    /* This is needed for when you don't want to check formal assignment,
     * but rather actual displaying of the pixels.
     * Usually VISIBLEON suffices and is also faster. */
    struct wlr_surface_output* surface_output;
    int unused_lx, unused_ly;
    if (!wlr_scene_node_coords(&client->scene->node, &unused_lx, &unused_ly))
        return 0;
    wl_list_for_each(surface_output, &client_surface(client)->current_outputs, link)
        if (surface_output->output == monitor->wlr_output)
            return 1;
    return 0;
}

static inline int client_is_stopped(WindowClient* client)
{
    int pid;
    siginfo_t in = {};
#ifdef XWAYLAND
    if (client_is_x11(client))
        return 0;
#endif

    wl_client_get_credentials(client->surface.xdg->client->client, &pid, nullptr, nullptr);
    if (waitid(P_PID, pid, &in, WNOHANG | WCONTINUED | WSTOPPED | WNOWAIT) < 0) {
        /* This process is not our child process, while is very unlikely that
         * it is stopped, in order to do not skip frames, assume that it is. */
        if (errno == ECHILD)
            return 1;
    } else if (in.si_pid) {
        if (in.si_code == CLD_STOPPED || in.si_code == CLD_TRAPPED)
            return 1;
        if (in.si_code == CLD_CONTINUED)
            return 0;
    }

    return 0;
}

static inline int client_is_unmanaged([[maybe_unused]] WindowClient* client)
{
#ifdef XWAYLAND
    if (client_is_x11(client))
        return client->surface.xwayland->override_redirect;
#endif
    return 0;
}

static inline void client_notify_enter(struct wlr_surface* surface, struct wlr_keyboard* keyboard)
{
    if (keyboard)
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    else
        wlr_seat_keyboard_notify_enter(seat, surface, nullptr, 0, nullptr);
}

static inline void client_send_close(WindowClient* client)
{
#ifdef XWAYLAND
    if (client_is_x11(client)) {
        wlr_xwayland_surface_close(client->surface.xwayland);
        return;
    }
#endif
    wlr_xdg_toplevel_send_close(client->surface.xdg->toplevel);
}

static inline void client_set_border_color(WindowClient* client, const float color[static 4])
{
    for (int i = 0; i < 4; i++)
        wlr_scene_rect_set_color(client->border_rects[i], color);
}

static inline void client_send_fullscreen(WindowClient* client, int fullscreen)
{
#ifdef XWAYLAND
    if (client_is_x11(client)) {
        wlr_xwayland_surface_set_fullscreen(client->surface.xwayland, fullscreen);
        return;
    }
#endif
    wlr_xdg_toplevel_set_fullscreen(client->surface.xdg->toplevel, fullscreen);
}

static inline void client_set_scale(struct wlr_surface* surface, float scale)
{
    wlr_fractional_scale_v1_notify_scale(surface, scale);
    wlr_surface_set_preferred_buffer_scale(surface, (int32_t)ceilf(scale));
}

static inline uint32_t client_set_size(WindowClient* client, uint32_t width, uint32_t height)
{
#ifdef XWAYLAND
    if (client_is_x11(client)) {
        wlr_xwayland_surface_configure(client->surface.xwayland,
                                       client->geometry.x + client->border_width,
                                       client->geometry.y + client->border_width, width, height);
        return 0;
    }
#endif
    if ((int32_t)width == client->surface.xdg->toplevel->current.width &&
        (int32_t)height == client->surface.xdg->toplevel->current.height)
        return 0;
    return wlr_xdg_toplevel_set_size(client->surface.xdg->toplevel, (int32_t)width,
                                     (int32_t)height);
}

static inline void client_set_tiled(WindowClient* client, uint32_t edges)
{
#ifdef XWAYLAND
    if (client_is_x11(client)) {
        wlr_xwayland_surface_set_maximized(client->surface.xwayland, edges != WLR_EDGE_NONE,
                                           edges != WLR_EDGE_NONE);
        return;
    }
#endif
    if (wl_resource_get_version(client->surface.xdg->toplevel->resource) >=
        XDG_TOPLEVEL_STATE_TILED_RIGHT_SINCE_VERSION) {
        wlr_xdg_toplevel_set_tiled(client->surface.xdg->toplevel, edges);
    } else {
        wlr_xdg_toplevel_set_maximized(client->surface.xdg->toplevel, edges != WLR_EDGE_NONE);
    }
}

static inline void client_set_suspended(WindowClient* client, int suspended)
{
#ifdef XWAYLAND
    if (client_is_x11(client))
        return;
#endif

    wlr_xdg_toplevel_set_suspended(client->surface.xdg->toplevel, suspended);
}

static inline int client_wants_focus([[maybe_unused]] WindowClient* client)
{
#ifdef XWAYLAND
    return client_is_unmanaged(client) &&
           wlr_xwayland_surface_override_redirect_wants_focus(client->surface.xwayland) &&
           wlr_xwayland_surface_icccm_input_model(client->surface.xwayland) !=
               WLR_ICCCM_INPUT_MODEL_NONE;
#endif
    return 0;
}

static inline int client_wants_fullscreen(WindowClient* client)
{
#ifdef XWAYLAND
    if (client_is_x11(client))
        return client->surface.xwayland->fullscreen;
#endif
    return client->surface.xdg->toplevel->requested.fullscreen;
}

#endif
