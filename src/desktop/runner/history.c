/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the command history and how often each entry ran
 */

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/runner/runner.h"
#include "internal.h"

/* variables */
static char** runner_history; /* oldest first */
static int runner_history_count;

/* function implementations */
static const char* runner_history_path(void)
{
    static char path[PATH_MAX];
    const char* dir = getenv("XDG_STATE_HOME");
    const char* home = getenv("HOME");

    if (dir && *dir)
        snprintf(path, sizeof path, "%s/g0wm/runner_history", dir);
    else if (home && *home)
        snprintf(
            path, sizeof path, "%s/.local/state/g0wm/runner_history", home);
    else
        return nullptr;
    return path;
}

void runner_trim_history(void)
{
    int drop = runner_history_count - (int)MIN(runner_history_size, INT_MAX);

    if (drop <= 0)
        return;
    for (int i = 0; i < drop; i++)
        free(runner_history[i]);
    memmove(runner_history,
            runner_history + drop,
            (runner_history_count - drop) * sizeof(char*));
    runner_history_count -= drop;
}

static void runner_push_history(const char* cmd)
{
    if (!(runner_history = realloc(
              runner_history, (runner_history_count + 1) * sizeof(char*))) ||
        !(runner_history[runner_history_count++] = strdup(cmd)))
        util_die("strdup:");
}

/* read once, after that every run rewrites the file */
void runner_read_history(void)
{
    const char* path = runner_history_path();
    char line[sizeof runner_buffer + 1];
    size_t n;
    FILE* f;

    runner_history_read = 1;
    if (!path || !(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        n = strcspn(line, "\n");
        line[n] = '\0';
        if (n && n < sizeof runner_buffer)
            runner_push_history(line);
    }
    fclose(f);
    runner_trim_history();
}

static void runner_write_history(void)
{
    const char* path = runner_history_path();
    char tmp[PATH_MAX + 4], dir[PATH_MAX];
    char* slash;
    FILE* f;

    if (!path)
        return;
    snprintf(dir, sizeof dir, "%s", path);
    if ((slash = strrchr(dir, '/')))
        *slash = '\0';
    snprintf(tmp, sizeof tmp, "%s.new", path);
    if (settings_make_dirs(dir) < 0 || !(f = fopen(tmp, "w"))) {
        fprintf(stderr, "g0wm: cannot write %s: %s\n", path, strerror(errno));
        return;
    }
    for (int i = 0; i < runner_history_count; i++)
        fprintf(f, "%s\n", runner_history[i]);
    if (fclose(f) != 0 || rename(tmp, path) < 0) {
        fprintf(stderr, "g0wm: cannot write %s: %s\n", path, strerror(errno));
        unlink(tmp);
    }
}

/* Every run counts for the application it opened or the command in its
 * first word, a recent one up to five times as much as the oldest. */
void runner_weigh(void)
{
    char word[sizeof runner_buffer];
    const char* key = word;
    const char* line;
    char** hit;
    size_t n;
    int off, mode, w;

    for (int i = 0; i < runner_command_count; i++)
        runner_use[i] = 0;
    for (int i = 0; i < runner_app_count; i++)
        runner_app_use[i] = 0;
    for (int i = 0; i < runner_history_count; i++) {
        w = 1 + (i + 1) * 4 / runner_history_count;
        mode = runner_mode(runner_history[i], &off);
        line = runner_history[i] + off;
        if (mode == ModeApp && runner_app_count) {
            key = line;
            if ((hit = bsearch(&key,
                               runner_app_names,
                               runner_app_count,
                               sizeof(char*),
                               runner_compare)))
                runner_app_use[hit - runner_app_names] += w;
            key = word;
        }
        if ((mode != ModeRun && mode != ModeTerm) || !runner_command_count)
            continue;
        n = strcspn(line, " \t");
        if (n >= sizeof word)
            continue;
        memcpy(word, line, n);
        word[n] = '\0';
        if ((hit = bsearch(&key,
                           runner_commands,
                           runner_command_count,
                           sizeof(char*),
                           runner_compare)))
            runner_use[hit - runner_commands] += w;
    }
}

void runner_record(const char* cmd)
{
    if (!runner_history_size)
        return;
    runner_push_history(cmd);
    runner_trim_history();
    runner_write_history();
    runner_weigh();
}

/* Up and Down through the history like a shell, skipping repeats. Down past
 * the newest entry clears the prompt. */
void runner_recall(int step)
{
    int pos =
        runner_history_pos < 0 ? runner_history_count : runner_history_pos;
    const char* shown =
        runner_history_pos < 0 ? "" : runner_history[runner_history_pos];

    if (runner_history_pos < 0 && step > 0)
        return;
    do
        pos += step;
    while (pos >= 0 && pos < runner_history_count &&
           !strcmp(runner_history[pos], shown));
    if (pos < 0)
        return;
    if (pos >= runner_history_count) {
        runner_history_pos = -1;
        runner_buffer[0] = '\0';
    } else {
        runner_history_pos = pos;
        snprintf(
            runner_buffer, sizeof runner_buffer, "%s", runner_history[pos]);
    }
    runner_length = (int)strlen(runner_buffer);
    runner_cursor = runner_length;
}
