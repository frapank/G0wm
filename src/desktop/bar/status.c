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
static int bar_status_escape(const char* text, uint32_t* scheme);
static void bar_status_stop(void);

/* function implementations */
/* whether the bar on m shows the status text */
int bar_status_shown(Monitor* monitor)
{
    if (settings->bar.status == BAR_STATUS_MAIN)
        return monitor == bar_main_monitor();
    if (settings->bar.status == BAR_STATUS_FOCUSED)
        return settings->bar.single_monitor || monitor == selected_monitor;
    return 1;
}

/* One escape at text: ^c#RRGGBB^ and ^b#RRGGBB^ set the foreground and the
 * background of what follows (an eight digit form carries alpha too), ^d^
 * goes back to SchemeNorm, ^sN!cmd^ opens a clickable area for button N
 * (1 left, 2 right, 3 middle, 4/5 wheel up/down; ^s!^ is 1) and ^e^ closes
 * it. Returns the bytes it takes, and applies a colour to scheme when that is
 * given; zero means text does not start one. */
static int bar_status_escape(const char* text, uint32_t* scheme)
{
    if (text[0] != '^')
        return 0;

    /* start cliccable area */
    if (text[1] == 's') {
        int bang_index = text[2] >= '1' && text[2] <= '0' + STATUS_BUTTONS ? 3 : 2;
        int cmd_end = bang_index + 1;

        if (text[bang_index] != '!' || !text[cmd_end] || text[cmd_end] == '^')
            return 0;
        while (text[cmd_end] && text[cmd_end] != '^')
            cmd_end++;
        return text[cmd_end] == '^' ? cmd_end + 1 : 0;
    }

    /* cmd_end cliccable area */
    if (text[1] == 'e' && text[2] == '^')
        return 3;
    if (text[1] == 'e' && text[2] == '!' && text[3] == '^')
        return 4;

    /* retrun to default colors */
    if (text[1] == 'd' && text[2] == '^') {
        if (scheme) {
            scheme[ColFg] = settings->look.colors[COLOR_SCHEME_STATUS][ColFg];
            scheme[ColBg] = settings->look.colors[COLOR_SCHEME_STATUS][ColBg];
        }
        return 3;
    }

    if ((text[1] != 'c' && text[1] != 'b') || text[2] != '#')
        return 0;

    static constexpr char hex_digits[] = "0123456789abcdef";
    const char* digits_start = text + 3;
    uint32_t color = 0;
    int digit_count = 0;

    /* | 0x20 lowercases a letter and leaves a digit alone */
    for (const char* digit_pos; digit_count < 8 && digits_start[digit_count] &&
                                (digit_pos = strchr(hex_digits, digits_start[digit_count] | 0x20));
         digit_count++)
        color = color << 4 | (uint32_t)(digit_pos - hex_digits);
    if ((digit_count != 6 && digit_count != 8) || digits_start[digit_count] != '^')
        return 0;
    if (digit_count == 6)
        color = color << 8 | 0xff; /* the short form is opaque */

    if (scheme)
        scheme[text[1] == 'c' ? ColFg : ColBg] = color;
    return digit_count + 4;
}

/* Draws the status text one colour run at a time, and returns its width
 * without the escapes. With render off it only measures, which is what
 * places the box before there is anything to draw in it. */
int bar_draw_status(Monitor* monitor, const char* text, int x, int width, int render)
{
    if (!monitor || !text)
        return 0;

    uint32_t scheme[3];
    memcpy(scheme, settings->look.colors[COLOR_SCHEME_STATUS], sizeof(scheme));

    /* the runs cover the glyphs only, so the padding around them would keep
     * whatever the buffer held */
    if (render) {
        drwl_setscheme(monitor->draw_context, settings->look.colors[COLOR_SCHEME_STATUS]);
        drwl_rect(monitor->draw_context, x, 0, width, monitor->bar_metrics.height_px, 1, 1);
    }

    const int x_end = x + width;
    const char* text_pos = text;
    int total_width = 0;

    char segment[sizeof(bar_status_text)];
    while (*text_pos) {
        size_t segment_len = 0;

        while (*text_pos && segment_len + 1 < sizeof(segment)) {
            if (*text_pos == '^') {
                if (text_pos[1] == '^') {
                    segment[segment_len++] = *text_pos;
                    text_pos += 2;
                    continue;
                }
                if (bar_status_escape(text_pos, nullptr))
                    break;
            }
            segment[segment_len++] = *text_pos++;
        }
        segment[segment_len] = '\0';

        int segment_width = (int)drwl_font_getwidth(monitor->draw_context, segment);
        total_width += segment_width;
        if (render && segment_width > 0 && x < x_end) {
            /* drwl_text() ellipsizes what it cannot fit */
            if (x + segment_width > x_end)
                segment_width = x_end - x;
            drwl_setscheme(monitor->draw_context, scheme);
            drwl_text(monitor->draw_context, x, 0, segment_width, monitor->bar_metrics.height_px, 0,
                      segment, 0);
            x += segment_width;
        }

        text_pos += bar_status_escape(text_pos, scheme);
    }

    return total_width;
}

/* Copies into cmd the command for button at pointer_x pixels into the status
 * text. Must walk the text the same way bar_draw_status() does. */
static int bar_status_area(Monitor* monitor,
                           const char* text,
                           int pointer_x,
                           int button,
                           char* cmd,
                           size_t cmd_size)
{
    const char* text_pos = text;
    const char* button_cmds[STATUS_BUTTONS] = {};
    int segment_end_x = 0;

    char segment[sizeof(bar_status_text)];
    while (*text_pos) {
        size_t segment_len = 0;

        while (*text_pos && segment_len + 1 < sizeof(segment)) {
            if (*text_pos == '^') {
                if (text_pos[1] == '^') {
                    segment[segment_len++] = *text_pos;
                    text_pos += 2;
                    continue;
                }
                if (bar_status_escape(text_pos, nullptr))
                    break;
            }
            segment[segment_len++] = *text_pos++;
        }
        segment[segment_len] = '\0';

        segment_end_x += (int)drwl_font_getwidth(monitor->draw_context, segment);
        if (pointer_x < segment_end_x) {
            if (!button_cmds[button - 1])
                return 0;
            size_t cmd_len = strcspn(button_cmds[button - 1], "^");
            if (cmd_len >= cmd_size)
                cmd_len = cmd_size - 1;
            memcpy(cmd, button_cmds[button - 1], cmd_len);
            cmd[cmd_len] = '\0';
            return 1;
        }

        const int escape_len = bar_status_escape(text_pos, nullptr);
        if (escape_len) {
            if (text_pos[1] == 's' && text_pos[2] == '!')
                button_cmds[0] = text_pos + 3;
            else if (text_pos[1] == 's')
                button_cmds[text_pos[2] - '1'] = text_pos + 4;
            else if (text_pos[1] == 'e')
                memset(button_cmds, 0, sizeof(button_cmds));
            text_pos += escape_len;
        }
    }
    return 0;
}

static int bar_status_command(Monitor* monitor,
                              int pointer_x,
                              int button,
                              char* cmd,
                              size_t cmd_size)
{
    return monitor && bar_status_shown(monitor) && button >= 1 && button <= STATUS_BUTTONS &&
           bar_status_area(monitor, bar_status_text, pointer_x, button, cmd, cmd_size);
}

static void bar_status_run(char* cmd)
{
    char* argv[] = { "/bin/sh", "-c", cmd, nullptr };

    spawn(&(ActionArg){ .pointer = argv });
}

int bar_status_click(Monitor* monitor, int pointer_x, int button)
{
    char cmd[sizeof(bar_status_text)];

    if (!bar_status_command(monitor, pointer_x, button, cmd, sizeof(cmd)))
        return 0;
    bar_status_run(cmd);
    return 1;
}

int bar_status_scroll(Monitor* monitor, int pointer_x, double scroll_delta)
{
    /* a mouse notch is 15; touchpads send it in slices */
    constexpr double notch = 15;
    static double scroll_accumulated;
    char cmd[sizeof(bar_status_text)];

    if (scroll_delta == 0 ||
        !bar_status_command(monitor, pointer_x, scroll_delta < 0 ? 4 : 5, cmd, sizeof(cmd)))
        return 0;
    if ((scroll_accumulated < 0) != (scroll_delta < 0))
        scroll_accumulated = 0;
    for (scroll_accumulated += scroll_delta; scroll_accumulated >= notch;
         scroll_accumulated -= notch)
        bar_status_run(cmd);
    for (; scroll_accumulated <= -notch; scroll_accumulated += notch)
        bar_status_run(cmd);
    return 1;
}

int bar_handle_status_input(int fd, unsigned int event_mask, void*)
{
    static char input_buffer[STATUS_MAX];
    static size_t input_len;
    /* in a line too long for input_buffer, up to its newline */
    static int skipping_long_line;

    if (event_mask & WL_EVENT_ERROR)
        util_die("status in event error");
    if (event_mask & WL_EVENT_HANGUP) {
        bar_status_stop();
        return 0;
    }

    const ssize_t bytes_read =
        read(fd, input_buffer + input_len, sizeof(input_buffer) - 1 - input_len);
    if (bytes_read < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
            return 0;
        util_die("read:");
    }
    /* EOF: the status process is gone. The fd stays readable forever, so
     * keeping the source alive would spin the event loop at full speed and
     * starve rendering and input. */
    if (bytes_read == 0) {
        bar_status_stop();
        return 0;
    }

    input_len += (size_t)bytes_read;
    input_buffer[input_len] = '\0';

    char* line_start = input_buffer;
    char* latest_line = nullptr;
    for (char* line_end; (line_end = strchr(line_start, '\n')); line_start = line_end + 1) {
        *line_end = '\0';
        if (skipping_long_line)
            skipping_long_line = 0;
        else
            latest_line = line_start;
    }
    if (latest_line && strcmp(bar_status_text, latest_line)) {
        snprintf(bar_status_text, sizeof(bar_status_text), "%s", latest_line);
        bar_draw_all();
    }

    /* keep the start of the next line */
    input_len = strlen(line_start);
    memmove(input_buffer, line_start, input_len + 1);
    if (input_len == sizeof(input_buffer) - 1) {
        input_len = 0;
        skipping_long_line = 1;
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
