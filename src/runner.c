/*
 * See LICENSE file for copyright and license details.
 *
 * the command prompt and its calculator
 */
#include "g0wn.h"

/* function declarations */
#ifdef RUNNER
static void runnerbuildcache(void);
static void runnerfreecache(void);
static int runnerpathstale(void);
#endif /* RUNNER */

/* variables */
#ifdef RUNNER
static char** runner_cmds;
static int runner_ncmds;
static struct timespec runner_stamp; /* newest PATH mtime the cache was built
                                        from */
#endif                               /* RUNNER */

/* function implementations */
#ifdef RUNNER
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
}

static void runnerfreecache(void)
{
    while (runner_ncmds)
        free(runner_cmds[--runner_ncmds]);
    free(runner_cmds);
    runner_cmds = NULL;
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
    int i;

    if (!runner_len)
        return NULL;
    for (i = 0; i < runner_ncmds; i++)
        if (!strncmp(runner_cmds[i], runner_buf, runner_len))
            return runner_cmds[i];
    return NULL;
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

void runnertoggle(const Arg* arg)
{
    if (!selmon)
        return;
    if (!runner_active) {
        if (runnerpathstale() || !runner_cmds) {
            runnerfreecache();
            runnerbuildcache();
        }
        runner_len = 0;
        runner_cur = 0;
        runner_buf[0] = '\0';
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
    char* argv[4];
    Arg a;

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
        if (runner_cur < runner_len)
            runner_cur++;
    } else if (mods & WLR_MODIFIER_CTRL &&
               xkb_keysym_to_lower(sym) == XKB_KEY_b) {
        if (runner_cur > 0)
            runner_cur--;
    } else if (mods & WLR_MODIFIER_CTRL &&
               xkb_keysym_to_lower(sym) == XKB_KEY_w) {
        /* delete the word behind the cursor: skip trailing spaces, then the
         * run of non-spaces before them, same as a shell's line editor */
        int end = runner_cur;
        while (runner_cur > 0 && runner_buf[runner_cur - 1] == ' ')
            runner_cur--;
        while (runner_cur > 0 && runner_buf[runner_cur - 1] != ' ')
            runner_cur--;
        memmove(
            runner_buf + runner_cur, runner_buf + end, runner_len - end + 1);
        runner_len -= end - runner_cur;
    } else
        switch (sym) {
            case XKB_KEY_Escape:
                runner_active = 0;
                break;
            case XKB_KEY_Return:
            case XKB_KEY_KP_Enter: {
                double calcval;
                sug = runnersuggest();
                cmd = sug ? sug : runner_buf;
                /* A bare expression has no business reaching /bin/sh: it
                 * would just fail as an unknown command. Tab already turns
                 * it into its result for anyone who wants that value. */
                if (*cmd && !(!sug && runnercalc(&calcval))) {
                    argv[0] = "/bin/sh";
                    argv[1] = "-c";
                    argv[2] = (char*)cmd;
                    argv[3] = NULL;
                    a.v = argv;
                    spawn(&a);
                }
                runner_active = 0;
                break;
            }
            case XKB_KEY_BackSpace:
                if (runner_cur) {
                    memmove(runner_buf + runner_cur - 1,
                            runner_buf + runner_cur,
                            runner_len - runner_cur + 1);
                    runner_cur--;
                    runner_len--;
                }
                break;
            case XKB_KEY_Tab:
                sug = runnersuggest();
                if (sug && strlen(sug) < sizeof(runner_buf)) {
                    runner_len = (int)strlen(sug);
                    memcpy(runner_buf, sug, runner_len + 1);
                    runner_cur = runner_len;
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
            default:
                if (codepoint >= 0x20 && codepoint < 0x7f &&
                    runner_len < (int)sizeof(runner_buf) - 1) {
                    memmove(runner_buf + runner_cur + 1,
                            runner_buf + runner_cur,
                            runner_len - runner_cur + 1);
                    runner_buf[runner_cur] = (char)codepoint;
                    runner_cur++;
                    runner_len++;
                }
                break;
        }
    drawselbar();
}

#endif /* RUNNER */
