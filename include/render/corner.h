/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_CORNER_H
#define G0WM_CORNER_H

#include <stdint.h>

#include "core/g0wm.h"

int corner_bar_radius(Monitor* m);
unsigned int corner_border_width(void);
void corner_clear(uint32_t* px, int w, int h, int r);
void corner_cut(uint32_t* px, int w, int h, int r);
int corner_client_radius(Client* c);
void corner_draw_all(Client* c);

#endif
