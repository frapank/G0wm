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
static const char* settings_enum_name(const EnumEntry* table, int value)
{
    for (const EnumEntry* entry = table; entry->name; entry++)
        if (entry->value == value)
            return entry->name;
    return "unknown";
}

static const ActionEntry* settings_action_by_func(void (*func)(const ActionArg*))
{
    for (size_t i = 0; i < settings_action_count; i++)
        if (settings_actions[i].callback == func)
            return &settings_actions[i];
    return nullptr;
}

static const char* settings_arrange_name(void (*arrange)(Monitor*))
{
    for (size_t i = 0; i < settings_arrange_count; i++)
        if (settings_arranges[i].callback == arrange)
            return settings_arranges[i].name;
    return "tile";
}

static void settings_put_indent(void)
{
    for (int i = 0; i < settings_depth; i++)
        fputc('\t', settings_file);
}

/* a blank line between top level groups */
static void settings_start_group(const char* key)
{
    if (!settings_depth) {
        if (settings_previous && strcmp(settings_previous, key))
            fputc('\n', settings_file);
        settings_previous = key;
    }
    settings_put_indent();
    fputs(key, settings_file);
}

/* quoted only when it has to be */
static void settings_put_word(const char* text)
{
    fputc(' ', settings_file);
    if (*text && !strpbrk(text, " \t\"'\\{}")) {
        fputs(text, settings_file);
        return;
    }
    fputc('"', settings_file);
    for (const char* letter = text; *letter; letter++) {
        if (*letter == '"' || *letter == '\\')
            fputc('\\', settings_file);
        fputc(*letter, settings_file);
    }
    fputc('"', settings_file);
}

/* floats, so a double's digits would only be conversion noise */
static void settings_put_number(double value)
{
    fprintf(settings_file, " %g", round(value * 1e6) / 1e6);
}

/* a comment over k */
static void settings_put_note(const char* key, const char* text)
{
    if (!settings_depth) {
        if (settings_previous)
            fputc('\n', settings_file);
        settings_previous = key;
    }
    settings_put_indent();
    fprintf(settings_file, "# %s\n", text);
}

static void settings_write_open(const char* key)
{
    settings_start_group(key);
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
static void settings_write_string(const char* key, const char* value)
{
    settings_start_group(key);
    if (value)
        settings_put_word(value);
    fputc('\n', settings_file);
}

static void settings_write_int(const char* key, long value)
{
    settings_start_group(key);
    fprintf(settings_file, " %ld\n", value);
}

static void settings_write_number(const char* key, double value)
{
    settings_start_group(key);
    settings_put_number(value);
    fputc('\n', settings_file);
}

static void settings_write_bool(const char* key, int value)
{
    settings_start_group(key);
    fputs(value ? " true\n" : " false\n", settings_file);
}

static void settings_write_hex(const char* key, uint32_t color)
{
    settings_start_group(key);
    fprintf(settings_file, " #%08x\n", color);
}

static void settings_write_list(const char* key, const char* const* words, size_t count)
{
    settings_start_group(key);
    for (size_t i = 0; i < count; i++)
        settings_put_word(words[i]);
    fputc('\n', settings_file);
}

static void settings_write_combo(uint32_t modifiers, const char* last_name)
{
    fputc(' ', settings_file);
    for (const EnumEntry* entry = settings_modifier_names; entry->name; entry++)
        if (modifiers & (uint32_t)entry->value)
            fprintf(settings_file, "%s+", entry->name);
    fputs(last_name, settings_file);
}

/* logo+alt, or none */
static void settings_write_mods(const char* key, uint32_t modifiers)
{
    const char* separator = " ";

    settings_start_group(key);
    if (!modifiers)
        fputs(" none", settings_file);
    for (const EnumEntry* entry = settings_modifier_names; entry->name; entry++) {
        if (modifiers & (uint32_t)entry->value) {
            fprintf(settings_file, "%s%s", separator, entry->name);
            separator = "+";
        }
    }
    fputc('\n', settings_file);
}

static void settings_write_arg(char kind, const ActionArg* argument)
{
    switch (kind) {
        case 'i':
            fprintf(settings_file, " %d", argument->integer);
            break;
        case 'u':
            fprintf(settings_file, " %u", argument->unsigned_integer);
            break;
        case 'f':
            settings_put_number(argument->floating);
            break;
        case 'v':
            for (const char* const* word = argument->pointer; word && *word; word++)
                settings_put_word(*word);
            break;
        case 'l':
            if (argument->pointer)
                settings_put_word(((const WindowLayout*)argument->pointer)->symbol);
            break;
        case 'c':
            settings_put_word(
                settings_enum_name(settings_drag_modes, (int)argument->unsigned_integer));
            break;
    }
}

static void settings_write_look(void)
{
    settings_write_open("look");
    settings_write_list("fonts", settings->look.fonts, settings->look.font_count);
    settings_write_open("colors");
    settings_put_note("norm", "fg bg border");
    for (size_t i = 0; i < COLOR_SCHEME_COUNT; i++) {
        if (!settings_scheme_names[i])
            continue;
        settings_start_group(settings_scheme_names[i]);
        fprintf(settings_file, " #%08x #%08x #%08x\n", settings->look.colors[i][ColFg],
                settings->look.colors[i][ColBg], settings->look.colors[i][ColBorder]);
    }
    settings_write_close();
    settings_write_hex("rootcolor", settings_pack_rgba(settings->look.root_color));
    settings_write_hex("fullscreen_bg", settings_pack_rgba(settings->look.fullscreen_color));
#ifdef INTEGRATED_BACKGROUND
    settings_put_note("wallpaper", "empty for none");
    settings_write_string("wallpaper", settings->look.wallpaper);
#endif
    settings_put_note("tags", "up to 31");
    settings_write_list("tags", (const char* const*)settings->look.tags, settings->look.tag_count);
    settings_write_close();
}

static void settings_write_windows(void)
{
    settings_write_open("windows");
    settings_write_int("borderpx", settings->windows.border_width);
    settings_put_note("cornerstyle", "normal or squircle");
    settings_write_string(
        "cornerstyle", settings_enum_name(settings_corner_styles, settings->windows.corner_style));
    settings_write_int("cornerpx", settings->windows.corner_radius);
    settings_write_int("gaps", settings->windows.gaps);
    settings_write_int("gappx", settings->windows.gap_size);
    settings_write_bool("smartgaps", settings->windows.smart_gaps);
    settings_write_bool("titlebar", settings->windows.title_bar);
    settings_write_int("titlepadding", settings->windows.title_padding);
    settings_write_bool("titlecenter", settings->windows.title_center);
    settings_put_note("titleclose", "shown with the pointer over it");
    settings_write_bool("titleclose", settings->windows.title_close);
    settings_write_hex("titleclosecolor", settings->windows.title_close_color);
    settings_write_hex("privatecolor", settings->windows.private_color);
    settings_put_note("privateround", "below 0 follows bartagdotround");
    settings_write_number("privateround", settings->windows.private_round);
    settings_put_note("alwaysprivate", "every window private, the dot marks public ones");
    settings_write_bool("alwaysprivate", settings->windows.always_private);
    settings_write_hex("publiccolor", settings->windows.public_color);
    settings_put_note("privatetitle", "shown for a private window, {app} is its app id");
    settings_write_string("privatetitle", settings->windows.private_title);
    settings_put_note("movethreshold", "px dragged on a title before the window moves");
    settings_write_int("movethreshold", settings->input.move_threshold);
    settings_put_note("movesnap", "px at which a moved floating window sticks, 0 off");
    settings_write_int("movesnap", settings->input.move_snap);
    settings_write_close();
}

static void settings_write_private(void)
{
    settings_write_open("private");
    settings_put_note("text", "screen capture placeholder, only {app} is expanded");
    settings_write_string("text", settings->privacy.text);
    settings_put_note("image", "optional image above the text, transparency is kept");
    settings_write_string("image", settings->privacy.image);
    settings_write_int("imagesize", settings->privacy.image_size);
    settings_put_note("textscale", "relative to look.fonts, 1.2 is 20% larger");
    settings_write_number("textscale", settings->privacy.text_scale);
    settings_write_hex("color", settings->privacy.background);
    settings_write_hex("textcolor", settings->privacy.foreground);
    settings_write_close();
}

static void settings_write_bar(void)
{
    settings_write_open("bar");
    settings_write_bool("showbar", settings->bar.enabled);
    settings_write_bool("topbar", settings->bar.top);
    settings_write_bool("barwintitle", settings->bar.window_title);
    settings_put_note("bartagdots", "dots instead of the tag names");
    settings_write_bool("bartagdots", settings->bar.tag_dots);
    settings_put_note("bartagdotsize", "px, 0 for automatic");
    settings_write_int("bartagdotsize", settings->bar.tag_dot_size);
    settings_put_note("bartagdotgap", "px between dots, 0 for automatic");
    settings_write_int("bartagdotgap", settings->bar.tag_dot_gap);
    settings_put_note("bartagdotwidth", "selected pill, times the dot size");
    settings_write_number("bartagdotwidth", settings->bar.tag_dot_width);
    settings_put_note("bartagdotround", "0 square to 1 fully round corners");
    settings_write_number("bartagdotround", settings->bar.tag_dot_round);
    settings_put_note("barurgentpulse", "ms per pulse of an urgent tag, 0 off");
    settings_write_int("barurgentpulse", settings->bar.urgent_pulse);
    settings_write_bool("barltsymbol", settings->bar.layout_symbol);
    settings_write_int("barboxradius", settings->bar.box_radius);
    settings_write_int("barpadding", settings->bar.padding);
    settings_put_note("barheight", "times the font height, 0 for automatic");
    settings_write_number("barheight", settings->bar.height);
    settings_put_note("barsinglemon", "one bar only, on the first monitor");
    settings_write_bool("barsinglemon", settings->bar.single_monitor);
    settings_put_note("barstatus", "focus, all or main");
    settings_write_string("barstatus",
                          settings_enum_name(settings_bar_statuses, settings->bar.status));
    settings_write_bool("showsystray", settings->systray.enabled);
    settings_write_int("systrayspacing", settings->systray.spacing);
    settings_write_int("systraypadding", settings->systray.padding);
    settings_put_note("systrayiconsize", "0 fills the bar");
    settings_write_int("systrayiconsize", settings->systray.icon_size);
    settings_write_bool("shownotifications", settings->notifications.enabled);
    settings_put_note("privatenotify", "hidden from screen sharing until toggleprivatenotify");
    settings_write_bool("privatenotify", settings->notifications.private);
    settings_put_note("notification_timeout", "seconds");
    settings_write_int("notification_timeout", settings->notifications.timeout);
    settings_write_int("notification_linewidth", settings->notifications.line_width);
    settings_write_int("notification_lineradius", settings->notifications.line_radius);
    settings_write_string("notification_actionsign", settings->notifications.action_sign);
    settings_put_note("notification_privatetext", "{app} for application name");
    settings_write_string("notification_privatetext", settings->notifications.private_text);
    settings_put_note("notification_privatehide", "screen sharing shows no notification at all");
    settings_write_bool("notification_privatehide", settings->notifications.private_hide);
    settings_put_note("notification_dot", "left or right of the free part of the bar");
    settings_write_string("notification_dot", settings_enum_name(settings_notification_dots,
                                                                 settings->notifications.dot));
    settings_put_note("notification_privatecolor", "#00000000 follows privatecolor");
    settings_write_hex("notification_privatecolor", settings->notifications.private_color);
    settings_put_note("notification_publiccolor", "#00000000 follows publiccolor");
    settings_write_hex("notification_publiccolor", settings->notifications.public_color);
    settings_put_note("runner_placeholder", "shown while nothing is typed");
    settings_write_string("runner_placeholder", settings->runner.placeholder);
    settings_put_note("runner_max_candidates", "0 for none");
    settings_write_int("runner_max_candidates", settings->runner.max_candidates);
    settings_put_note("runner_pillheight", "0 fits the font");
    settings_write_int("runner_pillheight", settings->runner.pill_height);
    settings_put_note("runner_pillpadding", "0 for automatic");
    settings_write_int("runner_pillpadding", settings->runner.pill_padding);
    settings_put_note("runner_pillround", "below 0 follows bartagdotround");
    settings_write_number("runner_pillround", settings->runner.pill_round);
    settings_write_bool("runner_fuzzy", settings->runner.fuzzy);
    settings_put_note("runner_icons", "none, selected or all");
    settings_write_string("runner_icons",
                          settings_enum_name(settings_runner_icons, settings->runner.icons));
    settings_put_note("runner_iconsize", "0 fits the font");
    settings_write_int("runner_iconsize", settings->runner.icon_size);
    settings_put_note("runner_iconround", "0 square to 1 round, for a missing icon");
    settings_write_number("runner_iconround", settings->runner.icon_round);
    settings_write_int("runner_iconborder", settings->runner.icon_border);
    settings_put_note("runner_history_size", "0 for none");
    settings_write_int("runner_history_size", settings->runner.history_size);
    settings_write_string("runner_shell", settings->runner.shell);
    settings_put_note("runner_terminal", "runs a line starting with runner_termprefix");
    settings_write_string("runner_terminal", settings->runner.terminal);
    settings_write_string("runner_termprefix", settings->runner.terminal_prefix);
    settings_put_note("runner_runprefix", "runs a command without a terminal");
    settings_write_string("runner_runprefix", settings->runner.run_prefix);
    settings_write_string("runner_calcprefix", settings->runner.calc_prefix);
    settings_write_string("runner_searchprefix", settings->runner.search_prefix);
    settings_put_note("runner_searchurl", "%s is the query, opened with xdg-open");
    settings_write_string("runner_searchurl", settings->runner.search_url);
    settings_write_string("runner_windowprefix", settings->runner.window_prefix);
    settings_write_string("runner_tagprefix", settings->runner.tag_prefix);
    settings_put_note("runner_files", "lists the files of a path starting with / or ~");
    settings_write_bool("runner_files", settings->runner.files);
    settings_put_note("runner_paths", "searched after $PATH");
    settings_write_string("runner_paths", settings->runner.paths);
    settings_put_note("runner_calc_precision", "significant digits, up to 17");
    settings_write_int("runner_calc_precision", settings->runner.calc_precision);
    settings_write_close();
}

static void settings_write_opacity(void)
{
    settings_write_open("opacity");
    settings_write_bool("opacity_enabled", settings->opacity.enabled);
    settings_write_number("opacity_focus", settings->opacity.focus);
    settings_write_number("opacity_unfocus", settings->opacity.unfocus);
    settings_put_note("opacity_deco", "bar, title bars and borders");
    settings_write_number("opacity_deco", settings->opacity.decorations);
#ifdef INTEGRATED_BACKGROUND
    settings_put_note("opacity_type", "normal or blur");
    settings_write_string("opacity_type",
                          settings_enum_name(settings_opacity_kinds, settings->opacity.type));
    settings_write_int("blur_radius", settings->opacity.blur_radius);
    settings_write_int("blur_passes", settings->opacity.blur_passes);
    settings_write_number("blur_saturation", settings->opacity.blur_saturation);
    settings_write_number("blur_brightness", settings->opacity.blur_brightness);
#endif
    settings_write_close();
}

static void settings_write_input(void)
{
    settings_write_open("input");
    settings_write_bool("sloppyfocus", settings->input.sloppy_focus);
    settings_write_open("xkb_rules");
    settings_write_string("rules", settings->input.xkb_rules.rules);
    settings_write_string("model", settings->input.xkb_rules.model);
    settings_write_string("layout", settings->input.xkb_rules.layout);
    settings_write_string("variant", settings->input.xkb_rules.variant);
    settings_write_string("options", settings->input.xkb_rules.options);
    settings_write_close();
    settings_write_int("repeat_rate", settings->input.repeat_rate);
    settings_write_int("repeat_delay", settings->input.repeat_delay);
    settings_write_string("cursor_theme", settings->input.cursor_theme);
    settings_write_int("cursor_size", settings->input.cursor_size);
    settings_write_bool("hide_cursor_when_typing", settings->input.hide_cursor_when_typing);
    settings_write_bool("tap_to_click", settings->input.tap_to_click);
    settings_write_bool("tap_and_drag", settings->input.tap_and_drag);
    settings_write_bool("drag_lock", settings->input.drag_lock);
    settings_write_bool("natural_scrolling", settings->input.natural_scrolling);
    settings_write_bool("disable_while_typing", settings->input.disable_while_typing);
    settings_write_bool("left_handed", settings->input.left_handed);
    settings_write_bool("middle_button_emulation", settings->input.middle_button_emulation);
    settings_put_note("scroll_method", "none, 2fg, edge or on-button-down");
    settings_write_string("scroll_method", settings_enum_name(settings_scroll_methods,
                                                              settings->input.scroll_method));
    settings_put_note("click_method", "none, button-areas or clickfinger");
    settings_write_string("click_method",
                          settings_enum_name(settings_click_methods, settings->input.click_method));
    settings_put_note("send_events_mode", "enabled, disabled or disabled-on-external-mouse");
    settings_write_string(
        "send_events_mode",
        settings_enum_name(settings_send_events, (int)settings->input.send_events_mode));
    settings_put_note("accel_profile", "none, flat or adaptive, speed from -1 to 1");
    settings_write_string("accel_profile", settings_enum_name(settings_accel_profiles,
                                                              settings->input.accel_profile));
    settings_write_number("accel_speed", settings->input.accel_speed);
    settings_put_note("touchpad_natural_scrolling", "the same three, for touchpads");
    settings_write_bool("touchpad_natural_scrolling", settings->input.touchpad_natural_scrolling);
    settings_write_string(
        "touchpad_accel_profile",
        settings_enum_name(settings_accel_profiles, settings->input.touchpad_accel_profile));
    settings_write_number("touchpad_accel_speed", settings->input.touchpad_accel_speed);
    settings_put_note("pinchzoom_mod", "held while pinching to zoom, none for off");
    settings_write_mods("pinchzoom_mod", settings->bindings.pinch_zoom_modifier);
    settings_put_note("button_map", "lrm or lmr");
    settings_write_string("button_map",
                          settings_enum_name(settings_button_maps, settings->input.tap_button_map));
    settings_put_note("tablet_output", "output names as wlr-randr shows them, none for all");
    settings_write_string("tablet_output", settings->input.tablet_output);
    settings_put_note("tablet_left_handed", "buttons on the right");
    settings_write_bool("tablet_left_handed", settings->input.tablet_left_handed);
    settings_put_note("touch_output", "none: the one udev reports");
    settings_write_string("touch_output", settings->input.touch_output);
    settings_write_close();
}

static void settings_write_misc(void)
{
    settings_write_open("misc");
    settings_put_note("log_level", "silent, error, info or debug");
    settings_write_string("log_level",
                          settings_enum_name(settings_log_levels, settings->misc.log_level));
    settings_put_note("bypass_surface_visibility",
                      "a hidden fullscreen window keeps the screen awake");
    settings_write_bool("bypass_surface_visibility", settings->misc.bypass_surface_visibility);
    settings_put_note("allow_tearing", "tearing for fullscreen windows that ask for it");
    settings_write_bool("allow_tearing", settings->misc.allow_tearing);
    settings_put_note("auto_vrr", "VRR for fullscreen games and videos");
    settings_write_bool("auto_vrr", settings->misc.auto_vrr);
    settings_write_close();
}

static void settings_write_layouts(void)
{
    settings_put_note("layout", "layout: symbol, then tile or tabbed");
    for (size_t i = 0; i < settings->rules.layout_count; i++) {
        settings_start_group("layout");
        settings_put_word(settings->rules.layouts[i].symbol);
        settings_put_word(settings_arrange_name(settings->rules.layouts[i].arrange));
        fputc('\n', settings_file);
    }
}

static void settings_write_rules(void)
{
    settings_put_note("rule", "rule: opacity 0 keeps the default, monitor -1 is the focused one, "
                              "without private it follows alwaysprivate");
    for (size_t i = 0; i < settings->rules.client_rule_count; i++) {
        settings_write_open("rule");
        settings_write_string("id", settings->rules.client_rules[i].app_id);
        settings_write_string("title", settings->rules.client_rules[i].title);
        settings_write_int("tags", settings->rules.client_rules[i].tags);
        settings_write_bool("isfloating", settings->rules.client_rules[i].is_floating);
        if (settings->rules.client_rules[i].is_private >= 0)
            settings_write_bool("private", settings->rules.client_rules[i].is_private);
        settings_write_string("privatetext", settings->rules.client_rules[i].private_text);
        settings_write_hex("privatecolor", settings->rules.client_rules[i].private_background);
        settings_write_hex("privatetextcolor", settings->rules.client_rules[i].private_foreground);
        settings_write_number("opacity_focus", settings->rules.client_rules[i].focused_opacity);
        settings_write_number("opacity_unfocus", settings->rules.client_rules[i].unfocused_opacity);
        settings_write_int("monitor", settings->rules.client_rules[i].monitor_index);
        settings_write_close();
    }
}

static void settings_write_monitors(void)
{
    settings_put_note("monitor", "monitor: the first matching name wins");
    for (size_t i = 0; i < settings->rules.monitor_rule_count; i++) {
        settings_write_open("monitor");
        settings_write_string("name", settings->rules.monitor_rules[i].name);
        settings_write_number("mfact", settings->rules.monitor_rules[i].master_factor);
        settings_write_int("nmaster", settings->rules.monitor_rules[i].master_count);
        settings_write_number("scale", settings->rules.monitor_rules[i].scale);
        settings_write_string("layout", settings->rules.monitor_rules[i].layout
                                            ? settings->rules.monitor_rules[i].layout->symbol
                                            : nullptr);
        settings_write_string("transform",
                              settings_enum_name(settings_transforms,
                                                 (int)settings->rules.monitor_rules[i].transform));
        settings_write_int("x", settings->rules.monitor_rules[i].x);
        settings_write_int("y", settings->rules.monitor_rules[i].y);
        settings_write_int("width", settings->rules.monitor_rules[i].width);
        settings_write_int("height", settings->rules.monitor_rules[i].height);
        settings_write_int("refresh", settings->rules.monitor_rules[i].refresh_hz);
        settings_write_close();
    }
}

/* one NULL terminated run of arguments per command */
static void settings_write_autostart(void)
{
    settings_put_note("autostart", "autostart: a command, run without a shell");
    for (const char* const* word = settings->bindings.autostart; *word; word++) {
        settings_start_group("autostart");
        for (; *word; word++)
            settings_put_word(*word);
        fputc('\n', settings_file);
    }
}

static void settings_write_keys(void)
{
    settings_put_note("key", "key: modifiers+key, action, argument");
    for (size_t i = 0; i < settings->bindings.key_binding_count; i++) {
        const ActionEntry* action =
            settings_action_by_func(settings->bindings.key_bindings[i].callback);
        char key_name[64];

        if (!action) {
            fprintf(stderr, "g0wm: binding %zu calls an action with no name\n", i);
            continue;
        }
        if (xkb_keysym_get_name(settings->bindings.key_bindings[i].keysym, key_name,
                                sizeof key_name) < 0)
            continue;
        settings_start_group("key");
        settings_write_combo(settings->bindings.key_bindings[i].modifiers, key_name);
        settings_put_word(action->name);
        settings_write_arg(action->argument_kind, &settings->bindings.key_bindings[i].argument);
        fputc('\n', settings_file);
    }
}

static void settings_write_buttons(void)
{
    settings_put_note("button", "button: bar area, modifiers+button, action, argument");
    for (size_t i = 0; i < settings->bindings.mouse_binding_count; i++) {
        const ActionEntry* action =
            settings_action_by_func(settings->bindings.mouse_bindings[i].callback);

        if (!action) {
            fprintf(stderr, "g0wm: click %zu calls an action with no name\n", i);
            continue;
        }
        settings_start_group("button");
        settings_put_word(settings_enum_name(settings_click_areas,
                                             (int)settings->bindings.mouse_bindings[i].click_area));
        settings_write_combo(settings->bindings.mouse_bindings[i].modifiers,
                             settings_enum_name(settings_mouse_buttons,
                                                (int)settings->bindings.mouse_bindings[i].button));
        settings_put_word(action->name);
        settings_write_arg(action->argument_kind, &settings->bindings.mouse_bindings[i].argument);
        fputc('\n', settings_file);
    }
}

static void settings_write_gestures(void)
{
    settings_put_note("gesture", "gesture: motion, fingers, action, argument");
    for (size_t i = 0; i < settings->bindings.gesture_binding_count; i++) {
        const ActionEntry* action =
            settings_action_by_func(settings->bindings.gesture_bindings[i].callback);

        if (!action) {
            fprintf(stderr, "g0wm: gesture %zu calls an action with no name\n", i);
            continue;
        }
        settings_start_group("gesture");
        settings_put_word(settings_enum_name(
            settings_gesture_motions, (int)settings->bindings.gesture_bindings[i].direction));
        fprintf(settings_file, " %u", settings->bindings.gesture_bindings[i].finger_count);
        settings_put_word(action->name);
        settings_write_arg(action->argument_kind, &settings->bindings.gesture_bindings[i].argument);
        fputc('\n', settings_file);
    }
}

static void settings_write_pad_buttons(void)
{
    settings_put_note("padbutton", "padbutton: button from 0, action, argument");
    for (size_t i = 0; i < settings->bindings.tablet_pad_binding_count; i++) {
        const ActionEntry* action =
            settings_action_by_func(settings->bindings.tablet_pad_bindings[i].callback);

        if (!action) {
            fprintf(stderr, "g0wm: pad button %zu calls an action with no name\n", i);
            continue;
        }
        settings_start_group("padbutton");
        fprintf(settings_file, " %u", settings->bindings.tablet_pad_bindings[i].button);
        settings_put_word(action->name);
        settings_write_arg(action->argument_kind,
                           &settings->bindings.tablet_pad_bindings[i].argument);
        fputc('\n', settings_file);
    }
}

static void settings_write_all(void)
{
    settings_put_note("", "a setting left out keeps its default, one with no value is off");
    settings_write_look();
    settings_write_windows();
    settings_write_private();
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
    size_t length;

    if ((settings_file = open_memstream(&text, &length))) {
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
