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

enum {
    ModeApp,
    ModeRun,
    ModeTerm,
    ModeCalc,
    ModeSearch,
    ModeWindow,
    ModeTag,
    ModeFile,
    NumModes
};

typedef struct {
    char* id;
    char* name;
    char* gname; /* the untranslated Name when it differs */
    char* exec;
    char* icon;
    int term;
    int hide;
    int order;
    Icon* img;
    int imgread;
} RunnerApp;

extern char** runner_commands;
extern int runner_command_count;
extern const char** runner_source;
extern int* runner_matches;
extern int runner_match_count;
extern Client** runner_window_clients;
extern int runner_selection;
extern int* runner_use;
extern int runner_history_read;
extern int runner_history_pos;
extern RunnerApp* runner_apps;
extern char** runner_app_names;
extern char** runner_app_generic_names;
extern int runner_app_count;
extern int* runner_app_use;

int runner_compare(const void* a, const void* b);
void runner_append(char** s, size_t* n, const char* add, size_t len);
char* runner_search_path(void);
void runner_build_cache(void);
void runner_free_cache(void);
char* runner_app_dirs(void);
void runner_build_apps(void);
void runner_free_apps(void);
char* runner_app_exec(const char* exec);
int runner_stale(char* p, char** cache, struct timespec* stamp);
void runner_trim_history(void);
void runner_read_history(void);
void runner_weigh(void);
void runner_record(const char* cmd);
void runner_recall(int step);
int runner_calc_fill(void);
int runner_mode(const char* s, int* off);
char* runner_expand(const char* s, size_t len);
void runner_free_entries(void);
void runner_filter(void);

#endif
