#ifndef TRAY_H
#define TRAY_H

#include "menu.h"
#include "watcher.h"

#include <pixman.h>
#include <wayland-util.h>

#include <stdint.h>

typedef void (*TrayNotifyCb)(void* data);

typedef struct {
    pixman_image_t* image;
    struct fcft_font* font;
    uint32_t* scheme;
    TrayNotifyCb cb;
    Watcher* watcher;
    void* monitor;
    int height;
    int iconsize;
    int spacing; /* between two icons */
    int padding; /* at both ends, the bar's right edge included */

    struct wl_list link;
} Tray;

Tray* createtray(void* monitor,
                 int height,
                 int iconsize,
                 int spacing,
                 int padding,
                 uint32_t* colorscheme,
                 const char** fonts,
                 const char* fontattrs,
                 TrayNotifyCb cb,
                 Watcher* watcher);
void destroytray(Tray* tray);

int tray_get_width(const Tray* tray);
int tray_get_icon_width(const Tray* tray);
unsigned int tray_index_at(const Tray* tray, double x);
void tray_update(Tray* tray);
void tray_leftclicked(Tray* tray, unsigned int index);
void tray_rightclicked(Tray* tray, unsigned int index, MenuPresentFn present);

#endif /* TRAY_H */
