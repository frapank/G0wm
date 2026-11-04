/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_PRIVACY_H
#define G0WM_PRIVACY_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>

#include "core/g0wm.h"

void privacy_after_commit(Monitor* m);
int privacy_before_commit(Monitor* m, struct wlr_output_state* state);
void privacy_handle_new_capture_session(struct wl_listener* listener,
                                        void* data);
void privacy_damage(void);
void privacy_finish(void);
const char* privacy_share_title(Client* c);
void privacy_shield_client(Client* c);
void privacy_toggle(const Arg* arg);

#endif
