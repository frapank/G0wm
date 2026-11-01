/*
 * See LICENSE file for copyright and license details.
 *
 * the command prompt and its calculator
 */
#include <errno.h>
#include <limits.h>

#include "g0wm.h"

/* function declarations */
static void runnerbuildcache(void);
static void runnerfreecache(void);
static int runnerpathstale(void);

/* variables */
static char** runner_cmds;
static int runner_ncmds;
static int* runner_match;
static int runner_nmatch;
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

/* function implementations */
static int runnercmp(const void* a, const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

static void runnerbuildcache(void)
{
    /* Same idea as dmenu_path: list every executable name under $PATH once,
     * so each keystroke only has to filter an already-built array. */
    const char* path = getenv("PATH");
    char *p, *dir, *saveptr;
    char full[1024];
    struct stat st;
    DIR* d;
    struct dirent* e;
    int i, w;
    size_t cap = 0;

    if (!path)
        return;
    if (!(p = strdup(path)))
        die("strdup:");
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

    if (!(runner_match =
              realloc(runner_match, MAX(runner_ncmds, 1) * sizeof(int))) ||
        !(runner_score =
              realloc(runner_score, MAX(runner_ncmds, 1) * sizeof(int))) ||
        !(runner_use = realloc(runner_use, MAX(runner_ncmds, 1) * sizeof(int))))
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

/* how far into s the command starts, past the terminal prefix if any */
static int runnertermoff(const char* s)
{
    size_t n;

    if (!runner_terminal || !*runner_terminal || !runner_termprefix ||
        !(n = strlen(runner_termprefix)) || strncmp(s, runner_termprefix, n))
        return 0;
    while (s[n] == ' ' || s[n] == '\t')
        n++;
    return (int)n;
}

int runnerquery(void)
{
    return runnertermoff(runner_buf);
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

/* Every run counts for the command in its first word, a recent one up to
 * five times as much as the oldest. */
static void runnerweigh(void)
{
    char word[sizeof runner_buf];
    const char* key = word;
    const char* line;
    char** hit;
    size_t n;
    int i;

    for (i = 0; i < runner_ncmds; i++)
        runner_use[i] = 0;
    if (!runner_ncmds)
        return;
    for (i = 0; i < runner_nhist; i++) {
        line = runner_hist[i] + runnertermoff(runner_hist[i]);
        n = strcspn(line, " \t");
        if (n >= sizeof word)
            continue;
        memcpy(word, line, n);
        word[n] = '\0';
        if ((hit = bsearch(
                 &key, runner_cmds, runner_ncmds, sizeof(char*), runnercmp)))
            runner_use[hit - runner_cmds] += 1 + (i + 1) * 4 / runner_nhist;
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

static void runnerfilter(void)
{
    int i, fold = 1, off = runnerquery(), qlen = runner_len - off;
    const char* q = runner_buf + off;
    double calcval;

    runner_nmatch = 0;
    runner_sel = 0;
    if (!qlen || runnercalc(&calcval))
        return;
    /* an uppercase letter makes the match case sensitive */
    for (i = 0; i < qlen; i++)
        if (q[i] >= 'A' && q[i] <= 'Z')
            fold = 0;
    for (i = 0; i < runner_ncmds; i++)
        if ((runner_score[i] = runnerscore(runner_cmds[i], q, qlen, fold))) {
            runner_score[i] += MIN(runner_use[i], 1 << 16) * 16;
            runner_match[runner_nmatch++] = i;
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

static int runnerpathstale(void)
{
    /* A directory's mtime moves whenever an entry is added or removed, so
     * stat()ing the PATH entries - a syscall each, no readdir - answers
     * whether a rescan would turn anything up. That keeps the cost of
     * noticing a newly installed program off every prompt but the one that
     * follows the install. */
    const char* path = getenv("PATH");
    char *p, *dir, *saveptr;
    struct stat st;
    struct timespec newest = { 0, 0 };

    if (!path)
        return 0;
    if (!(p = strdup(path)))
        die("strdup:");
    for (dir = strtok_r(p, ":", &saveptr); dir;
         dir = strtok_r(NULL, ":", &saveptr))
        /* to the nanosecond: at second granularity an install and the removal
         * that follows it within the same second cancel out */
        if (stat(dir, &st) == 0 && (st.st_mtim.tv_sec > newest.tv_sec ||
                                    (st.st_mtim.tv_sec == newest.tv_sec &&
                                     st.st_mtim.tv_nsec > newest.tv_nsec)))
            newest = st.st_mtim;
    free(p);

    if (newest.tv_sec == runner_stamp.tv_sec &&
        newest.tv_nsec == runner_stamp.tv_nsec)
        return 0;
    runner_stamp = newest;
    return 1;
}

const char* runnersuggest(void)
{
    return runner_nmatch ? runner_cmds[runner_match[runner_sel]] : NULL;
}

int runnercandidates(void)
{
    return runner_nmatch;
}

const char* runnercandidate(int i)
{
    return i >= 0 && i < runner_nmatch ? runner_cmds[runner_match[i]] : NULL;
}

int runnerselected(void)
{
    return runner_sel;
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

/* Only a buffer made purely of digits/operators/parens/whitespace, with at
 * least one digit and one operator, is worth trying to parse: this keeps
 * plain PATH lookups (letters) and bare numbers untouched. */
static int runnerisexpr(void)
{
    int i, digit = 0, op = 0;
    char ch;

    for (i = 0; i < runner_len; i++) {
        ch = runner_buf[i];
        if (ch >= '0' && ch <= '9')
            digit = 1;
        else if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '%')
            op = 1;
        else if (ch != '(' && ch != ')' && ch != '.' && ch != ' ' && ch != '\t')
            return 0;
    }
    return digit && op;
}

int runnercalc(double* out)
{
    struct RunnerCalcState c = { runner_buf, 0, 0 };
    double v;

    if (!runnerisexpr())
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
    spawn(&a);
    free(script);
}

void runnertoggle(const Arg* arg)
{
    if (!selmon)
        return;
    if (!runner_active) {
        if (runnerpathstale() || !runner_cmds) {
            runnerfreecache();
            runnerbuildcache();
        }
        if (!runner_histread && runner_history_size)
            runnerreadhist();
        /* runner_history_size may have shrunk on a reload */
        runnertrimhist();
        runnerweigh();
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
    const char* cmd;
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
            case XKB_KEY_KP_Enter: {
                double calcval;
                char line[sizeof runner_buf];
                int off = runnerquery();
                sug = runnersuggest();
                cmd = sug ? sug : runner_buf + off;
                /* A bare expression has no business reaching the shell: it
                 * would just fail as an unknown command. Tab already turns
                 * it into its result for anyone who wants that value. */
                if (off || (*cmd && !(!sug && runnercalc(&calcval)))) {
                    runnerexec(cmd, off > 0);
                    /* with the prefix, so a recalled line keeps it */
                    snprintf(line, sizeof line, "%.*s%s", off, runner_buf, cmd);
                    runnerrecord(line);
                }
                runner_active = 0;
                break;
            }
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
                    tab = 1;
                } else if (!sug) {
                    double calcval;
                    char calcbuf[48];
                    if (runnercalc(&calcval)) {
                        snprintf(calcbuf, sizeof calcbuf, "%.10g", calcval);
                        if (strlen(calcbuf) < sizeof(runner_buf)) {
                            runner_len = (int)strlen(calcbuf);
                            memcpy(runner_buf, calcbuf, runner_len + 1);
                            runner_cur = runner_len;
                        }
                    }
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
