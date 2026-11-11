/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: unixchad
 * SPDX-FileCopyrightText: Ben Collerson
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * keyboards, key bindings, key repeat and shortcut inhibitors
 */

#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <xkbcommon/xkbcommon.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/bar/notification.h"
#include "desktop/runner/runner.h"
#include "desktop/traypopup.h"
#include "input/keyboard.h"
#include "input/pointer.h"
#include "input/textinput.h"
#include "internal.h"
#include "output/lock.h"

/* function declarations */
static int input_hide_cursor(void*);
static struct wlr_keyboard_shortcuts_inhibitor_v1* input_focused_inhibitor(void);
static int input_key_binding(uint32_t mods, xkb_keysym_t sym);
static void input_handle_key(struct wl_listener* listener, void* data);
static void input_handle_modifiers(struct wl_listener* listener, void*);
static int input_key_repeat(void* data);

/* variables */
static uint32_t input_runner_repeat_codepoint; /* codepoint the armed key repeat types */

/* function implementations */
void input_create_keyboard(struct wlr_keyboard* keyboard)
{
    /* Set the keymap to match the group keymap */
    wlr_keyboard_set_keymap(keyboard, keyboard_group->wlr_group->keyboard.keymap);

    /* Add the new keyboard to the group */
    wlr_keyboard_group_add_keyboard(keyboard_group->wlr_group, keyboard);
}

KeyboardGroup* input_create_keyboard_group(void)
{
    KeyboardGroup* group = util_calloc(1, sizeof(*group));

    group->wlr_group = wlr_keyboard_group_create();
    group->wlr_group->data = group;

    struct xkb_keymap* keymap;
    /* Prepare an XKB keymap and assign it to the keyboard group. */
    struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!(keymap = xkb_keymap_new_from_names(context, &settings->input.xkb_rules,
                                             XKB_KEYMAP_COMPILE_NO_FLAGS)))
        util_die("failed to compile keymap");

    wlr_keyboard_set_keymap(&group->wlr_group->keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);

    wlr_keyboard_set_repeat_info(&group->wlr_group->keyboard, settings->input.repeat_rate,
                                 settings->input.repeat_delay);

    /* Set up listeners for keyboard events */
    LISTEN(&group->wlr_group->keyboard.events.key, &group->key, input_handle_key);
    LISTEN(&group->wlr_group->keyboard.events.modifiers, &group->modifiers, input_handle_modifiers);

    group->key_repeat_source = wl_event_loop_add_timer(event_loop, input_key_repeat, group);

    /* A seat can only have one keyboard, but this is a limitation of the
     * Wayland protocol - not wlroots. We assign all connected keyboards to the
     * same wlr_keyboard_group, which provides a single wlr_keyboard interface
     * for all of them. Set this combined wlr_keyboard as the seat keyboard.
     */
    wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
    return group;
}

void input_handle_new_keyboard_inhibitor(struct wl_listener*, void* data)
{
    wlr_keyboard_shortcuts_inhibitor_v1_activate(data);
}

void input_handle_keyboard_group_destroy(struct wl_listener* listener, void*)
{
    KeyboardGroup* group = wl_container_of(listener, group, destroy);
    wl_event_source_remove(group->key_repeat_source);
    wl_list_remove(&group->key.link);
    wl_list_remove(&group->modifiers.link);
    wl_list_remove(&group->destroy.link);
    wlr_keyboard_group_destroy(group->wlr_group);
    free(group);
}

static int input_hide_cursor(void*)
{
    wlr_cursor_unset_image(cursor);
    input_cursor_hidden = true;
    return 1;
}

static struct wlr_keyboard_shortcuts_inhibitor_v1* input_focused_inhibitor(void)
{
    struct wlr_keyboard_shortcuts_inhibitor_v1* inhibitor;

    wl_list_for_each(inhibitor, &keyboard_inhibit_manager->inhibitors, link) {
        if (inhibitor->seat == seat && inhibitor->surface == seat->keyboard_state.focused_surface)
            return inhibitor;
    }
    return nullptr;
}

static int input_key_binding(uint32_t mods, xkb_keysym_t sym)
{
    /*
     * Here we handle compositor keybindings. This is when the compositor is
     * processing keys, rather than passing them on to the client for its own
     * processing.
     */
    struct wlr_keyboard_shortcuts_inhibitor_v1* inhibitor = input_focused_inhibitor();
    int inhibited = inhibitor && inhibitor->active;

    for (const KeyBinding *binding = settings->bindings.key_bindings,
                          *end = binding + settings->bindings.key_binding_count;
         binding < end; binding++) {
        if (CLEANMASK(mods) == CLEANMASK(binding->modifiers) &&
            xkb_keysym_to_lower(sym) == xkb_keysym_to_lower(binding->keysym) && binding->callback &&
            (!inhibited || binding->callback == input_toggle_inhibit)) {
            binding->callback(&binding->argument);
            return 1;
        }
    }
    return 0;
}

static void input_handle_key(struct wl_listener* listener, void* data)
{
    /* This event is raised when a key is pressed or released. */
    KeyboardGroup* group = wl_container_of(listener, group, key);
    struct wlr_keyboard_key_event* event = data;

    /* Translate libinput keycode -> xkbcommon */
    uint32_t keycode = event->keycode + 8;
    /* Get a list of keysyms based on the keymap for this keyboard */
    const xkb_keysym_t* keysyms;
    int keysym_count =
        xkb_state_key_get_syms(group->wlr_group->keyboard.xkb_state, keycode, &keysyms);

    int handled = 0;
    uint32_t mods = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

    /* hide cursor when typing starts */
    if (settings->input.hide_cursor_when_typing && !input_cursor_hidden &&
        event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
        input_hide_cursor(nullptr);

    /* While the prompt is open, every key belongs to it: swallow press and
     * release instead of matching keybindings or forwarding to the client.
     * Returning early skips the repeat bookkeeping below, so disarm here what
     * that would have disarmed: the keystroke that opened the prompt counted
     * as handled and left a repeat armed, which would replay the binding and
     * toggle the prompt straight back off. */
    if (runner_active) {
        if (!lock_active && keysym_count > 0 && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            /* only the primary keysym: the codepoint belongs to the key, not
             * to each of the syms it can produce, so looping would type it
             * once per sym */
            uint32_t codepoint =
                xkb_state_key_get_utf32(group->wlr_group->keyboard.xkb_state, keycode);
            runner_key(keysyms[0], mods, codepoint);
            /* Arm the repeat for the prompt the way a handled binding does,
             * so a held key (backspace above all) repeats into it. Return and
             * Escape close the prompt from inside runner_key(), and there is
             * nothing left to repeat into once they have. */
            if (runner_active && group->wlr_group->keyboard.repeat_info.delay > 0) {
                group->modifiers_mask = mods;
                group->keysyms = keysyms;
                group->keysym_count = keysym_count;
                input_runner_repeat_codepoint = codepoint;
                runner_repeating = 1;
                wl_event_source_timer_update(group->key_repeat_source,
                                             group->wlr_group->keyboard.repeat_info.delay);
                return;
            }
        }
        group->keysym_count = 0;
        runner_repeating = 0;
        wl_event_source_timer_update(group->key_repeat_source, 0);
        return;
    }

    /* the open menu holds the keyboard, Escape closes it */
    if (!lock_active && traypopup_active()) {
        if (keysym_count > 0 && event->state == WL_KEYBOARD_KEY_STATE_PRESSED &&
            keysyms[0] == XKB_KEY_Escape)
            traypopup_dismiss();
        group->keysym_count = 0;
        wl_event_source_timer_update(group->key_repeat_source, 0);
        return;
    }

    if (!lock_active && bar_notification_picking()) {
        if (keysym_count > 0 && event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
            bar_notification_pick_key(keysyms[0]);
        group->keysym_count = 0;
        wl_event_source_timer_update(group->key_repeat_source, 0);
        return;
    }

    /* On _press_ if there is no active screen locker,
     * attempt to process a compositor keybinding. */
    if (!lock_active && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < keysym_count; i++)
            handled = input_key_binding(mods, keysyms[i]) || handled;
    }

    if (handled && group->wlr_group->keyboard.repeat_info.delay > 0) {
        group->modifiers_mask = mods;
        group->keysyms = keysyms;
        group->keysym_count = keysym_count;
        wl_event_source_timer_update(group->key_repeat_source,
                                     group->wlr_group->keyboard.repeat_info.delay);
    } else {
        group->keysym_count = 0;
        wl_event_source_timer_update(group->key_repeat_source, 0);
    }

    if (handled)
        return;

    if (textinput_key(group, event))
        return;

    wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
    /* Pass unhandled keycodes along to the client. */
    wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
}

static void input_handle_modifiers(struct wl_listener* listener, void*)
{
    /* This event is raised when a modifier key, such as shift or alt, is
     * pressed. We simply communicate this to the client. */
    KeyboardGroup* group = wl_container_of(listener, group, modifiers);

    if (textinput_modifiers(group))
        return;

    wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
    /* Send modifiers to the client. */
    wlr_seat_keyboard_notify_modifiers(seat, &group->wlr_group->keyboard.modifiers);
}

static int input_key_repeat(void* data)
{
    KeyboardGroup* group = data;
    if (!group->keysym_count || group->wlr_group->keyboard.repeat_info.rate <= 0)
        return 0;
    /* the repeat of the key that opened the picker would close it */
    if (bar_notification_picking())
        return 0;
    if (runner_active) {
        /* Holding the binding down past the repeat delay would replay it
         * while the prompt it just opened is up, closing it again: only a
         * repeat the prompt armed itself may run. Rearming after the fact
         * keeps a key that closed the prompt from repeating into nothing. */
        if (!runner_repeating)
            return 0;
        runner_key(group->keysyms[0], group->modifiers_mask, input_runner_repeat_codepoint);
        if (runner_active)
            wl_event_source_timer_update(group->key_repeat_source,
                                         1000 / group->wlr_group->keyboard.repeat_info.rate);
        else
            group->keysym_count = 0;
        return 0;
    }

    wl_event_source_timer_update(group->key_repeat_source,
                                 1000 / group->wlr_group->keyboard.repeat_info.rate);

    for (int i = 0; i < group->keysym_count; i++)
        input_key_binding(group->modifiers_mask, group->keysyms[i]);

    return 0;
}

void input_toggle_inhibit(const ActionArg*)
{
    struct wlr_keyboard_shortcuts_inhibitor_v1* inhibitor = input_focused_inhibitor();

    if (!inhibitor)
        return;
    if (inhibitor->active)
        wlr_keyboard_shortcuts_inhibitor_v1_deactivate(inhibitor);
    else
        wlr_keyboard_shortcuts_inhibitor_v1_activate(inhibitor);
}

void input_handle_new_virtual_keyboard(struct wl_listener*, void* data)
{
    struct wlr_virtual_keyboard_v1* virtual_keyboard = data;
    /* virtual keyboards shouldn't share keyboard group */
    KeyboardGroup* group = input_create_keyboard_group();
    group->virtual_keyboard = virtual_keyboard;
    /* Set the keymap to match the group keymap */
    wlr_keyboard_set_keymap(&virtual_keyboard->keyboard, group->wlr_group->keyboard.keymap);
    LISTEN(&virtual_keyboard->keyboard.base.events.destroy, &group->destroy,
           input_handle_keyboard_group_destroy);

    /* Add the new keyboard to the group */
    wlr_keyboard_group_add_keyboard(group->wlr_group, &virtual_keyboard->keyboard);
}
