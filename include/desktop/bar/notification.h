/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_DESKTOP_BAR_NOTIFICATION_H
#define G0WM_DESKTOP_BAR_NOTIFICATION_H

#include <xkbcommon/xkbcommon.h>

#include "core/g0wm.h"

void bar_notification_actions(const ActionArg*);
void bar_notification_dismiss(const ActionArg*);
void bar_notification_dismiss_all(const ActionArg*);
void bar_notification_finish(void);
void bar_notification_next(const ActionArg*);
void bar_notification_open(const ActionArg* argument);
int bar_notification_picking(void);
void bar_notification_pick_key(xkb_keysym_t sym);
void bar_notification_prev(const ActionArg*);
void bar_notification_reset_private(void);
void bar_notification_scroll(const ActionArg*);
void bar_notification_toggle_private(const ActionArg*);
int bar_notification_wheel(Monitor* pointer_monitor, double delta, int horizontal);

#endif
