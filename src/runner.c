/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the command prompt and its calculator
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#include "g0wm.h"

/* function declarations */
static void runnerbuildcache(void);
static void runnerfreecache(void);
static int runnerstale(char* p, char** cache, struct timespec* stamp);

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
static const char** runner_prefixes[NumModes] = {
    [ModeRun] = &runner_runprefix,       [ModeTerm] = &runner_termprefix,
    [ModeCalc] = &runner_calcprefix,     [ModeSearch] = &runner_searchprefix,
    [ModeWindow] = &runner_windowprefix, [ModeTag] = &runner_tagprefix,
};
static char** runner_cmds;
static int runner_ncmds;
static const char** runner_src; /* what the candidates are picked from */
static int runner_nsrc;
static int* runner_match;
static int runner_nmatch;
static int runner_room;
static Client** runner_wins;
static char** runner_winlabels;
static int runner_nwins;
static int runner_sel;
static int runner_cycling;
static int* runner_score;
static int* runner_use;
static char** runner_hist; /* oldest first */
static int runner_nhist;
static int runner_histread;
static int runner_histpos = -1;
static struct timespec runner_stamp; /* newest PATH mtime the cache was built
                                        from */
static char* runner_cachepath;
static RunnerApp* runner_apps; /* sorted by name */
static char** runner_appnames;
static char** runner_appgnames;
static const char** runner_alt; /* other names the candidates go by */
static int runner_napps;
static int* runner_appuse;
static struct timespec runner_appstamp;
static char* runner_appcache;
static char** runner_entries;
static int runner_nentries;
static char* runner_entrydir;
static int runner_entryhidden;

/* function implementations */
static int runnercmp(const void* a, const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

static void runnerappend(char** s, size_t* n, const char* add, size_t len)
{
    if (!(*s = realloc(*s, *n + len + 1)))
        die("realloc:");
    memcpy(*s + *n, add, len);
    *n += len;
    (*s)[*n] = '\0';
}

/* $PATH followed by runner_paths, with ~ expanded */
static char* runnersearchpath(void)
{
    const char* path = getenv("PATH");
    const char* home = getenv("HOME");
    const char* e = runner_paths;
    char* out = NULL;
    size_t n = 0, len;

    runnerappend(&out, &n, path ? path : "", path ? strlen(path) : 0);
    while (e && *e) {
        len = strcspn(e, ":");
        if (len) {
            if (n)
                runnerappend(&out, &n, ":", 1);
            if (e[0] == '~' && (len == 1 || e[1] == '/') && home) {
                runnerappend(&out, &n, home, strlen(home));
                e++;
                len--;
            }
            runnerappend(&out, &n, e, len);
        }
        e += len;
        if (*e == ':')
            e++;
    }
    return out;
}

static void runnerbuildcache(void)
{
    /* Same idea as dmenu_path: list every executable name under $PATH once,
     * so each keystroke only has to filter an already-built array. */
    char *p = runnersearchpath(), *dir, *saveptr;
    char full[1024];
    struct stat st;
    DIR* d;
    struct dirent* e;
    int i, w;
    size_t cap = 0;

    for (dir = strtok_r(p, ":", &saveptr); dir;
         dir = strtok_r(NULL, ":", &saveptr)) {
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
            if ((size_t)runner_ncmds == cap) {
                cap = cap ? cap * 2 : 256;
                if (!(runner_cmds = realloc(runner_cmds, cap * sizeof(char*))))
                    die("realloc:");
            }
            if (!(runner_cmds[runner_ncmds++] = strdup(e->d_name)))
                die("strdup:");
        }
        closedir(d);
    }
    free(p);

    qsort(runner_cmds, runner_ncmds, sizeof(char*), runnercmp);
    for (i = 0, w = 0; i < runner_ncmds; i++) {
        if (w && !strcmp(runner_cmds[w - 1], runner_cmds[i]))
            free(runner_cmds[i]);
        else
            runner_cmds[w++] = runner_cmds[i];
    }
    runner_ncmds = w;

    if (!(runner_use = realloc(runner_use, MAX(runner_ncmds, 1) * sizeof(int))))
        die("realloc:");
}

static void runnerfreecache(void)
{
    while (runner_ncmds)
        free(runner_cmds[--runner_ncmds]);
    free(runner_cmds);
    runner_cmds = NULL;
    runner_nmatch = 0;
    runner_sel = 0;
}

/* the applications directories, the user's first */
static char* runnerappdirs(void)
{
    const char* home = getenv("HOME");
    const char* datahome = getenv("XDG_DATA_HOME");
    const char* datadirs = getenv("XDG_DATA_DIRS");
    char *dirs, *dir, *saveptr, *out = NULL;
    size_t n = 0;

    if (datahome && *datahome) {
        runnerappend(&out, &n, datahome, strlen(datahome));
    } else if (home && *home) {
        runnerappend(&out, &n, home, strlen(home));
        runnerappend(&out, &n, "/.local/share", 13);
    }
    if (n)
        runnerappend(&out, &n, "/applications", 13);
    if (!(dirs = strdup(datadirs && *datadirs ? datadirs
                                              : "/usr/local/share:/usr/share")))
        die("strdup:");
    for (dir = strtok_r(dirs, ":", &saveptr); dir;
         dir = strtok_r(NULL, ":", &saveptr)) {
        if (n)
            runnerappend(&out, &n, ":", 1);
        runnerappend(&out, &n, dir, strlen(dir));
        runnerappend(&out, &n, "/applications", 13);
    }
    free(dirs);
    if (!out)
        runnerappend(&out, &n, "", 0);
    return out;
}

static char* runnertrim(char* s)
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
static int runnernamerank(const char* k)
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

static void runnerreadapp(const char* path, const char* id, int order)
{
    char line[4096], *k, *v, *eq;
    int group = 0, app = 0, rank = 0, r;
    RunnerApp a = { 0 };
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
        k = runnertrim(line);
        v = runnertrim(eq + 1);
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
        } else if ((r = runnernamerank(k)) > rank) {
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
        a.gname = NULL;
    }

    /* kept even when hidden, it still hides the same id further down */
    a.hide |= !app || !a.name || !a.exec || !*a.exec;
    if (!(a.id = strdup(id)))
        die("strdup:");
    a.order = order;
    if (!(runner_apps =
              realloc(runner_apps, (runner_napps + 1) * sizeof(RunnerApp))))
        die("realloc:");
    runner_apps[runner_napps++] = a;
}

/* desktop file ids take the path below applications/ with / turned to - */
static void runnerscanapps(const char* dir, const char* prefix, int* order)
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
            runnerscanapps(path, id, order);
        } else if (n > 8 && !strcmp(id + n - 8, ".desktop")) {
            runnerreadapp(path, id, (*order)++);
        }
    }
    closedir(d);
}

static int runnerbyid(const void* a, const void* b)
{
    const RunnerApp *x = a, *y = b;
    int c = strcmp(x->id, y->id);

    return c ? c : x->order - y->order;
}

static int runnerbyname(const void* a, const void* b)
{
    return strcmp(((const RunnerApp*)a)->name, ((const RunnerApp*)b)->name);
}

static void runnerfreeapp(RunnerApp* a)
{
    free(a->id);
    free(a->name);
    free(a->gname);
    free(a->exec);
    free(a->icon);
    if (a->img)
        destroyicon(a->img);
}

static void runnerbuildapps(void)
{
    char *p = runnerappdirs(), *dir, *saveptr;
    int i, w, order = 0;

    for (dir = strtok_r(p, ":", &saveptr); dir;
         dir = strtok_r(NULL, ":", &saveptr))
        runnerscanapps(dir, "", &order);
    free(p);

    /* the first of an id wins, then the hidden ones go */
    qsort(runner_apps, runner_napps, sizeof(RunnerApp), runnerbyid);
    for (i = 0, w = 0; i < runner_napps; i++) {
        if ((w && !strcmp(runner_apps[w - 1].id, runner_apps[i].id)) ||
            runner_apps[i].hide)
            runnerfreeapp(&runner_apps[i]);
        else
            runner_apps[w++] = runner_apps[i];
    }
    runner_napps = w;
    qsort(runner_apps, runner_napps, sizeof(RunnerApp), runnerbyname);

    if (!(runner_appnames =
              realloc(runner_appnames, MAX(runner_napps, 1) * sizeof(char*))) ||
        !(runner_appgnames = realloc(runner_appgnames,
                                     MAX(runner_napps, 1) * sizeof(char*))) ||
        !(runner_appuse =
              realloc(runner_appuse, MAX(runner_napps, 1) * sizeof(int))))
        die("realloc:");
    for (i = 0; i < runner_napps; i++) {
        runner_appnames[i] = runner_apps[i].name;
        runner_appgnames[i] = runner_apps[i].gname;
    }
}

static void runnerfreeapps(void)
{
    while (runner_napps)
        runnerfreeapp(&runner_apps[--runner_napps]);
    free(runner_apps);
    runner_apps = NULL;
    runner_nmatch = 0;
    runner_sel = 0;
}

/* Exec with the escapes of the file undone and the field codes dropped,
 * there being no files or urls to hand it */
static char* runnerappexec(const char* exec)
{
    char *out = ecalloc(1, strlen(exec) + 1), *p = out;

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

static int runnermodeon(int mode)
{
    switch (mode) {
        case ModeTerm:
            return runner_terminal && *runner_terminal;
        case ModeSearch:
            return runner_searchurl && *runner_searchurl;
        default:
            return 1;
    }
}

/* The mode s asks for by its prefix, the longest one if several fit. *off
 * is where the rest starts, past the prefix and any blanks after it. An
 * empty prefix turns its mode off. */
static int runnermode(const char* s, int* off)
{
    const char* p;
    size_t n, len = 0;
    int m, mode = ModeApp;

    for (m = 0; m < NumModes; m++) {
        p = runner_prefixes[m] ? *runner_prefixes[m] : NULL;
        if (!p || !(n = strlen(p)) || n <= len || strncmp(s, p, n) ||
            !runnermodeon(m))
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

int runnerquery(void)
{
    int off;

    runnermode(runner_buf, &off);
    return off;
}

static const char* runnerhistpath(void)
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
        return NULL;
    return path;
}

static void runnertrimhist(void)
{
    int i, drop = runner_nhist - (int)MIN(runner_history_size, INT_MAX);

    if (drop <= 0)
        return;
    for (i = 0; i < drop; i++)
        free(runner_hist[i]);
    memmove(
        runner_hist, runner_hist + drop, (runner_nhist - drop) * sizeof(char*));
    runner_nhist -= drop;
}

static void runnerpushhist(const char* cmd)
{
    if (!(runner_hist =
              realloc(runner_hist, (runner_nhist + 1) * sizeof(char*))) ||
        !(runner_hist[runner_nhist++] = strdup(cmd)))
        die("strdup:");
}

/* read once, after that every run rewrites the file */
static void runnerreadhist(void)
{
    const char* path = runnerhistpath();
    char line[sizeof runner_buf + 1];
    size_t n;
    FILE* f;

    runner_histread = 1;
    if (!path || !(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        n = strcspn(line, "\n");
        line[n] = '\0';
        if (n && n < sizeof runner_buf)
            runnerpushhist(line);
    }
    fclose(f);
    runnertrimhist();
}

static void runnerwritehist(void)
{
    const char* path = runnerhistpath();
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
    if (mkdirp(dir) < 0 || !(f = fopen(tmp, "w"))) {
        fprintf(stderr, "g0wm: cannot write %s: %s\n", path, strerror(errno));
        return;
    }
    for (i = 0; i < runner_nhist; i++)
        fprintf(f, "%s\n", runner_hist[i]);
    if (fclose(f) != 0 || rename(tmp, path) < 0) {
        fprintf(stderr, "g0wm: cannot write %s: %s\n", path, strerror(errno));
        unlink(tmp);
    }
}

/* Every run counts for the application it opened or the command in its
 * first word, a recent one up to five times as much as the oldest. */
static void runnerweigh(void)
{
    char word[sizeof runner_buf];
    const char* key = word;
    const char* line;
    char** hit;
    size_t n;
    int i, off, mode, w;

    for (i = 0; i < runner_ncmds; i++)
        runner_use[i] = 0;
    for (i = 0; i < runner_napps; i++)
        runner_appuse[i] = 0;
    for (i = 0; i < runner_nhist; i++) {
        w = 1 + (i + 1) * 4 / runner_nhist;
        mode = runnermode(runner_hist[i], &off);
        line = runner_hist[i] + off;
        if (mode == ModeApp && runner_napps) {
            key = line;
            if ((hit = bsearch(&key,
                               runner_appnames,
                               runner_napps,
                               sizeof(char*),
                               runnercmp)))
                runner_appuse[hit - runner_appnames] += w;
            key = word;
        }
        if ((mode != ModeRun && mode != ModeTerm) || !runner_ncmds)
            continue;
        n = strcspn(line, " \t");
        if (n >= sizeof word)
            continue;
        memcpy(word, line, n);
        word[n] = '\0';
        if ((hit = bsearch(
                 &key, runner_cmds, runner_ncmds, sizeof(char*), runnercmp)))
            runner_use[hit - runner_cmds] += w;
    }
}

static void runnerrecord(const char* cmd)
{
    if (!runner_history_size)
        return;
    runnerpushhist(cmd);
    runnertrimhist();
    runnerwritehist();
    runnerweigh();
}

static int runnerfold(int ch, int fold)
{
    return fold && ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch;
}

/* 0 when cand doesn't match q. Without runner_fuzzy only a prefix matches,
 * with it the letters of q just have to show up in order. The start of the
 * name, the start of a word and letters typed in a row score higher. */
static int runnerscore(const char* cand, const char* q, int qlen, int fold)
{
    int score = 0, run = 0, i = 0, j, clen = (int)strlen(cand);

    if (clen == qlen && !strcmp(cand, q))
        return 1 << 24;
    if (!runner_fuzzy)
        return !strncmp(cand, q, qlen);
    for (j = 0; cand[j] && i < qlen; j++) {
        if (runnerfold((unsigned char)cand[j], fold) !=
            runnerfold((unsigned char)q[i], fold)) {
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

static int runnerbyscore(const void* a, const void* b)
{
    int x = *(const int*)a, y = *(const int*)b;

    if (runner_score[x] != runner_score[y])
        return runner_score[y] - runner_score[x];
    return x - y;
}

/* the open windows, the last focused first */
static void runnerwindows(void)
{
    const char *id, *title;
    size_t n;
    Client* c;
    int i = 0;

    while (runner_nwins)
        free(runner_winlabels[--runner_nwins]);
    n = wl_list_length(&fstack);
    if (!(runner_wins = realloc(runner_wins, MAX(n, 1) * sizeof(Client*))) ||
        !(runner_winlabels =
              realloc(runner_winlabels, MAX(n, 1) * sizeof(char*))))
        die("realloc:");
    wl_list_for_each(c, &fstack, flink)
    {
        if (client_is_unmanaged(c) || !c->mon)
            continue;
        id = client_get_appid(c);
        title = sharetitle(c);
        n = strlen(id) + strlen(title) + 3;
        runner_winlabels[i] = ecalloc(1, n);
        if (!*title || !strcmp(id, title))
            snprintf(runner_winlabels[i], n, "%s", id);
        else
            snprintf(runner_winlabels[i], n, "%s: %s", id, title);
        runner_wins[i++] = c;
    }
    runner_nwins = i;
}

/* the first len bytes of s, ~ standing for $HOME */
static char* runnerexpand(const char* s, size_t len)
{
    const char* home = getenv("HOME");
    char* out = NULL;
    size_t n = 0;

    if (len && *s == '~' && home) {
        runnerappend(&out, &n, home, strlen(home));
        s++;
        len--;
    }
    runnerappend(&out, &n, s, len);
    return out;
}

static void runnerfreeentries(void)
{
    while (runner_nentries)
        free(runner_entries[--runner_nentries]);
    free(runner_entrydir);
    runner_entrydir = NULL;
}

/* what is in the typed directory, dot files only once the name starts with a
 * dot, and a slash after each directory */
static void runnerentries(int off)
{
    int hidden = runner_buf[off] == '.';
    struct dirent* e;
    struct stat st;
    char* dir;
    size_t n;
    DIR* d;

    if (!off) {
        runnerfreeentries();
        return;
    }
    dir = runnerexpand(runner_buf, off);
    if (runner_entrydir && runner_entryhidden == hidden &&
        !strcmp(dir, runner_entrydir)) {
        free(dir);
        return;
    }
    runnerfreeentries();
    runner_entrydir = dir;
    runner_entryhidden = hidden;
    if (!(d = opendir(dir)))
        return;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") ||
            (*e->d_name == '.' && !hidden))
            continue;
        if (!(runner_entries = realloc(runner_entries,
                                       (runner_nentries + 1) * sizeof(char*))))
            die("realloc:");
        n = strlen(e->d_name);
        runner_entries[runner_nentries] = ecalloc(1, n + 2);
        memcpy(runner_entries[runner_nentries], e->d_name, n);
        if (fstatat(dirfd(d), e->d_name, &st, 0) == 0 && S_ISDIR(st.st_mode))
            runner_entries[runner_nentries][n] = '/';
        runner_nentries++;
    }
    closedir(d);
    qsort(runner_entries, runner_nentries, sizeof(char*), runnercmp);
}

static void runnersource(int mode)
{
    runner_alt = NULL;
    switch (mode) {
        case ModeApp:
            runner_src = (const char**)runner_appnames;
            runner_alt = (const char**)runner_appgnames;
            runner_nsrc = runner_napps;
            break;
        case ModeRun:
        case ModeTerm:
            runner_src = (const char**)runner_cmds;
            runner_nsrc = runner_ncmds;
            break;
        case ModeWindow:
            runnerwindows();
            runner_src = (const char**)runner_winlabels;
            runner_nsrc = runner_nwins;
            break;
        case ModeTag:
            runner_src = (const char**)tags;
            runner_nsrc = (int)ntags;
            break;
        case ModeFile:
            runnerentries(runnerquery());
            runner_src = (const char**)runner_entries;
            runner_nsrc = runner_nentries;
            break;
        default:
            runner_src = NULL;
            runner_nsrc = 0;
            break;
    }
    if (runner_nsrc > runner_room) {
        runner_room = runner_nsrc;
        if (!(runner_match =
                  realloc(runner_match, runner_room * sizeof(int))) ||
            !(runner_score = realloc(runner_score, runner_room * sizeof(int))))
            die("realloc:");
    }
}

static void runnerfilter(void)
{
    int i, fold = 1, off, mode = runnermode(runner_buf, &off);
    int qlen = runner_len - off, *use = NULL;
    const char* q = runner_buf + off;
    double calcval;

    runner_nmatch = 0;
    runner_sel = 0;
    runnersource(mode);
    /* with nothing typed yet a short list shows all of it */
    if (!qlen && (mode == ModeWindow || mode == ModeTag || mode == ModeFile))
        for (i = 0; i < runner_nsrc; i++)
            runner_match[runner_nmatch++] = i;
    if (!qlen || runnercalc(&calcval))
        return;
    /* an uppercase letter makes the match case sensitive */
    for (i = 0; i < qlen; i++)
        if (q[i] >= 'A' && q[i] <= 'Z')
            fold = 0;
    if (runner_src == (const char**)runner_cmds)
        use = runner_use;
    else if (runner_src == (const char**)runner_appnames)
        use = runner_appuse;
    for (i = 0; i < runner_nsrc; i++) {
        runner_score[i] = runnerscore(runner_src[i], q, qlen, fold);
        if (runner_alt && runner_alt[i])
            runner_score[i] =
                MAX(runner_score[i], runnerscore(runner_alt[i], q, qlen, fold));
        if (runner_score[i]) {
            if (use)
                runner_score[i] += MIN(use[i], 1 << 16) * 16;
            runner_match[runner_nmatch++] = i;
        }
    }
    qsort(runner_match, runner_nmatch, sizeof(int), runnerbyscore);
}

static void runnerselect(int step)
{
    if (runner_nmatch)
        runner_sel = (runner_sel + step + runner_nmatch) % runner_nmatch;
}

/* Up and Down through the history like a shell, skipping repeats. Down past
 * the newest entry clears the prompt. */
static void runnerrecall(int step)
{
    int pos = runner_histpos < 0 ? runner_nhist : runner_histpos;
    const char* shown = runner_histpos < 0 ? "" : runner_hist[runner_histpos];

    if (runner_histpos < 0 && step > 0)
        return;
    do
        pos += step;
    while (pos >= 0 && pos < runner_nhist && !strcmp(runner_hist[pos], shown));
    if (pos < 0)
        return;
    if (pos >= runner_nhist) {
        runner_histpos = -1;
        runner_buf[0] = '\0';
    } else {
        runner_histpos = pos;
        snprintf(runner_buf, sizeof runner_buf, "%s", runner_hist[pos]);
    }
    runner_len = (int)strlen(runner_buf);
    runner_cur = runner_len;
}

static int runnerstale(char* p, char** cache, struct timespec* stamp)
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
            die("strdup:");
    }
    for (dir = strtok_r(p, ":", &saveptr); dir;
         dir = strtok_r(NULL, ":", &saveptr))
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

const char* runnersuggest(void)
{
    return runner_nmatch ? runner_src[runner_match[runner_sel]] : NULL;
}

int runnercandidates(void)
{
    return runner_nmatch;
}

const char* runnercandidate(int i)
{
    return i >= 0 && i < runner_nmatch ? runner_src[runner_match[i]] : NULL;
}

int runnerselected(void)
{
    return runner_sel;
}

int runnerhasicons(void)
{
    return runner_src == (const char**)runner_appnames;
}

/* the icon of candidate i, read the first time it's asked for */
Icon* runnericon(int i)
{
    RunnerApp* a;

    if (!runnerhasicons() || i < 0 || i >= runner_nmatch)
        return NULL;
    a = &runner_apps[runner_match[i]];
    if (!a->imgread) {
        a->imgread = 1;
        a->img = createiconfromname(a->icon, NULL, 64);
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

static double runnercalcexpr(struct RunnerCalcState* c);

/* Every recursive call goes through here first, so one depth check bounds
 * atom, term and expr together; runner_buf is 256 bytes, so even fully
 * parenthesized input like "(((((1)))))" cannot come close to this limit. */
static int runnercalcenter(struct RunnerCalcState* c)
{
    if (++c->depth > 64) {
        c->failed = 1;
        return 0;
    }
    return 1;
}

static double runnercalcnum(struct RunnerCalcState* c)
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

static double runnercalcatom(struct RunnerCalcState* c)
{
    double v;

    if (!runnercalcenter(c))
        return 0;
    while (*c->s == ' ' || *c->s == '\t')
        c->s++;
    if (*c->s == '-') {
        c->s++;
        v = -runnercalcatom(c);
    } else if (*c->s == '+') {
        c->s++;
        v = runnercalcatom(c);
    } else if (*c->s == '(') {
        c->s++;
        v = runnercalcexpr(c);
        while (*c->s == ' ' || *c->s == '\t')
            c->s++;
        if (*c->s != ')')
            c->failed = 1;
        else
            c->s++;
    } else {
        v = runnercalcnum(c);
    }
    c->depth--;
    return v;
}

static double runnercalcterm(struct RunnerCalcState* c)
{
    double v = runnercalcatom(c), rhs;

    for (;;) {
        while (*c->s == ' ' || *c->s == '\t')
            c->s++;
        if (*c->s == '*') {
            c->s++;
            v *= runnercalcatom(c);
        } else if (*c->s == '/') {
            c->s++;
            rhs = runnercalcatom(c);
            if (rhs == 0.0) {
                c->failed = 1;
                return 0;
            }
            v /= rhs;
        } else if (*c->s == '%') {
            c->s++;
            rhs = runnercalcatom(c);
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

static double runnercalcexpr(struct RunnerCalcState* c)
{
    double v = runnercalcterm(c);

    for (;;) {
        while (*c->s == ' ' || *c->s == '\t')
            c->s++;
        if (*c->s == '+') {
            c->s++;
            v += runnercalcterm(c);
        } else if (*c->s == '-') {
            c->s++;
            v -= runnercalcterm(c);
        } else {
            break;
        }
    }
    return v;
}

/* Only digits, operators, parens and blanks are worth parsing. Outside the
 * calculator mode it also takes an operator, so a bare number is left
 * alone. */
static int runnerisexpr(const char* s, int needop)
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
void runnercalcformat(char* out, size_t n, double v)
{
    snprintf(out, n, "%.*g", (int)MIN(MAX(runner_calc_precision, 1), 17), v);
}

int runnercalc(double* out)
{
    int off, mode = runnermode(runner_buf, &off);
    struct RunnerCalcState c = { runner_buf + off, 0, 0 };
    double v;

    if ((mode != ModeApp && mode != ModeCalc) ||
        !runnerisexpr(c.s, mode == ModeApp))
        return 0;
    v = runnercalcexpr(&c);
    while (*c.s == ' ' || *c.s == '\t')
        c.s++;
    if (c.failed || *c.s != '\0' || !isfinite(v))
        return 0;
    *out = v;
    return 1;
}

/* runner_buf holds UTF-8, so the cursor steps over whole codepoints: it skips
 * continuation bytes (10xxxxxx) and only ever rests on a character start. */
static int runnerprev(int i)
{
    if (i > 0)
        do
            i--;
        while (i > 0 && ((unsigned char)runner_buf[i] & 0xC0) == 0x80);
    return i;
}

static int runnernext(int i)
{
    if (i < runner_len)
        do
            i++;
        while (i < runner_len && ((unsigned char)runner_buf[i] & 0xC0) == 0x80);
    return i;
}

/* removes the bytes [from, to) and leaves the cursor at from */
static void runnercut(int from, int to)
{
    memmove(runner_buf + from, runner_buf + to, runner_len - to + 1);
    runner_len -= to - from;
    runner_cur = from;
}

/* In a terminal the command goes to the shell inside it as one argument,
 * so pipes and lists stay in the terminal too. An empty command just opens
 * the terminal. */
static void runnerexec(const char* cmd, int term)
{
    char* sh =
        (char*)(runner_shell && *runner_shell ? runner_shell : "/bin/sh");
    char* argv[6];
    char* script = NULL;
    char *path = NULL, *saved = NULL;
    size_t n;
    Arg a;

    argv[0] = sh;
    argv[1] = "-c";
    argv[2] = (char*)cmd;
    argv[3] = NULL;
    if (term) {
        n = strlen(runner_terminal) + 32;
        script = ecalloc(1, n);
        snprintf(script,
                 n,
                 *cmd ? "exec %s \"$0\" -c \"$1\"" : "exec %s",
                 runner_terminal);
        argv[2] = script;
        argv[3] = sh;
        argv[4] = (char*)cmd;
        argv[5] = NULL;
    }
    a.v = argv;
    /* only the child gets the wider PATH */
    if (runner_paths && *runner_paths) {
        path = runnersearchpath();
        if (getenv("PATH") && !(saved = strdup(getenv("PATH"))))
            die("strdup:");
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

static void runneropen(const char* what)
{
    char* argv[] = { "xdg-open", (char*)what, NULL };

    spawn(&(Arg){ .v = argv });
}

/* runner_searchurl with q in place of its %s, or after it without one */
static void runnersearch(const char* q)
{
    const char* url = runner_searchurl;
    const char* at = strstr(url, "%s");
    char *s, *p;

    p = s = ecalloc(1, strlen(url) + 3 * strlen(q) + 1);
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
    runneropen(s);
    free(s);
}

/* puts the result in place of the expression, after the prefix if any */
static int runnercalcfill(void)
{
    char res[48];
    double v;
    int off;

    if (!runnercalc(&v))
        return 0;
    runnermode(runner_buf, &off);
    runnercalcformat(res, sizeof res, v);
    if (off + strlen(res) >= sizeof runner_buf)
        return 0;
    memcpy(runner_buf + off, res, strlen(res) + 1);
    runner_len = (int)strlen(runner_buf);
    runner_cur = runner_len;
    return 1;
}

static void runnerfocus(Client* c)
{
    Client* w;

    /* it may have closed while the prompt was up */
    wl_list_for_each(w, &clients, link)
    {
        if (w == c) {
            activateclient(c);
            return;
        }
    }
}

static void runnerenter(void)
{
    const char* sug = runnersuggest();
    const char* cmd;
    char line[sizeof runner_buf];
    RunnerApp* app;
    char *exec, *path;
    int off, mode = runnermode(runner_buf, &off);

    runner_active = 0;
    cmd = sug ? sug : runner_buf + off;
    switch (mode) {
        case ModeApp:
            /* nothing to open, or a bare expression that Tab turns into
             * its result */
            if (!sug)
                return;
            app = &runner_apps[runner_match[runner_sel]];
            exec = runnerappexec(app->exec);
            runnerexec(exec, app->term);
            free(exec);
            break;
        case ModeRun:
            if (!*cmd)
                return;
            runnerexec(cmd, 0);
            break;
        case ModeTerm:
            runnerexec(cmd, 1);
            break;
        case ModeCalc:
            /* stays open to go on from the result, or to fix a typo */
            if (runnercalcfill() || runner_len > off)
                runner_active = 1;
            return;
        case ModeSearch:
            if (!*cmd)
                return;
            runnersearch(cmd);
            break;
        case ModeWindow:
            if (sug)
                runnerfocus(runner_wins[runner_match[runner_sel]]);
            return;
        case ModeTag:
            if (sug)
                view(&(Arg){ .ui = 1u << runner_match[runner_sel] });
            return;
        case ModeFile:
            snprintf(line, sizeof line, "%.*s%s", off, runner_buf, cmd);
            path = runnerexpand(line, strlen(line));
            runneropen(path);
            free(path);
            break;
    }
    /* with the prefix, so a recalled line keeps its mode */
    snprintf(line, sizeof line, "%.*s%s", off, runner_buf, cmd);
    runnerrecord(line);
}

void runnertoggle(const Arg* arg)
{
    if (!selmon)
        return;
    if (!runner_active) {
        if (runnerstale(runnersearchpath(), &runner_cachepath, &runner_stamp) ||
            !runner_cmds) {
            runnerfreecache();
            runnerbuildcache();
        }
        if (runnerstale(runnerappdirs(), &runner_appcache, &runner_appstamp) ||
            !runner_apps) {
            runnerfreeapps();
            runnerbuildapps();
        }
        if (!runner_histread && runner_history_size)
            runnerreadhist();
        /* runner_history_size may have shrunk on a reload */
        runnertrimhist();
        runnerweigh();
        runnerfreeentries();
        runner_histpos = -1;
        runner_len = 0;
        runner_cur = 0;
        runner_buf[0] = '\0';
        runnerfilter();
        runner_cycling = 0;
        /* the repeat this very binding is arming must not reach the prompt */
        runner_repeating = 0;
    }
    runner_active = !runner_active;
    drawselbar();
}

void runnerkey(xkb_keysym_t sym, uint32_t mods, uint32_t codepoint)
{
    const char* sug;
    char before[sizeof runner_buf];
    int tab = 0;

    memcpy(before, runner_buf, runner_len + 1);

    /* Ctrl+<letter> line-editing, readline-style. Ctrl doesn't change the
     * keysym a key produces, only mods, so these are matched on the plain
     * letter. Every other Ctrl combination falls through untouched: the
     * codepoint it produces is a control character, which the text case
     * below already rejects. */
    if (mods & WLR_MODIFIER_CTRL && xkb_keysym_to_lower(sym) == XKB_KEY_c) {
        runner_len = 0;
        runner_cur = 0;
        runner_buf[0] = '\0';
    } else if (mods & WLR_MODIFIER_CTRL &&
               xkb_keysym_to_lower(sym) == XKB_KEY_a) {
        runner_cur = 0;
    } else if (mods & WLR_MODIFIER_CTRL &&
               xkb_keysym_to_lower(sym) == XKB_KEY_e) {
        runner_cur = runner_len;
    } else if (mods & WLR_MODIFIER_CTRL &&
               xkb_keysym_to_lower(sym) == XKB_KEY_f) {
        runner_cur = runnernext(runner_cur);
    } else if (mods & WLR_MODIFIER_CTRL &&
               xkb_keysym_to_lower(sym) == XKB_KEY_b) {
        runner_cur = runnerprev(runner_cur);
    } else if (mods & WLR_MODIFIER_CTRL &&
               xkb_keysym_to_lower(sym) == XKB_KEY_w) {
        /* delete the word behind the cursor: skip trailing spaces, then the
         * run of non-spaces before them, same as a shell's line editor.
         * Byte-wise is safe here: no UTF-8 continuation byte is a space. */
        int start = runner_cur;
        while (start > 0 && runner_buf[start - 1] == ' ')
            start--;
        while (start > 0 && runner_buf[start - 1] != ' ')
            start--;
        runnercut(start, runner_cur);
    } else
        switch (sym) {
            case XKB_KEY_Escape:
                runner_active = 0;
                break;
            case XKB_KEY_Return:
            case XKB_KEY_KP_Enter:
                runnerenter();
                break;
            case XKB_KEY_BackSpace:
                runnercut(runnerprev(runner_cur), runner_cur);
                break;
            case XKB_KEY_Delete:
            case XKB_KEY_KP_Delete:
                runnercut(runner_cur, runnernext(runner_cur));
                break;
            case XKB_KEY_Left:
            case XKB_KEY_KP_Left:
                runner_cur = runnerprev(runner_cur);
                break;
            case XKB_KEY_Right:
            case XKB_KEY_KP_Right:
                runner_cur = runnernext(runner_cur);
                break;
            case XKB_KEY_Up:
            case XKB_KEY_KP_Up:
                runnerrecall(-1);
                break;
            case XKB_KEY_Down:
            case XKB_KEY_KP_Down:
                runnerrecall(1);
                break;
            case XKB_KEY_Home:
            case XKB_KEY_KP_Home:
                runner_cur = 0;
                break;
            case XKB_KEY_End:
            case XKB_KEY_KP_End:
                runner_cur = runner_len;
                break;
            case XKB_KEY_Tab:
            case XKB_KEY_ISO_Left_Tab: {
                /* The first Tab takes the selected candidate, the next ones
                 * move to the following candidate. Shift goes backwards. */
                int back =
                    sym == XKB_KEY_ISO_Left_Tab || mods & WLR_MODIFIER_SHIFT;
                if (runner_nmatch && (runner_cycling || back))
                    runnerselect(back ? -1 : 1);
                sug = runnersuggest();
                if (sug && runnerquery() + strlen(sug) < sizeof(runner_buf)) {
                    runner_len = runnerquery() + (int)strlen(sug);
                    memcpy(runner_buf + runnerquery(), sug, strlen(sug) + 1);
                    runner_cur = runner_len;
                    runner_cycling = 1;
                    /* a completed directory lists what is inside it */
                    tab = runnerquery() < runner_len;
                } else if (!sug) {
                    runnercalcfill();
                }
                break;
            }
            default: {
                /* any printable codepoint, stored as UTF-8: C0, DEL and C1
                 * controls are what Ctrl combinations and the like produce,
                 * and putcp() turns away surrogates and out-of-range values */
                char enc[5];
                int n;

                if (codepoint < 0x20 || (codepoint >= 0x7f && codepoint < 0xa0))
                    break;
                n = (int)putcp(enc, sizeof enc, codepoint);
                if (n && runner_len + n < (int)sizeof(runner_buf)) {
                    memmove(runner_buf + runner_cur + n,
                            runner_buf + runner_cur,
                            runner_len - runner_cur + 1);
                    memcpy(runner_buf + runner_cur, enc, n);
                    runner_cur += n;
                    runner_len += n;
                }
                break;
            }
        }
    /* the list stays put while Tab goes through it */
    if (runner_active && !tab && strcmp(before, runner_buf)) {
        runner_cycling = 0;
        runnerfilter();
    }
    drawselbar();
}
