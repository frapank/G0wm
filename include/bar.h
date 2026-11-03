/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_BAR_H
#define G0WM_BAR_H

#include <stdint.h>
#include <wlr/types/wlr_scene.h>
#include <xkbcommon/xkbcommon.h>

#include "g0wm.h"

constexpr int STATUS_MAX = 4096;
#define TEXTW(mon, text) (drwl_font_getwidth(mon->drw, text) + mon->lrpad)
constexpr int STATUS_BUTTONS = 5;
/* the status box: bar_draw() and input_handle_button() agree on it to the pixel
 */
#define STATUSW(mon) (bar_draw_status((mon), bar_status_text, 0, 0, 0) + 2)

bool bar_accepts_input(struct wlr_scene_buffer* buffer, double* sx, double* sy);
void bar_finish(void);
void bar_draw(Monitor* m);
void bar_draw_all(void);
void bar_draw_selected(void);
int bar_draw_status(Monitor* m, const char* text, int x, int w, int render);
void bar_notification_actions(const Arg* arg);
void bar_notification_dismiss(const Arg* arg);
void bar_notification_dismiss_all(const Arg* arg);
void bar_notification_finish(void);
void bar_notification_next(const Arg* arg);
void bar_notification_open(const Arg* arg);
int bar_notification_picking(void);
void bar_notification_pick_key(xkb_keysym_t sym);
void bar_notification_prev(const Arg* arg);
void bar_notification_scroll(const Arg* arg);
int bar_notification_wheel(Monitor* pm, double delta, int horizontal);
int bar_on_title_bar(Client* c, double x, double y);
void bar_place_title_bar(Client* c);
int bar_status_click(Monitor* pm, int x, int button);
int bar_handle_status_input(int fd, unsigned int mask, void* data);
int bar_status_scroll(Monitor* pm, int x, double delta);
int bar_status_shown(Monitor* m);
int bar_tag_width(Monitor* m, Monitor* s, uint32_t i);
int bar_title_click(Client* c, double x, double y);
int bar_title_height(Client* c);
void bar_title_hover(Client* c, double x, double y);
void bar_toggle(const Arg* arg);
void bar_toggle_title_bars(const Arg* arg);
void bar_tray_activate(const Arg* arg);
void bar_tray_menu(const Arg* arg);
void bar_update(Monitor* m);

extern char bar_status_text[STATUS_MAX];

#endif
