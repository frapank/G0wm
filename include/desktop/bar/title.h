/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_DESKTOP_BAR_TITLE_H
#define G0WM_DESKTOP_BAR_TITLE_H

#include "core/g0wm.h"

int bar_on_title_bar(Client* c, double x, double y);
void bar_place_title_bar(Client* c);
int bar_title_click(Client* c, double x, double y);
int bar_title_height(Client* c);
void bar_title_hover(Client* c, double x, double y);
void bar_toggle_title_bars(const Arg* arg);

#endif
