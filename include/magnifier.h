/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_MAGNIFIER_H
#define G0WM_MAGNIFIER_H

#include <wlr/types/wlr_output.h>

#include "g0wm.h"

void magnifier_drop(Monitor* m);
int magnifier_frame(Monitor* m, struct wlr_output_state* state);
void magnifier_zoom(Monitor* m, float z);
void magnifier_magnify(const Arg* arg);

#endif
