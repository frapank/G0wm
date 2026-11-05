/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_DESKTOP_NOTIFY_QUEUE_H
#define G0WM_DESKTOP_NOTIFY_QUEUE_H

#include <stddef.h>

#include "desktop/notify/notify.h"

/* NULL if nothing is on screen */
const Notification* notify_current(void);
size_t notify_queued(void);
/* newest first, NULL past the end */
const Notification* notify_history(size_t i);
size_t notify_history_length(void);
void notify_history_remove(size_t i);
/* Drops the current one; the next queued takes its place. */
void notify_dismiss(void);
void notify_dismiss_all(void);
/* Fires "default" (or action i) and drops the notification unless it is
 * resident. */
void notify_invoke(void);
void notify_action(size_t i);
/* While held (the user is picking an action) the timer stops and new
 * notifications queue up instead of taking the box. */
void notify_hold(int hold);
int notify_held(unsigned int* left_ms);

#endif
