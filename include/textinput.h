/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_TEXTINPUT_H
#define G0WM_TEXTINPUT_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>

#include "g0wm.h"

void createinputmethod(struct wl_listener* listener, void* data);
void createtextinput(struct wl_listener* listener, void* data);
void textinputarrange(void);
void textinputfocus(struct wl_listener* listener, void* data);
int textinputkey(KeyboardGroup* group, struct wlr_keyboard_key_event* event);
int textinputmodifiers(KeyboardGroup* group);

#endif
