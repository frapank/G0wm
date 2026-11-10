/*
 * SPDX-FileCopyrightText: janetski
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_SYSTRAY_ITEM_H
#define G0WM_SYSTRAY_ITEM_H

#include "desktop/systray/icon.h"
#include "desktop/systray/watcher.h"

#include <wayland-util.h>

/*
 * The FDO spec says "org.freedesktop.StatusNotifierItem"[1],
 * but both the client libraries[2,3] actually use "org.kde.StatusNotifierItem"
 *
 * [1] https://www.freedesktop.org/wiki/Specifications/StatusNotifierItem/
 * [2] https://github.com/AyatanaIndicators/libayatana-appindicator-glib
 * [3] https://invent.kde.org/frameworks/kstatusnotifieritem
 *
 */
#define SNI_NAME "org.kde.StatusNotifierItem"
#define SNI_OPATH "/StatusNotifierItem"
#define SNI_IFACE "org.kde.StatusNotifierItem"

typedef struct TrayItem {
    struct wl_list icons;
    char* bus_name;
    char* object_path;
    char* menu_object_path;
    char* app_id;
    char* icon_theme_path; /* where IconName is looked up first, if set */
    RasterIcon* icon;
    FallbackIconGlyph* fallback_icon;

    TrayWatcher* watcher;

    int foreground_color;

    int is_ready;

    struct wl_list link;
} TrayItem;

[[nodiscard]] TrayItem* item_create(const char* bus_name,
                                    const char* object_path,
                                    TrayWatcher* watcher);
void item_destroy(TrayItem* item);

void item_activate(TrayItem* item);
void item_show_menu(TrayItem* item);

#endif
