/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_DESKTOP_BAR_BAR_H
#define G0WM_DESKTOP_BAR_BAR_H

#include <drwl.h>
#include <stdint.h>
#include <wlr/types/wlr_scene.h>

#include "core/g0wm.h"
#include "desktop/bar/status.h"

#define TEXTW(mon, text) (drwl_font_getwidth(mon->drw, text) + mon->lrpad)

/* the status box: bar_draw() and input_handle_button() agree on it to the pixel
 */
#define STATUSW(mon) (bar_draw_status((mon), bar_status_text, 0, 0, 0) + 2)

extern char bar_status_text[STATUS_MAX];

bool bar_accepts_input(struct wlr_scene_buffer* buffer, double* sx, double* sy);
void bar_finish(void);
void bar_draw(Monitor* m);
void bar_draw_all(void);
void bar_draw_selected(void);
int bar_tag_width(Monitor* m, Monitor* s, uint32_t i);
void bar_toggle(const Arg* arg);
void bar_update(Monitor* m);

#endif
