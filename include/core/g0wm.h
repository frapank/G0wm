/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_H
#define G0WM_H

#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_content_type_v1.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/util/box.h>
#ifdef INTEGRATED_BACKGROUND
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif
#ifdef XWAYLAND
#include <wlr/xwayland.h>
#endif

#include "core/util.h"
#include "desktop/systray/watcher.h"

#ifndef MAX
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#endif
#ifndef MIN
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#endif
#define LENGTH(X) (sizeof X / sizeof X[0])
#define LISTEN(E, L, H) wl_signal_add((E), ((L)->notify = (H), (L)))
#define LISTEN_STATIC(E, H)                                                                        \
    do {                                                                                           \
        struct wl_listener* _l = util_calloc(1, sizeof(*_l));                                      \
        _l->notify = (H);                                                                          \
        wl_signal_add((E), _l);                                                                    \
    } while (0)

enum {
    LyrBg,
    LyrBottom,
    LyrTile,
    LyrFloat,
    LyrTop,
    LyrFS,
    LyrOverlay,
    LyrBlock,
    NUM_LAYERS
}; /* scene layers */

typedef union {
    int i;
    uint32_t ui;
    float f;
    const void* v;
} Arg;

typedef struct Client Client;
typedef struct KeyboardGroup KeyboardGroup;
typedef struct LayerSurface LayerSurface;
typedef struct Monitor Monitor;

void check_idle_inhibitor(struct wlr_surface* exclude);
void change_virtual_console(const Arg* arg);
void quit(const Arg*);
void spawn(const Arg* arg);

extern void* exclusive_focus;
extern struct wl_event_loop* event_loop;
extern struct wlr_scene* scene;
extern struct wlr_scene_tree* layers[NUM_LAYERS];
extern struct wlr_scene_tree* drag_icon;
extern struct wlr_renderer* renderer;
extern struct wlr_allocator* allocator;
extern struct wlr_content_type_manager_v1* content_type_manager;
extern struct wlr_tearing_control_manager_v1* tearing_manager;
extern struct wl_list clients;     /* tiling order */
extern struct wl_list focus_stack; /* focus order */
extern struct wlr_ext_foreign_toplevel_list_v1* ext_toplevel_list;
extern struct wlr_foreign_toplevel_manager_v1* foreign_toplevel_manager;
extern struct wlr_screencopy_manager_v1* screencopy_manager;
extern struct wlr_idle_notifier_v1* idle_notifier;
extern struct wlr_keyboard_shortcuts_inhibit_manager_v1* keyboard_inhibit_manager;
extern struct wlr_output_manager_v1* output_manager;
extern struct wlr_pointer_constraints_v1* pointer_constraints;
extern struct wlr_pointer_gestures_v1* pointer_gestures;
extern struct wlr_relative_pointer_manager_v1* relative_pointer_manager;
extern struct wlr_cursor* cursor;
extern struct wlr_xcursor_manager* cursor_manager;
extern struct wlr_scene_rect* root_background;
#ifdef INTEGRATED_BACKGROUND
extern GdkPixbuf* wallpaper_source; /* full-res decode, cached across resizes */
#endif
extern struct wlr_scene_rect* locked_background;
extern struct wlr_seat* seat;
extern KeyboardGroup* keyboard_group;
extern struct wlr_output_layout* output_layout;
extern struct wlr_box screen_geometry;
extern struct wl_list monitors;
extern Monitor* selected_monitor;
extern struct wl_event_source* status_event_source;
extern Watcher tray_watcher;
#ifdef XWAYLAND
extern struct wlr_xwayland* xwayland;
#endif

#endif
