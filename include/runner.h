/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_RUNNER_H
#define G0WM_RUNNER_H

#include <stddef.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

#include "g0wm.h"
#include "systray/icon.h"

int runnercalc(double* out);
void runnercalcformat(char* out, size_t n, double v);
const char* runnercandidate(int i);
int runnercandidates(void);
int runnerhasicons(void);
Icon* runnericon(int i);
void runnerkey(xkb_keysym_t sym, uint32_t mods, uint32_t codepoint);
int runnerquery(void);
int runnerselected(void);
const char* runnersuggest(void);
void runnertoggle(const Arg* arg);

extern int runner_active;
extern char runner_buf[256];
extern int runner_len;
extern int runner_cur;       /* cursor offset into runner_buf, 0..runner_len */
extern int runner_repeating; /* the armed repeat belongs to the prompt */

#endif
