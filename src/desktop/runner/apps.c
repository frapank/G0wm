/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * commands from $PATH and desktop applications, with their caches
 */

#include <dirent.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/systray/icon.h"
#include "internal.h"

/* function implementations */
/* $PATH followed by runner_paths, with ~ expanded */
char* runner_search_path(void)
{
    const char* path = getenv("PATH");
    const char* home = getenv("HOME");
    const char* e = runner_paths;
    char* out = nullptr;
    size_t n = 0, len;

    runner_append(&out, &n, path ? path : "", path ? strlen(path) : 0);
    while (e && *e) {
        len = strcspn(e, ":");
        if (len) {
            if (n)
                runner_append(&out, &n, ":", 1);
            if (e[0] == '~' && (len == 1 || e[1] == '/') && home) {
                runner_append(&out, &n, home, strlen(home));
                e++;
                len--;
            }
            runner_append(&out, &n, e, len);
        }
        e += len;
        if (*e == ':')
            e++;
    }
    return out;
}

void runner_build_cache(void)
{
    /* Same idea as dmenu_path: list every executable name under $PATH once,
     * so each keystroke only has to filter an already-built array. */
    char *p = runner_search_path(), *dir, *saveptr;
    char full[1024];
    struct stat st;
    DIR* d;
    struct dirent* e;
    int w = 0;
    size_t cap = 0;

    for (dir = strtok_r(p, ":", &saveptr); dir;
         dir = strtok_r(nullptr, ":", &saveptr)) {
        if (!(d = opendir(dir)))
            continue;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.')
                continue;
            if (snprintf(full, sizeof full, "%s/%s", dir, e->d_name) >=
                (int)sizeof full)
                continue;
            /* Regular files only: +x on a directory just means it can be
             * traversed, so access() alone would list the subdirectories a
             * PATH entry happens to carry as if they were commands. */
            if (stat(full, &st) != 0 || !S_ISREG(st.st_mode) ||
                access(full, X_OK) != 0)
                continue;
            if ((size_t)runner_command_count == cap) {
                cap = cap ? cap * 2 : 256;
                if (!(runner_commands =
                          realloc(runner_commands, cap * sizeof(char*))))
                    util_die("realloc:");
            }
            if (!(runner_commands[runner_command_count++] = strdup(e->d_name)))
                util_die("strdup:");
        }
        closedir(d);
    }
    free(p);

    qsort(runner_commands, runner_command_count, sizeof(char*), runner_compare);
    for (int i = 0; i < runner_command_count; i++) {
        if (w && !strcmp(runner_commands[w - 1], runner_commands[i]))
            free(runner_commands[i]);
        else
            runner_commands[w++] = runner_commands[i];
    }
    runner_command_count = w;

    if (!(runner_use =
              realloc(runner_use, MAX(runner_command_count, 1) * sizeof(int))))
        util_die("realloc:");
}

void runner_free_cache(void)
{
    while (runner_command_count)
        free(runner_commands[--runner_command_count]);
    free(runner_commands);
    runner_commands = nullptr;
    runner_match_count = 0;
    runner_selection = 0;
}

/* the applications directories, the user's first */
char* runner_app_dirs(void)
{
    const char* home = getenv("HOME");
    const char* datahome = getenv("XDG_DATA_HOME");
    const char* datadirs = getenv("XDG_DATA_DIRS");
    char *dirs, *dir, *saveptr, *out = nullptr;
    size_t n = 0;

    if (datahome && *datahome) {
        runner_append(&out, &n, datahome, strlen(datahome));
    } else if (home && *home) {
        runner_append(&out, &n, home, strlen(home));
        runner_append(&out, &n, "/.local/share", 13);
    }
    if (n)
        runner_append(&out, &n, "/applications", 13);
    if (!(dirs = strdup(datadirs && *datadirs ? datadirs
                                              : "/usr/local/share:/usr/share")))
        util_die("strdup:");
    for (dir = strtok_r(dirs, ":", &saveptr); dir;
         dir = strtok_r(nullptr, ":", &saveptr)) {
        if (n)
            runner_append(&out, &n, ":", 1);
        runner_append(&out, &n, dir, strlen(dir));
        runner_append(&out, &n, "/applications", 13);
    }
    free(dirs);
    if (!out)
        runner_append(&out, &n, "", 0);
    return out;
}

static char* runner_trim(char* s)
{
    char* e;

    while (*s == ' ' || *s == '\t')
        s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
        *--e = '\0';
    return s;
}

/* how well a Name key fits the locale, 0 when it doesn't */
static int runner_name_rank(const char* k)
{
    const char* lang = getenv("LC_ALL");
    size_t n;

    if (!strcmp(k, "Name"))
        return 1;
    if (strncmp(k, "Name[", 5) || !*(k + 5))
        return 0;
    if (!lang || !*lang)
        lang = getenv("LC_MESSAGES");
    if (!lang || !*lang)
        lang = getenv("LANG");
    if (!lang)
        return 0;
    k += 5;
    n = strcspn(lang, ".@");
    if (strlen(k) == n + 1 && !strncmp(k, lang, n))
        return 3;
    n = strcspn(lang, "_.@");
    if (strlen(k) == n + 1 && !strncmp(k, lang, n))
        return 2;
    return 0;
}

static void runner_read_app(const char* path, const char* id, int order)
{
    char line[4096], *k, *v, *eq;
    int group = 0, app = 0, rank = 0, r;
    RunnerApp a = {};
    FILE* f;

    if (!(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (*line == '[') {
            group = !strcmp(line, "[Desktop Entry]");
            continue;
        }
        if (!group || *line == '#' || !(eq = strchr(line, '=')))
            continue;
        *eq = '\0';
        k = runner_trim(line);
        v = runner_trim(eq + 1);
        if (!strcmp(k, "Type")) {
            app = !strcmp(v, "Application");
        } else if (!strcmp(k, "Exec") && !a.exec) {
            a.exec = strdup(v);
        } else if (!strcmp(k, "Icon") && !a.icon) {
            a.icon = strdup(v);
        } else if (!strcmp(k, "Terminal")) {
            a.term = !strcmp(v, "true");
        } else if (!strcmp(k, "NoDisplay") || !strcmp(k, "Hidden")) {
            a.hide |= !strcmp(v, "true");
        } else if ((r = runner_name_rank(k)) > rank) {
            free(a.name);
            a.name = strdup(v);
            rank = r;
        }
        if (!strcmp(k, "Name") && !a.gname)
            a.gname = strdup(v);
    }
    fclose(f);
    if (a.gname && a.name && !strcmp(a.gname, a.name)) {
        free(a.gname);
        a.gname = nullptr;
    }

    /* kept even when hidden, it still hides the same id further down */
    a.hide |= !app || !a.name || !a.exec || !*a.exec;
    if (!(a.id = strdup(id)))
        util_die("strdup:");
    a.order = order;
    if (!(runner_apps =
              realloc(runner_apps, (runner_app_count + 1) * sizeof(RunnerApp))))
        util_die("realloc:");
    runner_apps[runner_app_count++] = a;
}

/* desktop file ids take the path below applications/ with / turned to - */
static void runner_scan_apps(const char* dir, const char* prefix, int* order)
{
    char path[PATH_MAX], id[PATH_MAX];
    struct dirent* e;
    struct stat st;
    size_t n;
    DIR* d;

    if (!(d = opendir(dir)))
        return;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >=
                (int)sizeof path ||
            snprintf(id, sizeof id, "%s%s", prefix, e->d_name) >=
                (int)sizeof id ||
            stat(path, &st) != 0)
            continue;
        n = strlen(id);
        if (S_ISDIR(st.st_mode) && n + 1 < sizeof id) {
            strcat(id, "-");
            runner_scan_apps(path, id, order);
        } else if (n > 8 && !strcmp(id + n - 8, ".desktop")) {
            runner_read_app(path, id, (*order)++);
        }
    }
    closedir(d);
}

static int runner_by_id(const void* a, const void* b)
{
    const RunnerApp *x = a, *y = b;
    int c = strcmp(x->id, y->id);

    return c ? c : x->order - y->order;
}

static int runner_by_name(const void* a, const void* b)
{
    return strcmp(((const RunnerApp*)a)->name, ((const RunnerApp*)b)->name);
}

static void runner_free_app(RunnerApp* a)
{
    free(a->id);
    free(a->name);
    free(a->gname);
    free(a->exec);
    free(a->icon);
    if (a->img)
        icon_destroy(a->img);
}

void runner_build_apps(void)
{
    char *p = runner_app_dirs(), *dir, *saveptr;
    int w = 0, order = 0;

    for (dir = strtok_r(p, ":", &saveptr); dir;
         dir = strtok_r(nullptr, ":", &saveptr))
        runner_scan_apps(dir, "", &order);
    free(p);

    /* the first of an id wins, then the hidden ones go */
    qsort(runner_apps, runner_app_count, sizeof(RunnerApp), runner_by_id);
    for (int i = 0; i < runner_app_count; i++) {
        if ((w && !strcmp(runner_apps[w - 1].id, runner_apps[i].id)) ||
            runner_apps[i].hide)
            runner_free_app(&runner_apps[i]);
        else
            runner_apps[w++] = runner_apps[i];
    }
    runner_app_count = w;
    qsort(runner_apps, runner_app_count, sizeof(RunnerApp), runner_by_name);

    if (!(runner_app_names = realloc(
              runner_app_names, MAX(runner_app_count, 1) * sizeof(char*))) ||
        !(runner_app_generic_names =
              realloc(runner_app_generic_names,
                      MAX(runner_app_count, 1) * sizeof(char*))) ||
        !(runner_app_use =
              realloc(runner_app_use, MAX(runner_app_count, 1) * sizeof(int))))
        util_die("realloc:");
    for (int i = 0; i < runner_app_count; i++) {
        runner_app_names[i] = runner_apps[i].name;
        runner_app_generic_names[i] = runner_apps[i].gname;
    }
}

void runner_free_apps(void)
{
    while (runner_app_count)
        runner_free_app(&runner_apps[--runner_app_count]);
    free(runner_apps);
    runner_apps = nullptr;
    runner_match_count = 0;
    runner_selection = 0;
}

/* Exec with the escapes of the file undone and the field codes dropped,
 * there being no files or urls to hand it */
char* runner_app_exec(const char* exec)
{
    char *out = util_calloc(1, strlen(exec) + 1), *p = out;

    for (; *exec; exec++) {
        if (*exec == '\\' && exec[1]) {
            exec++;
            *p++ = *exec == 's'   ? ' '
                   : *exec == 'n' ? '\n'
                   : *exec == 't' ? '\t'
                                  : *exec;
        } else if (*exec == '%' && exec[1]) {
            if (*++exec == '%')
                *p++ = '%';
        } else {
            *p++ = *exec;
        }
    }
    return out;
}

int runner_stale(char* p, char** cache, struct timespec* stamp)
{
    /* A directory's mtime moves whenever an entry is added or removed, so
     * stat()ing the PATH entries - a syscall each, no readdir - answers
     * whether a rescan would turn anything up. That keeps the cost of
     * noticing a newly installed program off every prompt but the one that
     * follows the install. */
    char *dir, *saveptr;
    struct stat st;
    struct timespec newest = { 0, 0 };
    int moved = !*cache || strcmp(p, *cache);

    if (moved) {
        free(*cache);
        if (!(*cache = strdup(p)))
            util_die("strdup:");
    }
    for (dir = strtok_r(p, ":", &saveptr); dir;
         dir = strtok_r(nullptr, ":", &saveptr))
        /* to the nanosecond: at second granularity an install and the removal
         * that follows it within the same second cancel out */
        if (stat(dir, &st) == 0 && (st.st_mtim.tv_sec > newest.tv_sec ||
                                    (st.st_mtim.tv_sec == newest.tv_sec &&
                                     st.st_mtim.tv_nsec > newest.tv_nsec)))
            newest = st.st_mtim;
    free(p);

    if (!moved && newest.tv_sec == stamp->tv_sec &&
        newest.tv_nsec == stamp->tv_nsec)
        return 0;
    *stamp = newest;
    return 1;
}
