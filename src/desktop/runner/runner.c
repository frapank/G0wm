/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the command prompt, its keys and running what was picked
 */

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/bar/bar.h"
#include "desktop/runner/runner.h"
#include "desktop/systray/icon.h"
#include "internal.h"
#include "output/monitor.h"
#include "window/client.h"
#include "window/layout.h"

/* variables */
int runner_active;
char runner_buffer[256];
int runner_length;
/* cursor offset into runner_buffer, 0..runner_length */
int runner_cursor;
int runner_repeating; /* the armed repeat belongs to the prompt */
char** runner_commands;
int runner_command_count;
const char** runner_source; /* what the candidates are picked from */
int* runner_matches;
int runner_match_count;
WindowClient** runner_window_clients;
int runner_selection;
static int runner_cycling;
int* runner_use;
int runner_history_read;
int runner_history_pos = -1;
static struct timespec runner_stamp; /* newest PATH mtime the cache was built
                                        from */
static char* runner_cache_path;
RunnerApplication* runner_apps; /* sorted by name */
char** runner_app_names;
char** runner_app_untranslated_names;
int runner_app_count;
int* runner_app_use;
static struct timespec runner_app_stamp;
static char* runner_app_cache;

/* function implementations */
int runner_compare(const void* first, const void* second)
{
    return strcmp(*(char* const*)first, *(char* const*)second);
}

void runner_append(char** text, size_t* length, const char* addition, size_t addition_length)
{
    if (!(*text = realloc(*text, *length + addition_length + 1)))
        util_die("realloc:");
    memcpy(*text + *length, addition, addition_length);
    *length += addition_length;
    (*text)[*length] = '\0';
}

int runner_query(void)
{
    int off;

    runner_mode(runner_buffer, &off);
    return off;
}

static void runner_select(int step)
{
    if (runner_match_count)
        runner_selection = (runner_selection + step + runner_match_count) % runner_match_count;
}

const char* runner_suggest(void)
{
    return runner_match_count ? runner_source[runner_matches[runner_selection]] : nullptr;
}

int runner_candidate_count(void)
{
    return runner_match_count;
}

const char* runner_candidate(int i)
{
    return i >= 0 && i < runner_match_count ? runner_source[runner_matches[i]] : nullptr;
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
RasterIcon* runner_icon(int match_index)
{
    if (!runner_has_icons() || match_index < 0 || match_index >= runner_match_count)
        return nullptr;
    RunnerApplication* application = &runner_apps[runner_matches[match_index]];
    if (!application->icon_load_attempted) {
        application->icon_load_attempted = 1;
        application->icon_image = icon_create_from_name(application->icon_name, nullptr, 64);
    }
    return application->icon_image;
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
        while (i < runner_length && ((unsigned char)runner_buffer[i] & 0xC0) == 0x80);
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
    char* shell = (char*)(runner_shell && *runner_shell ? runner_shell : "/bin/sh");
    char* script = nullptr;
    char* path = nullptr;
    char* saved = nullptr;

    char* argv[6];
    argv[0] = shell;
    argv[1] = "-c";
    argv[2] = (char*)cmd;
    argv[3] = nullptr;
    if (term) {
        const size_t script_size = strlen(runner_terminal) + 32;
        script = util_calloc(1, script_size);
        snprintf(script, script_size, *cmd ? "exec %s \"$0\" -c \"$1\"" : "exec %s",
                 runner_terminal);
        argv[2] = script;
        argv[3] = shell;
        argv[4] = (char*)cmd;
        argv[5] = nullptr;
    }
    const ActionArg spawn_arg = { .pointer = argv };
    /* only the child gets the wider PATH */
    if (runner_paths && *runner_paths) {
        path = runner_search_path();
        if (getenv("PATH") && !(saved = strdup(getenv("PATH"))))
            util_die("strdup:");
        setenv("PATH", path, 1);
    }
    spawn(&spawn_arg);
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

    spawn(&(ActionArg){ .pointer = argv });
}

/* runner_search_url with q in place of its %s, or after it without one */
static void runner_search(const char* query)
{
    const char* url = runner_search_url;
    const char* placeholder = strstr(url, "%s");
    char* result = util_calloc(1, strlen(url) + 3 * strlen(query) + 1);
    char* write_at = result;
    if (placeholder) {
        memcpy(write_at, url, placeholder - url);
        write_at += placeholder - url;
    } else {
        write_at = stpcpy(write_at, url);
    }
    for (; *query; query++)
        if (isalnum((unsigned char)*query) || strchr("-_.~", *query))
            *write_at++ = *query;
        else
            write_at += sprintf(write_at, "%%%02X", (unsigned char)*query);
    if (placeholder)
        strcpy(write_at, placeholder + 2);
    runner_open(result);
    free(result);
}

static void runner_focus(WindowClient* client)
{
    /* it may have closed while the prompt was up */
    WindowClient* candidate;
    wl_list_for_each(candidate, &clients, link) {
        if (candidate == client) {
            client_activate(client);
            return;
        }
    }
}

static void runner_enter(void)
{
    const char* sug = runner_suggest();
    int off, mode = runner_mode(runner_buffer, &off);

    runner_active = 0;
    const char* cmd = sug ? sug : runner_buffer + off;
    char line[sizeof runner_buffer];
    RunnerApplication* application;
    char* exec;
    char* path;
    switch (mode) {
        case RUNNER_MODE_APPLICATION:
            /* nothing to open, or a bare expression that Tab turns into
             * its result */
            if (!sug)
                return;
            application = &runner_apps[runner_matches[runner_selection]];
            exec = runner_app_exec(application->exec_command);
            runner_exec(exec, application->run_in_terminal);
            free(exec);
            break;
        case RUNNER_MODE_COMMAND:
            if (!*cmd)
                return;
            runner_exec(cmd, 0);
            break;
        case RUNNER_MODE_TERMINAL:
            runner_exec(cmd, 1);
            break;
        case RUNNER_MODE_CALCULATOR:
            /* stays open to go on from the result, or to fix a typo */
            if (runner_calc_fill() || runner_length > off)
                runner_active = 1;
            return;
        case RUNNER_MODE_SEARCH:
            if (!*cmd)
                return;
            runner_search(cmd);
            break;
        case RUNNER_MODE_WINDOW:
            if (sug)
                runner_focus(runner_window_clients[runner_matches[runner_selection]]);
            return;
        case RUNNER_MODE_TAG:
            if (sug)
                layout_view(
                    &(ActionArg){ .unsigned_integer = 1u << runner_matches[runner_selection] });
            return;
        case RUNNER_MODE_FILE:
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

void runner_toggle(const ActionArg*)
{
    if (!selected_monitor)
        return;
    if (!runner_active) {
        if (runner_stale(runner_search_path(), &runner_cache_path, &runner_stamp) ||
            !runner_commands) {
            runner_free_cache();
            runner_build_cache();
        }
        if (runner_stale(runner_app_dirs(), &runner_app_cache, &runner_app_stamp) || !runner_apps) {
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
    int tab = 0;

    char before[sizeof runner_buffer];
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
    } else if (mods & WLR_MODIFIER_CTRL && xkb_keysym_to_lower(sym) == XKB_KEY_a) {
        runner_cursor = 0;
    } else if (mods & WLR_MODIFIER_CTRL && xkb_keysym_to_lower(sym) == XKB_KEY_e) {
        runner_cursor = runner_length;
    } else if (mods & WLR_MODIFIER_CTRL && xkb_keysym_to_lower(sym) == XKB_KEY_f) {
        runner_cursor = runner_next(runner_cursor);
    } else if (mods & WLR_MODIFIER_CTRL && xkb_keysym_to_lower(sym) == XKB_KEY_b) {
        runner_cursor = runner_prev(runner_cursor);
    } else if (mods & WLR_MODIFIER_CTRL && xkb_keysym_to_lower(sym) == XKB_KEY_w) {
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
                int back = sym == XKB_KEY_ISO_Left_Tab || mods & WLR_MODIFIER_SHIFT;
                if (runner_match_count && (runner_cycling || back))
                    runner_select(back ? -1 : 1);
                const char* suggestion = runner_suggest();
                if (suggestion && runner_query() + strlen(suggestion) < sizeof(runner_buffer)) {
                    runner_length = runner_query() + (int)strlen(suggestion);
                    memcpy(runner_buffer + runner_query(), suggestion, strlen(suggestion) + 1);
                    runner_cursor = runner_length;
                    runner_cycling = 1;
                    /* a completed directory lists what is inside it */
                    tab = runner_query() < runner_length;
                } else if (!suggestion) {
                    runner_calc_fill();
                }
                break;
            }
            default: {
                /* any printable codepoint, stored as UTF-8: C0, DEL and C1
                 * controls are what Ctrl combinations and the like produce,
                 * and util_put_codepoint() turns away surrogates and
                 * out-of-range values */
                if (codepoint < 0x20 || (codepoint >= 0x7f && codepoint < 0xa0))
                    break;
                char encoded[5];
                const int encoded_length =
                    (int)util_put_codepoint(encoded, sizeof encoded, codepoint);
                if (encoded_length && runner_length + encoded_length < (int)sizeof(runner_buffer)) {
                    memmove(runner_buffer + runner_cursor + encoded_length,
                            runner_buffer + runner_cursor, runner_length - runner_cursor + 1);
                    memcpy(runner_buffer + runner_cursor, encoded, encoded_length);
                    runner_cursor += encoded_length;
                    runner_length += encoded_length;
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
