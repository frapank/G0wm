/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_PRIVACY_H
#define G0WM_PRIVACY_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>

#include "g0wm.h"

void privacyafter(Monitor* m);
int privacybefore(Monitor* m, struct wlr_output_state* state);
void privacycapture(struct wl_listener* listener, void* data);
void privacydamage(void);
void privacyfini(void);
const char* sharetitle(Client* c);
void shieldclient(Client* c);
void toggleprivate(const Arg* arg);

#endif
