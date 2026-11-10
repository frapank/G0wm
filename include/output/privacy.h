/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_PRIVACY_H
#define G0WM_PRIVACY_H

#include <stddef.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>

#include "core/g0wm.h"

void privacy_after_commit(Monitor* monitor);
int privacy_before_commit(Monitor* monitor, struct wlr_output_state* state);
void privacy_handle_new_capture_session(struct wl_listener*, void* data);
void privacy_damage(void);
void privacy_finish(void);
void privacy_reload(void);
void privacy_placeholder(char* buf, size_t size, const char* template, const char* app);
const char* privacy_share_title(Client* client);
void privacy_shield_client(Client* client);
void privacy_toggle(const Arg*);

#endif
