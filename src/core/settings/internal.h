/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_CORE_SETTINGS_INTERNAL_H
#define G0WM_CORE_SETTINGS_INTERNAL_H

#include <scfg.h>
#include <stdint.h>

#include "core/g0wm.h"

typedef struct {
    int val;
    const char* name;
} Enum;

typedef struct {
    const char* name;
    void (*arrange)(Monitor*);
} Arrange;

/* which member of the Arg a binding uses: 'i' int, 'u' unsigned, 'f' float,
 * 'v' an execvp() list, 'l' a layout symbol, 'c' a drag mode, 0 none */
typedef struct {
    void (*func)(const Arg*);
    const char* name;
    char arg;
} Action;

extern const Enum settings_modifier_names[];
extern const Enum settings_drag_modes[];
extern const Enum settings_click_areas[];
extern const Enum settings_mouse_buttons[];
extern const Enum settings_gesture_motions[];
extern const Enum settings_runner_icons[];
extern const Enum settings_bar_statuses[];
extern const Enum settings_corner_styles[];
#ifdef INTEGRATED_BACKGROUND
extern const Enum settings_opacity_kinds[];
#endif
extern const Enum settings_transforms[];
extern const Enum settings_scroll_methods[];
extern const Enum settings_click_methods[];
extern const Enum settings_send_events[];
extern const Enum settings_accel_profiles[];
extern const Enum settings_button_maps[];
extern const Enum settings_log_levels[];
extern const Arrange settings_arranges[];
extern const size_t settings_arrange_count;
extern const Action settings_actions[];
extern const size_t settings_action_count;
extern const char* const settings_scheme_names[];
extern const struct scfg_block* settings_builtin;

uint32_t settings_pack_rgba(const float color[static 4]);
void settings_load_builtin(void);
void settings_apply(const struct scfg_block* block);
void settings_validate(const struct scfg_block* want,
                       const struct scfg_block* got,
                       const char* path,
                       int* problems);
char* settings_rendered_defaults(void);

#endif
