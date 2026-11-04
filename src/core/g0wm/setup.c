/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * creating and destroying the wlroots objects and the global state
 */

#include <dbus/dbus.h>
#include <drwl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_content_type_v1.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_security_context_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_dialog_v1.h>
#include <wlr/types/wlr_xdg_foreign_registry.h>
#include <wlr/types/wlr_xdg_foreign_v1.h>
#include <wlr/types/wlr_xdg_foreign_v2.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_system_bell_v1.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#ifdef XWAYLAND
#include <wlr/xwayland.h>
#endif

#include "core/activation.h"
#include "core/dbus.h"
#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/bar/bar.h"
#include "desktop/bar/notification.h"
#include "desktop/bar/status.h"
#include "desktop/notify/notify.h"
#include "desktop/systray/watcher.h"
#include "desktop/traypopup.h"
#include "input/gesture.h"
#include "input/keyboard.h"
#include "input/pointer.h"
#include "input/seat.h"
#include "input/tablet.h"
#include "input/textinput.h"
#include "internal.h"
#include "output/lock.h"
#include "output/monitor.h"
#include "output/privacy.h"
#include "window/client.h"
#include "window/toplevel.h"
#include "window/xwayland.h"

/* function declarations */
static void cleanup_listeners(void);
static void handle_new_idle_inhibitor(struct wl_listener* listener, void* data);
static void handle_idle_inhibitor_destroy(struct wl_listener* listener,
                                          void* data);
static bool filter_global(const struct wl_client* client,
                          const struct wl_global* global,
                          void* data);
static void handle_gpu_reset(struct wl_listener* listener, void* data);
static void recreate_renderer(void* data);

/* variables */
struct wl_display* display;
struct wlr_session* session;
static struct wlr_xdg_shell* xdg_shell;
struct wlr_xdg_activation_v1* activation;
static struct wlr_xdg_decoration_manager_v1* xdg_decoration_manager;
static struct wlr_idle_inhibit_manager_v1* idle_inhibit_manager;
static struct wlr_layer_shell_v1* layer_shell;
static struct wlr_virtual_keyboard_manager_v1* virtual_keyboard_manager;
static struct wlr_virtual_pointer_manager_v1* virtual_pointer_manager;
static struct wlr_cursor_shape_manager_v1* cursor_shape_manager;
static struct wlr_output_power_manager_v1* output_power_manager;
static struct wlr_session_lock_manager_v1* session_lock_manager;
static struct wlr_security_context_manager_v1* security_context_manager;
static struct wlr_xdg_foreign_registry* foreign_registry;
struct wlr_backend* backend;
static struct wlr_compositor* compositor;
static DBusConnection* bus_connection;
static struct wl_event_source* bus_source;
/* global event handlers */
static struct wl_listener cursor_axis = { .notify = input_handle_axis };
static struct wl_listener cursor_button = { .notify = input_handle_button };
static struct wl_listener cursor_frame = { .notify = input_handle_frame };
static struct wl_listener cursor_hold_begin = { .notify =
                                                    input_handle_hold_begin };
static struct wl_listener cursor_hold_end = { .notify = input_handle_hold_end };
static struct wl_listener cursor_motion = { .notify = input_handle_motion };
static struct wl_listener cursor_motion_absolute = {
    .notify = input_handle_motion_absolute
};
static struct wl_listener cursor_pinch_begin = { .notify =
                                                     input_handle_pinch_begin };
static struct wl_listener cursor_pinch_end = { .notify =
                                                   input_handle_pinch_end };
static struct wl_listener cursor_pinch_update = {
    .notify = input_handle_pinch_update
};
static struct wl_listener cursor_swipe_begin = { .notify =
                                                     input_handle_swipe_begin };
static struct wl_listener cursor_swipe_end = { .notify =
                                                   input_handle_swipe_end };
static struct wl_listener cursor_swipe_update = {
    .notify = input_handle_swipe_update
};
static struct wl_listener cursor_tablet_axis = { .notify = tablet_handle_axis };
static struct wl_listener cursor_tablet_button = { .notify =
                                                       tablet_handle_button };
static struct wl_listener cursor_tablet_proximity = {
    .notify = tablet_handle_proximity
};
static struct wl_listener cursor_tablet_tip = { .notify = tablet_handle_tip };
static struct wl_listener cursor_touch_cancel = {
    .notify = tablet_handle_touch_cancel
};
static struct wl_listener cursor_touch_down = { .notify =
                                                    tablet_handle_touch_down };
static struct wl_listener cursor_touch_frame = {
    .notify = tablet_handle_touch_frame
};
static struct wl_listener cursor_touch_motion = {
    .notify = tablet_handle_touch_motion
};
static struct wl_listener cursor_touch_up = { .notify =
                                                  tablet_handle_touch_up };
static struct wl_listener gpu_reset = { .notify = handle_gpu_reset };
static struct wl_listener keyboard_focus_change = {
    .notify = tablet_handle_keyboard_focus_change
};
static struct wl_listener text_input_focus_change = {
    .notify = textinput_handle_focus_change
};
static struct wl_listener layout_change = { .notify = monitor_update_layout };
static struct wl_listener new_capture_request = {
    .notify = toplevel_handle_new_capture_request
};
static struct wl_listener new_capture_session = {
    .notify = privacy_handle_new_capture_session
};
static struct wl_listener new_idle_inhibitor = {
    .notify = handle_new_idle_inhibitor
};
static struct wl_listener new_input_device = { .notify =
                                                   input_handle_new_input };
static struct wl_listener new_input_method = {
    .notify = textinput_handle_new_input_method
};
static struct wl_listener new_keyboard_inhibitor = {
    .notify = input_handle_new_keyboard_inhibitor
};
static struct wl_listener new_text_input = {
    .notify = textinput_handle_new_text_input
};
static struct wl_listener new_virtual_keyboard = {
    .notify = input_handle_new_virtual_keyboard
};
static struct wl_listener new_virtual_pointer = {
    .notify = input_handle_new_virtual_pointer
};
static struct wl_listener new_pointer_constraint = {
    .notify = input_handle_new_pointer_constraint
};
static struct wl_listener new_output = { .notify = monitor_handle_new_output };
static struct wl_listener new_xdg_toplevel = { .notify =
                                                   client_handle_new_toplevel };
static struct wl_listener new_xdg_popup = { .notify = client_handle_new_popup };
static struct wl_listener new_xdg_decoration = {
    .notify = client_handle_new_decoration
};
static struct wl_listener new_layer_surface = {
    .notify = monitor_handle_new_layer_surface
};
static struct wl_listener output_manager_apply = {
    .notify = monitor_handle_config_apply
};
static struct wl_listener output_manager_test = {
    .notify = monitor_handle_config_test
};
static struct wl_listener output_power_set_mode = {
    .notify = monitor_handle_power_set_mode
};
static struct wl_listener request_activate = {
    .notify = client_handle_request_activate
};
static struct wl_listener request_cursor = { .notify =
                                                 input_handle_request_cursor };
static struct wl_listener request_set_primary_selection = {
    .notify = input_handle_request_primary_selection
};
static struct wl_listener request_set_selection = {
    .notify = input_handle_request_selection
};
static struct wl_listener request_set_cursor_shape = {
    .notify = input_handle_request_cursor_shape
};
static struct wl_listener ring_bell = { .notify = client_handle_ring_bell };
static struct wl_listener request_start_drag = {
    .notify = input_handle_request_start_drag
};
static struct wl_listener start_drag = { .notify = input_handle_start_drag };
static struct wl_listener new_session_lock = { .notify = lock_handle_new_lock };
#ifdef XWAYLAND
static struct wl_listener new_xwayland_surface = {
    .notify = xwayland_handle_new_surface
};
static struct wl_listener xwayland_ready = { .notify = xwayland_handle_ready };
#endif
pid_t* autostart_pids;
size_t autostart_len;
void* exclusive_focus;
struct wl_event_loop* event_loop;
struct wlr_scene* scene;
struct wlr_scene_tree* layers[NUM_LAYERS];
struct wlr_scene_tree* drag_icon;
struct wlr_renderer* renderer;
struct wlr_allocator* allocator;
struct wlr_content_type_manager_v1* content_type_manager;
struct wlr_tearing_control_manager_v1* tearing_manager;
struct wl_list clients;     /* tiling order */
struct wl_list focus_stack; /* focus order */
struct wlr_ext_foreign_toplevel_list_v1* ext_toplevel_list;
struct wlr_foreign_toplevel_manager_v1* foreign_toplevel_manager;
struct wlr_screencopy_manager_v1* screencopy_manager;
struct wlr_idle_notifier_v1* idle_notifier;
struct wlr_keyboard_shortcuts_inhibit_manager_v1* keyboard_inhibit_manager;
struct wlr_output_manager_v1* output_manager;
struct wlr_pointer_constraints_v1* pointer_constraints;
struct wlr_pointer_gestures_v1* pointer_gestures;
struct wlr_relative_pointer_manager_v1* relative_pointer_manager;
struct wlr_cursor* cursor;
struct wlr_xcursor_manager* cursor_manager;
struct wlr_scene_rect* root_background;
#ifdef INTEGRATED_BACKGROUND
GdkPixbuf* wallpaper_source; /* full-res decode, cached across resizes */
#endif
struct wlr_scene_rect* locked_background;
struct wlr_seat* seat;
KeyboardGroup* keyboard_group;
struct wlr_output_layout* output_layout;
struct wlr_box screen_geometry;
struct wl_list monitors;
struct wl_event_source* status_event_source;
Watcher tray_watcher = { .running = 0 };
#ifdef XWAYLAND
struct wlr_xwayland* xwayland;
#endif
/* The renderer can't be destroyed from inside its own lost signal: wlroots
 * asserts the listener list is empty, and the emission still holds markers
 * in it. Recreate once the loop is idle instead. */
static struct wl_event_source* gpu_reset_idle;

/* function implementations */
void check_idle_inhibitor(struct wlr_surface* exclude)
{
    int inhibited = 0, unused_lx, unused_ly;
    struct wlr_idle_inhibitor_v1* inhibitor;
    wl_list_for_each(inhibitor, &idle_inhibit_manager->inhibitors, link)
    {
        struct wlr_surface* surface =
            wlr_surface_get_root_surface(inhibitor->surface);
        struct wlr_scene_tree* tree = surface->data;
        /* An unmapped surface owns no scene tree (client_handle_unmap() clears
         * surface->data), so it cannot be visible and must not keep the
         * session awake. */
        if (exclude != surface &&
            (bypass_surface_visibility ||
             (surface->mapped &&
              (!tree ||
               wlr_scene_node_coords(&tree->node, &unused_lx, &unused_ly))))) {
            inhibited = 1;
            break;
        }
    }

    wlr_idle_notifier_v1_set_inhibited(idle_notifier, inhibited);
}

void cleanup(void)
{
    cleanup_listeners();
#ifdef XWAYLAND
    wlr_xwayland_destroy(xwayland);
    xwayland = nullptr;
#endif
    wl_display_destroy_clients(display);

    /* kill child processes */
    for (size_t i = 0; i < autostart_len; i++) {
        if (0 < autostart_pids[i]) {
            kill(autostart_pids[i], SIGTERM);
            waitpid(autostart_pids[i], nullptr, 0);
        }
    }

    wlr_xcursor_manager_destroy(cursor_manager);

    input_handle_keyboard_group_destroy(&keyboard_group->destroy, nullptr);
    keyboard_group = nullptr; /* input_update_seat_caps() runs as the backend
                                 drops the devices */

    /* before the watcher, an open menu still holds a bus connection */
    traypopup_cleanup();
    if (tray_watcher.running)
        watcher_stop(&tray_watcher);
    bar_notification_finish();
    bar_finish();
    privacy_finish();
    if (notifications_enabled)
        notify_stop();
    if (bus_connection) {
        bus_stop(bus_connection, bus_source);
        dbus_connection_unref(bus_connection);
    }

    /* If it's not destroyed manually, it will cause a use-after-free of
     * wlr_seat. Destroy it until it's fixed on the wlroots side */
    wlr_backend_destroy(backend);

    wl_display_destroy(display);
    /* Destroy after the wayland display (when the monitors are already
       destroyed) to avoid destroying them with an invalid scene output. */
    wlr_scene_node_destroy(&scene->tree.node);

#ifdef INTEGRATED_BACKGROUND
    if (wallpaper_source)
        g_object_unref(wallpaper_source);
#endif

    drwl_fini();
}

static void cleanup_listeners(void)
{
    wl_list_remove(&cursor_axis.link);
    wl_list_remove(&cursor_button.link);
    wl_list_remove(&cursor_frame.link);
    wl_list_remove(&cursor_hold_begin.link);
    wl_list_remove(&cursor_hold_end.link);
    wl_list_remove(&cursor_motion.link);
    wl_list_remove(&cursor_motion_absolute.link);
    wl_list_remove(&cursor_pinch_begin.link);
    wl_list_remove(&cursor_pinch_end.link);
    wl_list_remove(&cursor_pinch_update.link);
    wl_list_remove(&cursor_swipe_begin.link);
    wl_list_remove(&cursor_swipe_end.link);
    wl_list_remove(&cursor_swipe_update.link);
    wl_list_remove(&cursor_tablet_axis.link);
    wl_list_remove(&cursor_tablet_button.link);
    wl_list_remove(&cursor_tablet_proximity.link);
    wl_list_remove(&cursor_tablet_tip.link);
    wl_list_remove(&cursor_touch_cancel.link);
    wl_list_remove(&cursor_touch_down.link);
    wl_list_remove(&cursor_touch_frame.link);
    wl_list_remove(&cursor_touch_motion.link);
    wl_list_remove(&cursor_touch_up.link);
    wl_list_remove(&gpu_reset.link);
    wl_list_remove(&keyboard_focus_change.link);
    wl_list_remove(&text_input_focus_change.link);
    wl_list_remove(&new_idle_inhibitor.link);
    wl_list_remove(&layout_change.link);
    wl_list_remove(&new_input_device.link);
    wl_list_remove(&new_input_method.link);
    wl_list_remove(&new_text_input.link);
    wl_list_remove(&new_virtual_keyboard.link);
    wl_list_remove(&new_virtual_pointer.link);
    wl_list_remove(&new_pointer_constraint.link);
    wl_list_remove(&new_capture_request.link);
    wl_list_remove(&new_capture_session.link);
    wl_list_remove(&new_keyboard_inhibitor.link);
    wl_list_remove(&new_output.link);
    wl_list_remove(&new_xdg_toplevel.link);
    wl_list_remove(&new_xdg_decoration.link);
    wl_list_remove(&new_xdg_popup.link);
    wl_list_remove(&new_layer_surface.link);
    wl_list_remove(&output_manager_apply.link);
    wl_list_remove(&output_manager_test.link);
    wl_list_remove(&output_power_set_mode.link);
    wl_list_remove(&request_activate.link);
    wl_list_remove(&request_cursor.link);
    wl_list_remove(&request_set_primary_selection.link);
    wl_list_remove(&request_set_selection.link);
    wl_list_remove(&request_set_cursor_shape.link);
    wl_list_remove(&request_start_drag.link);
    wl_list_remove(&ring_bell.link);
    wl_list_remove(&start_drag.link);
    wl_list_remove(&new_session_lock.link);
#ifdef XWAYLAND
    wl_list_remove(&new_xwayland_surface.link);
    wl_list_remove(&xwayland_ready.link);
#endif
}

static void handle_new_idle_inhibitor(struct wl_listener*, void* data)
{
    struct wlr_idle_inhibitor_v1* idle_inhibitor = data;
    LISTEN_STATIC(&idle_inhibitor->events.destroy,
                  handle_idle_inhibitor_destroy);

    check_idle_inhibitor(nullptr);
}

static void handle_idle_inhibitor_destroy(struct wl_listener* listener,
                                          void* data)
{
    /* `data` is the wlr_surface of the idle inhibitor being destroyed,
     * at this point the idle inhibitor is still in the list of the manager */
    check_idle_inhibitor(wlr_surface_get_root_surface(data));
    wl_list_remove(&listener->link);
    free(listener);
}

static bool filter_global(const struct wl_client* client,
                          const struct wl_global* global,
                          void*)
{
    static const char* const privileged[] = {
        "ext_data_control_manager_v1",
        "ext_foreign_toplevel_image_capture_source_manager_v1",
        "ext_foreign_toplevel_list_v1",
        "ext_idle_notifier_v1",
        "ext_image_copy_capture_manager_v1",
        "ext_output_image_capture_source_manager_v1",
        "ext_session_lock_manager_v1",
        "wp_security_context_manager_v1",
        "zwlr_data_control_manager_v1",
        "zwlr_export_dmabuf_manager_v1",
        "zwlr_foreign_toplevel_manager_v1",
        "zwlr_gamma_control_manager_v1",
        "zwlr_layer_shell_v1",
        "zwlr_output_manager_v1",
        "zwlr_output_power_manager_v1",
        "zwlr_screencopy_manager_v1",
        "zwlr_virtual_pointer_manager_v1",
        "zwp_input_method_manager_v2",
        "zwp_virtual_keyboard_manager_v1",
    };
    const char* name = wl_global_get_interface(global)->name;

    if (!wlr_security_context_manager_v1_lookup_client(security_context_manager,
                                                       client))
        return true;
    for (size_t i = 0; i < LENGTH(privileged); i++)
        if (!strcmp(name, privileged[i]))
            return false;
    return true;
}

static void handle_gpu_reset(struct wl_listener*, void*)
{
    if (!gpu_reset_idle)
        gpu_reset_idle =
            wl_event_loop_add_idle(event_loop, recreate_renderer, nullptr);
}

/* Scene buffers cache the texture made from our own (shm) buffers, and it dies
 * with the old renderer. Re-handing the same buffer drops it, so the next
 * frame re-uploads. Client buffers carry their texture themselves. */
static void drop_texture(struct wlr_scene_buffer* sb, int, int, void*)
{
    struct wlr_buffer* b = sb->buffer;
    if (!b || wlr_client_buffer_get(b))
        return;
    wlr_buffer_lock(b);
    wlr_scene_buffer_set_buffer(sb, b);
    wlr_buffer_unlock(b);
}

static void recreate_renderer(void*)
{
    struct wlr_renderer* old_drw = renderer;
    struct wlr_allocator* old_alloc = allocator;
    struct Monitor* m;
    gpu_reset_idle = nullptr;
    if (!(renderer = wlr_renderer_autocreate(backend)))
        util_die("couldn't recreate renderer");

    if (!(allocator = wlr_allocator_autocreate(backend, renderer)))
        util_die("couldn't recreate allocator");

    wl_list_remove(&gpu_reset.link);
    wl_signal_add(&renderer->events.lost, &gpu_reset);

    wlr_compositor_set_renderer(compositor, renderer);

    wl_list_for_each(m, &monitors, link)
    {
        wlr_output_init_render(m->wlr_output, allocator, renderer);
    }

    /* before the old renderer goes, so the textures are freed while it lives */
    wlr_scene_node_for_each_buffer(&scene->tree.node, drop_texture, nullptr);

    wlr_allocator_destroy(old_alloc);
    wlr_renderer_destroy(old_drw);
}

void setup(void)
{
    int drm_fd, sig[] = { SIGCHLD, SIGINT, SIGTERM, SIGPIPE };
    struct sigaction sa = { .sa_flags = SA_RESTART,
                            .sa_handler = handle_signal };
    char cursor_size_str[8];
    sigemptyset(&sa.sa_mask);

    for (int i = 0; i < (int)LENGTH(sig); i++)
        sigaction(sig[i], &sa, nullptr);

    wlr_log_init(log_level, nullptr);

    /* The Wayland display is managed by libwayland. It handles accepting
     * clients from the Unix socket, managing Wayland globals, and so on. */
    display = wl_display_create();
    event_loop = wl_display_get_event_loop(display);

    /* The backend is a wlroots feature which abstracts the underlying input and
     * output hardware. The autocreate option will choose the most suitable
     * backend based on the current environment, such as opening an X11 window
     * if an X11 server is running. */
    if (!(backend = wlr_backend_autocreate(event_loop, &session)))
        util_die("couldn't create backend");

    /* Initialize the scene graph used to lay out windows */
    scene = wlr_scene_create();
    root_background = wlr_scene_rect_create(&scene->tree, 0, 0, root_color);
    for (int i = 0; i < NUM_LAYERS; i++)
        layers[i] = wlr_scene_tree_create(&scene->tree);
    drag_icon = wlr_scene_tree_create(&scene->tree);
    wlr_scene_node_place_below(&drag_icon->node, &layers[LyrBlock]->node);

    /* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
     * can also specify a renderer using the WLR_RENDERER env var.
     * The renderer is responsible for defining the various pixel formats it
     * supports for shared memory, this configures that for clients. */
    if (!(renderer = wlr_renderer_autocreate(backend)))
        util_die("couldn't create renderer");
    wl_signal_add(&renderer->events.lost, &gpu_reset);

    /* Create shm, drm and linux_dmabuf interfaces by ourselves.
     * The simplest way is to call:
     *      wlr_renderer_init_wl_display(renderer);
     * but we need to create the linux_dmabuf interface manually to integrate it
     * with wlr_scene. */
    wlr_renderer_init_wl_shm(renderer, display);

    if (wlr_renderer_get_texture_formats(renderer, WLR_BUFFER_CAP_DMABUF)) {
        wlr_drm_create(display, renderer);
        wlr_scene_set_linux_dmabuf_v1(
            scene,
            wlr_linux_dmabuf_v1_create_with_renderer(display, 5, renderer));
    }

    if ((drm_fd = wlr_renderer_get_drm_fd(renderer)) >= 0 &&
        renderer->features.timeline && backend->features.timeline)
        wlr_linux_drm_syncobj_manager_v1_create(display, 1, drm_fd);

    /* Autocreates an allocator for us.
     * The allocator is the bridge between the renderer and the backend. It
     * handles the buffer creation, allowing wlroots to render onto the
     * screen */
    if (!(allocator = wlr_allocator_autocreate(backend, renderer)))
        util_die("couldn't create allocator");

    /* This creates some hands-off wlroots interfaces. The compositor is
     * necessary for clients to allocate surfaces and the data device manager
     * handles the clipboard. Each of these wlroots interfaces has room for you
     * to dig your fingers in and play with their behavior if you want. Note
     * that the clients cannot set the selection directly without compositor
     * approval, see the input_handle_request_selection() function. */
    compositor = wlr_compositor_create(display, 6, renderer);
    wlr_subcompositor_create(display);
    wlr_data_device_manager_create(display);
    wlr_export_dmabuf_manager_v1_create(display);
    screencopy_manager = wlr_screencopy_manager_v1_create(display);
    wl_signal_add(&wlr_ext_image_copy_capture_manager_v1_create(display, 1)
                       ->events.new_session,
                  &new_capture_session);
    wlr_ext_output_image_capture_source_manager_v1_create(display, 1);
    wlr_data_control_manager_v1_create(display);
    wlr_ext_data_control_manager_v1_create(display, 1);
    wlr_primary_selection_v1_device_manager_create(display);
    wlr_viewporter_create(display);
    wlr_single_pixel_buffer_manager_v1_create(display);
    wlr_fractional_scale_manager_v1_create(display, 1);
    wlr_presentation_create(display, backend, 2);
    wlr_alpha_modifier_v1_create(display);
    content_type_manager = wlr_content_type_manager_v1_create(display, 1);
    tearing_manager = wlr_tearing_control_manager_v1_create(display, 1);
    security_context_manager = wlr_security_context_manager_v1_create(display);
    wl_display_set_global_filter(display, filter_global, nullptr);

    /* Initializes the interface used to implement urgency hints */
    activation = wlr_xdg_activation_v1_create(display);
    wl_signal_add(&activation->events.request_activate, &request_activate);

    wl_signal_add(&wlr_xdg_system_bell_v1_create(display, 1)->events.ring,
                  &ring_bell);

    foreign_registry = wlr_xdg_foreign_registry_create(display);
    wlr_xdg_foreign_v1_create(display, foreign_registry);
    wlr_xdg_foreign_v2_create(display, foreign_registry);
    wlr_xdg_wm_dialog_v1_create(display, 1);

    foreign_toplevel_manager = wlr_foreign_toplevel_manager_v1_create(display);
    ext_toplevel_list = wlr_ext_foreign_toplevel_list_v1_create(display, 1);
    wl_signal_add(
        &wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(
             display, 1)
             ->events.new_request,
        &new_capture_request);

    wlr_scene_set_gamma_control_manager_v1(
        scene, wlr_gamma_control_manager_v1_create(display));

    output_power_manager = wlr_output_power_manager_v1_create(display);
    wl_signal_add(&output_power_manager->events.set_mode,
                  &output_power_set_mode);

    /* Creates an output layout, which is a wlroots utility for working with an
     * arrangement of screens in a physical layout. */
    output_layout = wlr_output_layout_create(display);
    wl_signal_add(&output_layout->events.change, &layout_change);

    wlr_xdg_output_manager_v1_create(display, output_layout);

    /* Configure a listener to be notified when new outputs are available on the
     * backend. */
    wl_list_init(&monitors);
    wl_signal_add(&backend->events.new_output, &new_output);

    /* Set up our client lists, the xdg-shell and the layer-shell. The xdg-shell
     * is a Wayland protocol which is used for application windows. For more
     * detail on shells, refer to the article:
     *
     * https://drewdevault.com/2018/07/29/Wayland-shells.html
     */
    wl_list_init(&clients);
    wl_list_init(&focus_stack);

    xdg_shell = wlr_xdg_shell_create(display, 6);
    wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_toplevel);
    wl_signal_add(&xdg_shell->events.new_popup, &new_xdg_popup);

    layer_shell = wlr_layer_shell_v1_create(display, 3);
    wl_signal_add(&layer_shell->events.new_surface, &new_layer_surface);

    idle_notifier = wlr_idle_notifier_v1_create(display);

    idle_inhibit_manager = wlr_idle_inhibit_v1_create(display);
    wl_signal_add(&idle_inhibit_manager->events.new_inhibitor,
                  &new_idle_inhibitor);

    session_lock_manager = wlr_session_lock_manager_v1_create(display);
    wl_signal_add(&session_lock_manager->events.new_lock, &new_session_lock);
    locked_background =
        wlr_scene_rect_create(layers[LyrBlock],
                              screen_geometry.width,
                              screen_geometry.height,
                              (float[4]){ 0.1f, 0.1f, 0.1f, 1.0f });
    wlr_scene_node_set_enabled(&locked_background->node, 0);

    /* Use decoration protocols to negotiate server-side decorations */
    wlr_server_decoration_manager_set_default_mode(
        wlr_server_decoration_manager_create(display),
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
    xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(display);
    wl_signal_add(&xdg_decoration_manager->events.new_toplevel_decoration,
                  &new_xdg_decoration);

    pointer_constraints = wlr_pointer_constraints_v1_create(display);
    wl_signal_add(&pointer_constraints->events.new_constraint,
                  &new_pointer_constraint);

    relative_pointer_manager = wlr_relative_pointer_manager_v1_create(display);

    keyboard_inhibit_manager =
        wlr_keyboard_shortcuts_inhibit_v1_create(display);
    wl_signal_add(&keyboard_inhibit_manager->events.new_inhibitor,
                  &new_keyboard_inhibitor);

    /*
     * Creates a cursor, which is a wlroots utility for tracking the cursor
     * image shown on screen.
     */
    cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor, output_layout);

    /* Initialize the input_last_cursor destroy listener link so it's safe to
     * remove later */
    wl_list_init(&input_last_cursor.destroy.link);

    /* Creates an xcursor manager, another wlroots utility which loads up
     * Xcursor themes to source cursor images from and makes sure that cursor
     * images are available at all scale factors on the screen (necessary for
     * HiDPI support). Scaled cursors will be loaded with each output. */
    /* Passing nullptr as the theme name makes libxcursor look for a theme
     * called "default", which many systems don't ship; wlroots then falls back
     * to a fixed-size built-in cursor that ignores cursor_size entirely. Naming
     * a theme explicitly in config.h avoids that. */
    snprintf(cursor_size_str, sizeof(cursor_size_str), "%d", cursor_size);
    cursor_manager = wlr_xcursor_manager_create(cursor_theme, cursor_size);
    setenv("XCURSOR_SIZE", cursor_size_str, 1);
    if (cursor_theme)
        setenv("XCURSOR_THEME", cursor_theme, 1);

    /*
     * wlr_cursor *only* displays an image on screen. It does not move around
     * when the pointer moves. However, we can attach input devices to it, and
     * it will generate aggregate events for all of them. In these events, we
     * can choose how we want to process them, forwarding them to clients and
     * moving the cursor around. More detail on this process is described in
     * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html
     *
     * And more comments are sprinkled throughout the notify functions above.
     */
    wl_signal_add(&cursor->events.motion, &cursor_motion);
    wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
    wl_signal_add(&cursor->events.tablet_tool_axis, &cursor_tablet_axis);
    wl_signal_add(&cursor->events.tablet_tool_button, &cursor_tablet_button);
    wl_signal_add(&cursor->events.tablet_tool_proximity,
                  &cursor_tablet_proximity);
    wl_signal_add(&cursor->events.tablet_tool_tip, &cursor_tablet_tip);
    wl_signal_add(&cursor->events.touch_cancel, &cursor_touch_cancel);
    wl_signal_add(&cursor->events.touch_down, &cursor_touch_down);
    wl_signal_add(&cursor->events.touch_frame, &cursor_touch_frame);
    wl_signal_add(&cursor->events.touch_motion, &cursor_touch_motion);
    wl_signal_add(&cursor->events.touch_up, &cursor_touch_up);
    wl_signal_add(&cursor->events.button, &cursor_button);
    wl_signal_add(&cursor->events.axis, &cursor_axis);
    wl_signal_add(&cursor->events.frame, &cursor_frame);
    wl_signal_add(&cursor->events.swipe_begin, &cursor_swipe_begin);
    wl_signal_add(&cursor->events.swipe_update, &cursor_swipe_update);
    wl_signal_add(&cursor->events.swipe_end, &cursor_swipe_end);
    wl_signal_add(&cursor->events.pinch_begin, &cursor_pinch_begin);
    wl_signal_add(&cursor->events.pinch_update, &cursor_pinch_update);
    wl_signal_add(&cursor->events.pinch_end, &cursor_pinch_end);
    wl_signal_add(&cursor->events.hold_begin, &cursor_hold_begin);
    wl_signal_add(&cursor->events.hold_end, &cursor_hold_end);

    pointer_gestures = wlr_pointer_gestures_v1_create(display);

    cursor_shape_manager = wlr_cursor_shape_manager_v1_create(display, 1);
    tablet_manager = wlr_tablet_v2_create(display);
    wl_signal_add(&cursor_shape_manager->events.request_set_shape,
                  &request_set_cursor_shape);

    /*
     * Configures a seat, which is a single "seat" at which a user sits and
     * operates the computer. This conceptually includes up to one keyboard,
     * pointer, touch, and drawing tablet device. We also rig up a listener to
     * let us know when new input devices are available on the backend.
     */
    wl_signal_add(&backend->events.new_input, &new_input_device);
    virtual_keyboard_manager = wlr_virtual_keyboard_manager_v1_create(display);
    wl_signal_add(&virtual_keyboard_manager->events.new_virtual_keyboard,
                  &new_virtual_keyboard);
    virtual_pointer_manager = wlr_virtual_pointer_manager_v1_create(display);
    wl_signal_add(&virtual_pointer_manager->events.new_virtual_pointer,
                  &new_virtual_pointer);

    seat = wlr_seat_create(display, "seat0");
    wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
    wl_signal_add(&seat->events.request_set_selection, &request_set_selection);
    wl_signal_add(&seat->events.request_set_primary_selection,
                  &request_set_primary_selection);
    wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
    wl_signal_add(&seat->events.start_drag, &start_drag);
    wl_signal_add(&seat->keyboard_state.events.focus_change,
                  &keyboard_focus_change);
    wl_signal_add(&seat->keyboard_state.events.focus_change,
                  &text_input_focus_change);

    keyboard_group = input_create_keyboard_group();
    wl_list_init(&keyboard_group->destroy.link);

    wl_signal_add(
        &wlr_text_input_manager_v3_create(display)->events.new_text_input,
        &new_text_input);
    wl_signal_add(
        &wlr_input_method_manager_v2_create(display)->events.new_input_method,
        &new_input_method);

    output_manager = wlr_output_manager_v1_create(display);
    wl_signal_add(&output_manager->events.apply, &output_manager_apply);
    wl_signal_add(&output_manager->events.test, &output_manager_test);

    drwl_init();

    status_event_source =
        wl_event_loop_add_fd(wl_display_get_event_loop(display),
                             STDIN_FILENO,
                             WL_EVENT_READABLE,
                             bar_handle_status_input,
                             nullptr);

    /* Missing the session bus is not fatal: g0wm comes up without a tray
     * and/or bar notifications. */
    if (bar_enabled && (systray_enabled || notifications_enabled)) {
        if ((bus_connection = dbus_bus_get(DBUS_BUS_SESSION, nullptr)) &&
            (bus_source = bus_start(bus_connection, event_loop))) {
            if (systray_enabled)
                watcher_start(&tray_watcher, bus_connection, event_loop);
            if (notifications_enabled)
                notify_start(bus_connection,
                             event_loop,
                             notification_timeout,
                             bar_draw_all,
                             activation_token_new);
        } else
            fprintf(stderr,
                    "Couldn't connect to the session bus, "
                    "systray/notifications not available\n");
    }

    /* Make sure XWayland clients don't connect to the parent X server,
     * e.g when running in the x11 backend or the wayland backend and the
     * compositor has Xwayland support */
    unsetenv("DISPLAY");
#ifdef XWAYLAND
    /*
     * Initialise the XWayland X server.
     * It will be started when the first X client is started.
     */
    if ((xwayland = wlr_xwayland_create(display, compositor, 1))) {
        wl_signal_add(&xwayland->events.ready, &xwayland_ready);
        wl_signal_add(&xwayland->events.new_surface, &new_xwayland_surface);

        setenv("DISPLAY", xwayland->display_name, 1);
    } else {
        fprintf(stderr,
                "failed to setup XWayland X server, continuing without it\n");
    }
#endif
}
