/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_DESKTOP_RUNNER_RUNNER_H
#define G0WM_DESKTOP_RUNNER_RUNNER_H

#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

#include "core/g0wm.h"
#include "desktop/systray/icon.h"

extern int runner_active;

extern char runner_buffer[256];

extern int runner_length;

/* cursor offset into runner_buffer, 0..runner_length */
extern int runner_cursor;

extern int runner_repeating; /* the armed repeat belongs to the prompt */

const char* runner_candidate(int i);
int runner_candidate_count(void);
int runner_has_icons(void);
RasterIcon* runner_icon(int match_index);
void runner_key(xkb_keysym_t sym, uint32_t mods, uint32_t codepoint);
int runner_query(void);
int runner_selected(void);
const char* runner_suggest(void);
void runner_reload(void);
void runner_toggle(const ActionArg*);

#endif
