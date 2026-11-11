/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: Nikita Ivanov
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_LAYOUT_H
#define G0WM_LAYOUT_H

#include "core/g0wm.h"

typedef struct {
    const char* symbol;
    void (*arrange)(Monitor*);
} WindowLayout;

void layout_arrange(Monitor* monitor);
void layout_focus_stack(const ActionArg* argument);
void layout_inc_master(const ActionArg* argument);
void layout_move_stack(const ActionArg* argument);
void layout_resize_height(const ActionArg* argument);
void layout_resize_width(const ActionArg* argument);
void layout_set(const ActionArg* argument);
void layout_set_master_factor(const ActionArg* argument);
void layout_shift_tag(const ActionArg* argument);
void layout_shift_view(const ActionArg* argument);
void layout_swap_clients(WindowClient* first, WindowClient* second);
void layout_tabbed(Monitor* monitor);
WindowClient* layout_tab_top(Monitor* monitor);
void layout_tag(const ActionArg* argument);
void layout_tile(Monitor* monitor);
void layout_toggle_gaps(const ActionArg*);
void layout_toggle_tabbed(const ActionArg* argument);
void layout_toggle_tag(const ActionArg* argument);
void layout_toggle_view(const ActionArg* argument);
void layout_view(const ActionArg* argument);
void layout_zoom(const ActionArg*);

#endif
