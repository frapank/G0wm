/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_TOPLEVEL_H
#define G0WM_TOPLEVEL_H

#include <wayland-server-core.h>

#include "core/g0wm.h"

void toplevel_handle_new_capture_request(struct wl_listener*, void* data);
void toplevel_focus(Client* client);
void toplevel_map(Client* client);
void toplevel_unmap(Client* client);
void toplevel_update(Client* client);

#endif
