/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * session lock
 */
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>

#include "core/g0wm.h"
#include "core/util.h"
#include "desktop/traypopup.h"
#include "input/pointer.h"
#include "output/lock.h"
#include "output/monitor.h"
#include "window/client.h"

typedef struct {
    struct wlr_scene_tree* scene;

    struct wlr_session_lock_v1* lock;
    struct wl_listener new_surface;
    struct wl_listener unlock;
    struct wl_listener destroy;
} SessionLock;

/* function declarations */
static void lock_handle_new_surface(struct wl_listener* listener, void* data);
static void lock_destroy(SessionLock* lock, int unlocked);
static void lock_handle_destroy(struct wl_listener* listener, void* data);
static void lock_handle_unlock(struct wl_listener* listener, void* data);

/* variables */
int lock_active;
static struct wlr_session_lock_v1* lock_current;

/* function implementations */
static void lock_handle_new_surface(struct wl_listener* listener, void* data)
{
    SessionLock* lock = wl_container_of(listener, lock, new_surface);
    struct wlr_session_lock_surface_v1* lock_surface = data;
    Monitor* m = lock_surface->output->data;
    struct wlr_scene_tree* scene_tree = lock_surface->surface->data =
        wlr_scene_subsurface_tree_create(lock->scene, lock_surface->surface);
    m->lock_surface = lock_surface;

    wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
    wlr_session_lock_surface_v1_configure(
        lock_surface, m->m.width, m->m.height);

    LISTEN(&lock_surface->events.destroy,
           &m->destroy_lock_surface,
           lock_handle_surface_destroy);

    if (m == selected_monitor)
        client_notify_enter(lock_surface->surface, wlr_seat_get_keyboard(seat));
}

static void lock_destroy(SessionLock* lock, int unlock)
{
    wlr_seat_keyboard_notify_clear_focus(seat);
    if ((lock_active = !unlock))
        goto destroy;

    wlr_scene_node_set_enabled(&locked_background->node, 0);

    client_focus(client_top(selected_monitor), 0);
    input_motion(0, nullptr, 0, 0, 0, 0);

destroy:
    wl_list_remove(&lock->new_surface.link);
    wl_list_remove(&lock->unlock.link);
    wl_list_remove(&lock->destroy.link);

    wlr_scene_node_destroy(&lock->scene->node);
    lock_current = nullptr;
    free(lock);
}

void lock_handle_surface_destroy(struct wl_listener* listener, void*)
{
    Monitor* m = wl_container_of(listener, m, destroy_lock_surface);
    struct wlr_session_lock_surface_v1 *surface,
        *lock_surface = m->lock_surface;

    m->lock_surface = nullptr;
    wl_list_remove(&m->destroy_lock_surface.link);

    if (lock_surface->surface != seat->keyboard_state.focused_surface)
        return;

    if (lock_active && lock_current &&
        !wl_list_empty(&lock_current->surfaces)) {
        surface = wl_container_of(lock_current->surfaces.next, surface, link);
        client_notify_enter(surface->surface, wlr_seat_get_keyboard(seat));
    } else if (!lock_active) {
        client_focus(client_top(selected_monitor), 1);
    } else {
        wlr_seat_keyboard_clear_focus(seat);
    }
}

static void lock_handle_destroy(struct wl_listener* listener, void*)
{
    SessionLock* lock = wl_container_of(listener, lock, destroy);
    lock_destroy(lock, 0);
}

void lock_handle_new_lock(struct wl_listener*, void* data)
{
    struct wlr_session_lock_v1* session_lock = data;
    SessionLock* lock;
    wlr_scene_node_set_enabled(&locked_background->node, 1);
    if (lock_current) {
        wlr_session_lock_v1_destroy(session_lock);
        return;
    }
    lock = session_lock->data = util_calloc(1, sizeof(*lock));
    client_focus(nullptr, 0);
    /* an open menu would keep taking clicks under the lock surface */
    traypopup_dismiss();

    lock->scene = wlr_scene_tree_create(layers[LyrBlock]);
    lock_current = lock->lock = session_lock;
    lock_active = 1;

    LISTEN(&session_lock->events.new_surface,
           &lock->new_surface,
           lock_handle_new_surface);
    LISTEN(&session_lock->events.destroy, &lock->destroy, lock_handle_destroy);
    LISTEN(&session_lock->events.unlock, &lock->unlock, lock_handle_unlock);

    wlr_session_lock_v1_send_locked(session_lock);
}

static void lock_handle_unlock(struct wl_listener* listener, void*)
{
    SessionLock* lock = wl_container_of(listener, lock, unlock);
    lock_destroy(lock, 1);
}
