/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * foreign toplevel handles, for docks, taskbars and window capture
 */
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_output.h>

#include "client.h"
#include "g0wm.h"
#include "privacy.h"
#include "toplevel.h"

/* function declarations */
static void toplevel_handle_request_activate(struct wl_listener* listener,
                                             void* data);
static void toplevel_handle_request_close(struct wl_listener* listener,
                                          void* data);
static void toplevel_handle_request_fullscreen(struct wl_listener* listener,
                                               void* data);

/* variables */
static Client* toplevel_active;

/* function implementations */
void toplevel_handle_new_capture_request(struct wl_listener*, void* data)
{
    struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request*
        req = data;
    Client* c = req->toplevel_handle->data;

    if (!c)
        return;
    if (!c->capture &&
        !(c->capture = wlr_ext_image_capture_source_v1_create_with_scene_node(
              &c->scene_surface->node, event_loop, allocator, renderer)))
        return;
    privacy_shield_client(c);
    wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(
        req, c->capture);
}

static void toplevel_handle_request_activate(struct wl_listener* listener,
                                             void*)
{
    Client* c = wl_container_of(listener, c, ftl_activate);
    client_activate(c);
}

static void toplevel_handle_request_close(struct wl_listener* listener, void*)
{
    Client* c = wl_container_of(listener, c, ftl_close);
    client_send_close(c);
}

static void toplevel_handle_request_fullscreen(struct wl_listener* listener,
                                               void* data)
{
    Client* c = wl_container_of(listener, c, ftl_fullscreen);
    struct wlr_foreign_toplevel_handle_v1_fullscreen_event* event = data;
    client_set_fullscreen(c, event->fullscreen);
}

void toplevel_focus(Client* c)
{
    if (c && client_is_unmanaged(c))
        return;
    if (c && !c->ftl)
        c = nullptr;
    if (c == toplevel_active)
        return;
    if (toplevel_active)
        wlr_foreign_toplevel_handle_v1_set_activated(toplevel_active->ftl, 0);
    if ((toplevel_active = c))
        wlr_foreign_toplevel_handle_v1_set_activated(c->ftl, 1);
}

void toplevel_map(Client* c)
{
    Client* p = client_get_parent(c);

    c->ftl = wlr_foreign_toplevel_handle_v1_create(foreign_toplevel_manager);
    if (c->ftl) {
        c->ftl->data = c;
        LISTEN(&c->ftl->events.request_activate,
               &c->ftl_activate,
               toplevel_handle_request_activate);
        LISTEN(&c->ftl->events.request_close,
               &c->ftl_close,
               toplevel_handle_request_close);
        LISTEN(&c->ftl->events.request_fullscreen,
               &c->ftl_fullscreen,
               toplevel_handle_request_fullscreen);
        if (p && p->ftl)
            wlr_foreign_toplevel_handle_v1_set_parent(c->ftl, p->ftl);
    }
    c->eftl = wlr_ext_foreign_toplevel_handle_v1_create(
        ext_toplevel_list,
        &(struct wlr_ext_foreign_toplevel_handle_v1_state){
            .title = privacy_share_title(c), .app_id = client_get_app_id(c) });
    if (c->eftl)
        c->eftl->data = c;

    toplevel_update(c);
    if (client_surface(c) == seat->keyboard_state.focused_surface)
        toplevel_focus(c);
}

void toplevel_unmap(Client* c)
{
    if (c == toplevel_active)
        toplevel_active = nullptr;
    if (c->ftl) {
        wl_list_remove(&c->ftl_activate.link);
        wl_list_remove(&c->ftl_close.link);
        wl_list_remove(&c->ftl_fullscreen.link);
        wlr_foreign_toplevel_handle_v1_destroy(c->ftl);
        c->ftl = nullptr;
    }
    if (c->eftl) {
        wlr_ext_foreign_toplevel_handle_v1_destroy(c->eftl);
        c->eftl = nullptr;
    }
    c->capture = nullptr; /* freed with the scene node */
}

void toplevel_update(Client* c)
{
    const char* title = privacy_share_title(c);
    const char* appid = client_get_app_id(c);
    struct wlr_output* out = c->mon ? c->mon->wlr_output : nullptr;
    struct wlr_foreign_toplevel_handle_v1_output *o, *tmp;
    int on = 0;

    if (!c->ftl && !c->eftl)
        return;
    if (c->eftl && (!c->eftl->title || strcmp(c->eftl->title, title) ||
                    !c->eftl->app_id || strcmp(c->eftl->app_id, appid)))
        wlr_ext_foreign_toplevel_handle_v1_update_state(
            c->eftl,
            &(struct wlr_ext_foreign_toplevel_handle_v1_state){
                .title = title, .app_id = appid });
    if (!c->ftl)
        return;
    if (!c->ftl->title || strcmp(c->ftl->title, title))
        wlr_foreign_toplevel_handle_v1_set_title(c->ftl, title);
    if (!c->ftl->app_id || strcmp(c->ftl->app_id, appid))
        wlr_foreign_toplevel_handle_v1_set_app_id(c->ftl, appid);

    wl_list_for_each_safe(o, tmp, &c->ftl->outputs, link)
    {
        if (o->output == out)
            on = 1;
        else
            wlr_foreign_toplevel_handle_v1_output_leave(c->ftl, o->output);
    }
    if (out && !on)
        wlr_foreign_toplevel_handle_v1_output_enter(c->ftl, out);

    if (!(c->ftl->state & WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN) !=
        !c->isfullscreen)
        wlr_foreign_toplevel_handle_v1_set_fullscreen(c->ftl, c->isfullscreen);
}
