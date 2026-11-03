/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_CORNER_H
#define G0WM_CORNER_H

#include <stdint.h>

#include "g0wm.h"

int barcorner(Monitor* m);
unsigned int borderwidth(void);
void cornerclear(uint32_t* px, int w, int h, int r);
void cornercut(uint32_t* px, int w, int h, int r);
int cornerradius(Client* c);
void drawcorners(Client* c);

#endif
