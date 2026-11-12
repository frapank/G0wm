/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_DESKTOP_BAR_INTERNAL_H
#define G0WM_DESKTOP_BAR_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "core/g0wm.h"
#include "desktop/notify/notify.h"
#include "desktop/systray/icon.h"
#include "render/buffer.h"

Monitor* bar_main_monitor(void);
void bar_draw_pill(Monitor* monitor,
                   int x,
                   int y,
                   int width,
                   int height,
                   double radius,
                   bool hollow,
                   uint32_t rgba);
uint32_t bar_fade(uint32_t rgba);
bool bar_private_dot(const WindowClient* client, uint32_t* rgba);
double bar_private_radius(int diameter);
uint32_t* bar_opaque_scheme(int scheme);
RenderBuffer* bar_share_buffer(RenderBuffer** pool, int width, int height);
void bar_draw_title_bar(WindowClient* client);
void bar_draw_notification(Monitor* monitor,
                           int x,
                           int width,
                           const Notification* notification,
                           long hist,
                           bool shared);
bool bar_notification_private(void);
bool bar_notification_private_dot(uint32_t* rgba);
const Notification* bar_notification_shown(long* hist);
void bar_draw_icon(Monitor* monitor, RasterIcon* icon, int start_x, int size);
void bar_draw_icon_holder(Monitor* monitor, int start_x, int size);
int bar_runner_icon_size(Monitor* monitor);
void bar_draw_candidates(Monitor* monitor, int start_x, int end_x);
void bar_tray_notify(void* data);

#endif
