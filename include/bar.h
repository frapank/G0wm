/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_BAR_H
#define G0WM_BAR_H

#include <stdint.h>
#include <wlr/types/wlr_scene.h>
#include <xkbcommon/xkbcommon.h>

#include "g0wm.h"

constexpr int STATUS_MAX = 4096;
#define TEXTW(mon, text) (drwl_font_getwidth(mon->drw, text) + mon->lrpad)
constexpr int STATUS_BUTTONS = 5;
/* the status box: drawbar() and buttonpress() agree on it to the pixel */
#define STATUSW(mon) (drawstatus((mon), stext, 0, 0, 0) + 2)

bool baracceptsinput(struct wlr_scene_buffer* buffer, double* sx, double* sy);
void barfini(void);
void drawbar(Monitor* m);
void drawbars(void);
void drawselbar(void);
int drawstatus(Monitor* m, const char* text, int x, int w, int render);
void notifyactions(const Arg* arg);
void notifydismiss(const Arg* arg);
void notifydismissall(const Arg* arg);
void notifyfini(void);
void notifynext(const Arg* arg);
void notifyopen(const Arg* arg);
int notifypicking(void);
void notifypickkey(xkb_keysym_t sym);
void notifyprev(const Arg* arg);
void notifyscroll(const Arg* arg);
int notifywheel(Monitor* pm, double delta, int horizontal);
int ontitle(Client* c, double x, double y);
void settitle(Client* c);
int statusclick(Monitor* pm, int x, int button);
int statusin(int fd, unsigned int mask, void* data);
int statusscroll(Monitor* pm, int x, double delta);
int statusshown(Monitor* m);
int tagwidth(Monitor* m, Monitor* s, uint32_t i);
int titleclick(Client* c, double x, double y);
int titleheight(Client* c);
void titlehover(Client* c, double x, double y);
void togglebar(const Arg* arg);
void toggletitlebar(const Arg* arg);
void trayactivate(const Arg* arg);
void traymenu(const Arg* arg);
void updatebar(Monitor* m);

extern char stext[STATUS_MAX];

#endif
