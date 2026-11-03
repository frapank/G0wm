/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_TRAYPOPUP_H
#define G0WM_TRAYPOPUP_H

#include "systray/menu.h"

int traypopup_active(void);
void traypopup_cleanup(void);
void traypopup_click(double lx, double ly);
void traypopup_dismiss(void);
void traypopup_motion(double lx, double ly);
void traypopup_present(const char* const* labels, int n, Menu* menu);

#endif
