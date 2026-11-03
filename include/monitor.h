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

#include "buffer.h"
#include "g0wm.h"
#include "layout.h"
#include "systray/tray.h"

struct LayerSurface {
    /* Must keep this field first */
    unsigned int type; /* LayerShell */

    Monitor* mon;
    struct wlr_scene_tree* scene;
    struct wlr_scene_tree* popups;
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
    struct wlr_scene_buffer* scene_buffer; /* bar buffer */
#ifdef INTEGRATED_BACKGROUND
    struct wlr_scene_buffer* wallpaper; /* wallpaper buffer */
    struct wlr_buffer* wallpaperbuf; /* what wallpaper's node was last handed */
    struct wlr_scene_buffer* barblur; /* the bar's own frosted backdrop */
#endif
    struct wlr_scene_rect* fullscreen_bg; /* See createmon() for info */
    struct wl_listener frame;
    struct wl_listener destroy;
    struct wl_listener request_state;
    struct wl_listener destroy_lock_surface;
    struct wlr_session_lock_surface_v1* lock_surface;
    struct wlr_box m; /* monitor area, layout-relative */
    struct {
        int width, height;
        int real_width, real_height; /* non-scaled */
        int titlew; /* free box shared by the window title and the
                     * notification, 0 when it doesn't fit */
        float scale;
    } b; /* bar area */
    struct {
        int height;
        int real_height; /* non-scaled */
    } t;                 /* per-client title bar */
    Tray* tray;
    struct wlr_box w;         /* window area, layout-relative */
    struct wl_list layers[4]; /* LayerSurface.link */
    const Layout* lt[2];
    const Layout* taglt[32][2];
    unsigned int tagsellt[32];
    int gaps;
    unsigned int seltags;
    unsigned int sellt;
    uint32_t tagset[2];
    float mfact;
    int gamma_lut_changed;
    int nmaster;
    char ltsymbol[16];
    int asleep;
    int autovrr;                     /* 1 turned on by gamemode(), -1 refused */
    float zoom;                      /* magnifier level, 1 is off */
    struct wlr_swapchain* zoomchain; /* the frames, rendered unzoomed */
    struct wlr_buffer* zoombuf;      /* the latest of them */
    struct wlr_texture* zoomtex;     /* zoombuf, to draw from */
    struct wlr_fbox zoomsrc;         /* the part of it on show */
    struct wlr_buffer* shown;
    Drwl* drw;
    Buffer* pool[2];
    Buffer* sharepool[1];
    int sharex, sharew;
#ifdef INTEGRATED_BACKGROUND
    Buffer* wallpaperpool[1];
    Buffer* blurpool[1];        /* wallpaperpool, blurred */
    int blurw, blurh;           /* size blurpool was rendered at */
    int wallpaperw, wallpaperh; /* size wallpaperpool was rendered at */
#endif
    int lrpad;
};

void arrangelayers(Monitor* m);
void createlayersurface(struct wl_listener* listener, void* data);
void createmon(struct wl_listener* listener, void* data);
void focusmon(const Arg* arg);
void outputmgrapply(struct wl_listener* listener, void* data);
void outputmgrtest(struct wl_listener* listener, void* data);
void powermgrsetmode(struct wl_listener* listener, void* data);
void reloadmons(void);
void tagmon(const Arg* arg);
void updatemons(struct wl_listener* listener, void* data);
Monitor* xytomon(double x, double y);

#endif
