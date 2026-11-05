/*
 * SPDX-FileCopyrightText: Hansvon
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_OPACITY_H
#define G0WM_OPACITY_H

#include <wlr/types/wlr_scene.h>

#include "core/g0wm.h"

float opacity_decorations_current(void);
void opacity_reload(void);
void opacity_set_scene_buffer(struct wlr_scene_buffer* buffer, int, int, void* data);
void opacity_set_border_color(Client* client, int scheme);
void opacity_change_focused(const Arg* arg);
void opacity_change_unfocused(const Arg* arg);
void opacity_toggle(const Arg*);
#ifdef INTEGRATED_BACKGROUND
void opacity_blur_bar(Monitor* monitor);
void opacity_blur_client(Client* client);
void opacity_set_wallpaper(Monitor* monitor);
#endif

#endif
