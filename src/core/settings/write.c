/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * writing the config file from the values in effect
 */

#include <drwl.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "internal.h"
#include "output/monitor.h"
#include "window/layout.h"

/* variables */
static FILE* settings_file;
static int settings_depth;
static const char* settings_previous; /* last top level directive */

/* function implementations */
static const char* settings_enum_name(const Enum* e, int val)
{
    for (; e->name; e++)
        if (e->val == val)
            return e->name;
    return "unknown";
}

static const Action* settings_action_by_func(void (*func)(const Arg*))
{
    size_t i;

    for (i = 0; i < settings_action_count; i++)
        if (settings_actions[i].func == func)
            return &settings_actions[i];
    return nullptr;
}

static const char* settings_arrange_name(void (*fn)(Monitor*))
{
    size_t i;

    for (i = 0; i < settings_arrange_count; i++)
        if (settings_arranges[i].arrange == fn)
            return settings_arranges[i].name;
    return "tile";
}

static void settings_put_indent(void)
{
    int i;

    for (i = 0; i < settings_depth; i++)
        fputc('\t', settings_file);
}

/* a blank line between top level groups */
static void settings_start_group(const char* k)
{
    if (!settings_depth) {
        if (settings_previous && strcmp(settings_previous, k))
            fputc('\n', settings_file);
        settings_previous = k;
    }
    settings_put_indent();
    fputs(k, settings_file);
}

/* quoted only when it has to be */
static void settings_put_word(const char* s)
{
    fputc(' ', settings_file);
    if (*s && !strpbrk(s, " \t\"'\\{}")) {
        fputs(s, settings_file);
        return;
    }
    fputc('"', settings_file);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            fputc('\\', settings_file);
        fputc(*s, settings_file);
    }
    fputc('"', settings_file);
}

/* floats, so a double's digits would only be conversion noise */
static void settings_put_number(double v)
{
    fprintf(settings_file, " %g", round(v * 1e6) / 1e6);
}

/* a comment over k */
static void settings_put_note(const char* k, const char* s)
{
    if (!settings_depth) {
        if (settings_previous)
            fputc('\n', settings_file);
        settings_previous = k;
    }
    settings_put_indent();
    fprintf(settings_file, "# %s\n", s);
}

static void settings_write_open(const char* k)
{
    settings_start_group(k);
    fputs(" {\n", settings_file);
    settings_depth++;
}

static void settings_write_close(void)
{
    settings_depth--;
    settings_put_indent();
    fputs("}\n", settings_file);
}

/* NULL is off, not empty */
static void settings_write_string(const char* k, const char* v)
{
    settings_start_group(k);
    if (v)
        settings_put_word(v);
    fputc('\n', settings_file);
}

static void settings_write_int(const char* k, long v)
{
    settings_start_group(k);
    fprintf(settings_file, " %ld\n", v);
}

static void settings_write_number(const char* k, double v)
{
    settings_start_group(k);
    settings_put_number(v);
    fputc('\n', settings_file);
}

static void settings_write_bool(const char* k, int v)
{
    settings_start_group(k);
    fputs(v ? " true\n" : " false\n", settings_file);
}

static void settings_write_hex(const char* k, uint32_t c)
{
    settings_start_group(k);
    fprintf(settings_file, " #%08x\n", c);
}

static void settings_write_list(const char* k, const char* const* v, size_t n)
{
    size_t i;

    settings_start_group(k);
    for (i = 0; i < n; i++)
        settings_put_word(v[i]);
    fputc('\n', settings_file);
}

static void settings_write_combo(uint32_t mod, const char* last)
{
    const Enum* e;

    fputc(' ', settings_file);
    for (e = settings_modifier_names; e->name; e++)
        if (mod & (uint32_t)e->val)
            fprintf(settings_file, "%s+", e->name);
    fputs(last, settings_file);
}

/* logo+alt, or none */
static void settings_write_mods(const char* k, uint32_t mod)
{
    const Enum* e;
    const char* sep = " ";

    settings_start_group(k);
    if (!mod)
        fputs(" none", settings_file);
    for (e = settings_modifier_names; e->name; e++) {
        if (mod & (uint32_t)e->val) {
            fprintf(settings_file, "%s%s", sep, e->name);
            sep = "+";
        }
    }
    fputc('\n', settings_file);
}

static void settings_write_arg(char kind, const Arg* arg)
{
    const char* const* v;

    switch (kind) {
        case 'i':
            fprintf(settings_file, " %d", arg->i);
            break;
        case 'u':
            fprintf(settings_file, " %u", arg->ui);
            break;
        case 'f':
            settings_put_number(arg->f);
            break;
        case 'v':
            for (v = arg->v; v && *v; v++)
                settings_put_word(*v);
            break;
        case 'l':
            if (arg->v)
                settings_put_word(((const Layout*)arg->v)->symbol);
            break;
        case 'c':
            settings_put_word(
                settings_enum_name(settings_drag_modes, (int)arg->ui));
            break;
    }
}

static void settings_write_look(void)
{
    size_t i;

    settings_write_open("look");
    settings_write_list("fonts", fonts, font_count);
    settings_write_open("colors");
    settings_put_note("norm", "fg bg border");
    for (i = 0; i < NumSchemes; i++) {
        if (!settings_scheme_names[i])
            continue;
        settings_start_group(settings_scheme_names[i]);
        fprintf(settings_file,
                " #%08x #%08x #%08x\n",
                colors[i][ColFg],
                colors[i][ColBg],
                colors[i][ColBorder]);
    }
    settings_write_close();
    settings_write_hex("rootcolor", settings_pack_rgba(root_color));
    settings_write_hex("fullscreen_bg", settings_pack_rgba(fullscreen_color));
#ifdef INTEGRATED_BACKGROUND
    settings_put_note("wallpaper", "empty for none");
    settings_write_string("wallpaper", wallpaper);
#endif
    settings_put_note("tags", "up to 31");
    settings_write_list("tags", (const char* const*)tags, tag_count);
    settings_write_close();
}

static void settings_write_windows(void)
{
    settings_write_open("windows");
    settings_write_int("borderpx", border_width);
    settings_put_note("cornerstyle", "normal or squircle");
    settings_write_string(
        "cornerstyle",
        settings_enum_name(settings_corner_styles, corner_style));
    settings_write_int("cornerpx", corner_radius);
    settings_write_int("gaps", gaps);
    settings_write_int("gappx", gap_size);
    settings_write_bool("smartgaps", smart_gaps);
    settings_write_bool("titlebar", title_bar);
    settings_write_int("titlepadding", title_padding);
    settings_write_bool("titlecenter", title_center);
    settings_put_note("titleclose", "shown with the pointer over it");
    settings_write_bool("titleclose", title_close);
    settings_write_hex("titleclosecolor", title_close_color);
    settings_write_hex("privatecolor", private_color);
    settings_put_note("privateround", "below 0 follows bartagdotround");
    settings_write_number("privateround", private_round);
    settings_put_note("alwaysprivate",
                      "every window private, the dot marks public ones");
    settings_write_bool("alwaysprivate", always_private);
    settings_write_hex("publiccolor", public_color);
    settings_put_note("privatetitle",
                      "shown for a private window, {app} is its app id");
    settings_write_string("privatetitle", private_title);
    settings_put_note("movethreshold",
                      "px dragged on a title before the window moves");
    settings_write_int("movethreshold", move_threshold);
    settings_put_note("movesnap",
                      "px at which a moved floating window sticks, 0 off");
    settings_write_int("movesnap", move_snap);
    settings_write_close();
}

static void settings_write_bar(void)
{
    settings_write_open("bar");
    settings_write_bool("showbar", bar_enabled);
    settings_write_bool("topbar", bar_top);
    settings_write_bool("barwintitle", bar_window_title);
    settings_put_note("bartagdots", "dots instead of the tag names");
    settings_write_bool("bartagdots", bar_tag_dots);
    settings_put_note("bartagdotsize", "px, 0 for automatic");
    settings_write_int("bartagdotsize", bar_tag_dot_size);
    settings_put_note("bartagdotgap", "px between dots, 0 for automatic");
    settings_write_int("bartagdotgap", bar_tag_dot_gap);
    settings_put_note("bartagdotwidth", "selected pill, times the dot size");
    settings_write_number("bartagdotwidth", bar_tag_dot_width);
    settings_put_note("bartagdotround", "0 square to 1 fully round corners");
    settings_write_number("bartagdotround", bar_tag_dot_round);
    settings_put_note("barurgentpulse", "ms per pulse of an urgent tag, 0 off");
    settings_write_int("barurgentpulse", bar_urgent_pulse);
    settings_write_bool("barltsymbol", bar_layout_symbol);
    settings_write_int("barboxradius", bar_box_radius);
    settings_write_int("barpadding", bar_padding);
    settings_put_note("barheight", "times the font height, 0 for automatic");
    settings_write_number("barheight", bar_height);
    settings_put_note("barsinglemon", "one bar only, on the first monitor");
    settings_write_bool("barsinglemon", bar_single_monitor);
    settings_put_note("barstatus", "focus, all or main");
    settings_write_string(
        "barstatus", settings_enum_name(settings_bar_statuses, bar_status));
    settings_write_bool("showsystray", systray_enabled);
    settings_write_int("systrayspacing", systray_spacing);
    settings_write_int("systraypadding", systray_padding);
    settings_put_note("systrayiconsize", "0 fills the bar");
    settings_write_int("systrayiconsize", systray_icon_size);
    settings_write_bool("shownotifications", notifications_enabled);
    settings_put_note("notification_timeout", "seconds");
    settings_write_int("notification_timeout", notification_timeout);
    settings_write_int("notification_linewidth", notification_line_width);
    settings_write_int("notification_lineradius", notification_line_radius);
    settings_write_string("notification_actionsign", notification_action_sign);
    settings_put_note("runner_placeholder", "shown while nothing is typed");
    settings_write_string("runner_placeholder", runner_placeholder);
    settings_put_note("runner_max_candidates", "0 for none");
    settings_write_int("runner_max_candidates", runner_max_candidates);
    settings_put_note("runner_pillheight", "0 fits the font");
    settings_write_int("runner_pillheight", runner_pill_height);
    settings_put_note("runner_pillpadding", "0 for automatic");
    settings_write_int("runner_pillpadding", runner_pill_padding);
    settings_put_note("runner_pillround", "below 0 follows bartagdotround");
    settings_write_number("runner_pillround", runner_pill_round);
    settings_write_bool("runner_fuzzy", runner_fuzzy);
    settings_put_note("runner_icons", "none, selected or all");
    settings_write_string(
        "runner_icons",
        settings_enum_name(settings_runner_icons, runner_icons));
    settings_put_note("runner_iconsize", "0 fits the font");
    settings_write_int("runner_iconsize", runner_icon_size);
    settings_put_note("runner_iconround",
                      "0 square to 1 round, for a missing icon");
    settings_write_number("runner_iconround", runner_icon_round);
    settings_write_int("runner_iconborder", runner_icon_border);
    settings_put_note("runner_history_size", "0 for none");
    settings_write_int("runner_history_size", runner_history_size);
    settings_write_string("runner_shell", runner_shell);
    settings_put_note("runner_terminal",
                      "runs a line starting with runner_termprefix");
    settings_write_string("runner_terminal", runner_terminal);
    settings_write_string("runner_termprefix", runner_terminal_prefix);
    settings_put_note("runner_runprefix", "runs a command without a terminal");
    settings_write_string("runner_runprefix", runner_run_prefix);
    settings_write_string("runner_calcprefix", runner_calc_prefix);
    settings_write_string("runner_searchprefix", runner_search_prefix);
    settings_put_note("runner_searchurl",
                      "%s is the query, opened with xdg-open");
    settings_write_string("runner_searchurl", runner_search_url);
    settings_write_string("runner_windowprefix", runner_window_prefix);
    settings_write_string("runner_tagprefix", runner_tag_prefix);
    settings_put_note("runner_files",
                      "lists the files of a path starting with / or ~");
    settings_write_bool("runner_files", runner_files);
    settings_put_note("runner_paths", "searched after $PATH");
    settings_write_string("runner_paths", runner_paths);
    settings_put_note("runner_calc_precision", "significant digits, up to 17");
    settings_write_int("runner_calc_precision", runner_calc_precision);
    settings_write_close();
}

static void settings_write_opacity(void)
{
    settings_write_open("opacity");
    settings_write_bool("opacity_enabled", opacity_enabled);
    settings_write_number("opacity_focus", opacity_focus);
    settings_write_number("opacity_unfocus", opacity_unfocus);
    settings_put_note("opacity_deco", "bar, title bars and borders");
    settings_write_number("opacity_deco", opacity_decorations);
#ifdef INTEGRATED_BACKGROUND
    settings_put_note("opacity_type", "normal or blur");
    settings_write_string(
        "opacity_type",
        settings_enum_name(settings_opacity_kinds, opacity_type));
    settings_write_int("blur_radius", blur_radius);
    settings_write_int("blur_passes", blur_passes);
    settings_write_number("blur_saturation", blur_saturation);
    settings_write_number("blur_brightness", blur_brightness);
#endif
    settings_write_close();
}

static void settings_write_input(void)
{
    settings_write_open("input");
    settings_write_bool("sloppyfocus", sloppy_focus);
    settings_write_open("xkb_rules");
    settings_write_string("rules", xkb_rules.rules);
    settings_write_string("model", xkb_rules.model);
    settings_write_string("layout", xkb_rules.layout);
    settings_write_string("variant", xkb_rules.variant);
    settings_write_string("options", xkb_rules.options);
    settings_write_close();
    settings_write_int("repeat_rate", repeat_rate);
    settings_write_int("repeat_delay", repeat_delay);
    settings_write_string("cursor_theme", cursor_theme);
    settings_write_int("cursor_size", cursor_size);
    settings_write_bool("hide_cursor_when_typing", hide_cursor_when_typing);
    settings_write_bool("tap_to_click", tap_to_click);
    settings_write_bool("tap_and_drag", tap_and_drag);
    settings_write_bool("drag_lock", drag_lock);
    settings_write_bool("natural_scrolling", natural_scrolling);
    settings_write_bool("disable_while_typing", disable_while_typing);
    settings_write_bool("left_handed", left_handed);
    settings_write_bool("middle_button_emulation", middle_button_emulation);
    settings_put_note("scroll_method", "none, 2fg, edge or on-button-down");
    settings_write_string(
        "scroll_method",
        settings_enum_name(settings_scroll_methods, scroll_method));
    settings_put_note("click_method", "none, button-areas or clickfinger");
    settings_write_string(
        "click_method",
        settings_enum_name(settings_click_methods, click_method));
    settings_put_note("send_events_mode",
                      "enabled, disabled or disabled-on-external-mouse");
    settings_write_string(
        "send_events_mode",
        settings_enum_name(settings_send_events, (int)send_events_mode));
    settings_put_note("accel_profile",
                      "none, flat or adaptive, speed from -1 to 1");
    settings_write_string(
        "accel_profile",
        settings_enum_name(settings_accel_profiles, accel_profile));
    settings_write_number("accel_speed", accel_speed);
    settings_put_note("touchpad_natural_scrolling",
                      "the same three, for touchpads");
    settings_write_bool("touchpad_natural_scrolling",
                        touchpad_natural_scrolling);
    settings_write_string(
        "touchpad_accel_profile",
        settings_enum_name(settings_accel_profiles, touchpad_accel_profile));
    settings_write_number("touchpad_accel_speed", touchpad_accel_speed);
    settings_put_note("pinchzoom_mod",
                      "held while pinching to zoom, none for off");
    settings_write_mods("pinchzoom_mod", pinch_zoom_modifier);
    settings_put_note("button_map", "lrm or lmr");
    settings_write_string(
        "button_map", settings_enum_name(settings_button_maps, tap_button_map));
    settings_put_note("tablet_output",
                      "output names as wlr-randr shows them, none for all");
    settings_write_string("tablet_output", tablet_output);
    settings_put_note("tablet_left_handed", "buttons on the right");
    settings_write_bool("tablet_left_handed", tablet_left_handed);
    settings_put_note("touch_output", "none: the one udev reports");
    settings_write_string("touch_output", touch_output);
    settings_write_close();
}

static void settings_write_misc(void)
{
    settings_write_open("misc");
    settings_put_note("log_level", "silent, error, info or debug");
    settings_write_string("log_level",
                          settings_enum_name(settings_log_levels, log_level));
    settings_put_note("bypass_surface_visibility",
                      "a hidden fullscreen window keeps the screen awake");
    settings_write_bool("bypass_surface_visibility", bypass_surface_visibility);
    settings_put_note("allow_tearing",
                      "tearing for fullscreen windows that ask for it");
    settings_write_bool("allow_tearing", allow_tearing);
    settings_put_note("auto_vrr", "VRR for fullscreen games and videos");
    settings_write_bool("auto_vrr", auto_vrr);
    settings_write_close();
}

static void settings_write_layouts(void)
{
    size_t i;

    settings_put_note("layout", "layout: symbol, then tile or tabbed");
    for (i = 0; i < layout_count; i++) {
        settings_start_group("layout");
        settings_put_word(layouts[i].symbol);
        settings_put_word(settings_arrange_name(layouts[i].arrange));
        fputc('\n', settings_file);
    }
}

static void settings_write_rules(void)
{
    size_t i;

    settings_put_note(
        "rule",
        "rule: opacity 0 keeps the default, monitor -1 is the focused one, "
        "without private it follows alwaysprivate");
    for (i = 0; i < rule_count; i++) {
        settings_write_open("rule");
        settings_write_string("id", rules[i].id);
        settings_write_string("title", rules[i].title);
        settings_write_int("tags", rules[i].tags);
        settings_write_bool("isfloating", rules[i].isfloating);
        if (rules[i].isprivate >= 0)
            settings_write_bool("private", rules[i].isprivate);
        settings_write_number("opacity_focus", rules[i].opacity_focus);
        settings_write_number("opacity_unfocus", rules[i].opacity_unfocus);
        settings_write_int("monitor", rules[i].monitor);
        settings_write_close();
    }
}

static void settings_write_monitors(void)
{
    size_t i;

    settings_put_note("monitor", "monitor: the first matching name wins");
    for (i = 0; i < monitor_rule_count; i++) {
        settings_write_open("monitor");
        settings_write_string("name", monitor_rules[i].name);
        settings_write_number("mfact", monitor_rules[i].mfact);
        settings_write_int("nmaster", monitor_rules[i].nmaster);
        settings_write_number("scale", monitor_rules[i].scale);
        settings_write_string("layout",
                              monitor_rules[i].lt ? monitor_rules[i].lt->symbol
                                                  : nullptr);
        settings_write_string(
            "transform",
            settings_enum_name(settings_transforms, (int)monitor_rules[i].rr));
        settings_write_int("x", monitor_rules[i].x);
        settings_write_int("y", monitor_rules[i].y);
        settings_write_int("width", monitor_rules[i].width);
        settings_write_int("height", monitor_rules[i].height);
        settings_write_int("refresh", monitor_rules[i].refresh);
        settings_write_close();
    }
}

/* one NULL terminated run of arguments per command */
static void settings_write_autostart(void)
{
    const char* const* p;

    settings_put_note("autostart", "autostart: a command, run without a shell");
    for (p = autostart; *p; p++) {
        settings_start_group("autostart");
        for (; *p; p++)
            settings_put_word(*p);
        fputc('\n', settings_file);
    }
}

static void settings_write_keys(void)
{
    size_t i;

    settings_put_note("key", "key: modifiers+key, action, argument");
    for (i = 0; i < key_count; i++) {
        const Action* act = settings_action_by_func(keys[i].func);
        char name[64];

        if (!act) {
            fprintf(
                stderr, "g0wm: binding %zu calls an action with no name\n", i);
            continue;
        }
        if (xkb_keysym_get_name(keys[i].keysym, name, sizeof name) < 0)
            continue;
        settings_start_group("key");
        settings_write_combo(keys[i].mod, name);
        settings_put_word(act->name);
        settings_write_arg(act->arg, &keys[i].arg);
        fputc('\n', settings_file);
    }
}

static void settings_write_buttons(void)
{
    size_t i;

    settings_put_note("button",
                      "button: bar area, modifiers+button, action, argument");
    for (i = 0; i < button_count; i++) {
        const Action* act = settings_action_by_func(buttons[i].func);

        if (!act) {
            fprintf(
                stderr, "g0wm: click %zu calls an action with no name\n", i);
            continue;
        }
        settings_start_group("button");
        settings_put_word(
            settings_enum_name(settings_click_areas, (int)buttons[i].click));
        settings_write_combo(
            buttons[i].mod,
            settings_enum_name(settings_mouse_buttons, (int)buttons[i].button));
        settings_put_word(act->name);
        settings_write_arg(act->arg, &buttons[i].arg);
        fputc('\n', settings_file);
    }
}

static void settings_write_gestures(void)
{
    size_t i;

    settings_put_note("gesture", "gesture: motion, fingers, action, argument");
    for (i = 0; i < gesture_count; i++) {
        const Action* act = settings_action_by_func(gestures[i].func);

        if (!act) {
            fprintf(
                stderr, "g0wm: gesture %zu calls an action with no name\n", i);
            continue;
        }
        settings_start_group("gesture");
        settings_put_word(settings_enum_name(settings_gesture_motions,
                                             (int)gestures[i].motion));
        fprintf(settings_file, " %u", gestures[i].fingers);
        settings_put_word(act->name);
        settings_write_arg(act->arg, &gestures[i].arg);
        fputc('\n', settings_file);
    }
}

static void settings_write_pad_buttons(void)
{
    size_t i;

    settings_put_note("padbutton",
                      "padbutton: button from 0, action, argument");
    for (i = 0; i < pad_button_count; i++) {
        const Action* act = settings_action_by_func(pad_buttons[i].func);

        if (!act) {
            fprintf(stderr,
                    "g0wm: pad button %zu calls an action with no name\n",
                    i);
            continue;
        }
        settings_start_group("padbutton");
        fprintf(settings_file, " %u", pad_buttons[i].button);
        settings_put_word(act->name);
        settings_write_arg(act->arg, &pad_buttons[i].arg);
        fputc('\n', settings_file);
    }
}

static void settings_write_all(void)
{
    settings_put_note(
        "", "a setting left out keeps its default, one with no value is off");
    settings_write_look();
    settings_write_windows();
    settings_write_bar();
    settings_write_opacity();
    settings_write_input();
    settings_write_misc();
    settings_write_layouts();
    settings_write_rules();
    settings_write_monitors();
    settings_write_autostart();
    settings_write_keys();
    settings_write_buttons();
    settings_write_gestures();
    settings_write_pad_buttons();
}

/* the defaults, rendered; NULL and a message when they cannot be */
char* settings_rendered_defaults(void)
{
    char* text = nullptr;
    size_t len;

    if ((settings_file = open_memstream(&text, &len))) {
        settings_depth = 0;
        settings_previous = nullptr;
        settings_write_all();
        if (!fclose(settings_file))
            return text;
    }
    free(text);
    fprintf(stderr, "g0wm: cannot render the settings\n");
    return nullptr;
}
