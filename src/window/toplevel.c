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
static WindowClient* toplevel_active;

/* function implementations */
void toplevel_handle_new_capture_request(struct wl_listener*, void* data)
{
    struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request* request = data;
    WindowClient* client = request->toplevel_handle->data;

    if (!client)
        return;
    if (!client->capture_source &&
        !(client->capture_source = wlr_ext_image_capture_source_v1_create_with_scene_node(
              &client->scene_surface->node, event_loop, allocator, renderer)))
        return;
    privacy_shield_client(client);
    wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(request,
                                                                            client->capture_source);
}

static void toplevel_handle_request_activate(struct wl_listener* listener, void*)
{
    WindowClient* client = wl_container_of(listener, client, foreign_toplevel_activate);
    client_activate(client);
}

static void toplevel_handle_request_close(struct wl_listener* listener, void*)
{
    WindowClient* client = wl_container_of(listener, client, foreign_toplevel_close);
    client_send_close(client);
}

static void toplevel_handle_request_fullscreen(struct wl_listener* listener, void* data)
{
    WindowClient* client = wl_container_of(listener, client, foreign_toplevel_fullscreen);
    struct wlr_foreign_toplevel_handle_v1_fullscreen_event* event = data;
    client_set_fullscreen(client, event->fullscreen);
}

void toplevel_focus(WindowClient* client)
{
    if (client && client_is_unmanaged(client))
        return;
    if (client && !client->foreign_toplevel)
        client = nullptr;
    if (client == toplevel_active)
        return;
    if (toplevel_active)
        wlr_foreign_toplevel_handle_v1_set_activated(toplevel_active->foreign_toplevel, 0);
    if ((toplevel_active = client))
        wlr_foreign_toplevel_handle_v1_set_activated(client->foreign_toplevel, 1);
}

void toplevel_map(WindowClient* client)
{
    WindowClient* parent = client_get_parent(client);

    client->foreign_toplevel = wlr_foreign_toplevel_handle_v1_create(foreign_toplevel_manager);
    if (client->foreign_toplevel) {
        client->foreign_toplevel->data = client;
        LISTEN(&client->foreign_toplevel->events.request_activate,
               &client->foreign_toplevel_activate, toplevel_handle_request_activate);
        LISTEN(&client->foreign_toplevel->events.request_close, &client->foreign_toplevel_close,
               toplevel_handle_request_close);
        LISTEN(&client->foreign_toplevel->events.request_fullscreen,
               &client->foreign_toplevel_fullscreen, toplevel_handle_request_fullscreen);
        if (parent && parent->foreign_toplevel)
            wlr_foreign_toplevel_handle_v1_set_parent(client->foreign_toplevel,
                                                      parent->foreign_toplevel);
    }
    client->ext_foreign_toplevel = wlr_ext_foreign_toplevel_handle_v1_create(
        ext_toplevel_list,
        &(struct wlr_ext_foreign_toplevel_handle_v1_state){ .title = privacy_share_title(client),
                                                            .app_id = client_get_app_id(client) });
    if (client->ext_foreign_toplevel)
        client->ext_foreign_toplevel->data = client;

    toplevel_update(client);
    if (client_surface(client) == seat->keyboard_state.focused_surface)
        toplevel_focus(client);
}

void toplevel_unmap(WindowClient* client)
{
    if (client == toplevel_active)
        toplevel_active = nullptr;
    if (client->foreign_toplevel) {
        wl_list_remove(&client->foreign_toplevel_activate.link);
        wl_list_remove(&client->foreign_toplevel_close.link);
        wl_list_remove(&client->foreign_toplevel_fullscreen.link);
        wlr_foreign_toplevel_handle_v1_destroy(client->foreign_toplevel);
        client->foreign_toplevel = nullptr;
    }
    if (client->ext_foreign_toplevel) {
        wlr_ext_foreign_toplevel_handle_v1_destroy(client->ext_foreign_toplevel);
        client->ext_foreign_toplevel = nullptr;
    }
    client->capture_source = nullptr; /* freed with the scene node */
}

void toplevel_update(WindowClient* client)
{
    const char* title = privacy_share_title(client);
    const char* app_id = client_get_app_id(client);
    struct wlr_output* monitor_output = client->monitor ? client->monitor->wlr_output : nullptr;
    int output_present = 0;

    if (!client->foreign_toplevel && !client->ext_foreign_toplevel)
        return;
    if (client->ext_foreign_toplevel && (!client->ext_foreign_toplevel->title ||
                                         strcmp(client->ext_foreign_toplevel->title, title) ||
                                         !client->ext_foreign_toplevel->app_id ||
                                         strcmp(client->ext_foreign_toplevel->app_id, app_id)))
        wlr_ext_foreign_toplevel_handle_v1_update_state(
            client->ext_foreign_toplevel,
            &(struct wlr_ext_foreign_toplevel_handle_v1_state){ .title = title, .app_id = app_id });
    if (!client->foreign_toplevel)
        return;
    if (!client->foreign_toplevel->title || strcmp(client->foreign_toplevel->title, title))
        wlr_foreign_toplevel_handle_v1_set_title(client->foreign_toplevel, title);
    if (!client->foreign_toplevel->app_id || strcmp(client->foreign_toplevel->app_id, app_id))
        wlr_foreign_toplevel_handle_v1_set_app_id(client->foreign_toplevel, app_id);

    struct wlr_foreign_toplevel_handle_v1_output* handle_output;
    struct wlr_foreign_toplevel_handle_v1_output* tmp;
    wl_list_for_each_safe(handle_output, tmp, &client->foreign_toplevel->outputs, link) {
        if (handle_output->output == monitor_output)
            output_present = 1;
        else
            wlr_foreign_toplevel_handle_v1_output_leave(client->foreign_toplevel,
                                                        handle_output->output);
    }
    if (monitor_output && !output_present)
        wlr_foreign_toplevel_handle_v1_output_enter(client->foreign_toplevel, monitor_output);

    if (!(client->foreign_toplevel->state & WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN) !=
        !client->is_fullscreen)
        wlr_foreign_toplevel_handle_v1_set_fullscreen(client->foreign_toplevel,
                                                      client->is_fullscreen);
}
