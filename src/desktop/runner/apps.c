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
    const char* entry = runner_paths;
    char* out = nullptr;
    size_t length = 0;

    runner_append(&out, &length, path ? path : "", path ? strlen(path) : 0);
    size_t entry_length;
    while (entry && *entry) {
        entry_length = strcspn(entry, ":");
        if (entry_length) {
            if (length)
                runner_append(&out, &length, ":", 1);
            if (entry[0] == '~' && (entry_length == 1 || entry[1] == '/') && home) {
                runner_append(&out, &length, home, strlen(home));
                entry++;
                entry_length--;
            }
            runner_append(&out, &length, entry, entry_length);
        }
        entry += entry_length;
        if (*entry == ':')
            entry++;
    }
    return out;
}

void runner_build_cache(void)
{
    /* Same idea as dmenu_path: list every executable name under $PATH once,
     * so each keystroke only has to filter an already-built array. */
    char* paths = runner_search_path();
    size_t capacity = 0;

    char* saveptr;
    char full[1024];
    struct stat file_info;
    for (char* dir = strtok_r(paths, ":", &saveptr); dir; dir = strtok_r(nullptr, ":", &saveptr)) {
        DIR* directory = opendir(dir);
        if (!directory)
            continue;
        for (struct dirent* entry; (entry = readdir(directory));) {
            if (entry->d_name[0] == '.')
                continue;
            if (snprintf(full, sizeof full, "%s/%s", dir, entry->d_name) >= (int)sizeof full)
                continue;
            /* Regular files only: +x on a directory just means it can be
             * traversed, so access() alone would list the subdirectories a
             * PATH entry happens to carry as if they were commands. */
            if (stat(full, &file_info) != 0 || !S_ISREG(file_info.st_mode) ||
                access(full, X_OK) != 0)
                continue;
            if ((size_t)runner_command_count == capacity) {
                capacity = capacity ? capacity * 2 : 256;
                if (!(runner_commands = realloc(runner_commands, capacity * sizeof(char*))))
                    util_die("realloc:");
            }
            if (!(runner_commands[runner_command_count++] = strdup(entry->d_name)))
                util_die("strdup:");
        }
        closedir(directory);
    }
    free(paths);

    qsort(runner_commands, runner_command_count, sizeof(char*), runner_compare);
    int unique_count = 0;
    for (int i = 0; i < runner_command_count; i++) {
        if (unique_count && !strcmp(runner_commands[unique_count - 1], runner_commands[i]))
            free(runner_commands[i]);
        else
            runner_commands[unique_count++] = runner_commands[i];
    }
    runner_command_count = unique_count;

    if (!(runner_use = realloc(runner_use, MAX(runner_command_count, 1) * sizeof(int))))
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
    char* out = nullptr;
    size_t length = 0;

    if (datahome && *datahome) {
        runner_append(&out, &length, datahome, strlen(datahome));
    } else if (home && *home) {
        runner_append(&out, &length, home, strlen(home));
        runner_append(&out, &length, "/.local/share", 13);
    }
    if (length)
        runner_append(&out, &length, "/applications", 13);
    char* dirs;
    if (!(dirs = strdup(datadirs && *datadirs ? datadirs : "/usr/local/share:/usr/share")))
        util_die("strdup:");
    char* dir;
    char* saveptr;
    for (dir = strtok_r(dirs, ":", &saveptr); dir; dir = strtok_r(nullptr, ":", &saveptr)) {
        if (length)
            runner_append(&out, &length, ":", 1);
        runner_append(&out, &length, dir, strlen(dir));
        runner_append(&out, &length, "/applications", 13);
    }
    free(dirs);
    if (!out)
        runner_append(&out, &length, "", 0);
    return out;
}

static char* runner_trim(char* text)
{

    while (*text == ' ' || *text == '\t')
        text++;
    char* end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t'))
        *--end = '\0';
    return text;
}

/* how well a Name key fits the locale, 0 when it doesn't */
static int runner_name_rank(const char* key)
{
    const char* lang = getenv("LC_ALL");

    if (!strcmp(key, "Name"))
        return 1;
    if (strncmp(key, "Name[", 5) || !*(key + 5))
        return 0;
    if (!lang || !*lang)
        lang = getenv("LC_MESSAGES");
    if (!lang || !*lang)
        lang = getenv("LANG");
    if (!lang)
        return 0;
    key += 5;
    size_t length = strcspn(lang, ".@");
    if (strlen(key) == length + 1 && !strncmp(key, lang, length))
        return 3;
    length = strcspn(lang, "_.@");
    if (strlen(key) == length + 1 && !strncmp(key, lang, length))
        return 2;
    return 0;
}

static void runner_read_app(const char* path, const char* id, int order)
{
    FILE* file = fopen(path, "r");
    if (!file)
        return;
    int group = 0;
    int is_application = 0;
    int rank = 0;
    RunnerApplication application = {};
    char line[4096];
    while (fgets(line, sizeof line, file)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (*line == '[') {
            group = !strcmp(line, "[Desktop Entry]");
            continue;
        }
        char* equals = strchr(line, '=');
        if (!group || *line == '#' || !equals)
            continue;
        *equals = '\0';
        const char* key = runner_trim(line);
        const char* value = runner_trim(equals + 1);
        if (!strcmp(key, "Type")) {
            is_application = !strcmp(value, "Application");
        } else if (!strcmp(key, "Exec") && !application.exec_command) {
            application.exec_command = strdup(value);
        } else if (!strcmp(key, "Icon") && !application.icon_name) {
            application.icon_name = strdup(value);
        } else if (!strcmp(key, "Terminal")) {
            application.run_in_terminal = !strcmp(value, "true");
        } else if (!strcmp(key, "NoDisplay") || !strcmp(key, "Hidden")) {
            application.is_hidden |= !strcmp(value, "true");
        } else {
            const int name_rank = runner_name_rank(key);
            if (name_rank > rank) {
                free(application.name);
                application.name = strdup(value);
                rank = name_rank;
            }
        }
        if (!strcmp(key, "Name") && !application.untranslated_name)
            application.untranslated_name = strdup(value);
    }
    fclose(file);
    if (application.untranslated_name && application.name &&
        !strcmp(application.untranslated_name, application.name)) {
        free(application.untranslated_name);
        application.untranslated_name = nullptr;
    }

    /* kept even when hidden, it still hides the same id further down */
    application.is_hidden |= !is_application || !application.name || !application.exec_command ||
                             !*application.exec_command;
    if (!(application.desktop_id = strdup(id)))
        util_die("strdup:");
    application.source_order = order;
    if (!(runner_apps = realloc(runner_apps, (runner_app_count + 1) * sizeof(RunnerApplication))))
        util_die("realloc:");
    runner_apps[runner_app_count++] = application;
}

/* desktop file ids take the path below applications/ with / turned to - */
static void runner_scan_apps(const char* dir, const char* prefix, int* order)
{
    char path[PATH_MAX], id[PATH_MAX];

    DIR* directory = opendir(dir);
    if (!directory)
        return;
    struct stat file_info;
    for (struct dirent* entry; (entry = readdir(directory));) {
        if (entry->d_name[0] == '.')
            continue;
        if (snprintf(path, sizeof path, "%s/%s", dir, entry->d_name) >= (int)sizeof path ||
            snprintf(id, sizeof id, "%s%s", prefix, entry->d_name) >= (int)sizeof id ||
            stat(path, &file_info) != 0)
            continue;
        const size_t id_length = strlen(id);
        if (S_ISDIR(file_info.st_mode) && id_length + 1 < sizeof id) {
            strcat(id, "-");
            runner_scan_apps(path, id, order);
        } else if (id_length > 8 && !strcmp(id + id_length - 8, ".desktop")) {
            runner_read_app(path, id, (*order)++);
        }
    }
    closedir(directory);
}

static int runner_by_id(const void* first, const void* second)
{
    const RunnerApplication* left = first;
    const RunnerApplication* right = second;
    const int order = strcmp(left->desktop_id, right->desktop_id);

    return order ? order : left->source_order - right->source_order;
}

static int runner_by_name(const void* first, const void* second)
{
    return strcmp(((const RunnerApplication*)first)->name,
                  ((const RunnerApplication*)second)->name);
}

static void runner_free_app(RunnerApplication* application)
{
    free(application->desktop_id);
    free(application->name);
    free(application->untranslated_name);
    free(application->exec_command);
    free(application->icon_name);
    if (application->icon_image)
        icon_destroy(application->icon_image);
}

void runner_build_apps(void)
{
    char* dirs = runner_app_dirs();
    int order = 0;

    char* saveptr;
    for (char* dir = strtok_r(dirs, ":", &saveptr); dir; dir = strtok_r(nullptr, ":", &saveptr))
        runner_scan_apps(dir, "", &order);
    free(dirs);

    /* the first of an id wins, then the hidden ones go */
    qsort(runner_apps, runner_app_count, sizeof(RunnerApplication), runner_by_id);
    int kept_count = 0;
    for (int i = 0; i < runner_app_count; i++) {
        if ((kept_count &&
             !strcmp(runner_apps[kept_count - 1].desktop_id, runner_apps[i].desktop_id)) ||
            runner_apps[i].is_hidden)
            runner_free_app(&runner_apps[i]);
        else
            runner_apps[kept_count++] = runner_apps[i];
    }
    runner_app_count = kept_count;
    qsort(runner_apps, runner_app_count, sizeof(RunnerApplication), runner_by_name);

    if (!(runner_app_names = realloc(runner_app_names, MAX(runner_app_count, 1) * sizeof(char*))) ||
        !(runner_app_untranslated_names =
              realloc(runner_app_untranslated_names, MAX(runner_app_count, 1) * sizeof(char*))) ||
        !(runner_app_use = realloc(runner_app_use, MAX(runner_app_count, 1) * sizeof(int))))
        util_die("realloc:");
    for (int i = 0; i < runner_app_count; i++) {
        runner_app_names[i] = runner_apps[i].name;
        runner_app_untranslated_names[i] = runner_apps[i].untranslated_name;
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
            *p++ = *exec == 's' ? ' ' : *exec == 'n' ? '\n' : *exec == 't' ? '\t' : *exec;
        } else if (*exec == '%' && exec[1]) {
            if (*++exec == '%')
                *p++ = '%';
        } else {
            *p++ = *exec;
        }
    }
    return out;
}

int runner_stale(char* paths, char** cache, struct timespec* stamp)
{
    /* A directory's mtime moves whenever an entry is added or removed, so
     * stat()ing the PATH entries - a syscall each, no readdir - answers
     * whether a rescan would turn anything up. That keeps the cost of
     * noticing a newly installed program off every prompt but the one that
     * follows the install. */
    struct timespec newest = { 0, 0 };
    int moved = !*cache || strcmp(paths, *cache);

    if (moved) {
        free(*cache);
        if (!(*cache = strdup(paths)))
            util_die("strdup:");
    }
    char* saveptr;
    struct stat file_info;
    for (char* dir = strtok_r(paths, ":", &saveptr); dir; dir = strtok_r(nullptr, ":", &saveptr))
        /* to the nanosecond: at second granularity an install and the removal
         * that follows it within the same second cancel out */
        if (stat(dir, &file_info) == 0 && (file_info.st_mtim.tv_sec > newest.tv_sec ||
                                           (file_info.st_mtim.tv_sec == newest.tv_sec &&
                                            file_info.st_mtim.tv_nsec > newest.tv_nsec)))
            newest = file_info.st_mtim;
    free(paths);

    if (!moved && newest.tv_sec == stamp->tv_sec && newest.tv_nsec == stamp->tv_nsec)
        return 0;
    *stamp = newest;
    return 1;
}
