/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: unixchad
 * SPDX-FileCopyrightText: Ben Collerson
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_INPUT_INTERNAL_H
#define G0WM_INPUT_INTERNAL_H

#include <wlr/types/wlr_keyboard.h>

extern bool input_cursor_hidden;

void input_create_keyboard(struct wlr_keyboard* keyboard);

#endif
