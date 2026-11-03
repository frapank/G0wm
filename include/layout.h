/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: Nikita Ivanov
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_LAYOUT_H
#define G0WM_LAYOUT_H

#include "g0wm.h"

typedef struct {
    const char* symbol;
    void (*arrange)(Monitor*);
} Layout;

void arrange(Monitor* m);
void focusstack(const Arg* arg);
void incnmaster(const Arg* arg);
void movestack(const Arg* arg);
void resizeheight(const Arg* arg);
void resizewidth(const Arg* arg);
void setlayout(const Arg* arg);
void setmfact(const Arg* arg);
void shifttag(const Arg* arg);
void shiftview(const Arg* arg);
void swapclients(Client* a, Client* b);
void tabbed(Monitor* m);
Client* tabtop(Monitor* m);
void tag(const Arg* arg);
void tile(Monitor* m);
void togglegaps(const Arg* arg);
void toggletabbed(const Arg* arg);
void toggletag(const Arg* arg);
void toggleview(const Arg* arg);
void view(const Arg* arg);
void zoom(const Arg* arg);

#endif
