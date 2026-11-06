/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_DESKTOP_BAR_TITLE_H
#define G0WM_DESKTOP_BAR_TITLE_H

#include "core/g0wm.h"

int bar_on_title_bar(Client* client, double pointer_x, double pointer_y);
void bar_place_title_bar(Client* client);
int bar_title_click(Client* client, double pointer_x, double pointer_y);
int bar_title_height(Client* client);
void bar_title_hover(Client* client, double pointer_x, double pointer_y);
void bar_toggle_title_bars(const Arg*);

#endif
