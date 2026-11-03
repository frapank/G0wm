/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the command prompt and its calculator
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon.h>

#include "bar.h"
#include "client.h"
#include "g0wm.h"
#include "layout.h"
#include "privacy.h"
#include "runner.h"
#include "settings.h"
#include "systray/icon.h"
#include "util.h"

/* function declarations */
static void runner_build_cache(void);
static void runner_free_cache(void);
static int runner_stale(char* p, char** cache, struct timespec* stamp);

/* enums */
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

/* types */
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

/* variables */
int runner_active;
char runner_buffer[256];
int runner_length;
/* cursor offset into runner_buffer, 0..runner_length */
int runner_cursor;
int runner_repeating; /* the armed repeat belongs to the prompt */
static const char** runner_prefixes[NumModes] = {
    [ModeRun] = &runner_run_prefix,       [ModeTerm] = &runner_terminal_prefix,
    [ModeCalc] = &runner_calc_prefix,     [ModeSearch] = &runner_search_prefix,
    [ModeWindow] = &runner_window_prefix, [ModeTag] = &runner_tag_prefix,
};
static char** runner_commands;
static int runner_command_count;
static const char** runner_source; /* what the candidates are picked from */
static int runner_source_count;
static int* runner_matches;
static int runner_match_count;
static int runner_room;
static Client** runner_window_clients;
static char** runner_window_labels;
static int runner_window_count;
static int runner_selection;
static int runner_cycling;
static int* runner_scores;
static int* runner_use;
static char** runner_history; /* oldest first */
static int runner_history_count;
static int runner_history_read;
static int runner_history_pos = -1;
static struct timespec runner_stamp; /* newest PATH mtime the cache was built
                                        from */
static char* runner_cache_path;
static RunnerApp* runner_apps; /* sorted by name */
static char** runner_app_names;
static char** runner_app_generic_names;
static const char** runner_alt_names; /* other names the candidates go by */
static int runner_app_count;
static int* runner_app_use;
static struct timespec runner_app_stamp;
static char* runner_app_cache;
static char** runner_entries;
static int runner_entry_count;
static char* runner_entry_dir;
static int runner_entry_hidden;

/* function implementations */
static int runner_compare(const void* a, const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

static void runner_append(char** s, size_t* n, const char* add, size_t len)
{
    if (!(*s = realloc(*s, *n + len + 1)))
        util_die("realloc:");
    memcpy(*s + *n, add, len);
    *n += len;
    (*s)[*n] = '\0';
}

/* $PATH followed by runner_paths, with ~ expanded */
static char* runner_search_path(void)
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

static void runner_build_cache(void)
{
    /* Same idea as dmenu_path: list every executable name under $PATH once,
     * so each keystroke only has to filter an already-built array. */
    char *p = runner_search_path(), *dir, *saveptr;
    char full[1024];
    struct stat st;
    DIR* d;
    struct dirent* e;
    int i, w;
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
    for (i = 0, w = 0; i < runner_command_count; i++) {
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

static void runner_free_cache(void)
{
    while (runner_command_count)
        free(runner_commands[--runner_command_count]);
    free(runner_commands);
    runner_commands = nullptr;
    runner_match_count = 0;
    runner_selection = 0;
}

/* the applications directories, the user's first */
static char* runner_app_dirs(void)
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

static void runner_build_apps(void)
{
    char *p = runner_app_dirs(), *dir, *saveptr;
    int i, w, order = 0;

    for (dir = strtok_r(p, ":", &saveptr); dir;
         dir = strtok_r(nullptr, ":", &saveptr))
        runner_scan_apps(dir, "", &order);
    free(p);

    /* the first of an id wins, then the hidden ones go */
    qsort(runner_apps, runner_app_count, sizeof(RunnerApp), runner_by_id);
    for (i = 0, w = 0; i < runner_app_count; i++) {
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
    for (i = 0; i < runner_app_count; i++) {
        runner_app_names[i] = runner_apps[i].name;
        runner_app_generic_names[i] = runner_apps[i].gname;
    }
}

static void runner_free_apps(void)
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
static char* runner_app_exec(const char* exec)
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
static int runner_mode(const char* s, int* off)
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

int runner_query(void)
{
    int off;

    runner_mode(runner_buffer, &off);
    return off;
}

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

static void runner_trim_history(void)
{
    int i, drop = runner_history_count - (int)MIN(runner_history_size, INT_MAX);

    if (drop <= 0)
        return;
    for (i = 0; i < drop; i++)
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
static void runner_read_history(void)
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
    int i;

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
    for (i = 0; i < runner_history_count; i++)
        fprintf(f, "%s\n", runner_history[i]);
    if (fclose(f) != 0 || rename(tmp, path) < 0) {
        fprintf(stderr, "g0wm: cannot write %s: %s\n", path, strerror(errno));
        unlink(tmp);
    }
}

/* Every run counts for the application it opened or the command in its
 * first word, a recent one up to five times as much as the oldest. */
static void runner_weigh(void)
{
    char word[sizeof runner_buffer];
    const char* key = word;
    const char* line;
    char** hit;
    size_t n;
    int i, off, mode, w;

    for (i = 0; i < runner_command_count; i++)
        runner_use[i] = 0;
    for (i = 0; i < runner_app_count; i++)
        runner_app_use[i] = 0;
    for (i = 0; i < runner_history_count; i++) {
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

static void runner_record(const char* cmd)
{
    if (!runner_history_size)
        return;
    runner_push_history(cmd);
    runner_trim_history();
    runner_write_history();
    runner_weigh();
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
static char* runner_expand(const char* s, size_t len)
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

static void runner_free_entries(void)
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

static void runner_filter(void)
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

static void runner_select(int step)
{
    if (runner_match_count)
        runner_selection =
            (runner_selection + step + runner_match_count) % runner_match_count;
}

/* Up and Down through the history like a shell, skipping repeats. Down past
 * the newest entry clears the prompt. */
static void runner_recall(int step)
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

static int runner_stale(char* p, char** cache, struct timespec* stamp)
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

const char* runner_suggest(void)
{
    return runner_match_count ? runner_source[runner_matches[runner_selection]]
                              : nullptr;
}

int runner_candidate_count(void)
{
    return runner_match_count;
}

const char* runner_candidate(int i)
{
    return i >= 0 && i < runner_match_count ? runner_source[runner_matches[i]]
                                            : nullptr;
}

int runner_selected(void)
{
    return runner_selection;
}

int runner_has_icons(void)
{
    return runner_source == (const char**)runner_app_names;
}

/* the icon of candidate i, read the first time it's asked for */
Icon* runner_icon(int i)
{
    RunnerApp* a;

    if (!runner_has_icons() || i < 0 || i >= runner_match_count)
        return nullptr;
    a = &runner_apps[runner_matches[i]];
    if (!a->imgread) {
        a->imgread = 1;
        a->img = icon_create_from_name(a->icon, nullptr, 64);
    }
    return a->img;
}

/* +, -, *, /, %, parens, a decimal point and whitespace: nothing here can
 * reach a shell or a file, so a malformed expression can only fail to
 * parse, never do anything. */
struct RunnerCalcState {
    const char* s;
    int depth;
    int failed;
};

static double runner_calc_expr(struct RunnerCalcState* c);

/* Every recursive call goes through here first, so one depth check bounds
 * atom, term and expr together; runner_buffer is 256 bytes, so even fully
 * parenthesized input like "(((((1)))))" cannot come close to this limit. */
static int runner_calc_enter(struct RunnerCalcState* c)
{
    if (++c->depth > 64) {
        c->failed = 1;
        return 0;
    }
    return 1;
}

static double runner_calc_number(struct RunnerCalcState* c)
{
    double v = 0, frac = 0.1;
    int any = 0;

    while (*c->s >= '0' && *c->s <= '9') {
        v = v * 10 + (*c->s - '0');
        c->s++;
        any = 1;
    }
    if (*c->s == '.') {
        c->s++;
        while (*c->s >= '0' && *c->s <= '9') {
            v += (*c->s - '0') * frac;
            frac *= 0.1;
            c->s++;
            any = 1;
        }
    }
    if (!any)
        c->failed = 1;
    return v;
}

static double runner_calc_atom(struct RunnerCalcState* c)
{
    double v;

    if (!runner_calc_enter(c))
        return 0;
    while (*c->s == ' ' || *c->s == '\t')
        c->s++;
    if (*c->s == '-') {
        c->s++;
        v = -runner_calc_atom(c);
    } else if (*c->s == '+') {
        c->s++;
        v = runner_calc_atom(c);
    } else if (*c->s == '(') {
        c->s++;
        v = runner_calc_expr(c);
        while (*c->s == ' ' || *c->s == '\t')
            c->s++;
        if (*c->s != ')')
            c->failed = 1;
        else
            c->s++;
    } else {
        v = runner_calc_number(c);
    }
    c->depth--;
    return v;
}

static double runner_calc_term(struct RunnerCalcState* c)
{
    double v = runner_calc_atom(c), rhs;

    for (;;) {
        while (*c->s == ' ' || *c->s == '\t')
            c->s++;
        if (*c->s == '*') {
            c->s++;
            v *= runner_calc_atom(c);
        } else if (*c->s == '/') {
            c->s++;
            rhs = runner_calc_atom(c);
            if (rhs == 0.0) {
                c->failed = 1;
                return 0;
            }
            v /= rhs;
        } else if (*c->s == '%') {
            c->s++;
            rhs = runner_calc_atom(c);
            if (rhs == 0.0) {
                c->failed = 1;
                return 0;
            }
            v = fmod(v, rhs);
        } else {
            break;
        }
    }
    return v;
}

static double runner_calc_expr(struct RunnerCalcState* c)
{
    double v = runner_calc_term(c);

    for (;;) {
        while (*c->s == ' ' || *c->s == '\t')
            c->s++;
        if (*c->s == '+') {
            c->s++;
            v += runner_calc_term(c);
        } else if (*c->s == '-') {
            c->s++;
            v -= runner_calc_term(c);
        } else {
            break;
        }
    }
    return v;
}

/* Only digits, operators, parens and blanks are worth parsing. Outside the
 * calculator mode it also takes an operator, so a bare number is left
 * alone. */
static int runner_is_expr(const char* s, int needop)
{
    int digit = 0, op = !needop;
    char ch;

    for (; *s; s++) {
        ch = *s;
        if (ch >= '0' && ch <= '9')
            digit = 1;
        else if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '%')
            op = 1;
        else if (ch != '(' && ch != ')' && ch != '.' && ch != ' ' && ch != '\t')
            return 0;
    }
    return digit && op;
}

/* a double has no more than 17 significant digits */
void runner_calc_format(char* out, size_t n, double v)
{
    snprintf(out, n, "%.*g", (int)MIN(MAX(runner_calc_precision, 1), 17), v);
}

int runner_calc(double* out)
{
    int off, mode = runner_mode(runner_buffer, &off);
    struct RunnerCalcState c = { runner_buffer + off, 0, 0 };
    double v;

    if ((mode != ModeApp && mode != ModeCalc) ||
        !runner_is_expr(c.s, mode == ModeApp))
        return 0;
    v = runner_calc_expr(&c);
    while (*c.s == ' ' || *c.s == '\t')
        c.s++;
    if (c.failed || *c.s != '\0' || !isfinite(v))
        return 0;
    *out = v;
    return 1;
}

/* runner_buffer holds UTF-8, so the cursor steps over whole codepoints: it
 * skips continuation bytes (10xxxxxx) and only ever rests on a character start.
 */
static int runner_prev(int i)
{
    if (i > 0)
        do
            i--;
        while (i > 0 && ((unsigned char)runner_buffer[i] & 0xC0) == 0x80);
    return i;
}

static int runner_next(int i)
{
    if (i < runner_length)
        do
            i++;
        while (i < runner_length &&
               ((unsigned char)runner_buffer[i] & 0xC0) == 0x80);
    return i;
}

/* removes the bytes [from, to) and leaves the cursor at from */
static void runner_cut(int from, int to)
{
    memmove(runner_buffer + from, runner_buffer + to, runner_length - to + 1);
    runner_length -= to - from;
    runner_cursor = from;
}

/* In a terminal the command goes to the shell inside it as one argument,
 * so pipes and lists stay in the terminal too. An empty command just opens
 * the terminal. */
static void runner_exec(const char* cmd, int term)
{
    char* sh =
        (char*)(runner_shell && *runner_shell ? runner_shell : "/bin/sh");
    char* argv[6];
    char* script = nullptr;
    char *path = nullptr, *saved = nullptr;
    size_t n;
    Arg a;

    argv[0] = sh;
    argv[1] = "-c";
    argv[2] = (char*)cmd;
    argv[3] = nullptr;
    if (term) {
        n = strlen(runner_terminal) + 32;
        script = util_calloc(1, n);
        snprintf(script,
                 n,
                 *cmd ? "exec %s \"$0\" -c \"$1\"" : "exec %s",
                 runner_terminal);
        argv[2] = script;
        argv[3] = sh;
        argv[4] = (char*)cmd;
        argv[5] = nullptr;
    }
    a.v = argv;
    /* only the child gets the wider PATH */
    if (runner_paths && *runner_paths) {
        path = runner_search_path();
        if (getenv("PATH") && !(saved = strdup(getenv("PATH"))))
            util_die("strdup:");
        setenv("PATH", path, 1);
    }
    spawn(&a);
    if (path) {
        if (saved)
            setenv("PATH", saved, 1);
        else
            unsetenv("PATH");
    }
    free(path);
    free(saved);
    free(script);
}

static void runner_open(const char* what)
{
    char* argv[] = { "xdg-open", (char*)what, nullptr };

    spawn(&(Arg){ .v = argv });
}

/* runner_search_url with q in place of its %s, or after it without one */
static void runner_search(const char* q)
{
    const char* url = runner_search_url;
    const char* at = strstr(url, "%s");
    char *s, *p;

    p = s = util_calloc(1, strlen(url) + 3 * strlen(q) + 1);
    if (at) {
        memcpy(p, url, at - url);
        p += at - url;
    } else {
        p = stpcpy(p, url);
    }
    for (; *q; q++)
        if (isalnum((unsigned char)*q) || strchr("-_.~", *q))
            *p++ = *q;
        else
            p += sprintf(p, "%%%02X", (unsigned char)*q);
    if (at)
        strcpy(p, at + 2);
    runner_open(s);
    free(s);
}

/* puts the result in place of the expression, after the prefix if any */
static int runner_calc_fill(void)
{
    char res[48];
    double v;
    int off;

    if (!runner_calc(&v))
        return 0;
    runner_mode(runner_buffer, &off);
    runner_calc_format(res, sizeof res, v);
    if (off + strlen(res) >= sizeof runner_buffer)
        return 0;
    memcpy(runner_buffer + off, res, strlen(res) + 1);
    runner_length = (int)strlen(runner_buffer);
    runner_cursor = runner_length;
    return 1;
}

static void runner_focus(Client* c)
{
    Client* w;

    /* it may have closed while the prompt was up */
    wl_list_for_each(w, &clients, link)
    {
        if (w == c) {
            client_activate(c);
            return;
        }
    }
}

static void runner_enter(void)
{
    const char* sug = runner_suggest();
    const char* cmd;
    char line[sizeof runner_buffer];
    RunnerApp* app;
    char *exec, *path;
    int off, mode = runner_mode(runner_buffer, &off);

    runner_active = 0;
    cmd = sug ? sug : runner_buffer + off;
    switch (mode) {
        case ModeApp:
            /* nothing to open, or a bare expression that Tab turns into
             * its result */
            if (!sug)
                return;
            app = &runner_apps[runner_matches[runner_selection]];
            exec = runner_app_exec(app->exec);
            runner_exec(exec, app->term);
            free(exec);
            break;
        case ModeRun:
            if (!*cmd)
                return;
            runner_exec(cmd, 0);
            break;
        case ModeTerm:
            runner_exec(cmd, 1);
            break;
        case ModeCalc:
            /* stays open to go on from the result, or to fix a typo */
            if (runner_calc_fill() || runner_length > off)
                runner_active = 1;
            return;
        case ModeSearch:
            if (!*cmd)
                return;
            runner_search(cmd);
            break;
        case ModeWindow:
            if (sug)
                runner_focus(
                    runner_window_clients[runner_matches[runner_selection]]);
            return;
        case ModeTag:
            if (sug)
                layout_view(
                    &(Arg){ .ui = 1u << runner_matches[runner_selection] });
            return;
        case ModeFile:
            snprintf(line, sizeof line, "%.*s%s", off, runner_buffer, cmd);
            path = runner_expand(line, strlen(line));
            runner_open(path);
            free(path);
            break;
    }
    /* with the prefix, so a recalled line keeps its mode */
    snprintf(line, sizeof line, "%.*s%s", off, runner_buffer, cmd);
    runner_record(line);
}

void runner_toggle(const Arg*)
{
    if (!selected_monitor)
        return;
    if (!runner_active) {
        if (runner_stale(
                runner_search_path(), &runner_cache_path, &runner_stamp) ||
            !runner_commands) {
            runner_free_cache();
            runner_build_cache();
        }
        if (runner_stale(
                runner_app_dirs(), &runner_app_cache, &runner_app_stamp) ||
            !runner_apps) {
            runner_free_apps();
            runner_build_apps();
        }
        if (!runner_history_read && runner_history_size)
            runner_read_history();
        /* runner_history_size may have shrunk on a reload */
        runner_trim_history();
        runner_weigh();
        runner_free_entries();
        runner_history_pos = -1;
        runner_length = 0;
        runner_cursor = 0;
        runner_buffer[0] = '\0';
        runner_filter();
        runner_cycling = 0;
        /* the repeat this very binding is arming must not reach the prompt */
        runner_repeating = 0;
    }
    runner_active = !runner_active;
    bar_draw_selected();
}

void runner_key(xkb_keysym_t sym, uint32_t mods, uint32_t codepoint)
{
    const char* sug;
    char before[sizeof runner_buffer];
    int tab = 0;

    memcpy(before, runner_buffer, runner_length + 1);

    /* Ctrl+<letter> line-editing, readline-style. Ctrl doesn't change the
     * keysym a key produces, only mods, so these are matched on the plain
     * letter. Every other Ctrl combination falls through untouched: the
     * codepoint it produces is a control character, which the text case
     * below already rejects. */
    if (mods & WLR_MODIFIER_CTRL && xkb_keysym_to_lower(sym) == XKB_KEY_c) {
        runner_length = 0;
        runner_cursor = 0;
        runner_buffer[0] = '\0';
    } else if (mods & WLR_MODIFIER_CTRL &&
               xkb_keysym_to_lower(sym) == XKB_KEY_a) {
        runner_cursor = 0;
    } else if (mods & WLR_MODIFIER_CTRL &&
               xkb_keysym_to_lower(sym) == XKB_KEY_e) {
        runner_cursor = runner_length;
    } else if (mods & WLR_MODIFIER_CTRL &&
               xkb_keysym_to_lower(sym) == XKB_KEY_f) {
        runner_cursor = runner_next(runner_cursor);
    } else if (mods & WLR_MODIFIER_CTRL &&
               xkb_keysym_to_lower(sym) == XKB_KEY_b) {
        runner_cursor = runner_prev(runner_cursor);
    } else if (mods & WLR_MODIFIER_CTRL &&
               xkb_keysym_to_lower(sym) == XKB_KEY_w) {
        /* delete the word behind the cursor: skip trailing spaces, then the
         * run of non-spaces before them, same as a shell's line editor.
         * Byte-wise is safe here: no UTF-8 continuation byte is a space. */
        int start = runner_cursor;
        while (start > 0 && runner_buffer[start - 1] == ' ')
            start--;
        while (start > 0 && runner_buffer[start - 1] != ' ')
            start--;
        runner_cut(start, runner_cursor);
    } else
        switch (sym) {
            case XKB_KEY_Escape:
                runner_active = 0;
                break;
            case XKB_KEY_Return:
            case XKB_KEY_KP_Enter:
                runner_enter();
                break;
            case XKB_KEY_BackSpace:
                runner_cut(runner_prev(runner_cursor), runner_cursor);
                break;
            case XKB_KEY_Delete:
            case XKB_KEY_KP_Delete:
                runner_cut(runner_cursor, runner_next(runner_cursor));
                break;
            case XKB_KEY_Left:
            case XKB_KEY_KP_Left:
                runner_cursor = runner_prev(runner_cursor);
                break;
            case XKB_KEY_Right:
            case XKB_KEY_KP_Right:
                runner_cursor = runner_next(runner_cursor);
                break;
            case XKB_KEY_Up:
            case XKB_KEY_KP_Up:
                runner_recall(-1);
                break;
            case XKB_KEY_Down:
            case XKB_KEY_KP_Down:
                runner_recall(1);
                break;
            case XKB_KEY_Home:
            case XKB_KEY_KP_Home:
                runner_cursor = 0;
                break;
            case XKB_KEY_End:
            case XKB_KEY_KP_End:
                runner_cursor = runner_length;
                break;
            case XKB_KEY_Tab:
            case XKB_KEY_ISO_Left_Tab: {
                /* The first Tab takes the selected candidate, the next ones
                 * move to the following candidate. Shift goes backwards. */
                int back =
                    sym == XKB_KEY_ISO_Left_Tab || mods & WLR_MODIFIER_SHIFT;
                if (runner_match_count && (runner_cycling || back))
                    runner_select(back ? -1 : 1);
                sug = runner_suggest();
                if (sug &&
                    runner_query() + strlen(sug) < sizeof(runner_buffer)) {
                    runner_length = runner_query() + (int)strlen(sug);
                    memcpy(
                        runner_buffer + runner_query(), sug, strlen(sug) + 1);
                    runner_cursor = runner_length;
                    runner_cycling = 1;
                    /* a completed directory lists what is inside it */
                    tab = runner_query() < runner_length;
                } else if (!sug) {
                    runner_calc_fill();
                }
                break;
            }
            default: {
                /* any printable codepoint, stored as UTF-8: C0, DEL and C1
                 * controls are what Ctrl combinations and the like produce,
                 * and util_put_codepoint() turns away surrogates and
                 * out-of-range values */
                char enc[5];
                int n;

                if (codepoint < 0x20 || (codepoint >= 0x7f && codepoint < 0xa0))
                    break;
                n = (int)util_put_codepoint(enc, sizeof enc, codepoint);
                if (n && runner_length + n < (int)sizeof(runner_buffer)) {
                    memmove(runner_buffer + runner_cursor + n,
                            runner_buffer + runner_cursor,
                            runner_length - runner_cursor + 1);
                    memcpy(runner_buffer + runner_cursor, enc, n);
                    runner_cursor += n;
                    runner_length += n;
                }
                break;
            }
        }
    /* the list stays put while Tab goes through it */
    if (runner_active && !tab && strcmp(before, runner_buffer)) {
        runner_cycling = 0;
        runner_filter();
    }
    bar_draw_selected();
}
