/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_TEXTINPUT_H
#define G0WM_TEXTINPUT_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>

#include "core/g0wm.h"

void textinput_handle_new_input_method(struct wl_listener*, void* data);
void textinput_handle_new_text_input(struct wl_listener*, void* data);
void textinput_arrange(void);
void textinput_handle_focus_change(struct wl_listener*, void* data);
int textinput_key(KeyboardGroup* group, struct wlr_keyboard_key_event* event);
int textinput_modifiers(KeyboardGroup* group);

#endif
