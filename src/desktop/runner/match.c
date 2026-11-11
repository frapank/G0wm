/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the candidates for what is typed, and their ranking
 */

#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <wayland-server-core.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/runner/calc.h"
#include "desktop/runner/runner.h"
#include "internal.h"
#include "output/privacy.h"
#include "window/client.h"

/* variables */
static int runner_source_count;
static int runner_room;
static char** runner_window_labels;
static int runner_window_count;
static int* runner_scores;
static const char** runner_alt_names; /* other names the candidates go by */
static char** runner_entries;
static int runner_entry_count;
static char* runner_entry_dir;
static int runner_entry_hidden;

/* function implementations */
static int runner_mode_on(int mode)
{
    switch (mode) {
        case RUNNER_MODE_TERMINAL:
            return settings->runner.terminal && *settings->runner.terminal;
        case RUNNER_MODE_SEARCH:
            return settings->runner.search_url && *settings->runner.search_url;
        default:
            return 1;
    }
}

/* The mode s asks for by its prefix, the longest one if several fit. *off
 * is where the rest starts, past the prefix and any blanks after it. An
 * empty prefix turns its mode off. */
int runner_mode(const char* text, int* offset)
{
    const char* runner_prefixes[RUNNER_MODE_COUNT] = {
        [RUNNER_MODE_COMMAND] = settings->runner.run_prefix,
        [RUNNER_MODE_TERMINAL] = settings->runner.terminal_prefix,
        [RUNNER_MODE_CALCULATOR] = settings->runner.calc_prefix,
        [RUNNER_MODE_SEARCH] = settings->runner.search_prefix,
        [RUNNER_MODE_WINDOW] = settings->runner.window_prefix,
        [RUNNER_MODE_TAG] = settings->runner.tag_prefix,
    };
    size_t best_length = 0;
    int mode = RUNNER_MODE_APPLICATION;

    for (int candidate = 0; candidate < RUNNER_MODE_COUNT; candidate++) {
        const char* prefix = runner_prefixes[candidate];
        if (!prefix)
            continue;
        const size_t prefix_length = strlen(prefix);
        if (!prefix_length || prefix_length <= best_length ||
            strncmp(text, prefix, prefix_length) || !runner_mode_on(candidate))
            continue;
        mode = candidate;
        best_length = prefix_length;
    }
    /* a path is matched by the name after its last slash */
    if (mode == RUNNER_MODE_APPLICATION && settings->runner.files &&
        (*text == '/' || (*text == '~' && (!text[1] || text[1] == '/')))) {
        const char* last_slash = strrchr(text, '/');
        *offset = last_slash ? (int)(last_slash - text) + 1 : 0;
        return RUNNER_MODE_FILE;
    }
    while (best_length && (text[best_length] == ' ' || text[best_length] == '\t'))
        best_length++;
    *offset = (int)best_length;
    return mode;
}

static int runner_fold(int character, int fold)
{
    return fold && character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character;
}

/* 0 when cand doesn't match q. Without runner_fuzzy only a prefix matches,
 * with it the letters of q just have to show up in order. The start of the
 * name, the start of a word and letters typed in a row score higher. */
static int runner_score(const char* candidate, const char* query, int query_length, int fold)
{
    const int candidate_length = (int)strlen(candidate);

    if (candidate_length == query_length && !strcmp(candidate, query))
        return 1 << 24;
    if (!settings->runner.fuzzy)
        return !strncmp(candidate, query, query_length);
    int score = 0;
    int run = 0;
    int query_index = 0;
    int candidate_index = 0;
    for (; candidate[candidate_index] && query_index < query_length; candidate_index++) {
        if (runner_fold((unsigned char)candidate[candidate_index], fold) !=
            runner_fold((unsigned char)query[query_index], fold)) {
            run = 0;
            continue;
        }
        score += 1 + 3 * run;
        if (candidate_index == 0)
            score += 8;
        else if (strchr("-_. ", candidate[candidate_index - 1]))
            score += 4;
        run++;
        query_index++;
    }
    if (query_index < query_length)
        return 0;
    if (candidate_index == query_length)
        score += 20;
    /* the shorter name wins a tie */
    return MAX(score * 8 - (candidate_length - query_length), 1);
}

static int runner_by_score(const void* first, const void* second)
{
    const int left = *(const int*)first;
    const int right = *(const int*)second;

    if (runner_scores[left] != runner_scores[right])
        return runner_scores[right] - runner_scores[left];
    return left - right;
}

/* the open windows, the last focused first */
static void runner_windows(void)
{
    while (runner_window_count)
        free(runner_window_labels[--runner_window_count]);
    size_t window_count = wl_list_length(&focus_stack);
    if (!(runner_window_clients =
              realloc(runner_window_clients, MAX(window_count, 1) * sizeof(WindowClient*))) ||
        !(runner_window_labels =
              realloc(runner_window_labels, MAX(window_count, 1) * sizeof(char*))))
        util_die("realloc:");
    int window_index = 0;
    WindowClient* client;
    wl_list_for_each(client, &focus_stack, focus_link) {
        if (client_is_unmanaged(client) || !client->monitor)
            continue;
        const char* id = client_get_app_id(client);
        const char* title = privacy_share_title(client);
        const size_t label_size = strlen(id) + strlen(title) + 3;
        runner_window_labels[window_index] = util_calloc(1, label_size);
        if (!*title || !strcmp(id, title))
            snprintf(runner_window_labels[window_index], label_size, "%s", id);
        else
            snprintf(runner_window_labels[window_index], label_size, "%s: %s", id, title);
        runner_window_clients[window_index++] = client;
    }
    runner_window_count = window_index;
}

/* the first len bytes of s, ~ standing for $HOME */
char* runner_expand(const char* text, size_t length)
{
    const char* home = getenv("HOME");
    char* out = nullptr;
    size_t out_length = 0;

    if (length && *text == '~' && home) {
        runner_append(&out, &out_length, home, strlen(home));
        text++;
        length--;
    }
    runner_append(&out, &out_length, text, length);
    return out;
}

void runner_free_entries(void)
{
    while (runner_entry_count)
        free(runner_entries[--runner_entry_count]);
    free(runner_entry_dir);
    runner_entry_dir = nullptr;
}

/* what is in the typed directory, dot files only once the name starts with a
 * dot, and a slash after each directory */
static void runner_read_entries(int offset)
{
    const int hidden = runner_buffer[offset] == '.';

    if (!offset) {
        runner_free_entries();
        return;
    }
    char* dir = runner_expand(runner_buffer, offset);
    if (runner_entry_dir && runner_entry_hidden == hidden && !strcmp(dir, runner_entry_dir)) {
        free(dir);
        return;
    }
    runner_free_entries();
    runner_entry_dir = dir;
    runner_entry_hidden = hidden;
    DIR* directory = opendir(dir);
    if (!directory)
        return;
    for (struct dirent* entry; (entry = readdir(directory));) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            (*entry->d_name == '.' && !hidden))
            continue;
        if (!(runner_entries = realloc(runner_entries, (runner_entry_count + 1) * sizeof(char*))))
            util_die("realloc:");
        const size_t name_length = strlen(entry->d_name);
        runner_entries[runner_entry_count] = util_calloc(1, name_length + 2);
        memcpy(runner_entries[runner_entry_count], entry->d_name, name_length);
        struct stat file_info;
        if (fstatat(dirfd(directory), entry->d_name, &file_info, 0) == 0 &&
            S_ISDIR(file_info.st_mode))
            runner_entries[runner_entry_count][name_length] = '/';
        runner_entry_count++;
    }
    closedir(directory);
    qsort(runner_entries, runner_entry_count, sizeof(char*), runner_compare);
}

static void runner_set_source(int mode)
{
    runner_alt_names = nullptr;
    switch (mode) {
        case RUNNER_MODE_APPLICATION:
            runner_source = (const char**)runner_app_names;
            runner_alt_names = (const char**)runner_app_untranslated_names;
            runner_source_count = runner_app_count;
            break;
        case RUNNER_MODE_COMMAND:
        case RUNNER_MODE_TERMINAL:
            runner_source = (const char**)runner_commands;
            runner_source_count = runner_command_count;
            break;
        case RUNNER_MODE_WINDOW:
            runner_windows();
            runner_source = (const char**)runner_window_labels;
            runner_source_count = runner_window_count;
            break;
        case RUNNER_MODE_TAG:
            runner_source = (const char**)settings->look.tags;
            runner_source_count = (int)settings->look.tag_count;
            break;
        case RUNNER_MODE_FILE:
            runner_read_entries(runner_query());
            runner_source = (const char**)runner_entries;
            runner_source_count = runner_entry_count;
            break;
        default:
            runner_source = nullptr;
            runner_source_count = 0;
            break;
    }
    if (runner_source_count > runner_room) {
        runner_room = runner_source_count;
        if (!(runner_matches = realloc(runner_matches, runner_room * sizeof(int))) ||
            !(runner_scores = realloc(runner_scores, runner_room * sizeof(int))))
            util_die("realloc:");
    }
}

void runner_filter(void)
{
    int offset;
    const int mode = runner_mode(runner_buffer, &offset);
    const int query_length = runner_length - offset;
    const char* query = runner_buffer + offset;
    int fold = 1;
    int* use = nullptr;

    runner_match_count = 0;
    runner_selection = 0;
    runner_set_source(mode);
    /* with nothing typed yet a short list shows all of it */
    if (!query_length &&
        (mode == RUNNER_MODE_WINDOW || mode == RUNNER_MODE_TAG || mode == RUNNER_MODE_FILE))
        for (int i = 0; i < runner_source_count; i++)
            runner_matches[runner_match_count++] = i;
    double calc_value;
    if (!query_length || runner_calc(&calc_value))
        return;
    /* an uppercase letter makes the match case sensitive */
    for (int i = 0; i < query_length; i++)
        if (query[i] >= 'A' && query[i] <= 'Z')
            fold = 0;
    if (runner_source == (const char**)runner_commands)
        use = runner_use;
    else if (runner_source == (const char**)runner_app_names)
        use = runner_app_use;
    for (int i = 0; i < runner_source_count; i++) {
        runner_scores[i] = runner_score(runner_source[i], query, query_length, fold);
        if (runner_alt_names && runner_alt_names[i])
            runner_scores[i] =
                MAX(runner_scores[i], runner_score(runner_alt_names[i], query, query_length, fold));
        if (runner_scores[i]) {
            if (use)
                runner_scores[i] += MIN(use[i], 1 << 16) * 16;
            runner_matches[runner_match_count++] = i;
        }
    }
    qsort(runner_matches, runner_match_count, sizeof(int), runner_by_score);
}
