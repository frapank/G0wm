/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_DESKTOP_BAR_STATUS_H
#define G0WM_DESKTOP_BAR_STATUS_H

#include "core/g0wm.h"

constexpr int STATUS_MAX = 4096;

constexpr int STATUS_BUTTONS = 5;

int bar_draw_status(Monitor* monitor, const char* text, int x, int width, int render);
int bar_status_click(Monitor* monitor, int pointer_x, int button);
int bar_handle_status_input(int fd, unsigned int event_mask, void*);
int bar_status_scroll(Monitor* monitor, int pointer_x, double scroll_delta);
int bar_status_shown(Monitor* monitor);

#endif
