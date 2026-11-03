/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * text-input-v3 and input-method-v2: the bridge between apps and an input
 * method, with its keyboard grab and candidate popup
 */
#include <linux/input-event-codes.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/util/box.h>

#include "g0wm.h"
#include "input.h"
#include "lock.h"
#include "monitor.h"
#include "textinput.h"
#include "util.h"

typedef struct {
    struct wlr_text_input_v3* wlr;
    struct wl_list link; /* textinput_list */
    struct wl_listener enable;
    struct wl_listener commit;
    struct wl_listener disable;
    struct wl_listener destroy;
} TextInput;

typedef struct {
    struct wlr_input_popup_surface_v2* popup;
    struct wlr_scene_tree* tree;
    /* last rectangle sent, an unchanged one makes the IM recommit forever;
     * negative width means none yet */
    struct wlr_box sent;
    struct wl_list link; /* popups */
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
} InputPopup;

typedef struct {
    struct wlr_surface* surface;
    int x, y, found;
} SurfaceFind;

/* function declarations */
static void textinput_find_surface(struct wlr_scene_buffer* buffer,
                                   int sx,
                                   int sy,
                                   void* data);
static void textinput_activate(TextInput* ti);
static void textinput_handle_im_commit(struct wl_listener* listener,
                                       void* data);
static void textinput_deactivate(void);
static void textinput_handle_im_destroy(struct wl_listener* listener,
                                        void* data);
static void textinput_handle_grab_destroy(struct wl_listener* listener,
                                          void* data);
static void textinput_handle_grab_keyboard(struct wl_listener* listener,
                                           void* data);
static void textinput_handle_new_popup(struct wl_listener* listener,
                                       void* data);
static void textinput_send_state(TextInput* ti);
static struct wlr_input_method_keyboard_grab_v2* textinput_keyboard_grab(
    KeyboardGroup* g);
static void textinput_handle_popup_commit(struct wl_listener* listener,
                                          void* data);
static void textinput_handle_popup_destroy(struct wl_listener* listener,
                                           void* data);
static void textinput_handle_popup_map(struct wl_listener* listener,
                                       void* data);
static void textinput_popup_place(InputPopup* p);
static void textinput_popups_update(void);
static void textinput_handle_popup_unmap(struct wl_listener* listener,
                                         void* data);
static int textinput_surface_coords(struct wlr_surface* surface,
                                    int* x,
                                    int* y);
static void textinput_handle_commit(struct wl_listener* listener, void* data);
static void textinput_handle_destroy(struct wl_listener* listener, void* data);
static void textinput_handle_disable(struct wl_listener* listener, void* data);
static void textinput_handle_enable(struct wl_listener* listener, void* data);
static void textinput_sync(struct wlr_surface* focus);
static void textinput_update(TextInput* ti);

/* variables */
static struct wlr_input_method_v2* textinput_im;
static TextInput*
    textinput_served; /* the text input the input method is active for */
static struct wl_list textinput_list = { &textinput_list, &textinput_list };
static struct wl_list textinput_popups = { &textinput_popups,
                                           &textinput_popups };
static struct wl_listener textinput_im_commit = {
    .notify = textinput_handle_im_commit
};
static struct wl_listener textinput_im_destroy = {
    .notify = textinput_handle_im_destroy
};
static struct wl_listener textinput_im_grab = {
    .notify = textinput_handle_grab_keyboard
};
static struct wl_listener textinput_im_grab_destroy = {
    .notify = textinput_handle_grab_destroy
};
static struct wl_listener textinput_im_popup = {
    .notify = textinput_handle_new_popup
};

/* function implementations */

static void textinput_find_surface(struct wlr_scene_buffer* buffer,
                                   int sx,
                                   int sy,
                                   void* data)
{
    SurfaceFind* f = data;
    struct wlr_scene_surface* ss = wlr_scene_surface_try_from_buffer(buffer);

    if (f->found || !ss || ss->surface != f->surface)
        return;
    f->x = sx;
    f->y = sy;
    f->found = 1;
}

static int textinput_surface_coords(struct wlr_surface* surface, int* x, int* y)
{
    SurfaceFind f = { .surface = surface };

    wlr_scene_node_for_each_buffer(
        &scene->tree.node, textinput_find_surface, &f);
    *x = f.x;
    *y = f.y;
    return f.found;
}

static void textinput_popup_place(InputPopup* p)
{
    struct wlr_surface* focus =
        textinput_served ? textinput_served->wlr->focused_surface : nullptr;
    struct wlr_box rect = {};
    struct wlr_box box;
    Monitor* m;
    int sx, sy, w, h, x, y;

    if (!focus || !p->popup->surface->mapped ||
        !textinput_surface_coords(focus, &sx, &sy)) {
        wlr_scene_node_set_enabled(&p->tree->node, 0);
        p->sent.width = -1;
        return;
    }
    if (textinput_served->wlr->active_features &
        WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE)
        rect = textinput_served->wlr->current.cursor_rectangle;
    else
        rect.width = focus->current.width;
    rect.x += sx;
    rect.y += sy;

    if (!(m = monitor_at(rect.x, rect.y)))
        m = selected_monitor;
    if (!m) {
        wlr_scene_node_set_enabled(&p->tree->node, 0);
        p->sent.width = -1;
        return;
    }

    w = p->popup->surface->current.width;
    h = p->popup->surface->current.height;
    x = rect.x;
    y = rect.y + rect.height;
    if (x + w > m->m.x + m->m.width &&
        rect.x + rect.width - m->m.x > m->m.x + m->m.width - rect.x)
        x = rect.x + rect.width - w;
    if (y + h > m->m.y + m->m.height &&
        rect.y - m->m.y > m->m.y + m->m.height - (rect.y + rect.height))
        y = rect.y - h;
    x = MAX(m->m.x, MIN(x, m->m.x + m->m.width - w));
    y = MAX(m->m.y, MIN(y, m->m.y + m->m.height - h));

    wlr_scene_node_set_position(&p->tree->node, x, y);
    wlr_scene_node_raise_to_top(&p->tree->node);
    wlr_scene_node_set_enabled(&p->tree->node, 1);

    box = (struct wlr_box){ rect.x - x, rect.y - y, rect.width, rect.height };
    if (!wlr_box_equal(&box, &p->sent)) {
        p->sent = box;
        wlr_input_popup_surface_v2_send_text_input_rectangle(p->popup, &box);
    }
}

static void textinput_popups_update(void)
{
    InputPopup* p;

    wl_list_for_each(p, &textinput_popups, link) textinput_popup_place(p);
}

static void textinput_handle_popup_map(struct wl_listener* listener, void*)
{
    InputPopup* p = wl_container_of(listener, p, map);

    textinput_popup_place(p);
}

static void textinput_handle_popup_unmap(struct wl_listener* listener, void*)
{
    InputPopup* p = wl_container_of(listener, p, unmap);

    textinput_popup_place(p);
}

static void textinput_handle_popup_commit(struct wl_listener* listener, void*)
{
    InputPopup* p = wl_container_of(listener, p, commit);

    textinput_popup_place(p);
}

static void textinput_handle_popup_destroy(struct wl_listener* listener, void*)
{
    InputPopup* p = wl_container_of(listener, p, destroy);

    wl_list_remove(&p->map.link);
    wl_list_remove(&p->unmap.link);
    wl_list_remove(&p->commit.link);
    wl_list_remove(&p->destroy.link);
    wl_list_remove(&p->link);
    wlr_scene_node_destroy(&p->tree->node);
    free(p);
}

static void textinput_handle_new_popup(struct wl_listener*, void* data)
{
    struct wlr_input_popup_surface_v2* s = data;
    InputPopup* p = util_calloc(1, sizeof(*p));

    p->popup = s;
    p->tree = wlr_scene_tree_create(layers[LyrOverlay]);
    wlr_scene_subsurface_tree_create(p->tree, s->surface);
    wlr_scene_node_set_enabled(&p->tree->node, 0);
    p->sent.width = -1;
    LISTEN(&s->surface->events.map, &p->map, textinput_handle_popup_map);
    LISTEN(&s->surface->events.unmap, &p->unmap, textinput_handle_popup_unmap);
    LISTEN(
        &s->surface->events.commit, &p->commit, textinput_handle_popup_commit);
    LISTEN(&s->events.destroy, &p->destroy, textinput_handle_popup_destroy);
    wl_list_insert(&textinput_popups, &p->link);
    textinput_popup_place(p);
}

static void textinput_send_state(TextInput* ti)
{
    struct wlr_text_input_v3* t = ti->wlr;

    if (t->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT)
        wlr_input_method_v2_send_surrounding_text(
            textinput_im,
            t->current.surrounding.text ? t->current.surrounding.text : "",
            t->current.surrounding.cursor,
            t->current.surrounding.anchor);
    wlr_input_method_v2_send_text_change_cause(textinput_im,
                                               t->current.text_change_cause);
    if (t->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE)
        wlr_input_method_v2_send_content_type(textinput_im,
                                              t->current.content_type.hint,
                                              t->current.content_type.purpose);
    textinput_popups_update();
    wlr_input_method_v2_send_done(textinput_im);
}

static void textinput_deactivate(void)
{
    if (!textinput_served)
        return;
    textinput_served = nullptr;
    if (textinput_im) {
        wlr_input_method_v2_send_deactivate(textinput_im);
        wlr_input_method_v2_send_done(textinput_im);
    }
    textinput_popups_update();
}

static void textinput_activate(TextInput* ti)
{
    if (textinput_served)
        textinput_deactivate();
    textinput_served = ti;
    wlr_input_method_v2_send_activate(textinput_im);
    textinput_send_state(ti);
}

static void textinput_update(TextInput* ti)
{
    if (!textinput_im || !ti->wlr->focused_surface || !ti->wlr->current_enabled)
        return;
    if (ti != textinput_served)
        textinput_activate(ti);
    else
        textinput_send_state(ti);
}

static void textinput_handle_enable(struct wl_listener* listener, void*)
{
    TextInput* ti = wl_container_of(listener, ti, enable);

    textinput_update(ti);
}

static void textinput_handle_commit(struct wl_listener* listener, void*)
{
    TextInput* ti = wl_container_of(listener, ti, commit);

    textinput_update(ti);
}

static void textinput_handle_disable(struct wl_listener* listener, void*)
{
    TextInput* ti = wl_container_of(listener, ti, disable);

    if (ti == textinput_served)
        textinput_deactivate();
}

static void textinput_handle_destroy(struct wl_listener* listener, void*)
{
    TextInput* ti = wl_container_of(listener, ti, destroy);

    if (ti == textinput_served)
        textinput_deactivate();
    wl_list_remove(&ti->enable.link);
    wl_list_remove(&ti->commit.link);
    wl_list_remove(&ti->disable.link);
    wl_list_remove(&ti->destroy.link);
    wl_list_remove(&ti->link);
    free(ti);
}

/* text inputs follow the keyboard focus while an input method is connected,
 * and only enter a surface of their own client */
static void textinput_sync(struct wlr_surface* focus)
{
    struct wlr_surface* surface = textinput_im ? focus : nullptr;
    TextInput* ti;

    if (textinput_served && (!textinput_served->wlr->focused_surface ||
                             textinput_served->wlr->focused_surface != surface))
        textinput_deactivate();
    wl_list_for_each(ti, &textinput_list, link)
    {
        if (ti->wlr->focused_surface == surface)
            continue;
        if (ti->wlr->focused_surface)
            wlr_text_input_v3_send_leave(ti->wlr);
        if (surface && wl_resource_get_client(ti->wlr->resource) ==
                           wl_resource_get_client(surface->resource))
            wlr_text_input_v3_send_enter(ti->wlr, surface);
    }
    textinput_popups_update();
}

static void textinput_handle_im_commit(struct wl_listener*, void*)
{
    if (!textinput_served || !textinput_served->wlr->focused_surface)
        return;
    if (textinput_im->current.preedit.text)
        wlr_text_input_v3_send_preedit_string(
            textinput_served->wlr,
            textinput_im->current.preedit.text,
            textinput_im->current.preedit.cursor_begin,
            textinput_im->current.preedit.cursor_end);
    if (textinput_im->current.commit_text)
        wlr_text_input_v3_send_commit_string(textinput_served->wlr,
                                             textinput_im->current.commit_text);
    if (textinput_im->current.delete.before_length ||
        textinput_im->current.delete.after_length)
        wlr_text_input_v3_send_delete_surrounding_text(
            textinput_served->wlr,
            textinput_im->current.delete.before_length,
            textinput_im->current.delete.after_length);
    wlr_text_input_v3_send_done(textinput_served->wlr);
}

static void textinput_handle_grab_keyboard(struct wl_listener*, void* data)
{
    struct wlr_input_method_keyboard_grab_v2* grab = data;
    struct wlr_keyboard* kb =
        keyboard_group ? &keyboard_group->wlr_group->keyboard : nullptr;

    wlr_input_method_keyboard_grab_v2_set_keyboard(grab, kb);
    if (kb)
        wlr_input_method_keyboard_grab_v2_send_modifiers(grab, &kb->modifiers);
    wl_signal_add(&grab->events.destroy, &textinput_im_grab_destroy);
}

/* resend the real modifiers, the grab may have changed them */
static void textinput_handle_grab_destroy(struct wl_listener*, void*)
{
    struct wlr_keyboard* kb = wlr_seat_get_keyboard(seat);

    wl_list_remove(&textinput_im_grab_destroy.link);
    if (kb)
        wlr_seat_keyboard_notify_modifiers(seat, &kb->modifiers);
}

static void textinput_handle_im_destroy(struct wl_listener*, void*)
{
    wl_list_remove(&textinput_im_commit.link);
    wl_list_remove(&textinput_im_destroy.link);
    wl_list_remove(&textinput_im_grab.link);
    wl_list_remove(&textinput_im_popup.link);
    textinput_im = nullptr;
    textinput_served = nullptr;
    textinput_sync(nullptr);
}

void textinput_handle_new_input_method(struct wl_listener*, void* data)
{
    if (textinput_im) {
        wlr_input_method_v2_send_unavailable(data);
        return;
    }
    textinput_im = data;
    wl_signal_add(&textinput_im->events.commit, &textinput_im_commit);
    wl_signal_add(&textinput_im->events.grab_keyboard, &textinput_im_grab);
    wl_signal_add(&textinput_im->events.new_popup_surface, &textinput_im_popup);
    wl_signal_add(&textinput_im->events.destroy, &textinput_im_destroy);
    textinput_sync(seat->keyboard_state.focused_surface);
}

void textinput_handle_new_text_input(struct wl_listener*, void* data)
{
    struct wlr_text_input_v3* wlr = data;
    TextInput* ti;

    if (wlr->seat != seat)
        return;
    ti = util_calloc(1, sizeof(*ti));
    ti->wlr = wlr;
    LISTEN(&wlr->events.enable, &ti->enable, textinput_handle_enable);
    LISTEN(&wlr->events.commit, &ti->commit, textinput_handle_commit);
    LISTEN(&wlr->events.disable, &ti->disable, textinput_handle_disable);
    LISTEN(&wlr->events.destroy, &ti->destroy, textinput_handle_destroy);
    wl_list_insert(&textinput_list, &ti->link);
    textinput_sync(seat->keyboard_state.focused_surface);
}

/* A window moved or resized: the popup follows the text cursor with it. */
void textinput_arrange(void)
{
    textinput_popups_update();
}

void textinput_handle_focus_change(struct wl_listener*, void* data)
{
    struct wlr_seat_keyboard_focus_change_event* event = data;

    textinput_sync(event->new_surface);
}

static struct wlr_input_method_keyboard_grab_v2* textinput_keyboard_grab(
    KeyboardGroup* g)
{
    struct wlr_input_method_keyboard_grab_v2* grab;

    if (lock_active || !textinput_im || !(grab = textinput_im->keyboard_grab))
        return nullptr;
    /* the input method types through its own virtual keyboard */
    if (g->vkb && wl_resource_get_client(g->vkb->resource) ==
                      wl_resource_get_client(grab->resource))
        return nullptr;
    return grab;
}

/* Returns 1 if the input method took the key. A release goes where its press
 * went, so neither side is left with a key that stays down. */
int textinput_key(KeyboardGroup* g, struct wlr_keyboard_key_event* event)
{
    struct wlr_input_method_keyboard_grab_v2* grab = textinput_keyboard_grab(g);
    uint32_t* word;
    uint32_t bit;

    if (event->keycode >= KEY_CNT)
        return 0;
    word = &g->imkeys[event->keycode / 32];
    bit = 1u << (event->keycode % 32);
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        if (!grab)
            return 0;
        *word |= bit;
    } else {
        if (!(*word & bit))
            return 0;
        *word &= ~bit;
        if (!grab)
            return 1;
    }
    wlr_input_method_keyboard_grab_v2_set_keyboard(grab,
                                                   &g->wlr_group->keyboard);
    wlr_input_method_keyboard_grab_v2_send_key(
        grab, event->time_msec, event->keycode, event->state);
    return 1;
}

int textinput_modifiers(KeyboardGroup* g)
{
    struct wlr_input_method_keyboard_grab_v2* grab = textinput_keyboard_grab(g);

    if (!grab)
        return 0;
    wlr_input_method_keyboard_grab_v2_set_keyboard(grab,
                                                   &g->wlr_group->keyboard);
    wlr_input_method_keyboard_grab_v2_send_modifiers(
        grab, &g->wlr_group->keyboard.modifiers);
    return 1;
}
