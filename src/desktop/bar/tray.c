/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the tray icons in the bar
 */

#include "desktop/bar/tray.h"
#include "core/g0wm.h"
#include "desktop/bar/bar.h"
#include "desktop/systray/tray.h"
#include "desktop/traypopup.h"
#include "internal.h"
#include "output/monitor.h"

/* function implementations */
void bar_tray_notify(void* data)
{
    bar_draw((Monitor*)data);
}

void bar_tray_activate(const ActionArg* argument)
{
    Monitor* monitor = bar_monitor(); /* the tray belongs to the bar clicked */

    if (!monitor)
        return;
    tray_left_clicked(monitor->tray, argument->unsigned_integer);
}

void bar_tray_menu(const ActionArg* argument)
{
    Monitor* monitor = bar_monitor();

    if (!monitor)
        return;
    tray_right_clicked(monitor->tray, argument->unsigned_integer, traypopup_present);
}
