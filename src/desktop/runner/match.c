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
static const char** runner_prefixes[NumModes] = {
    [ModeRun] = &runner_run_prefix,       [ModeTerm] = &runner_terminal_prefix,
    [ModeCalc] = &runner_calc_prefix,     [ModeSearch] = &runner_search_prefix,
    [ModeWindow] = &runner_window_prefix, [ModeTag] = &runner_tag_prefix,
};
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
        case ModeTerm:
            return runner_terminal && *runner_terminal;
        case ModeSearch:
            return runner_search_url && *runner_search_url;
        default:
            return 1;
    }
}

/* The mode s asks for by its prefix, the longest one if several fit. *off
 * is where the rest starts, past the prefix and any blanks after it. An
 * empty prefix turns its mode off. */
int runner_mode(const char* s, int* off)
{
    const char* p;
    size_t n, len = 0;
    int m, mode = ModeApp;

    for (m = 0; m < NumModes; m++) {
        p = runner_prefixes[m] ? *runner_prefixes[m] : nullptr;
        if (!p || !(n = strlen(p)) || n <= len || strncmp(s, p, n) ||
            !runner_mode_on(m))
            continue;
        mode = m;
        len = n;
    }
    /* a path is matched by the name after its last slash */
    if (mode == ModeApp && runner_files &&
        (*s == '/' || (*s == '~' && (!s[1] || s[1] == '/')))) {
        p = strrchr(s, '/');
        *off = p ? (int)(p - s) + 1 : 0;
        return ModeFile;
    }
    while (len && (s[len] == ' ' || s[len] == '\t'))
        len++;
    *off = (int)len;
    return mode;
}

static int runner_fold(int ch, int fold)
{
    return fold && ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch;
}

/* 0 when cand doesn't match q. Without runner_fuzzy only a prefix matches,
 * with it the letters of q just have to show up in order. The start of the
 * name, the start of a word and letters typed in a row score higher. */
static int runner_score(const char* cand, const char* q, int qlen, int fold)
{
    int score = 0, run = 0, i = 0, j, clen = (int)strlen(cand);

    if (clen == qlen && !strcmp(cand, q))
        return 1 << 24;
    if (!runner_fuzzy)
        return !strncmp(cand, q, qlen);
    for (j = 0; cand[j] && i < qlen; j++) {
        if (runner_fold((unsigned char)cand[j], fold) !=
            runner_fold((unsigned char)q[i], fold)) {
            run = 0;
            continue;
        }
        score += 1 + 3 * run;
        if (j == 0)
            score += 8;
        else if (strchr("-_. ", cand[j - 1]))
            score += 4;
        run++;
        i++;
    }
    if (i < qlen)
        return 0;
    if (j == qlen)
        score += 20;
    /* the shorter name wins a tie */
    return MAX(score * 8 - (clen - qlen), 1);
}

static int runner_by_score(const void* a, const void* b)
{
    int x = *(const int*)a, y = *(const int*)b;

    if (runner_scores[x] != runner_scores[y])
        return runner_scores[y] - runner_scores[x];
    return x - y;
}

/* the open windows, the last focused first */
static void runner_windows(void)
{
    const char *id, *title;
    size_t n;
    Client* c;
    int i = 0;

    while (runner_window_count)
        free(runner_window_labels[--runner_window_count]);
    n = wl_list_length(&focus_stack);
    if (!(runner_window_clients =
              realloc(runner_window_clients, MAX(n, 1) * sizeof(Client*))) ||
        !(runner_window_labels =
              realloc(runner_window_labels, MAX(n, 1) * sizeof(char*))))
        util_die("realloc:");
    wl_list_for_each(c, &focus_stack, flink)
    {
        if (client_is_unmanaged(c) || !c->mon)
            continue;
        id = client_get_app_id(c);
        title = privacy_share_title(c);
        n = strlen(id) + strlen(title) + 3;
        runner_window_labels[i] = util_calloc(1, n);
        if (!*title || !strcmp(id, title))
            snprintf(runner_window_labels[i], n, "%s", id);
        else
            snprintf(runner_window_labels[i], n, "%s: %s", id, title);
        runner_window_clients[i++] = c;
    }
    runner_window_count = i;
}

/* the first len bytes of s, ~ standing for $HOME */
char* runner_expand(const char* s, size_t len)
{
    const char* home = getenv("HOME");
    char* out = nullptr;
    size_t n = 0;

    if (len && *s == '~' && home) {
        runner_append(&out, &n, home, strlen(home));
        s++;
        len--;
    }
    runner_append(&out, &n, s, len);
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
static void runner_read_entries(int off)
{
    int hidden = runner_buffer[off] == '.';
    struct dirent* e;
    struct stat st;
    char* dir;
    size_t n;
    DIR* d;

    if (!off) {
        runner_free_entries();
        return;
    }
    dir = runner_expand(runner_buffer, off);
    if (runner_entry_dir && runner_entry_hidden == hidden &&
        !strcmp(dir, runner_entry_dir)) {
        free(dir);
        return;
    }
    runner_free_entries();
    runner_entry_dir = dir;
    runner_entry_hidden = hidden;
    if (!(d = opendir(dir)))
        return;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") ||
            (*e->d_name == '.' && !hidden))
            continue;
        if (!(runner_entries = realloc(
                  runner_entries, (runner_entry_count + 1) * sizeof(char*))))
            util_die("realloc:");
        n = strlen(e->d_name);
        runner_entries[runner_entry_count] = util_calloc(1, n + 2);
        memcpy(runner_entries[runner_entry_count], e->d_name, n);
        if (fstatat(dirfd(d), e->d_name, &st, 0) == 0 && S_ISDIR(st.st_mode))
            runner_entries[runner_entry_count][n] = '/';
        runner_entry_count++;
    }
    closedir(d);
    qsort(runner_entries, runner_entry_count, sizeof(char*), runner_compare);
}

static void runner_set_source(int mode)
{
    runner_alt_names = nullptr;
    switch (mode) {
        case ModeApp:
            runner_source = (const char**)runner_app_names;
            runner_alt_names = (const char**)runner_app_generic_names;
            runner_source_count = runner_app_count;
            break;
        case ModeRun:
        case ModeTerm:
            runner_source = (const char**)runner_commands;
            runner_source_count = runner_command_count;
            break;
        case ModeWindow:
            runner_windows();
            runner_source = (const char**)runner_window_labels;
            runner_source_count = runner_window_count;
            break;
        case ModeTag:
            runner_source = (const char**)tags;
            runner_source_count = (int)tag_count;
            break;
        case ModeFile:
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
        if (!(runner_matches =
                  realloc(runner_matches, runner_room * sizeof(int))) ||
            !(runner_scores =
                  realloc(runner_scores, runner_room * sizeof(int))))
            util_die("realloc:");
    }
}

void runner_filter(void)
{
    int i, fold = 1, off, mode = runner_mode(runner_buffer, &off);
    int qlen = runner_length - off, *use = nullptr;
    const char* q = runner_buffer + off;
    double calcval;

    runner_match_count = 0;
    runner_selection = 0;
    runner_set_source(mode);
    /* with nothing typed yet a short list shows all of it */
    if (!qlen && (mode == ModeWindow || mode == ModeTag || mode == ModeFile))
        for (i = 0; i < runner_source_count; i++)
            runner_matches[runner_match_count++] = i;
    if (!qlen || runner_calc(&calcval))
        return;
    /* an uppercase letter makes the match case sensitive */
    for (i = 0; i < qlen; i++)
        if (q[i] >= 'A' && q[i] <= 'Z')
            fold = 0;
    if (runner_source == (const char**)runner_commands)
        use = runner_use;
    else if (runner_source == (const char**)runner_app_names)
        use = runner_app_use;
    for (i = 0; i < runner_source_count; i++) {
        runner_scores[i] = runner_score(runner_source[i], q, qlen, fold);
        if (runner_alt_names && runner_alt_names[i])
            runner_scores[i] =
                MAX(runner_scores[i],
                    runner_score(runner_alt_names[i], q, qlen, fold));
        if (runner_scores[i]) {
            if (use)
                runner_scores[i] += MIN(use[i], 1 << 16) * 16;
            runner_matches[runner_match_count++] = i;
        }
    }
    qsort(runner_matches, runner_match_count, sizeof(int), runner_by_score);
}
