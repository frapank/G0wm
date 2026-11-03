/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_MAGNIFIER_H
#define G0WM_MAGNIFIER_H

#include <wlr/types/wlr_output.h>

#include "g0wm.h"

void magnifierdrop(Monitor* m);
int magnifierframe(Monitor* m, struct wlr_output_state* state);
void magnifierzoom(Monitor* m, float z);
void magnify(const Arg* arg);

#endif
