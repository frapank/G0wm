/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * startup, the event loop and spawning programs
 */

#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/util/log.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/bar/bar.h"
#include "internal.h"
#include "output/monitor.h"

Monitor* selected_monitor;

/* autostart is a list of NULL terminated argv arrays, ended by an empty one */
static void run_autostart(void)
{
    for (const char* const* argv = autostart; *argv; argv++) {
        autostart_len++;
        while (*++argv)
            ;
    }

    autostart_pids = calloc(autostart_len, sizeof(pid_t));

    size_t index = 0;
    for (const char* const* argv = autostart; *argv; index++, argv++) {
        autostart_pids[index] = fork();
        if (autostart_pids[index] == 0) {
            setsid();
            execvp(*argv, (char* const*)argv);
            fprintf(stderr, "g0wm: execvp %s: ", *argv);
            perror(nullptr);
            _exit(1);
        }
        while (*++argv)
            ;
    }
}

void change_virtual_console(const Arg* arg)
{
    wlr_session_change_vt(session, arg->ui);
}

/* stop the Wayland event loop, which makes run() return */
void quit(const Arg*)
{
    wl_display_terminate(display);
}

/* forget a reaped autostart child so cleanup() does not signal its pid */
static void autostart_forget_child(pid_t pid)
{
    for (size_t i = 0; i < autostart_len; i++) {
        if (autostart_pids[i] == pid) {
            autostart_pids[i] = -1;
            return;
        }
    }
}

void handle_unix_signal(int signo)
{
    if (signo == SIGCHLD) {
        pid_t pid;
        while ((pid = waitpid(-1, nullptr, WNOHANG)) > 0) {
            if (autostart_pids)
                autostart_forget_child(pid);
        }
    } else if (signo == SIGINT || signo == SIGTERM) {
        quit(nullptr);
    }
}

static void run(void)
{
    /* Add a Unix socket to the Wayland display. */
    const char* socket = wl_display_add_socket_auto(display);
    if (!socket)
        util_die("startup: display_add_socket_auto");
    setenv("WAYLAND_DISPLAY", socket, 1);

    /* Start the backend. This will enumerate outputs and inputs, become the DRM
     * master, etc */
    if (!wlr_backend_start(backend))
        util_die("startup: backend_start");

    /* the socket exists and the backend is started */
    run_autostart();

    bar_draw_all();

    /* At this point the outputs are initialized, choose initial
     * selected_monitor based on cursor position, and set default cursor image
     */
    selected_monitor = monitor_at(cursor->x, cursor->y);

    /* TODO hack to get cursor to display in its initial location (100, 100)
     * instead of (0, 0) and then jumping. Still may not be fully
     * initialized, as the image/coordinates are not transformed for the
     * monitor when displayed here */
    wlr_cursor_warp_closest(cursor, nullptr, cursor->x, cursor->y);
    wlr_cursor_set_xcursor(cursor, cursor_manager, "default");

    /* Run the Wayland event loop. This does not return until you exit the
     * compositor. Starting the backend rigged up all of the necessary event
     * loop configuration to listen to libinput events, DRM events, generate
     * frame events at the refresh rate, and so on. */
    wl_display_run(display);
}

/* run arg->v (a NULL terminated argv) detached from the compositor */
void spawn(const Arg* arg)
{
    if (fork() == 0) {
        close(STDIN_FILENO);
        open("/dev/null", O_RDWR);
        dup2(STDERR_FILENO, STDOUT_FILENO);
        setsid();
        char** argv = (char**)arg->v;
        execvp(argv[0], argv);

        /* only reached if execvp fails */
        fprintf(stderr, "g0wm: execvp %s failed: ", argv[0]);
        perror(nullptr);

        _exit(1);
    }
}

int main(int argc, char* argv[])
{
    bool debug = false;

    int option;
    while ((option = getopt(argc, argv, "hdvc")) != -1) {
        if (option == 'd')
            debug = true;
        else if (option == 'v')
            util_die("g0wm " VERSION);
        else if (option == 'c')
            return settings_write();
        else
            goto usage;
    }

    if (optind < argc)
        goto usage;

    /* Wayland requires XDG_RUNTIME_DIR for creating its communications socket
     */
    if (!getenv("XDG_RUNTIME_DIR"))
        util_die("XDG_RUNTIME_DIR must be set");

    settings_load();

    if (debug)
        log_level = WLR_DEBUG; /* Should be set after settings_load() */

    setup();
    run();

    cleanup();
    return EXIT_SUCCESS;

usage:
    util_die("Usage: %s [-v] [-d] [-c]", argv[0]);
}
