/*
 * SPDX-FileCopyrightText: Hansvon
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_OPACITY_H
#define G0WM_OPACITY_H

#include <wlr/types/wlr_scene.h>

#include "g0wm.h"

float decoopacity(void);
void reloadopacity(void);
void scenebuffersetopacity(struct wlr_scene_buffer* buffer,
                           int sx,
                           int sy,
                           void* data);
void setbordercolor(Client* c, int scheme);
void setopacityfocus(const Arg* arg);
void setopacityunfocus(const Arg* arg);
void toggleopacity(const Arg* arg);
#ifdef INTEGRATED_BACKGROUND
void blurbar(Monitor* m);
void blurclient(Client* c);
void setwallpaper(Monitor* m);
#endif

#endif
