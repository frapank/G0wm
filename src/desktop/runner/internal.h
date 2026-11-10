/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_DESKTOP_RUNNER_INTERNAL_H
#define G0WM_DESKTOP_RUNNER_INTERNAL_H

#include <stddef.h>
#include <time.h>

#include "core/g0wm.h"
#include "desktop/systray/icon.h"

enum RunnerMode {
    RUNNER_MODE_APPLICATION,
    RUNNER_MODE_COMMAND,
    RUNNER_MODE_TERMINAL,
    RUNNER_MODE_CALCULATOR,
    RUNNER_MODE_SEARCH,
    RUNNER_MODE_WINDOW,
    RUNNER_MODE_TAG,
    RUNNER_MODE_FILE,
    RUNNER_MODE_COUNT
};

typedef struct {
    char* desktop_id;
    char* name;
    char* untranslated_name; /* the untranslated Name when it differs */
    char* exec_command;
    char* icon_name;
    int run_in_terminal;
    int is_hidden;
    int source_order;
    RasterIcon* icon_image;
    int icon_load_attempted;
} RunnerApplication;

extern char** runner_commands;
extern int runner_command_count;
extern const char** runner_source;
extern int* runner_matches;
extern int runner_match_count;
extern WindowClient** runner_window_clients;
extern int runner_selection;
extern int* runner_use;
extern int runner_history_read;
extern int runner_history_pos;
extern RunnerApplication* runner_apps;
extern char** runner_app_names;
extern char** runner_app_untranslated_names;
extern int runner_app_count;
extern int* runner_app_use;

int runner_compare(const void* first, const void* second);
void runner_append(char** text, size_t* length, const char* addition, size_t addition_length);
char* runner_search_path(void);
void runner_build_cache(void);
void runner_free_cache(void);
char* runner_app_dirs(void);
void runner_build_apps(void);
void runner_free_apps(void);
char* runner_app_exec(const char* exec);
int runner_stale(char* paths, char** cache, struct timespec* stamp);
void runner_trim_history(void);
void runner_read_history(void);
void runner_weigh(void);
void runner_record(const char* cmd);
void runner_recall(int step);
int runner_calc_fill(void);
int runner_mode(const char* text, int* offset);
char* runner_expand(const char* text, size_t length);
void runner_free_entries(void);
void runner_filter(void);

#endif
