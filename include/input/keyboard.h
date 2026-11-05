/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: unixchad
 * SPDX-FileCopyrightText: Ben Collerson
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_INPUT_KEYBOARD_H
#define G0WM_INPUT_KEYBOARD_H

#include <linux/input-event-codes.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <xkbcommon/xkbcommon.h>

#include "core/g0wm.h"

struct KeyboardGroup {
    struct wlr_keyboard_group* wlr_group;

    int nsyms;
    const xkb_keysym_t* keysyms; /* invalid if nsyms == 0 */
    uint32_t mods;               /* invalid if nsyms == 0 */
    struct wl_event_source* key_repeat_source;

    struct wl_listener modifiers;
    struct wlr_virtual_keyboard_v1* vkb; /* NULL for the physical keyboards */
    uint32_t imkeys[KEY_CNT / 32]; /* held keys whose press went to the IM */
    struct wl_listener key;
    struct wl_listener destroy;
};

static_assert(KEY_CNT % 32 == 0);

void input_handle_new_keyboard_inhibitor(struct wl_listener*, void* data);
KeyboardGroup* input_create_keyboard_group(void);
void input_handle_keyboard_group_destroy(struct wl_listener* listener, void*);
void input_toggle_inhibit(const Arg*);
void input_handle_new_virtual_keyboard(struct wl_listener*, void* data);

#endif
