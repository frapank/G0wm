/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_CORE_SETTINGS_INTERNAL_H
#define G0WM_CORE_SETTINGS_INTERNAL_H

#include <scfg.h>
#include <stdint.h>

#include "core/g0wm.h"
#include "core/settings.h"

typedef struct SettingsAllocation SettingsAllocation;

typedef struct LoadedSettings {
    struct LoadedSettings* next;
    Settings values;
    struct scfg_block tree;
    SettingsAllocation* allocations;
} LoadedSettings;

void* settings_alloc(LoadedSettings* loaded, size_t count, size_t size);

typedef struct {
    int value;
    const char* name;
} EnumEntry;

typedef struct {
    const char* name;
    void (*callback)(Monitor*);
} LayoutAlgorithmEntry;

/* Binding argument encoding: 'i' integer, 'u' unsigned_integer, 'f' floating,
 * 'v' pointer to an execvp() list, 'l' layout symbol, 'c' drag mode, 0 none. */
typedef struct {
    void (*callback)(const ActionArg*);
    const char* name;
    char argument_kind;
} ActionEntry;

extern const EnumEntry settings_modifier_names[];
extern const EnumEntry settings_drag_modes[];
extern const EnumEntry settings_click_areas[];
extern const EnumEntry settings_mouse_buttons[];
extern const EnumEntry settings_gesture_motions[];
extern const EnumEntry settings_runner_icons[];
extern const EnumEntry settings_bar_statuses[];
extern const EnumEntry settings_notification_dots[];
extern const EnumEntry settings_corner_styles[];
#ifdef INTEGRATED_BACKGROUND
extern const EnumEntry settings_opacity_kinds[];
#endif
extern const EnumEntry settings_transforms[];
extern const EnumEntry settings_scroll_methods[];
extern const EnumEntry settings_click_methods[];
extern const EnumEntry settings_send_events[];
extern const EnumEntry settings_accel_profiles[];
extern const EnumEntry settings_button_maps[];
extern const EnumEntry settings_log_levels[];
extern const LayoutAlgorithmEntry settings_arranges[];
extern const size_t settings_arrange_count;
extern const ActionEntry settings_actions[];
extern const size_t settings_action_count;
extern const char* const settings_scheme_names[];
extern const struct scfg_block* settings_builtin;

uint32_t settings_pack_rgba(const float color[static 4]);
void settings_load_builtin(void);
void settings_apply(LoadedSettings* loaded, const struct scfg_block* block);
void settings_validate(const struct scfg_block* want,
                       const struct scfg_block* got,
                       const char* path,
                       int* problems);
char* settings_render(const Settings* values);

#endif
