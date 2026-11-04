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
} Layout;

void layout_arrange(Monitor* m);
void layout_focus_stack(const Arg* arg);
void layout_inc_master(const Arg* arg);
void layout_move_stack(const Arg* arg);
void layout_resize_height(const Arg* arg);
void layout_resize_width(const Arg* arg);
void layout_set(const Arg* arg);
void layout_set_master_factor(const Arg* arg);
void layout_shift_tag(const Arg* arg);
void layout_shift_view(const Arg* arg);
void layout_swap_clients(Client* a, Client* b);
void layout_tabbed(Monitor* m);
Client* layout_tab_top(Monitor* m);
void layout_tag(const Arg* arg);
void layout_tile(Monitor* m);
void layout_toggle_gaps(const Arg* arg);
void layout_toggle_tabbed(const Arg* arg);
void layout_toggle_tag(const Arg* arg);
void layout_toggle_view(const Arg* arg);
void layout_view(const Arg* arg);
void layout_zoom(const Arg* arg);

#endif
