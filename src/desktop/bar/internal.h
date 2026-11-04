/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_DESKTOP_BAR_INTERNAL_H
#define G0WM_DESKTOP_BAR_INTERNAL_H

#include <stdint.h>

#include "core/g0wm.h"
#include "desktop/notify/notify.h"
#include "desktop/systray/icon.h"
#include "render/buffer.h"

Monitor* bar_main_monitor(void);
Monitor* bar_monitor(void);
void bar_draw_pill(Monitor* m,
                   int x,
                   int y,
                   int w,
                   int h,
                   double r,
                   int hollow,
                   uint32_t rgba);
uint32_t bar_fade(uint32_t rgba);
int bar_private_dot(Client* c, uint32_t* rgba);
double bar_private_radius(int d);
uint32_t* bar_opaque_scheme(int scheme);
Buffer* bar_share_buffer(Buffer** pool, int w, int h);
void bar_draw_title_bar(Client* c);
void bar_draw_notification(Monitor* m,
                           int x,
                           int w,
                           const Notification* n,
                           long hist);
const Notification* bar_notification_shown(long* hist);
void bar_draw_icon(Monitor* m, Icon* icon, int x, int size);
void bar_draw_icon_holder(Monitor* m, int x, int size);
int bar_runner_icon_size(Monitor* m);
void bar_draw_candidates(Monitor* m, int x, int end);
void bar_tray_notify(void* data);

#endif
