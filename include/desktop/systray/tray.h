/*
 * SPDX-FileCopyrightText: janetski
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_SYSTRAY_TRAY_H
#define G0WM_SYSTRAY_TRAY_H

#include "desktop/systray/menu.h"
#include "desktop/systray/watcher.h"

#include <pixman.h>
#include <wayland-util.h>

#include <stdint.h>

typedef void (*TrayUpdateCallback)(void* data);

typedef struct {
    pixman_image_t* image;
    struct fcft_font* font;
    uint32_t* color_scheme;
    TrayUpdateCallback on_update;
    TrayWatcher* watcher;
    void* monitor;
    int height;
    int icon_size;
    int spacing; /* between two icons */
    int padding; /* at both ends, the bar's right edge included */

    struct wl_list link;
} SystemTray;

[[nodiscard]] SystemTray* tray_create(void* monitor,
                                      int height,
                                      int icon_size,
                                      int spacing,
                                      int padding,
                                      uint32_t* color_scheme,
                                      const char** fonts,
                                      const char* font_attributes,
                                      TrayUpdateCallback notify_callback,
                                      TrayWatcher* watcher);
void tray_destroy(SystemTray* tray);

int tray_get_width(const SystemTray* tray);
int tray_get_icon_width(const SystemTray* tray);
unsigned int tray_index_at(const SystemTray* tray, double x);
void tray_update(SystemTray* tray);
void tray_left_clicked(SystemTray* tray, unsigned int index);
void tray_right_clicked(SystemTray* tray, unsigned int index, TrayMenuPresentCallback present);

#endif
