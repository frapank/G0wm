/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the status text read from stdin, its colours and click areas
 */

#include <drwl.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <wayland-server-core.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/bar/bar.h"
#include "desktop/bar/status.h"
#include "internal.h"
#include "output/monitor.h"

/* function declarations */
static int bar_status_escape(const char* p, uint32_t* scm);
static void bar_status_stop(void);

/* function implementations */
/* whether the bar on m shows the status text */
int bar_status_shown(Monitor* m)
{
    if (bar_status == BarStatusMain)
        return m == bar_main_monitor();
    if (bar_status == BarStatusFocus)
        return bar_single_monitor || m == selected_monitor;
    return 1;
}

/* One escape at p: ^c#RRGGBB^ and ^b#RRGGBB^ set the foreground and the
 * background of what follows (an eight digit form carries alpha too), ^d^
 * goes back to SchemeNorm, ^sN!cmd^ opens a clickable area for button N
 * (1 left, 2 right, 3 middle, 4/5 wheel up/down; ^s!^ is 1) and ^e^ closes
 * it. Returns the bytes it takes, and applies a colour to scm when that is
 * given; zero means p does not start one. */
static int bar_status_escape(const char* p, uint32_t* scm)
{
    static constexpr char hex[] = "0123456789abcdef";
    const char* d;
    uint32_t clr = 0;
    int i;

    if (p[0] != '^')
        return 0;
    if (p[1] == 's') {
        i = p[2] >= '1' && p[2] <= '0' + STATUS_BUTTONS ? 3 : 2;
        if (p[i++] != '!' || !p[i] || p[i] == '^')
            return 0;
        for (; p[i] && p[i] != '^'; i++)
            ;
        return p[i] == '^' ? i + 1 : 0;
    }
    if (p[1] == 'e' && p[2] == '^')
        return 3;
    if (p[1] == 'e' && p[2] == '!' && p[3] == '^')
        return 4;
    if (p[1] == 'd' && p[2] == '^') {
        if (scm) {
            scm[ColFg] = colors[SchemeStatus][ColFg];
            scm[ColBg] = colors[SchemeStatus][ColBg];
        }
        return 3;
    }
    if ((p[1] != 'c' && p[1] != 'b') || p[2] != '#')
        return 0;

    /* | 0x20 lowercases a letter and leaves a digit alone */
    for (i = 0; i < 8 && p[3 + i] && (d = strchr(hex, p[3 + i] | 0x20)); i++)
        clr = clr << 4 | (uint32_t)(d - hex);
    if ((i != 6 && i != 8) || p[3 + i] != '^')
        return 0;
    if (i == 6)
        clr = clr << 8 | 0xff; /* the short form is opaque */

    if (scm)
        scm[p[1] == 'c' ? ColFg : ColBg] = clr;
    return i + 4;
}

/* Draws the status text one colour run at a time, and returns its width
 * without the escapes. With render off it only measures, which is what
 * places the box before there is anything to draw in it. */
int bar_draw_status(Monitor* m, const char* text, int x, int w, int render)
{
    uint32_t scm[3];
    char seg[sizeof(bar_status_text)];
    const char* p = text;
    int total = 0, sw, len, xend = x + w;
    size_t n;

    if (!m || !text)
        return 0;
    memcpy(scm, colors[SchemeStatus], sizeof(scm));

    /* the runs cover the glyphs only, so the padding around them would keep
     * whatever the buffer held */
    if (render) {
        drwl_setscheme(m->drw, colors[SchemeStatus]);
        drwl_rect(m->drw, x, 0, w, m->b.height, 1, 1);
    }

    while (*p) {
        for (n = 0; *p && n + 1 < sizeof(seg);) {
            if (*p == '^') {
                if (p[1] == '^') {
                    seg[n++] = *p;
                    p += 2;
                    continue;
                }
                if (bar_status_escape(p, nullptr))
                    break;
            }
            seg[n++] = *p++;
        }
        seg[n] = '\0';

        sw = (int)drwl_font_getwidth(m->drw, seg);
        total += sw;
        if (render && sw > 0 && x < xend) {
            if (x + sw > xend) /* drwl_text() ellipsizes what it cannot fit */
                sw = xend - x;
            drwl_setscheme(m->drw, scm);
            drwl_text(m->drw, x, 0, sw, m->b.height, 0, seg, 0);
            x += sw;
        }

        if ((len = bar_status_escape(p, scm)))
            p += len;
    }

    return total;
}

/* Copies into cmd the command for button at x pixels into the status text.
 * Must walk the text the same way bar_draw_status() does. */
static int bar_status_area(Monitor* m,
                           const char* text,
                           int x,
                           int button,
                           char* cmd,
                           size_t sz)
{
    char seg[sizeof(bar_status_text)];
    const char* p = text;
    const char* area[STATUS_BUTTONS] = {};
    int sx = 0, len;
    size_t n;

    while (*p) {
        for (n = 0; *p && n + 1 < sizeof(seg);) {
            if (*p == '^') {
                if (p[1] == '^') {
                    seg[n++] = *p;
                    p += 2;
                    continue;
                }
                if (bar_status_escape(p, nullptr))
                    break;
            }
            seg[n++] = *p++;
        }
        seg[n] = '\0';

        sx += (int)drwl_font_getwidth(m->drw, seg);
        if (x < sx) {
            if (!area[button - 1])
                return 0;
            n = strcspn(area[button - 1], "^");
            if (n >= sz)
                n = sz - 1;
            memcpy(cmd, area[button - 1], n);
            cmd[n] = '\0';
            return 1;
        }

        if ((len = bar_status_escape(p, nullptr))) {
            if (p[1] == 's' && p[2] == '!')
                area[0] = p + 3;
            else if (p[1] == 's')
                area[p[2] - '1'] = p + 4;
            else if (p[1] == 'e')
                memset(area, 0, sizeof(area));
            p += len;
        }
    }
    return 0;
}

static int bar_status_command(Monitor* pm,
                              int x,
                              int button,
                              char* cmd,
                              size_t sz)
{
    return pm && bar_status_shown(pm) && button >= 1 &&
           button <= STATUS_BUTTONS &&
           bar_status_area(pm, bar_status_text, x, button, cmd, sz);
}

static void bar_status_run(char* cmd)
{
    char* argv[] = { "/bin/sh", "-c", cmd, nullptr };

    spawn(&(Arg){ .v = argv });
}

int bar_status_click(Monitor* pm, int x, int button)
{
    char cmd[sizeof(bar_status_text)];

    if (!bar_status_command(pm, x, button, cmd, sizeof(cmd)))
        return 0;
    bar_status_run(cmd);
    return 1;
}

int bar_status_scroll(Monitor* pm, int x, double delta)
{
    /* a mouse notch is 15; touchpads send it in slices */
    constexpr double notch = 15;
    static double acc;
    char cmd[sizeof(bar_status_text)];

    if (delta == 0 ||
        !bar_status_command(pm, x, delta < 0 ? 4 : 5, cmd, sizeof(cmd)))
        return 0;
    if ((acc < 0) != (delta < 0))
        acc = 0;
    for (acc += delta; acc >= notch; acc -= notch)
        bar_status_run(cmd);
    for (; acc <= -notch; acc += notch)
        bar_status_run(cmd);
    return 1;
}

int bar_handle_status_input(int fd, unsigned int mask, void*)
{
    static char buf[STATUS_MAX];
    static size_t len;
    static int skip; /* in a line too long for buf, up to its newline */
    char *p, *nl, *line = nullptr;
    ssize_t n;

    if (mask & WL_EVENT_ERROR)
        util_die("status in event error");
    if (mask & WL_EVENT_HANGUP) {
        bar_status_stop();
        return 0;
    }

    n = read(fd, buf + len, sizeof(buf) - 1 - len);
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
            return 0;
        util_die("read:");
    }
    /* EOF: the status process is gone. The fd stays readable forever, so
     * keeping the source alive would spin the event loop at full speed and
     * starve rendering and input. */
    if (n == 0) {
        bar_status_stop();
        return 0;
    }

    len += (size_t)n;
    buf[len] = '\0';
    for (p = buf; (nl = strchr(p, '\n')); p = nl + 1) {
        *nl = '\0';
        if (skip)
            skip = 0;
        else
            line = p;
    }
    if (line) {
        snprintf(bar_status_text, sizeof(bar_status_text), "%s", line);
        bar_draw_all();
    }

    /* keep the start of the next line */
    len = strlen(p);
    memmove(buf, p, len + 1);
    if (len == sizeof(buf) - 1) {
        len = 0;
        skip = 1;
    }

    return 0;
}

/* Detach the bar status text from stdin, for good. */
static void bar_status_stop(void)
{
    if (!status_event_source)
        return;

    wl_event_source_remove(status_event_source);
    status_event_source = nullptr;
}
