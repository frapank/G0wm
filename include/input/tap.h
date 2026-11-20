/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_INPUT_TAP_H
#define G0WM_INPUT_TAP_H

#include <stdint.h>
#include <wayland-server-core.h>

#include "core/g0wm.h"

typedef struct InputTap {
    struct InputTap* next;
    struct wl_event_source* timer;
    uint32_t modifiers;
    uint32_t code;
    unsigned int area;
    unsigned int count;
    uint32_t time;
    void* target;
    Monitor* monitor;
    ActionArg context;
    void (*callback)(const ActionArg*);
    ActionArg argument;
} InputTap;

void input_tap_cancel(InputTap* tap);
void input_tap_destroy(InputTap* tap);
void input_taps_cancel(void);
void input_taps_destroy(void);
unsigned int input_tap_begin(InputTap* tap,
                             uint32_t modifiers,
                             uint32_t code,
                             unsigned int area,
                             void* target,
                             ActionArg context,
                             uint32_t time);
int input_tap_end(InputTap* tap,
                  void (*callback)(const ActionArg*),
                  ActionArg argument,
                  int higher);

#endif
