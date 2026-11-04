/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_ACTIVATION_H
#define G0WM_ACTIVATION_H

#include <wlr/types/wlr_xdg_activation_v1.h>

const char* activation_token_new(void);
int activation_token_ours(const struct wlr_xdg_activation_token_v1* token);

#endif
