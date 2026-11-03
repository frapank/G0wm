/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_TOPLEVEL_H
#define G0WM_TOPLEVEL_H

#include <wayland-server-core.h>

#include "g0wm.h"

void capturetoplevel(struct wl_listener* listener, void* data);
void toplevelfocus(Client* c);
void toplevelmap(Client* c);
void toplevelunmap(Client* c);
void toplevelupdate(Client* c);

#endif
