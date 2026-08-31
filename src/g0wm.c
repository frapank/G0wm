/*
 * See LICENSE file for copyright and license details.
 *
 * setup, teardown and the event loop
 */
#include "g0wm.h"

/* function declarations */
static void autostartexec(void);
static void cleanup(void);
static void cleanuplisteners(void);
static void createidleinhibitor(struct wl_listener* listener, void* data);
static void destroyidleinhibitor(struct wl_listener* listener, void* data);
static void gpureset(struct wl_listener* listener, void* data);
static void handlesig(int signo);
static void setup(void);

/* variables */
/* variables */
static pid_t child_pid = -1;
static struct wl_display* dpy;
static struct wlr_session* session;
static struct wlr_xdg_shell* xdg_shell;
static struct wlr_xdg_activation_v1* activation;
static struct wlr_xdg_decoration_manager_v1* xdg_decoration_mgr;
static struct wlr_idle_inhibit_manager_v1* idle_inhibit_mgr;
static struct wlr_layer_shell_v1* layer_shell;
static struct wlr_virtual_keyboard_manager_v1* virtual_keyboard_mgr;
static struct wlr_virtual_pointer_manager_v1* virtual_pointer_mgr;
static struct wlr_cursor_shape_manager_v1* cursor_shape_mgr;
static struct wlr_output_power_manager_v1* power_mgr;
static struct wlr_session_lock_manager_v1* session_lock_mgr;
static DBusConnection* bus_conn;
static struct wl_event_source* bus_source;
/* global event handlers */
static struct wl_listener cursor_axis = { .notify = axisnotify };
static struct wl_listener cursor_button = { .notify = buttonpress };
static struct wl_listener cursor_frame = { .notify = cursorframe };
static struct wl_listener cursor_motion = { .notify = motionrelative };
static struct wl_listener cursor_motion_absolute = { .notify = motionabsolute };
static struct wl_listener cursor_tablet_axis = { .notify = tabletaxis };
static struct wl_listener cursor_tablet_tip = { .notify = tablettip };
static struct wl_listener gpu_reset = { .notify = gpureset };
static struct wl_listener layout_change = { .notify = updatemons };
static struct wl_listener new_idle_inhibitor = { .notify =
                                                     createidleinhibitor };
static struct wl_listener new_input_device = { .notify = inputdevice };
static struct wl_listener new_virtual_keyboard = { .notify = virtualkeyboard };
static struct wl_listener new_virtual_pointer = { .notify = virtualpointer };
static struct wl_listener new_pointer_constraint = {
    .notify = createpointerconstraint
};
static struct wl_listener new_output = { .notify = createmon };
static struct wl_listener new_xdg_toplevel = { .notify = createnotify };
static struct wl_listener new_xdg_popup = { .notify = createpopup };
static struct wl_listener new_xdg_decoration = { .notify = createdecoration };
static struct wl_listener new_layer_surface = { .notify = createlayersurface };
static struct wl_listener output_mgr_apply = { .notify = outputmgrapply };
static struct wl_listener output_mgr_test = { .notify = outputmgrtest };
static struct wl_listener output_power_mgr_set_mode = { .notify =
                                                            powermgrsetmode };
static struct wl_listener request_cursor = { .notify = setcursor };
static struct wl_listener request_set_psel = { .notify = setpsel };
static struct wl_listener request_set_sel = { .notify = setsel };
static struct wl_listener request_set_cursor_shape = { .notify =
                                                           setcursorshape };
static struct wl_listener request_start_drag = { .notify = requeststartdrag };
static struct wl_listener start_drag = { .notify = startdrag };
static struct wl_listener new_session_lock = { .notify = locksession };
#ifdef XWAYLAND
static struct wl_listener new_xwayland_surface = { .notify = createnotifyx11 };
static struct wl_listener xwayland_ready = { .notify = xwaylandready };
#endif /* XWAYLAND */
static pid_t* autostart_pids;
static size_t autostart_len;

/* variables the other modules share, declared in g0wm.h */
int locked;
void* exclusive_focus;
struct wl_event_loop* event_loop;
struct wlr_backend* backend;
struct wlr_scene* scene;
struct wlr_scene_tree* layers[NUM_LAYERS];
struct wlr_scene_tree* drag_icon;
struct wlr_renderer* drw;
struct wlr_allocator* alloc;
struct wlr_compositor* compositor;
struct wl_list clients; /* tiling order */
struct wl_list fstack;  /* focus order */
struct wlr_idle_notifier_v1* idle_notifier;
struct wlr_output_manager_v1* output_mgr;
struct wlr_pointer_constraints_v1* pointer_constraints;
struct wlr_relative_pointer_manager_v1* relative_pointer_mgr;
struct wlr_cursor* cursor;
struct wlr_xcursor_manager* cursor_mgr;
struct last_cursor_state last_cursor;
struct wlr_scene_rect* root_bg;
#ifdef INTEGRATED_BACKGROUND
GdkPixbuf* wallpaper_src; /* full-res decode, cached across resizes */
#endif                    /* INTEGRATED_BACKGROUND */
struct wlr_scene_rect* locked_bg;
struct wlr_seat* seat;
KeyboardGroup* kb_group;
unsigned int cursor_mode;
Client* grabc;
struct wlr_output_layout* output_layout;
struct wlr_box sgeom;
struct wl_list mons;
Monitor* selmon;
char stext[256];
struct wl_event_source* status_event_source;
#ifdef SYSTRAY
Watcher watcher = { .running = 0 };
#endif /* SYSTRAY */
#ifdef RUNNER
int runner_active;
char runner_buf[256];
int runner_len;
int runner_cur;       /* cursor offset into runner_buf, 0..runner_len */
int runner_repeating; /* the armed repeat belongs to the prompt */
#endif                /* RUNNER */
struct wl_listener request_activate = { .notify = urgent };
#ifdef XWAYLAND
struct wlr_xwayland* xwayland;
#endif /* XWAYLAND */

/* function implementations */
static void autostartexec(void)
{
    const char* const* p;
    size_t i = 0;

    /* count entries */
    for (p = autostart; *p; autostart_len++, p++)
        while (*++p)
            ;

    autostart_pids = calloc(autostart_len, sizeof(pid_t));
    for (p = autostart; *p; i++, p++) {
        if ((autostart_pids[i] = fork()) == 0) {
            setsid();
            execvp(*p, (char* const*)p);
            die("g0wm: execvp %s:", *p);
        }
        /* skip arguments */
        while (*++p)
            ;
    }
}

void chvt(const Arg* arg)
{
    wlr_session_change_vt(session, arg->ui);
}

void checkidleinhibitor(struct wlr_surface* exclude)
{
    int inhibited = 0, unused_lx, unused_ly;
    struct wlr_idle_inhibitor_v1* inhibitor;
    wl_list_for_each(inhibitor, &idle_inhibit_mgr->inhibitors, link)
    {
        struct wlr_surface* surface =
            wlr_surface_get_root_surface(inhibitor->surface);
        struct wlr_scene_tree* tree = surface->data;
        /* An unmapped surface owns no scene tree (unmapnotify() clears
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

static void cleanup(void)
{
    size_t i;

    cleanuplisteners();
#ifdef XWAYLAND
    wlr_xwayland_destroy(xwayland);
    xwayland = NULL;
#endif
    wl_display_destroy_clients(dpy);

    /* kill child processes */
    for (i = 0; i < autostart_len; i++) {
        if (0 < autostart_pids[i]) {
            kill(autostart_pids[i], SIGTERM);
            waitpid(autostart_pids[i], NULL, 0);
        }
    }

    if (child_pid > 0) {
        kill(-child_pid, SIGTERM);
        waitpid(child_pid, NULL, 0);
    }
    wlr_xcursor_manager_destroy(cursor_mgr);

    destroykeyboardgroup(&kb_group->destroy, NULL);

#ifdef SYSTRAY
    if (watcher.running)
        watcher_stop(&watcher);
#endif
#ifdef NOTIFICATIONS
    if (shownotifications)
        notify_stop();
#endif
    if (bus_conn) {
        stopbus(bus_conn, bus_source);
        dbus_connection_unref(bus_conn);
    }

    /* If it's not destroyed manually, it will cause a use-after-free of
     * wlr_seat. Destroy it until it's fixed on the wlroots side */
    wlr_backend_destroy(backend);

    wl_display_destroy(dpy);
    /* Destroy after the wayland display (when the monitors are already
       destroyed) to avoid destroying them with an invalid scene output. */
    wlr_scene_node_destroy(&scene->tree.node);

#ifdef INTEGRATED_BACKGROUND
    if (wallpaper_src)
        g_object_unref(wallpaper_src);
#endif

    drwl_fini();
}

static void cleanuplisteners(void)
{
    wl_list_remove(&cursor_axis.link);
    wl_list_remove(&cursor_button.link);
    wl_list_remove(&cursor_frame.link);
    wl_list_remove(&cursor_motion.link);
    wl_list_remove(&cursor_motion_absolute.link);
    wl_list_remove(&cursor_tablet_axis.link);
    wl_list_remove(&cursor_tablet_tip.link);
    wl_list_remove(&gpu_reset.link);
    wl_list_remove(&new_idle_inhibitor.link);
    wl_list_remove(&layout_change.link);
    wl_list_remove(&new_input_device.link);
    wl_list_remove(&new_virtual_keyboard.link);
    wl_list_remove(&new_virtual_pointer.link);
    wl_list_remove(&new_pointer_constraint.link);
    wl_list_remove(&new_output.link);
    wl_list_remove(&new_xdg_toplevel.link);
    wl_list_remove(&new_xdg_decoration.link);
    wl_list_remove(&new_xdg_popup.link);
    wl_list_remove(&new_layer_surface.link);
    wl_list_remove(&output_mgr_apply.link);
    wl_list_remove(&output_mgr_test.link);
    wl_list_remove(&output_power_mgr_set_mode.link);
    wl_list_remove(&request_activate.link);
    wl_list_remove(&request_cursor.link);
    wl_list_remove(&request_set_psel.link);
    wl_list_remove(&request_set_sel.link);
    wl_list_remove(&request_set_cursor_shape.link);
    wl_list_remove(&request_start_drag.link);
    wl_list_remove(&start_drag.link);
    wl_list_remove(&new_session_lock.link);
#ifdef XWAYLAND
    wl_list_remove(&new_xwayland_surface.link);
    wl_list_remove(&xwayland_ready.link);
#endif
}

static void createidleinhibitor(struct wl_listener* listener, void* data)
{
    struct wlr_idle_inhibitor_v1* idle_inhibitor = data;
    LISTEN_STATIC(&idle_inhibitor->events.destroy, destroyidleinhibitor);

    checkidleinhibitor(NULL);
}

static void destroyidleinhibitor(struct wl_listener* listener, void* data)
{
    /* `data` is the wlr_surface of the idle inhibitor being destroyed,
     * at this point the idle inhibitor is still in the list of the manager */
    checkidleinhibitor(wlr_surface_get_root_surface(data));
    wl_list_remove(&listener->link);
    free(listener);
}

static void gpureset(struct wl_listener* listener, void* data)
{
    struct wlr_renderer* old_drw = drw;
    struct wlr_allocator* old_alloc = alloc;
    struct Monitor* m;
    if (!(drw = wlr_renderer_autocreate(backend)))
        die("couldn't recreate renderer");

    if (!(alloc = wlr_allocator_autocreate(backend, drw)))
        die("couldn't recreate allocator");

    wl_list_remove(&gpu_reset.link);
    wl_signal_add(&drw->events.lost, &gpu_reset);

    wlr_compositor_set_renderer(compositor, drw);

    wl_list_for_each(m, &mons, link)
    {
        wlr_output_init_render(m->wlr_output, alloc, drw);
    }

    wlr_allocator_destroy(old_alloc);
    wlr_renderer_destroy(old_drw);
}

static void handlesig(int signo)
{
    if (signo == SIGCHLD) {
        pid_t pid, *p, *lim;
        while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
            if (pid == child_pid)
                child_pid = -1;
            if (!(p = autostart_pids))
                continue;
            lim = &p[autostart_len];

            for (; p < lim; p++) {
                if (*p == pid) {
                    *p = -1;
                    break;
                }
            }
        }
    } else if (signo == SIGINT || signo == SIGTERM) {
        quit(NULL);
    }
}

void quit(const Arg* arg)
{
    wl_display_terminate(dpy);
}

void run(char* startup_cmd)
{
    /* Add a Unix socket to the Wayland display. */
    const char* socket = wl_display_add_socket_auto(dpy);
    if (!socket)
        die("startup: display_add_socket_auto");
    setenv("WAYLAND_DISPLAY", socket, 1);

    /* Start the backend. This will enumerate outputs and inputs, become the DRM
     * master, etc */
    if (!wlr_backend_start(backend))
        die("startup: backend_start");

    /* Now that the socket exists and the backend is started, run the startup
     * command */
    autostartexec();
    if (startup_cmd) {
        if ((child_pid = fork()) < 0)
            die("startup: fork:");
        if (child_pid == 0) {
            close(STDIN_FILENO);
            setsid();
            execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
            die("startup: execl:");
        }
    }

    /* Mark stdout as non-blocking to avoid the startup script
     * causing g0wm to freeze when a user neither closes stdin
     * nor consumes standard input in his startup script */

    if (fd_set_nonblock(STDOUT_FILENO) < 0)
        close(STDOUT_FILENO);

    drawbars();

    /* At this point the outputs are initialized, choose initial selmon based on
     * cursor position, and set default cursor image */
    selmon = xytomon(cursor->x, cursor->y);

    /* TODO hack to get cursor to display in its initial location (100, 100)
     * instead of (0, 0) and then jumping. Still may not be fully
     * initialized, as the image/coordinates are not transformed for the
     * monitor when displayed here */
    wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
    wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

    /* Run the Wayland event loop. This does not return until you exit the
     * compositor. Starting the backend rigged up all of the necessary event
     * loop configuration to listen to libinput events, DRM events, generate
     * frame events at the refresh rate, and so on. */
    wl_display_run(dpy);
}

static void setup(void)
{
    int drm_fd, i, sig[] = { SIGCHLD, SIGINT, SIGTERM, SIGPIPE };
    struct sigaction sa = { .sa_flags = SA_RESTART, .sa_handler = handlesig };
    char cursor_size_str[8];
    sigemptyset(&sa.sa_mask);

    for (i = 0; i < (int)LENGTH(sig); i++)
        sigaction(sig[i], &sa, NULL);

    wlr_log_init(log_level, NULL);

    /* The Wayland display is managed by libwayland. It handles accepting
     * clients from the Unix socket, managing Wayland globals, and so on. */
    dpy = wl_display_create();
    event_loop = wl_display_get_event_loop(dpy);

    /* The backend is a wlroots feature which abstracts the underlying input and
     * output hardware. The autocreate option will choose the most suitable
     * backend based on the current environment, such as opening an X11 window
     * if an X11 server is running. */
    if (!(backend = wlr_backend_autocreate(event_loop, &session)))
        die("couldn't create backend");

    /* Initialize the scene graph used to lay out windows */
    scene = wlr_scene_create();
    root_bg = wlr_scene_rect_create(&scene->tree, 0, 0, rootcolor);
    for (i = 0; i < NUM_LAYERS; i++)
        layers[i] = wlr_scene_tree_create(&scene->tree);
    drag_icon = wlr_scene_tree_create(&scene->tree);
    wlr_scene_node_place_below(&drag_icon->node, &layers[LyrBlock]->node);

    /* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
     * can also specify a renderer using the WLR_RENDERER env var.
     * The renderer is responsible for defining the various pixel formats it
     * supports for shared memory, this configures that for clients. */
    if (!(drw = wlr_renderer_autocreate(backend)))
        die("couldn't create renderer");
    wl_signal_add(&drw->events.lost, &gpu_reset);

    /* Create shm, drm and linux_dmabuf interfaces by ourselves.
     * The simplest way is to call:
     *      wlr_renderer_init_wl_display(drw);
     * but we need to create the linux_dmabuf interface manually to integrate it
     * with wlr_scene. */
    wlr_renderer_init_wl_shm(drw, dpy);

    if (wlr_renderer_get_texture_formats(drw, WLR_BUFFER_CAP_DMABUF)) {
        wlr_drm_create(dpy, drw);
        wlr_scene_set_linux_dmabuf_v1(
            scene, wlr_linux_dmabuf_v1_create_with_renderer(dpy, 5, drw));
    }

    if ((drm_fd = wlr_renderer_get_drm_fd(drw)) >= 0 &&
        drw->features.timeline && backend->features.timeline)
        wlr_linux_drm_syncobj_manager_v1_create(dpy, 1, drm_fd);

    /* Autocreates an allocator for us.
     * The allocator is the bridge between the renderer and the backend. It
     * handles the buffer creation, allowing wlroots to render onto the
     * screen */
    if (!(alloc = wlr_allocator_autocreate(backend, drw)))
        die("couldn't create allocator");

    /* This creates some hands-off wlroots interfaces. The compositor is
     * necessary for clients to allocate surfaces and the data device manager
     * handles the clipboard. Each of these wlroots interfaces has room for you
     * to dig your fingers in and play with their behavior if you want. Note
     * that the clients cannot set the selection directly without compositor
     * approval, see the setsel() function. */
    compositor = wlr_compositor_create(dpy, 6, drw);
    wlr_subcompositor_create(dpy);
    wlr_data_device_manager_create(dpy);
    wlr_export_dmabuf_manager_v1_create(dpy);
    wlr_screencopy_manager_v1_create(dpy);
    wlr_ext_image_copy_capture_manager_v1_create(dpy, 1);
    wlr_ext_output_image_capture_source_manager_v1_create(dpy, 1);
    wlr_data_control_manager_v1_create(dpy);
    wlr_ext_data_control_manager_v1_create(dpy, 1);
    wlr_primary_selection_v1_device_manager_create(dpy);
    wlr_viewporter_create(dpy);
    wlr_single_pixel_buffer_manager_v1_create(dpy);
    wlr_fractional_scale_manager_v1_create(dpy, 1);
    wlr_presentation_create(dpy, backend, 2);
    wlr_alpha_modifier_v1_create(dpy);

    /* Initializes the interface used to implement urgency hints */
    activation = wlr_xdg_activation_v1_create(dpy);
    wl_signal_add(&activation->events.request_activate, &request_activate);

    wlr_scene_set_gamma_control_manager_v1(
        scene, wlr_gamma_control_manager_v1_create(dpy));

    power_mgr = wlr_output_power_manager_v1_create(dpy);
    wl_signal_add(&power_mgr->events.set_mode, &output_power_mgr_set_mode);

    /* Creates an output layout, which is a wlroots utility for working with an
     * arrangement of screens in a physical layout. */
    output_layout = wlr_output_layout_create(dpy);
    wl_signal_add(&output_layout->events.change, &layout_change);

    wlr_xdg_output_manager_v1_create(dpy, output_layout);

    /* Configure a listener to be notified when new outputs are available on the
     * backend. */
    wl_list_init(&mons);
    wl_signal_add(&backend->events.new_output, &new_output);

    /* Set up our client lists, the xdg-shell and the layer-shell. The xdg-shell
     * is a Wayland protocol which is used for application windows. For more
     * detail on shells, refer to the article:
     *
     * https://drewdevault.com/2018/07/29/Wayland-shells.html
     */
    wl_list_init(&clients);
    wl_list_init(&fstack);

    xdg_shell = wlr_xdg_shell_create(dpy, 6);
    wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_toplevel);
    wl_signal_add(&xdg_shell->events.new_popup, &new_xdg_popup);

    layer_shell = wlr_layer_shell_v1_create(dpy, 3);
    wl_signal_add(&layer_shell->events.new_surface, &new_layer_surface);

    idle_notifier = wlr_idle_notifier_v1_create(dpy);

    idle_inhibit_mgr = wlr_idle_inhibit_v1_create(dpy);
    wl_signal_add(&idle_inhibit_mgr->events.new_inhibitor, &new_idle_inhibitor);

    session_lock_mgr = wlr_session_lock_manager_v1_create(dpy);
    wl_signal_add(&session_lock_mgr->events.new_lock, &new_session_lock);
    locked_bg = wlr_scene_rect_create(layers[LyrBlock],
                                      sgeom.width,
                                      sgeom.height,
                                      (float[4]){ 0.1f, 0.1f, 0.1f, 1.0f });
    wlr_scene_node_set_enabled(&locked_bg->node, 0);

    /* Use decoration protocols to negotiate server-side decorations */
    wlr_server_decoration_manager_set_default_mode(
        wlr_server_decoration_manager_create(dpy),
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
    xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
    wl_signal_add(&xdg_decoration_mgr->events.new_toplevel_decoration,
                  &new_xdg_decoration);

    pointer_constraints = wlr_pointer_constraints_v1_create(dpy);
    wl_signal_add(&pointer_constraints->events.new_constraint,
                  &new_pointer_constraint);

    relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(dpy);

    /*
     * Creates a cursor, which is a wlroots utility for tracking the cursor
     * image shown on screen.
     */
    cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor, output_layout);

    /* Initialize the last_cursor destroy listener link so it's safe to remove
     * later */
    wl_list_init(&last_cursor.destroy.link);

    /* Creates an xcursor manager, another wlroots utility which loads up
     * Xcursor themes to source cursor images from and makes sure that cursor
     * images are available at all scale factors on the screen (necessary for
     * HiDPI support). Scaled cursors will be loaded with each output. */
    /* Passing NULL as the theme name makes libxcursor look for a theme called
     * "default", which many systems don't ship; wlroots then falls back to a
     * fixed-size built-in cursor that ignores cursor_size entirely. Naming a
     * theme explicitly in config.h avoids that. */
    snprintf(cursor_size_str, sizeof(cursor_size_str), "%d", cursor_size);
    cursor_mgr = wlr_xcursor_manager_create(cursor_theme, cursor_size);
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
    wl_signal_add(&cursor->events.tablet_tool_tip, &cursor_tablet_tip);
    wl_signal_add(&cursor->events.button, &cursor_button);
    wl_signal_add(&cursor->events.axis, &cursor_axis);
    wl_signal_add(&cursor->events.frame, &cursor_frame);

    cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(dpy, 1);
    wl_signal_add(&cursor_shape_mgr->events.request_set_shape,
                  &request_set_cursor_shape);

    /*
     * Configures a seat, which is a single "seat" at which a user sits and
     * operates the computer. This conceptually includes up to one keyboard,
     * pointer, touch, and drawing tablet device. We also rig up a listener to
     * let us know when new input devices are available on the backend.
     */
    wl_signal_add(&backend->events.new_input, &new_input_device);
    virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(dpy);
    wl_signal_add(&virtual_keyboard_mgr->events.new_virtual_keyboard,
                  &new_virtual_keyboard);
    virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(dpy);
    wl_signal_add(&virtual_pointer_mgr->events.new_virtual_pointer,
                  &new_virtual_pointer);

    seat = wlr_seat_create(dpy, "seat0");
    wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
    wl_signal_add(&seat->events.request_set_selection, &request_set_sel);
    wl_signal_add(&seat->events.request_set_primary_selection,
                  &request_set_psel);
    wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
    wl_signal_add(&seat->events.start_drag, &start_drag);

    kb_group = createkeyboardgroup();
    wl_list_init(&kb_group->destroy.link);

    output_mgr = wlr_output_manager_v1_create(dpy);
    wl_signal_add(&output_mgr->events.apply, &output_mgr_apply);
    wl_signal_add(&output_mgr->events.test, &output_mgr_test);

    drwl_init();

    status_event_source = wl_event_loop_add_fd(wl_display_get_event_loop(dpy),
                                               STDIN_FILENO,
                                               WL_EVENT_READABLE,
                                               statusin,
                                               NULL);

    /* Missing the session bus is not fatal: g0wm comes up without a tray
     * and/or bar notifications. */
    if (showbar && (0
#ifdef SYSTRAY
                    || showsystray
#endif
#ifdef NOTIFICATIONS
                    || shownotifications
#endif
                    )) {
        if ((bus_conn = dbus_bus_get(DBUS_BUS_SESSION, NULL)) &&
            (bus_source = startbus(bus_conn, event_loop))) {
#ifdef SYSTRAY
            if (showsystray)
                watcher_start(&watcher, bus_conn, event_loop);
#endif
#ifdef NOTIFICATIONS
            if (shownotifications)
                notify_start(
                    bus_conn, event_loop, notification_timeout, drawbars);
#endif
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
    if ((xwayland = wlr_xwayland_create(dpy, compositor, 1))) {
        wl_signal_add(&xwayland->events.ready, &xwayland_ready);
        wl_signal_add(&xwayland->events.new_surface, &new_xwayland_surface);

        setenv("DISPLAY", xwayland->display_name, 1);
    } else {
        fprintf(stderr,
                "failed to setup XWayland X server, continuing without it\n");
    }
#endif
}

void spawn(const Arg* arg)
{
    if (fork() == 0) {
        close(STDIN_FILENO);
        open("/dev/null", O_RDWR);
        dup2(STDERR_FILENO, STDOUT_FILENO);
        setsid();
        execvp(((char**)arg->v)[0], (char**)arg->v);
        die("g0wm: execvp %s failed:", ((char**)arg->v)[0]);
    }
}

int main(int argc, char* argv[])
{
    char* startup_cmd = NULL;
    int c;

    while ((c = getopt(argc, argv, "s:hdv")) != -1) {
        if (c == 's')
            startup_cmd = optarg;
        else if (c == 'd')
            log_level = WLR_DEBUG;
        else if (c == 'v')
            die("g0wm " VERSION);
        else
            goto usage;
    }
    if (optind < argc)
        goto usage;

    /* Wayland requires XDG_RUNTIME_DIR for creating its communications socket
     */
    if (!getenv("XDG_RUNTIME_DIR"))
        die("XDG_RUNTIME_DIR must be set");
    setup();
    run(startup_cmd);
    cleanup();
    return EXIT_SUCCESS;

usage:
    die("Usage: %s [-v] [-d] [-s startup command]", argv[0]);
}
