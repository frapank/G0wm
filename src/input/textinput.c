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

#include "core/g0wm.h"
#include "core/util.h"
#include "input/keyboard.h"
#include "input/textinput.h"
#include "output/lock.h"
#include "output/monitor.h"

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
                                   int surface_x,
                                   int surface_y,
                                   void* data);
static void textinput_activate(TextInput* text_input);
static void textinput_handle_im_commit(struct wl_listener*, void*);
static void textinput_deactivate(void);
static void textinput_handle_im_destroy(struct wl_listener*, void*);
static void textinput_handle_grab_destroy(struct wl_listener*, void*);
static void textinput_handle_grab_keyboard(struct wl_listener*, void* data);
static void textinput_handle_new_popup(struct wl_listener*, void* data);
static void textinput_send_state(TextInput* text_input);
static struct wlr_input_method_keyboard_grab_v2* textinput_keyboard_grab(KeyboardGroup* group);
static void textinput_handle_popup_commit(struct wl_listener* listener, void*);
static void textinput_handle_popup_destroy(struct wl_listener* listener, void*);
static void textinput_handle_popup_map(struct wl_listener* listener, void*);
static void textinput_popup_place(InputPopup* popup);
static void textinput_popups_update(void);
static void textinput_handle_popup_unmap(struct wl_listener* listener, void*);
static int textinput_surface_coords(struct wlr_surface* surface, int* x, int* y);
static void textinput_handle_commit(struct wl_listener* listener, void*);
static void textinput_handle_destroy(struct wl_listener* listener, void*);
static void textinput_handle_disable(struct wl_listener* listener, void*);
static void textinput_handle_enable(struct wl_listener* listener, void*);
static void textinput_sync(struct wlr_surface* focus);
static void textinput_update(TextInput* text_input);

/* variables */
static struct wlr_input_method_v2* textinput_im;
static TextInput* textinput_served; /* the text input the input method is active for */
static struct wl_list textinput_list = { &textinput_list, &textinput_list };
static struct wl_list textinput_popups = { &textinput_popups, &textinput_popups };
static struct wl_listener textinput_im_commit = { .notify = textinput_handle_im_commit };
static struct wl_listener textinput_im_destroy = { .notify = textinput_handle_im_destroy };
static struct wl_listener textinput_im_grab = { .notify = textinput_handle_grab_keyboard };
static struct wl_listener textinput_im_grab_destroy = { .notify = textinput_handle_grab_destroy };
static struct wl_listener textinput_im_popup = { .notify = textinput_handle_new_popup };

/* function implementations */

static void textinput_find_surface(struct wlr_scene_buffer* buffer,
                                   int surface_x,
                                   int surface_y,
                                   void* data)
{
    SurfaceFind* find = data;
    struct wlr_scene_surface* scene_surface = wlr_scene_surface_try_from_buffer(buffer);

    if (find->found || !scene_surface || scene_surface->surface != find->surface)
        return;
    find->x = surface_x;
    find->y = surface_y;
    find->found = 1;
}

static int textinput_surface_coords(struct wlr_surface* surface, int* x, int* y)
{
    SurfaceFind find = { .surface = surface };

    wlr_scene_node_for_each_buffer(&scene->tree.node, textinput_find_surface, &find)
        ;
    *x = find.x;
    *y = find.y;
    return find.found;
}

static void textinput_popup_place(InputPopup* popup)
{
    struct wlr_surface* focus = textinput_served ? textinput_served->wlr->focused_surface : nullptr;
    struct wlr_box rect = {};
    int surface_x;
    int surface_y;

    if (!focus || !popup->popup->surface->mapped ||
        !textinput_surface_coords(focus, &surface_x, &surface_y)) {
        wlr_scene_node_set_enabled(&popup->tree->node, 0);
        popup->sent.width = -1;
        return;
    }
    if (textinput_served->wlr->active_features & WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE)
        rect = textinput_served->wlr->current.cursor_rectangle;
    else
        rect.width = focus->current.width;
    rect.x += surface_x;
    rect.y += surface_y;

    Monitor* monitor = monitor_at(rect.x, rect.y);
    if (!monitor)
        monitor = selected_monitor;
    if (!monitor) {
        wlr_scene_node_set_enabled(&popup->tree->node, 0);
        popup->sent.width = -1;
        return;
    }

    int popup_width = popup->popup->surface->current.width;
    int popup_height = popup->popup->surface->current.height;
    int x = rect.x;
    int y = rect.y + rect.height;
    if (x + popup_width > monitor->m.x + monitor->m.width &&
        rect.x + rect.width - monitor->m.x > monitor->m.x + monitor->m.width - rect.x)
        x = rect.x + rect.width - popup_width;
    if (y + popup_height > monitor->m.y + monitor->m.height &&
        rect.y - monitor->m.y > monitor->m.y + monitor->m.height - (rect.y + rect.height))
        y = rect.y - popup_height;
    x = MAX(monitor->m.x, MIN(x, monitor->m.x + monitor->m.width - popup_width));
    y = MAX(monitor->m.y, MIN(y, monitor->m.y + monitor->m.height - popup_height));

    wlr_scene_node_set_position(&popup->tree->node, x, y);
    wlr_scene_node_raise_to_top(&popup->tree->node);
    wlr_scene_node_set_enabled(&popup->tree->node, 1);

    struct wlr_box box = (struct wlr_box){ rect.x - x, rect.y - y, rect.width, rect.height };
    if (!wlr_box_equal(&box, &popup->sent)) {
        popup->sent = box;
        wlr_input_popup_surface_v2_send_text_input_rectangle(popup->popup, &box);
    }
}

static void textinput_popups_update(void)
{
    InputPopup* popup;

    wl_list_for_each(popup, &textinput_popups, link)
        textinput_popup_place(popup);
}

static void textinput_handle_popup_map(struct wl_listener* listener, void*)
{
    InputPopup* popup = wl_container_of(listener, popup, map);

    textinput_popup_place(popup);
}

static void textinput_handle_popup_unmap(struct wl_listener* listener, void*)
{
    InputPopup* popup = wl_container_of(listener, popup, unmap);

    textinput_popup_place(popup);
}

static void textinput_handle_popup_commit(struct wl_listener* listener, void*)
{
    InputPopup* popup = wl_container_of(listener, popup, commit);

    textinput_popup_place(popup);
}

static void textinput_handle_popup_destroy(struct wl_listener* listener, void*)
{
    InputPopup* popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->map.link);
    wl_list_remove(&popup->unmap.link);
    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);
    wl_list_remove(&popup->link);
    wlr_scene_node_destroy(&popup->tree->node);
    free(popup);
}

static void textinput_handle_new_popup(struct wl_listener*, void* data)
{
    struct wlr_input_popup_surface_v2* input_popup = data;
    InputPopup* popup = util_calloc(1, sizeof(*popup));

    popup->popup = input_popup;
    popup->tree = wlr_scene_tree_create(layers[LyrOverlay]);
    wlr_scene_subsurface_tree_create(popup->tree, input_popup->surface);
    wlr_scene_node_set_enabled(&popup->tree->node, 0);
    popup->sent.width = -1;
    LISTEN(&input_popup->surface->events.map, &popup->map, textinput_handle_popup_map);
    LISTEN(&input_popup->surface->events.unmap, &popup->unmap, textinput_handle_popup_unmap);
    LISTEN(&input_popup->surface->events.commit, &popup->commit, textinput_handle_popup_commit);
    LISTEN(&input_popup->events.destroy, &popup->destroy, textinput_handle_popup_destroy);
    wl_list_insert(&textinput_popups, &popup->link);
    textinput_popup_place(popup);
}

static void textinput_send_state(TextInput* text_input)
{
    struct wlr_text_input_v3* wlr_text_input = text_input->wlr;

    if (wlr_text_input->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT)
        wlr_input_method_v2_send_surrounding_text(
            textinput_im,
            wlr_text_input->current.surrounding.text ? wlr_text_input->current.surrounding.text
                                                     : "",
            wlr_text_input->current.surrounding.cursor, wlr_text_input->current.surrounding.anchor);
    wlr_input_method_v2_send_text_change_cause(textinput_im,
                                               wlr_text_input->current.text_change_cause);
    if (wlr_text_input->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE)
        wlr_input_method_v2_send_content_type(textinput_im,
                                              wlr_text_input->current.content_type.hint,
                                              wlr_text_input->current.content_type.purpose);
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

static void textinput_activate(TextInput* text_input)
{
    if (textinput_served)
        textinput_deactivate();
    textinput_served = text_input;
    wlr_input_method_v2_send_activate(textinput_im);
    textinput_send_state(text_input);
}

static void textinput_update(TextInput* text_input)
{
    if (!textinput_im || !text_input->wlr->focused_surface || !text_input->wlr->current_enabled)
        return;
    if (text_input != textinput_served)
        textinput_activate(text_input);
    else
        textinput_send_state(text_input);
}

static void textinput_handle_enable(struct wl_listener* listener, void*)
{
    TextInput* text_input = wl_container_of(listener, text_input, enable);

    textinput_update(text_input);
}

static void textinput_handle_commit(struct wl_listener* listener, void*)
{
    TextInput* text_input = wl_container_of(listener, text_input, commit);

    textinput_update(text_input);
}

static void textinput_handle_disable(struct wl_listener* listener, void*)
{
    TextInput* text_input = wl_container_of(listener, text_input, disable);

    if (text_input == textinput_served)
        textinput_deactivate();
}

static void textinput_handle_destroy(struct wl_listener* listener, void*)
{
    TextInput* text_input = wl_container_of(listener, text_input, destroy);

    if (text_input == textinput_served)
        textinput_deactivate();
    wl_list_remove(&text_input->enable.link);
    wl_list_remove(&text_input->commit.link);
    wl_list_remove(&text_input->disable.link);
    wl_list_remove(&text_input->destroy.link);
    wl_list_remove(&text_input->link);
    free(text_input);
}

/* text inputs follow the keyboard focus while an input method is connected,
 * and only enter a surface of their own client */
static void textinput_sync(struct wlr_surface* focus)
{
    struct wlr_surface* surface = textinput_im ? focus : nullptr;

    if (textinput_served && (!textinput_served->wlr->focused_surface ||
                             textinput_served->wlr->focused_surface != surface))
        textinput_deactivate();
    TextInput* text_input;
    wl_list_for_each(text_input, &textinput_list, link) {
        if (text_input->wlr->focused_surface == surface)
            continue;
        if (text_input->wlr->focused_surface)
            wlr_text_input_v3_send_leave(text_input->wlr);
        if (surface && wl_resource_get_client(text_input->wlr->resource) ==
                           wl_resource_get_client(surface->resource))
            wlr_text_input_v3_send_enter(text_input->wlr, surface);
    }
    textinput_popups_update();
}

static void textinput_handle_im_commit(struct wl_listener*, void*)
{
    if (!textinput_served || !textinput_served->wlr->focused_surface)
        return;
    if (textinput_im->current.preedit.text)
        wlr_text_input_v3_send_preedit_string(
            textinput_served->wlr, textinput_im->current.preedit.text,
            textinput_im->current.preedit.cursor_begin, textinput_im->current.preedit.cursor_end);
    if (textinput_im->current.commit_text)
        wlr_text_input_v3_send_commit_string(textinput_served->wlr,
                                             textinput_im->current.commit_text);
    if (textinput_im->current.delete.before_length || textinput_im->current.delete.after_length)
        wlr_text_input_v3_send_delete_surrounding_text(textinput_served->wlr,
                                                       textinput_im->current.delete.before_length,
                                                       textinput_im->current.delete.after_length);
    wlr_text_input_v3_send_done(textinput_served->wlr);
}

static void textinput_handle_grab_keyboard(struct wl_listener*, void* data)
{
    struct wlr_input_method_keyboard_grab_v2* grab = data;
    struct wlr_keyboard* keyboard = keyboard_group ? &keyboard_group->wlr_group->keyboard : nullptr;

    wlr_input_method_keyboard_grab_v2_set_keyboard(grab, keyboard);
    if (keyboard)
        wlr_input_method_keyboard_grab_v2_send_modifiers(grab, &keyboard->modifiers);
    wl_signal_add(&grab->events.destroy, &textinput_im_grab_destroy);
}

/* resend the real modifiers, the grab may have changed them */
static void textinput_handle_grab_destroy(struct wl_listener*, void*)
{
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);

    wl_list_remove(&textinput_im_grab_destroy.link);
    if (keyboard)
        wlr_seat_keyboard_notify_modifiers(seat, &keyboard->modifiers);
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

    if (wlr->seat != seat)
        return;
    TextInput* text_input = util_calloc(1, sizeof(*text_input));
    text_input->wlr = wlr;
    LISTEN(&wlr->events.enable, &text_input->enable, textinput_handle_enable);
    LISTEN(&wlr->events.commit, &text_input->commit, textinput_handle_commit);
    LISTEN(&wlr->events.disable, &text_input->disable, textinput_handle_disable);
    LISTEN(&wlr->events.destroy, &text_input->destroy, textinput_handle_destroy);
    wl_list_insert(&textinput_list, &text_input->link);
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

static struct wlr_input_method_keyboard_grab_v2* textinput_keyboard_grab(KeyboardGroup* group)
{
    if (lock_active || !textinput_im)
        return nullptr;
    struct wlr_input_method_keyboard_grab_v2* grab = textinput_im->keyboard_grab;
    if (!grab)
        return nullptr;
    /* the input method types through its own virtual keyboard */
    if (group->vkb &&
        wl_resource_get_client(group->vkb->resource) == wl_resource_get_client(grab->resource))
        return nullptr;
    return grab;
}

/* Returns 1 if the input method took the key. A release goes where its press
 * went, so neither side is left with a key that stays down. */
int textinput_key(KeyboardGroup* group, struct wlr_keyboard_key_event* event)
{
    struct wlr_input_method_keyboard_grab_v2* grab = textinput_keyboard_grab(group);

    if (event->keycode >= KEY_CNT)
        return 0;
    uint32_t* word = &group->imkeys[event->keycode / 32];
    uint32_t bit = 1u << (event->keycode % 32);
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
    wlr_input_method_keyboard_grab_v2_set_keyboard(grab, &group->wlr_group->keyboard);
    wlr_input_method_keyboard_grab_v2_send_key(grab, event->time_msec, event->keycode,
                                               event->state);
    return 1;
}

int textinput_modifiers(KeyboardGroup* group)
{
    struct wlr_input_method_keyboard_grab_v2* grab = textinput_keyboard_grab(group);

    if (!grab)
        return 0;
    wlr_input_method_keyboard_grab_v2_set_keyboard(grab, &group->wlr_group->keyboard);
    wlr_input_method_keyboard_grab_v2_send_modifiers(grab, &group->wlr_group->keyboard.modifiers);
    return 1;
}
