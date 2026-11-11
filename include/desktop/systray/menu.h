/*
 * SPDX-FileCopyrightText: janetski
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_SYSTRAY_MENU_H
#define G0WM_SYSTRAY_MENU_H

#include <dbus/dbus.h>

/* Longest label a menu entry is shown under, affixes included */
constexpr int MENU_LABEL_MAX = 64;

typedef struct TrayMenu TrayMenu;

/* Shows a level. The labels only live for the call, and it owes one pick. */
typedef void (*TrayMenuPresentCallback)(const char* const* labels, int n, TrayMenu* menu);

/* The menu is built on demand and not kept around */
void menu_show(DBusConnection* connection,
               const char* bus_name,
               const char* object_path,
               TrayMenuPresentCallback present);

/* Takes the entry chosen or opens its submenu, a negative index cancels. */
void menu_pick(TrayMenu* menu, int index);

#endif
