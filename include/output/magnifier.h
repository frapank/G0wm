/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_MAGNIFIER_H
#define G0WM_MAGNIFIER_H

#include <wlr/types/wlr_output.h>

#include "core/g0wm.h"

void magnifier_drop(Monitor* monitor);
int magnifier_frame(Monitor* monitor, struct wlr_output_state* state);
void magnifier_zoom(Monitor* monitor, float zoom_level);
void magnifier_magnify(const ActionArg* argument);

#endif
