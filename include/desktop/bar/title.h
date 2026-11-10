/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_DESKTOP_BAR_TITLE_H
#define G0WM_DESKTOP_BAR_TITLE_H

#include "core/g0wm.h"

int bar_on_title_bar(WindowClient* client, double pointer_x, double pointer_y);
void bar_place_title_bar(WindowClient* client);
int bar_title_click(WindowClient* client, double pointer_x, double pointer_y);
int bar_title_height(WindowClient* client);
void bar_title_hover(WindowClient* client, double pointer_x, double pointer_y);
void bar_toggle_title_bars(const ActionArg*);

#endif
