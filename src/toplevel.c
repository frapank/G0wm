/*
 * See LICENSE file for copyright and license details.
 *
 * foreign toplevel handles, for docks, taskbars and window capture
 */
#include "g0wm.h"

/* function declarations */
static void requestactivate(struct wl_listener* listener, void* data);
static void requestclose(struct wl_listener* listener, void* data);
static void requestfullscreen(struct wl_listener* listener, void* data);

/* variables */
static Client* activetop;

/* function implementations */
void capturetoplevel(struct wl_listener* listener, void* data)
{
    struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request*
        req = data;
    Client* c = req->toplevel_handle->data;

    if (!c)
        return;
    if (!c->capture &&
        !(c->capture = wlr_ext_image_capture_source_v1_create_with_scene_node(
              &c->scene_surface->node, event_loop, alloc, drw)))
        return;
    wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(
        req, c->capture);
}

static void requestactivate(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, ftl_activate);
    activateclient(c);
}

static void requestclose(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, ftl_close);
    client_send_close(c);
}

static void requestfullscreen(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, ftl_fullscreen);
    struct wlr_foreign_toplevel_handle_v1_fullscreen_event* event = data;
    setfullscreen(c, event->fullscreen);
}

void toplevelfocus(Client* c)
{
    if (c && client_is_unmanaged(c))
        return;
    if (c && !c->ftl)
        c = NULL;
    if (c == activetop)
        return;
    if (activetop)
        wlr_foreign_toplevel_handle_v1_set_activated(activetop->ftl, 0);
    if ((activetop = c))
        wlr_foreign_toplevel_handle_v1_set_activated(c->ftl, 1);
}

void toplevelmap(Client* c)
{
    Client* p = client_get_parent(c);

    c->ftl = wlr_foreign_toplevel_handle_v1_create(foreign_toplevel_mgr);
    if (c->ftl) {
        c->ftl->data = c;
        LISTEN(&c->ftl->events.request_activate,
               &c->ftl_activate,
               requestactivate);
        LISTEN(&c->ftl->events.request_close, &c->ftl_close, requestclose);
        LISTEN(&c->ftl->events.request_fullscreen,
               &c->ftl_fullscreen,
               requestfullscreen);
        if (p && p->ftl)
            wlr_foreign_toplevel_handle_v1_set_parent(c->ftl, p->ftl);
    }
    c->eftl = wlr_ext_foreign_toplevel_handle_v1_create(
        ext_toplevel_list,
        &(struct wlr_ext_foreign_toplevel_handle_v1_state){
            .title = client_get_title(c), .app_id = client_get_appid(c) });
    if (c->eftl)
        c->eftl->data = c;

    toplevelupdate(c);
    if (client_surface(c) == seat->keyboard_state.focused_surface)
        toplevelfocus(c);
}

void toplevelunmap(Client* c)
{
    if (c == activetop)
        activetop = NULL;
    if (c->ftl) {
        wl_list_remove(&c->ftl_activate.link);
        wl_list_remove(&c->ftl_close.link);
        wl_list_remove(&c->ftl_fullscreen.link);
        wlr_foreign_toplevel_handle_v1_destroy(c->ftl);
        c->ftl = NULL;
    }
    if (c->eftl) {
        wlr_ext_foreign_toplevel_handle_v1_destroy(c->eftl);
        c->eftl = NULL;
    }
    c->capture = NULL; /* freed with the scene node */
}

void toplevelupdate(Client* c)
{
    const char* title = client_get_title(c);
    const char* appid = client_get_appid(c);
    struct wlr_output* out = c->mon ? c->mon->wlr_output : NULL;
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
