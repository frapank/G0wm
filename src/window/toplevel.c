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

#include "core/g0wm.h"
#include "output/privacy.h"
#include "window/client.h"
#include "window/toplevel.h"

/* function declarations */
static void toplevel_handle_request_activate(struct wl_listener* listener, void*);
static void toplevel_handle_request_close(struct wl_listener* listener, void*);
static void toplevel_handle_request_fullscreen(struct wl_listener* listener, void* data);

/* variables */
static Client* toplevel_active;

/* function implementations */
void toplevel_handle_new_capture_request(struct wl_listener*, void* data)
{
    struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request* req = data;
    Client* client = req->toplevel_handle->data;

    if (!client)
        return;
    if (!client->capture &&
        !(client->capture = wlr_ext_image_capture_source_v1_create_with_scene_node(
              &client->scene_surface->node, event_loop, allocator, renderer)))
        return;
    privacy_shield_client(client);
    wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(req, client->capture);
}

static void toplevel_handle_request_activate(struct wl_listener* listener, void*)
{
    Client* client = wl_container_of(listener, client, ftl_activate);
    client_activate(client);
}

static void toplevel_handle_request_close(struct wl_listener* listener, void*)
{
    Client* client = wl_container_of(listener, client, ftl_close);
    client_send_close(client);
}

static void toplevel_handle_request_fullscreen(struct wl_listener* listener, void* data)
{
    Client* client = wl_container_of(listener, client, ftl_fullscreen);
    struct wlr_foreign_toplevel_handle_v1_fullscreen_event* event = data;
    client_set_fullscreen(client, event->fullscreen);
}

void toplevel_focus(Client* client)
{
    if (client && client_is_unmanaged(client))
        return;
    if (client && !client->ftl)
        client = nullptr;
    if (client == toplevel_active)
        return;
    if (toplevel_active)
        wlr_foreign_toplevel_handle_v1_set_activated(toplevel_active->ftl, 0);
    if ((toplevel_active = client))
        wlr_foreign_toplevel_handle_v1_set_activated(client->ftl, 1);
}

void toplevel_map(Client* client)
{
    Client* parent = client_get_parent(client);

    client->ftl = wlr_foreign_toplevel_handle_v1_create(foreign_toplevel_manager);
    if (client->ftl) {
        client->ftl->data = client;
        LISTEN(&client->ftl->events.request_activate, &client->ftl_activate,
               toplevel_handle_request_activate);
        LISTEN(&client->ftl->events.request_close, &client->ftl_close,
               toplevel_handle_request_close);
        LISTEN(&client->ftl->events.request_fullscreen, &client->ftl_fullscreen,
               toplevel_handle_request_fullscreen);
        if (parent && parent->ftl)
            wlr_foreign_toplevel_handle_v1_set_parent(client->ftl, parent->ftl);
    }
    client->eftl = wlr_ext_foreign_toplevel_handle_v1_create(
        ext_toplevel_list,
        &(struct wlr_ext_foreign_toplevel_handle_v1_state){ .title = privacy_share_title(client),
                                                            .app_id = client_get_app_id(client) });
    if (client->eftl)
        client->eftl->data = client;

    toplevel_update(client);
    if (client_surface(client) == seat->keyboard_state.focused_surface)
        toplevel_focus(client);
}

void toplevel_unmap(Client* client)
{
    if (client == toplevel_active)
        toplevel_active = nullptr;
    if (client->ftl) {
        wl_list_remove(&client->ftl_activate.link);
        wl_list_remove(&client->ftl_close.link);
        wl_list_remove(&client->ftl_fullscreen.link);
        wlr_foreign_toplevel_handle_v1_destroy(client->ftl);
        client->ftl = nullptr;
    }
    if (client->eftl) {
        wlr_ext_foreign_toplevel_handle_v1_destroy(client->eftl);
        client->eftl = nullptr;
    }
    client->capture = nullptr; /* freed with the scene node */
}

void toplevel_update(Client* client)
{
    const char* title = privacy_share_title(client);
    const char* appid = client_get_app_id(client);
    struct wlr_output* monitor_output = client->mon ? client->mon->wlr_output : nullptr;
    int output_present = 0;

    if (!client->ftl && !client->eftl)
        return;
    if (client->eftl && (!client->eftl->title || strcmp(client->eftl->title, title) ||
                         !client->eftl->app_id || strcmp(client->eftl->app_id, appid)))
        wlr_ext_foreign_toplevel_handle_v1_update_state(
            client->eftl,
            &(struct wlr_ext_foreign_toplevel_handle_v1_state){ .title = title, .app_id = appid });
    if (!client->ftl)
        return;
    if (!client->ftl->title || strcmp(client->ftl->title, title))
        wlr_foreign_toplevel_handle_v1_set_title(client->ftl, title);
    if (!client->ftl->app_id || strcmp(client->ftl->app_id, appid))
        wlr_foreign_toplevel_handle_v1_set_app_id(client->ftl, appid);

    struct wlr_foreign_toplevel_handle_v1_output* handle_output;
    struct wlr_foreign_toplevel_handle_v1_output* tmp;
    wl_list_for_each_safe(handle_output, tmp, &client->ftl->outputs, link) {
        if (handle_output->output == monitor_output)
            output_present = 1;
        else
            wlr_foreign_toplevel_handle_v1_output_leave(client->ftl, handle_output->output);
    }
    if (monitor_output && !output_present)
        wlr_foreign_toplevel_handle_v1_output_enter(client->ftl, monitor_output);

    if (!(client->ftl->state & WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN) !=
        !client->isfullscreen)
        wlr_foreign_toplevel_handle_v1_set_fullscreen(client->ftl, client->isfullscreen);
}
