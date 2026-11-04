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

/* function declarations */
static void run_autostart(void);
static void run(void);

/* variables */
Monitor* selected_monitor;
static char activation_token_tag; /* marks the tokens handed out below */

/* function implementations */
static void run_autostart(void)
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
            fprintf(stderr, "g0wm: execvp %s: ", *p);
            perror(nullptr);
            _exit(1);
        }
        /* skip arguments */
        while (*++p)
            ;
    }
}

void change_vt(const Arg* arg)
{
    wlr_session_change_vt(session, arg->ui);
}

void handle_signal(int signo)
{
    if (signo == SIGCHLD) {
        pid_t pid, *p, *lim;
        while ((pid = waitpid(-1, nullptr, WNOHANG)) > 0) {
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
        quit(nullptr);
    }
}

const char* activation_token_new(void)
{
    struct wlr_xdg_activation_token_v1* token =
        wlr_xdg_activation_token_v1_create(activation);

    if (!token)
        return nullptr;
    token->data = &activation_token_tag;
    return wlr_xdg_activation_token_v1_get_name(token);
}

/* a window activating with one of these was asked for by the user */
int activation_token_ours(const struct wlr_xdg_activation_token_v1* token)
{
    return token && token->data == &activation_token_tag;
}

void quit(const Arg*)
{
    wl_display_terminate(display);
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

void spawn(const Arg* arg)
{
    if (fork() == 0) {
        close(STDIN_FILENO);
        open("/dev/null", O_RDWR);
        dup2(STDERR_FILENO, STDOUT_FILENO);
        setsid();
        execvp(((char**)arg->v)[0], (char**)arg->v);
        fprintf(stderr, "g0wm: execvp %s failed: ", ((char**)arg->v)[0]);
        perror(nullptr);
        _exit(1);
    }
}

int main(int argc, char* argv[])
{
    int debug = 0;
    int c;

    while ((c = getopt(argc, argv, "hdvc")) != -1) {
        if (c == 'd')
            debug = 1;
        else if (c == 'v')
            util_die("g0wm " VERSION);
        else if (c == 'c')
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
        log_level = WLR_DEBUG;
    setup();
    run();
    cleanup();
    return EXIT_SUCCESS;

usage:
    util_die("Usage: %s [-v] [-d] [-c]", argv[0]);
}
