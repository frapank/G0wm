/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * reading the config file and applying each section
 */

#include <libinput.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <scfg.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "input/pointer.h"
#include "internal.h"
#include "window/layout.h"

/* variables */
static int settings_line; /* of the directive being read */

/* repeatable, so never missing */
static const char* const settings_records[] = { "layout", "rule",   "monitor", "autostart",
                                                "key",    "button", "gesture", "padbutton" };

/* function implementations */
[[gnu::format(printf, 1, 2)]] static void settings_warn(const char* fmt, ...)
{

    fprintf(stderr, "g0wm: config:%d: ", settings_line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

static int settings_enum_value(const EnumEntry* table,
                               const char* name,
                               int fallback,
                               const char* what)
{
    for (const EnumEntry* entry = table; entry->name; entry++)
        if (!strcmp(entry->name, name))
            return entry->value;
    settings_warn("%s is not a %s", name, what);
    return fallback;
}

static const ActionEntry* settings_action_by_name(const char* name)
{
    if (!name)
        return nullptr;
    for (size_t i = 0; i < settings_action_count; i++)
        if (!strcmp(settings_actions[i].name, name))
            return &settings_actions[i];
    settings_warn("%s is not an action", name);
    return nullptr;
}

static const struct scfg_directive* settings_find_directive(const struct scfg_block* block,
                                                            const char* key)
{
    if (!block)
        return nullptr;
    for (size_t i = 0; i < block->directives_len; i++) {
        if (!strcmp(block->directives[i].name, key)) {
            settings_line = block->directives[i].lineno;
            return &block->directives[i];
        }
    }
    return nullptr;
}

static const struct scfg_block* settings_find_section(const struct scfg_block* block,
                                                      const char* key)
{
    const struct scfg_directive* directive = settings_find_directive(block, key);

    return directive ? &directive->children : nullptr;
}

static size_t settings_count_directives(const struct scfg_block* block, const char* key)
{
    size_t count = 0;

    for (size_t i = 0; i < block->directives_len; i++)
        count += !strcmp(block->directives[i].name, key);
    return count;
}

static const char* settings_param_at(const struct scfg_directive* directive, size_t param_index)
{
    return directive && param_index < directive->params_len ? directive->params[param_index]
                                                            : nullptr;
}

static int settings_is_bool(const char* text)
{
    return text && (!strcmp(text, "true") || !strcmp(text, "false"));
}

static int settings_is_number(const char* text, double* out_value)
{
    if (!text || !*text)
        return 0;
    char* end;
    const double parsed = strtod(text, &end);
    if (*end)
        return 0;
    if (out_value)
        *out_value = parsed;
    return 1;
}

static double settings_number_value(const char* text, double fallback)
{
    double value;

    return settings_is_number(text, &value) ? value : fallback;
}

/* a negative one would not convert */
static unsigned int settings_uint_value(const char* text, unsigned int fallback)
{
    double value;

    if (!settings_is_number(text, &value))
        return fallback;
    if (value < 0 || value > UINT_MAX) {
        settings_warn("%s is out of range", text);
        return fallback;
    }
    return (unsigned int)value;
}

static uint32_t settings_hex_value(const char* text, uint32_t fallback)
{
    if (!text)
        return fallback;
    if (*text == '#' && strlen(text) == 9) {
        char* end;
        const unsigned long value = strtoul(text + 1, &end, 16);
        if (!*end)
            return (uint32_t)value;
    }
    settings_warn("%s is not an #rrggbbaa color", text);
    return fallback;
}

/* the getters keep the value config.h gave when the file has nothing usable */
static int settings_get_bool(const struct scfg_block* block, const char* key, int fallback)
{
    const char* text = settings_param_at(settings_find_directive(block, key), 0);

    return settings_is_bool(text) ? *text == 't' : fallback;
}

static double settings_get_number(const struct scfg_block* block, const char* key, double fallback)
{
    return settings_number_value(settings_param_at(settings_find_directive(block, key), 0),
                                 fallback);
}

static unsigned int settings_get_uint(const struct scfg_block* block,
                                      const char* key,
                                      unsigned int fallback)
{
    return settings_uint_value(settings_param_at(settings_find_directive(block, key), 0), fallback);
}

static const char* settings_get_string(const struct scfg_block* block,
                                       const char* key,
                                       const char* fallback)
{
    const struct scfg_directive* directive = settings_find_directive(block, key);

    return directive ? settings_param_at(directive, 0) : fallback;
}

static int settings_get_enum(const struct scfg_block* block,
                             const char* key,
                             const EnumEntry* enum_table,
                             int fallback,
                             const char* what)
{
    const char* name = settings_get_string(block, key, nullptr);

    return name ? settings_enum_value(enum_table, name, fallback, what) : fallback;
}

static uint32_t settings_get_hex(const struct scfg_block* block, const char* key, uint32_t fallback)
{
    return settings_hex_value(settings_get_string(block, key, nullptr), fallback);
}

static void settings_get_rgba(float color[static 4],
                              const struct scfg_block* block,
                              const char* key)
{
    uint32_t packed = settings_get_hex(block, key, settings_pack_rgba(color));

    color[0] = ((packed >> 24) & 0xFF) / 255.0f;
    color[1] = ((packed >> 16) & 0xFF) / 255.0f;
    color[2] = ((packed >> 8) & 0xFF) / 255.0f;
    color[3] = (packed & 0xFF) / 255.0f;
}

/* the parsed file outlives the settings, so its strings are used in place.
 * One slot to spare: the result is NULL terminated as well as counted. */
static const char** settings_collect_params(const struct scfg_directive* directive,
                                            size_t from,
                                            size_t* out_count)
{
    if (!directive || directive->params_len < from)
        return nullptr;
    const char** list = util_calloc(directive->params_len - from + 1, sizeof *list);
    for (size_t i = from; i < directive->params_len; i++)
        list[i - from] = directive->params[i];
    if (out_count)
        *out_count = directive->params_len - from;
    return list;
}

/* logo+shift+q: the mods into *mod, returns q */
static const char* settings_split_mods(const char* combo, uint32_t* modifiers)
{
    *modifiers = 0;
    const char* plus;
    while ((plus = strchr(combo, '+'))) {
        char modifier_name[16];
        snprintf(modifier_name, sizeof modifier_name, "%.*s", (int)(plus - combo), combo);
        *modifiers |=
            (uint32_t)settings_enum_value(settings_modifier_names, modifier_name, 0, "modifier");
        combo = plus + 1;
    }
    return combo;
}

static uint32_t settings_get_mods(const struct scfg_block* block,
                                  const char* key,
                                  uint32_t fallback)
{
    const char* combo = settings_get_string(block, key, nullptr);

    if (!combo)
        return fallback;
    if (!strcmp(combo, "none"))
        return 0;
    uint32_t modifiers;
    combo = settings_split_mods(combo, &modifiers);
    return modifiers | (uint32_t)settings_enum_value(settings_modifier_names, combo, 0, "modifier");
}

static const WindowLayout* settings_layout_by_symbol(const char* symbol)
{
    if (!symbol)
        return nullptr;
    for (size_t i = 0; i < settings->rules.layout_count; i++)
        if (!strcmp(settings->rules.layouts[i].symbol, symbol))
            return &settings->rules.layouts[i];
    settings_warn("no layout draws %s", symbol);
    return nullptr;
}

static ActionArg settings_get_arg(char kind,
                                  const struct scfg_directive* directive,
                                  size_t param_index)
{
    const char* text = settings_param_at(directive, param_index);
    ActionArg argument = {};

    switch (kind) {
        case 'i':
            argument.integer = (int)settings_number_value(text, 0);
            break;
        case 'u':
            argument.unsigned_integer = settings_uint_value(text, 0);
            break;
        case 'f':
            argument.floating = (float)settings_number_value(text, 0);
            break;
        case 'v':
            argument.pointer = settings_collect_params(directive, param_index, nullptr);
            break;
        case 'l':
            argument.pointer = settings_layout_by_symbol(text);
            break;
        case 'c':
            argument.unsigned_integer =
                text ? (uint32_t)settings_enum_value(settings_drag_modes, text, CURSOR_MODE_MOVE,
                                                     "drag mode")
                     : CURSOR_MODE_MOVE;
            break;
    }
    return argument;
}

static void settings_apply_colors(const struct scfg_block* block)
{
    for (size_t scheme = 0; scheme < COLOR_SCHEME_COUNT; scheme++) {
        if (!settings_scheme_names[scheme])
            continue;
        const struct scfg_directive* directive =
            settings_find_directive(block, settings_scheme_names[scheme]);
        if (!directive)
            continue;
        for (size_t color = 0; color < 3; color++)
            settings->look.colors[scheme][color] = settings_hex_value(
                settings_param_at(directive, color), settings->look.colors[scheme][color]);
    }
}

static void settings_apply_look(const struct scfg_block* block)
{
    static const char** owned_fonts;
    static const char** owned_tags;

    if (!block)
        return;
    const char** font_list = settings_collect_params(settings_find_directive(block, "fonts"), 0,
                                                     &settings->look.font_count);
    if (font_list) {
        free(owned_fonts);
        settings->look.fonts = owned_fonts = font_list;
    }
    settings_apply_colors(settings_find_section(block, "colors"));
    settings_get_rgba(settings->look.root_color, block, "rootcolor");
    settings_get_rgba(settings->look.fullscreen_color, block, "fullscreen_bg");
#ifdef INTEGRATED_BACKGROUND
    settings->look.wallpaper = settings_get_string(block, "wallpaper", settings->look.wallpaper);
#endif
    const char** tag_list = settings_collect_params(settings_find_directive(block, "tags"), 0,
                                                    &settings->look.tag_count);
    if (tag_list) {
        free(owned_tags);
        owned_tags = tag_list;
        settings->look.tags = (char**)tag_list;
        if (settings->look.tag_count > 31) { /* TAGMASK has to fit in a uint32_t */
            settings_warn("only the first 31 tags are used");
            settings->look.tag_count = 31;
        }
    }
}

static void settings_apply_windows(const struct scfg_block* block)
{
    if (!block)
        return;
    settings->windows.border_width =
        settings_get_uint(block, "borderpx", settings->windows.border_width);
    settings->windows.corner_style =
        settings_get_enum(block, "cornerstyle", settings_corner_styles,
                          settings->windows.corner_style, "corner style");
    settings->windows.corner_radius =
        settings_get_uint(block, "cornerpx", settings->windows.corner_radius);
    settings->windows.gaps = (int)settings_get_number(block, "gaps", settings->windows.gaps);
    settings->windows.gap_size = settings_get_uint(block, "gappx", settings->windows.gap_size);
    settings->windows.smart_gaps =
        settings_get_bool(block, "smartgaps", settings->windows.smart_gaps);
    settings->windows.title_bar = settings_get_bool(block, "titlebar", settings->windows.title_bar);
    settings->windows.title_padding =
        settings_get_uint(block, "titlepadding", settings->windows.title_padding);
    settings->windows.title_center =
        settings_get_bool(block, "titlecenter", settings->windows.title_center);
    settings->windows.title_close =
        settings_get_bool(block, "titleclose", settings->windows.title_close);
    settings->windows.title_close_color =
        settings_get_hex(block, "titleclosecolor", settings->windows.title_close_color);
    settings->windows.private_color =
        settings_get_hex(block, "privatecolor", settings->windows.private_color);
    settings->windows.private_round =
        (float)fmin(settings_get_number(block, "privateround", settings->windows.private_round), 1);
    settings->windows.always_private =
        settings_get_bool(block, "alwaysprivate", settings->windows.always_private);
    settings->windows.public_color =
        settings_get_hex(block, "publiccolor", settings->windows.public_color);
    settings->windows.private_title =
        settings_get_string(block, "privatetitle", settings->windows.private_title);
    settings->input.move_threshold =
        (int)settings_get_number(block, "movethreshold", settings->input.move_threshold);
    settings->input.move_snap =
        (int)settings_get_number(block, "movesnap", settings->input.move_snap);
}

static void settings_apply_private(const struct scfg_block* block)
{
    if (!block)
        return;
    settings->privacy.text = settings_get_string(block, "text", settings->privacy.text);
    settings->privacy.image = settings_get_string(block, "image", settings->privacy.image);
    settings->privacy.image_size =
        settings_get_uint(block, "imagesize", settings->privacy.image_size);
    settings->privacy.text_scale = (float)fmax(
        0.1, fmin(10, settings_get_number(block, "textscale", settings->privacy.text_scale)));
    settings->privacy.background = settings_get_hex(block, "color", settings->privacy.background);
    settings->privacy.foreground =
        settings_get_hex(block, "textcolor", settings->privacy.foreground);
}

static void settings_apply_bar(const struct scfg_block* block)
{
    if (!block)
        return;
    settings->bar.enabled = settings_get_bool(block, "showbar", settings->bar.enabled);
    settings->bar.top = settings_get_bool(block, "topbar", settings->bar.top);
    settings->bar.window_title =
        settings_get_bool(block, "barwintitle", settings->bar.window_title);
    settings->bar.tag_dots = settings_get_bool(block, "bartagdots", settings->bar.tag_dots);
    settings->bar.tag_dot_size =
        settings_get_uint(block, "bartagdotsize", settings->bar.tag_dot_size);
    settings->bar.tag_dot_gap = settings_get_uint(block, "bartagdotgap", settings->bar.tag_dot_gap);
    settings->bar.tag_dot_width =
        (float)fmax(settings_get_number(block, "bartagdotwidth", settings->bar.tag_dot_width), 1);
    settings->bar.tag_dot_round = (float)fmin(
        fmax(settings_get_number(block, "bartagdotround", settings->bar.tag_dot_round), 0), 1);
    settings->bar.urgent_pulse =
        settings_get_uint(block, "barurgentpulse", settings->bar.urgent_pulse);
    settings->bar.layout_symbol =
        settings_get_bool(block, "barltsymbol", settings->bar.layout_symbol);
    settings->bar.box_radius = settings_get_uint(block, "barboxradius", settings->bar.box_radius);
    settings->bar.padding = settings_get_uint(block, "barpadding", settings->bar.padding);
    settings->bar.height = (float)settings_get_number(block, "barheight", settings->bar.height);
    settings->bar.single_monitor =
        settings_get_bool(block, "barsinglemon", settings->bar.single_monitor);
    settings->bar.status = settings_get_enum(block, "barstatus", settings_bar_statuses,
                                             settings->bar.status, "bar status setting");
    settings->systray.enabled = settings_get_bool(block, "showsystray", settings->systray.enabled);
    settings->systray.spacing =
        settings_get_uint(block, "systrayspacing", settings->systray.spacing);
    settings->systray.padding =
        settings_get_uint(block, "systraypadding", settings->systray.padding);
    settings->systray.icon_size =
        settings_get_uint(block, "systrayiconsize", settings->systray.icon_size);
    settings->notifications.enabled =
        settings_get_bool(block, "shownotifications", settings->notifications.enabled);
    settings->notifications.private =
        settings_get_bool(block, "privatenotify", settings->notifications.private);
    settings->notifications.timeout =
        settings_get_uint(block, "notification_timeout", settings->notifications.timeout);
    settings->notifications.line_width =
        settings_get_uint(block, "notification_linewidth", settings->notifications.line_width);
    settings->notifications.line_radius =
        settings_get_uint(block, "notification_lineradius", settings->notifications.line_radius);
    settings->notifications.action_sign =
        settings_get_string(block, "notification_actionsign", settings->notifications.action_sign);
    settings->notifications.private_text = settings_get_string(
        block, "notification_privatetext", settings->notifications.private_text);
    settings->notifications.private_hide =
        settings_get_bool(block, "notification_privatehide", settings->notifications.private_hide);
    settings->notifications.dot =
        settings_get_enum(block, "notification_dot", settings_notification_dots,
                          settings->notifications.dot, "notification dot setting");
    settings->notifications.private_color =
        settings_get_hex(block, "notification_privatecolor", settings->notifications.private_color);
    settings->notifications.public_color =
        settings_get_hex(block, "notification_publiccolor", settings->notifications.public_color);
    settings->runner.placeholder =
        settings_get_string(block, "runner_placeholder", settings->runner.placeholder);
    settings->runner.max_candidates =
        settings_get_uint(block, "runner_max_candidates", settings->runner.max_candidates);
    settings->runner.pill_height =
        settings_get_uint(block, "runner_pillheight", settings->runner.pill_height);
    settings->runner.pill_padding =
        settings_get_uint(block, "runner_pillpadding", settings->runner.pill_padding);
    settings->runner.pill_round =
        (float)fmin(settings_get_number(block, "runner_pillround", settings->runner.pill_round), 1);
    settings->runner.fuzzy = settings_get_bool(block, "runner_fuzzy", settings->runner.fuzzy);
    settings->runner.icons = settings_get_enum(block, "runner_icons", settings_runner_icons,
                                               settings->runner.icons, "runner icon setting");
    settings->runner.icon_size =
        settings_get_uint(block, "runner_iconsize", settings->runner.icon_size);
    settings->runner.icon_round = (float)fmin(
        fmax(settings_get_number(block, "runner_iconround", settings->runner.icon_round), 0), 1);
    settings->runner.icon_border =
        settings_get_uint(block, "runner_iconborder", settings->runner.icon_border);
    settings->runner.history_size =
        settings_get_uint(block, "runner_history_size", settings->runner.history_size);
    settings->runner.shell = settings_get_string(block, "runner_shell", settings->runner.shell);
    settings->runner.terminal =
        settings_get_string(block, "runner_terminal", settings->runner.terminal);
    settings->runner.terminal_prefix =
        settings_get_string(block, "runner_termprefix", settings->runner.terminal_prefix);
    settings->runner.run_prefix =
        settings_get_string(block, "runner_runprefix", settings->runner.run_prefix);
    settings->runner.calc_prefix =
        settings_get_string(block, "runner_calcprefix", settings->runner.calc_prefix);
    settings->runner.search_prefix =
        settings_get_string(block, "runner_searchprefix", settings->runner.search_prefix);
    settings->runner.search_url =
        settings_get_string(block, "runner_searchurl", settings->runner.search_url);
    settings->runner.window_prefix =
        settings_get_string(block, "runner_windowprefix", settings->runner.window_prefix);
    settings->runner.tag_prefix =
        settings_get_string(block, "runner_tagprefix", settings->runner.tag_prefix);
    settings->runner.files = settings_get_bool(block, "runner_files", settings->runner.files);
    settings->runner.paths = settings_get_string(block, "runner_paths", settings->runner.paths);
    settings->runner.calc_precision =
        settings_get_uint(block, "runner_calc_precision", settings->runner.calc_precision);
}

static void settings_apply_opacity(const struct scfg_block* block)
{
    if (!block)
        return;
    settings->opacity.enabled =
        settings_get_bool(block, "opacity_enabled", settings->opacity.enabled);
    settings->opacity.focus =
        (float)settings_get_number(block, "opacity_focus", settings->opacity.focus);
    settings->opacity.unfocus =
        (float)settings_get_number(block, "opacity_unfocus", settings->opacity.unfocus);
    settings->opacity.decorations =
        (float)settings_get_number(block, "opacity_deco", settings->opacity.decorations);
#ifdef INTEGRATED_BACKGROUND
    settings->opacity.type = settings_get_enum(block, "opacity_type", settings_opacity_kinds,
                                               settings->opacity.type, "kind of opacity");
    settings->opacity.blur_radius =
        settings_get_uint(block, "blur_radius", settings->opacity.blur_radius);
    settings->opacity.blur_passes =
        settings_get_uint(block, "blur_passes", settings->opacity.blur_passes);
    settings->opacity.blur_saturation =
        (float)settings_get_number(block, "blur_saturation", settings->opacity.blur_saturation);
    settings->opacity.blur_brightness =
        (float)settings_get_number(block, "blur_brightness", settings->opacity.blur_brightness);
#endif
}

static void settings_apply_input(const struct scfg_block* block)
{
    if (!block)
        return;
    settings->input.sloppy_focus =
        settings_get_bool(block, "sloppyfocus", settings->input.sloppy_focus);
    const struct scfg_block* xkb_section = settings_find_section(block, "xkb_rules");
    if (xkb_section) {
        settings->input.xkb_rules.rules =
            settings_get_string(xkb_section, "rules", settings->input.xkb_rules.rules);
        settings->input.xkb_rules.model =
            settings_get_string(xkb_section, "model", settings->input.xkb_rules.model);
        settings->input.xkb_rules.layout =
            settings_get_string(xkb_section, "layout", settings->input.xkb_rules.layout);
        settings->input.xkb_rules.variant =
            settings_get_string(xkb_section, "variant", settings->input.xkb_rules.variant);
        settings->input.xkb_rules.options =
            settings_get_string(xkb_section, "options", settings->input.xkb_rules.options);
    }
    settings->input.repeat_rate =
        (int)settings_get_number(block, "repeat_rate", settings->input.repeat_rate);
    settings->input.repeat_delay =
        (int)settings_get_number(block, "repeat_delay", settings->input.repeat_delay);
    settings->input.cursor_theme =
        settings_get_string(block, "cursor_theme", settings->input.cursor_theme);
    settings->input.cursor_size =
        (int)settings_get_number(block, "cursor_size", settings->input.cursor_size);
    settings->input.hide_cursor_when_typing = settings_get_bool(
        block, "hide_cursor_when_typing", settings->input.hide_cursor_when_typing);
    settings->input.tap_to_click =
        settings_get_bool(block, "tap_to_click", settings->input.tap_to_click);
    settings->input.tap_and_drag =
        settings_get_bool(block, "tap_and_drag", settings->input.tap_and_drag);
    settings->input.drag_lock = settings_get_bool(block, "drag_lock", settings->input.drag_lock);
    settings->input.natural_scrolling =
        settings_get_bool(block, "natural_scrolling", settings->input.natural_scrolling);
    settings->input.disable_while_typing =
        settings_get_bool(block, "disable_while_typing", settings->input.disable_while_typing);
    settings->input.left_handed =
        settings_get_bool(block, "left_handed", settings->input.left_handed);
    settings->input.middle_button_emulation = settings_get_bool(
        block, "middle_button_emulation", settings->input.middle_button_emulation);
    settings->input.scroll_method = (enum libinput_config_scroll_method)settings_get_enum(
        block, "scroll_method", settings_scroll_methods, settings->input.scroll_method,
        "scroll method");
    settings->input.click_method = (enum libinput_config_click_method)settings_get_enum(
        block, "click_method", settings_click_methods, settings->input.click_method,
        "click method");
    settings->input.send_events_mode =
        (uint32_t)settings_get_enum(block, "send_events_mode", settings_send_events,
                                    (int)settings->input.send_events_mode, "send events mode");
    settings->input.accel_profile = (enum libinput_config_accel_profile)settings_get_enum(
        block, "accel_profile", settings_accel_profiles, settings->input.accel_profile,
        "acceleration profile");
    settings->input.accel_speed =
        settings_get_number(block, "accel_speed", settings->input.accel_speed);
    settings->input.touchpad_natural_scrolling = settings_get_bool(
        block, "touchpad_natural_scrolling", settings->input.touchpad_natural_scrolling);
    settings->input.touchpad_accel_profile = (enum libinput_config_accel_profile)settings_get_enum(
        block, "touchpad_accel_profile", settings_accel_profiles,
        settings->input.touchpad_accel_profile, "acceleration profile");
    settings->input.touchpad_accel_speed =
        settings_get_number(block, "touchpad_accel_speed", settings->input.touchpad_accel_speed);
    settings->bindings.pinch_zoom_modifier =
        settings_get_mods(block, "pinchzoom_mod", settings->bindings.pinch_zoom_modifier);
    settings->input.tap_button_map = (enum libinput_config_tap_button_map)settings_get_enum(
        block, "button_map", settings_button_maps, settings->input.tap_button_map, "button map");
    settings->input.tablet_output =
        settings_get_string(block, "tablet_output", settings->input.tablet_output);
    settings->input.tablet_left_handed =
        settings_get_bool(block, "tablet_left_handed", settings->input.tablet_left_handed);
    settings->input.touch_output =
        settings_get_string(block, "touch_output", settings->input.touch_output);
}

static void settings_apply_misc(const struct scfg_block* block)
{
    if (!block)
        return;
    settings->misc.log_level = settings_get_enum(block, "log_level", settings_log_levels,
                                                 settings->misc.log_level, "log level");
    settings->misc.bypass_surface_visibility = settings_get_bool(
        block, "bypass_surface_visibility", settings->misc.bypass_surface_visibility);
    settings->misc.allow_tearing =
        settings_get_bool(block, "allow_tearing", settings->misc.allow_tearing);
    settings->misc.auto_vrr = settings_get_bool(block, "auto_vrr", settings->misc.auto_vrr);
}

/* none keeps the defaults: a monitor needs a layout */
static void settings_apply_layouts(const struct scfg_block* block)
{
    if (!settings_count_directives(block, "layout"))
        return;
    WindowLayout* parsed_layouts =
        util_calloc(settings_count_directives(block, "layout"), sizeof *parsed_layouts);
    size_t count = 0;
    for (size_t i = 0; i < block->directives_len; i++) {
        const struct scfg_directive* directive = &block->directives[i];
        settings_line = directive->lineno;
        if (strcmp(directive->name, "layout"))
            continue;
        parsed_layouts[count].symbol =
            settings_param_at(directive, 0) ? settings_param_at(directive, 0) : "[ ]";
        const char* arrange_name =
            settings_param_at(directive, 1) ? settings_param_at(directive, 1) : "tile";
        size_t arrange_index = 0;
        while (arrange_index < settings_arrange_count &&
               strcmp(settings_arranges[arrange_index].name, arrange_name))
            arrange_index++;
        if (arrange_index < settings_arrange_count) {
            parsed_layouts[count].arrange = settings_arranges[arrange_index].callback;
        } else {
            parsed_layouts[count].arrange = layout_tile;
            settings_warn("%s is not a layout, using tile", arrange_name);
        }
        count++;
    }
    settings->rules.layouts = parsed_layouts;
    settings->rules.layout_count = count;
}

static void settings_apply_rules(const struct scfg_block* block)
{
    ClientRule* parsed_rules =
        util_calloc(settings_count_directives(block, "rule") + 1, sizeof *parsed_rules);
    size_t count = 0;
    for (size_t i = 0; i < block->directives_len; i++) {
        if (strcmp(block->directives[i].name, "rule"))
            continue;
        const struct scfg_block* rule_block = &block->directives[i].children;
        ClientRule* rule = &parsed_rules[count];
        rule->app_id = settings_get_string(rule_block, "id", nullptr);
        rule->title = settings_get_string(rule_block, "title", nullptr);
        rule->tags = settings_get_uint(rule_block, "tags", 0);
        rule->is_floating = settings_get_bool(rule_block, "isfloating", 0);
        rule->is_private = settings_get_bool(rule_block, "private", -1);
        rule->private_text = settings_get_string(rule_block, "privatetext", nullptr);
        rule->private_background = settings_get_hex(rule_block, "privatecolor", 0);
        rule->private_foreground = settings_get_hex(rule_block, "privatetextcolor", 0);
        rule->focused_opacity = (float)settings_get_number(rule_block, "opacity_focus", 0);
        rule->unfocused_opacity = (float)settings_get_number(rule_block, "opacity_unfocus", 0);
        rule->monitor_index = (int)settings_get_number(rule_block, "monitor", -1);
        count++;
    }
    settings->rules.client_rules = parsed_rules;
    settings->rules.client_rule_count = count;
}

static void settings_apply_monitors(const struct scfg_block* block)
{
    if (!settings_count_directives(block, "monitor"))
        return;
    MonitorRule* parsed_rules =
        util_calloc(settings_count_directives(block, "monitor"), sizeof *parsed_rules);
    size_t count = 0;
    for (size_t i = 0; i < block->directives_len; i++) {
        if (strcmp(block->directives[i].name, "monitor"))
            continue;
        const struct scfg_block* monitor_block = &block->directives[i].children;
        MonitorRule* rule = &parsed_rules[count];
        rule->name = settings_get_string(monitor_block, "name", nullptr);
        rule->master_factor = (float)settings_get_number(monitor_block, "mfact", 0.55);
        rule->master_count = (int)settings_get_number(monitor_block, "nmaster", 1);
        rule->scale = (float)settings_get_number(monitor_block, "scale", 1);
        rule->layout =
            settings_layout_by_symbol(settings_get_string(monitor_block, "layout", nullptr));
        if (!rule->layout)
            rule->layout = settings->rules.layouts;
        rule->transform = (enum wl_output_transform)settings_get_enum(
            monitor_block, "transform", settings_transforms, WL_OUTPUT_TRANSFORM_NORMAL,
            "transform");
        rule->x = (int)settings_get_number(monitor_block, "x", -1);
        rule->y = (int)settings_get_number(monitor_block, "y", -1);
        rule->width = (int)settings_get_number(monitor_block, "width", 0);
        rule->height = (int)settings_get_number(monitor_block, "height", 0);
        rule->refresh_hz = (int)settings_get_number(monitor_block, "refresh", 0);
        count++;
    }
    settings->rules.monitor_rules = parsed_rules;
    settings->rules.monitor_rule_count = count;
}

/* back to one flat run of arguments per command, each NULL terminated */
static void settings_apply_autostart(const struct scfg_block* block)
{
    size_t slots = 0;
    for (size_t i = 0; i < block->directives_len; i++)
        if (!strcmp(block->directives[i].name, "autostart"))
            slots += block->directives[i].params_len + 1;
    const char** flat_commands = util_calloc(slots + 1, sizeof *flat_commands);

    size_t count = 0;
    for (size_t i = 0; i < block->directives_len; i++) {
        const struct scfg_directive* directive = &block->directives[i];
        settings_line = directive->lineno;
        if (strcmp(directive->name, "autostart") || !directive->params_len)
            continue;
        for (size_t param = 0; param < directive->params_len; param++)
            flat_commands[count++] = directive->params[param];
        flat_commands[count++] = nullptr;
    }
    settings->bindings.autostart = flat_commands;
}

static void settings_apply_keys(const struct scfg_block* block)
{
    KeyBinding* parsed_keys =
        util_calloc(settings_count_directives(block, "key") + 1, sizeof *parsed_keys);
    size_t count = 0;
    for (size_t i = 0; i < block->directives_len; i++) {
        const struct scfg_directive* directive = &block->directives[i];
        settings_line = directive->lineno;
        const ActionEntry* action;
        if (strcmp(directive->name, "key") ||
            !(action = settings_action_by_name(settings_param_at(directive, 1))))
            continue;
        const char* key_name =
            settings_split_mods(settings_param_at(directive, 0), &parsed_keys[count].modifiers);
        const xkb_keysym_t keysym = xkb_keysym_from_name(key_name, XKB_KEYSYM_NO_FLAGS);
        if (keysym == XKB_KEY_NoSymbol) {
            settings_warn("%s is not a key", key_name);
            continue;
        }
        parsed_keys[count].keysym = keysym;
        parsed_keys[count].callback = action->callback;
        parsed_keys[count].argument = settings_get_arg(action->argument_kind, directive, 2);
        count++;
    }
    settings->bindings.key_bindings = parsed_keys;
    settings->bindings.key_binding_count = count;
}

static void settings_apply_buttons(const struct scfg_block* block)
{
    MouseBinding* parsed_buttons =
        util_calloc(settings_count_directives(block, "button") + 1, sizeof *parsed_buttons);
    size_t count = 0;
    for (size_t i = 0; i < block->directives_len; i++) {
        const struct scfg_directive* directive = &block->directives[i];
        settings_line = directive->lineno;
        const ActionEntry* action;
        if (strcmp(directive->name, "button") ||
            !(action = settings_action_by_name(settings_param_at(directive, 2))))
            continue;
        MouseBinding* button = &parsed_buttons[count];
        button->click_area = (unsigned int)settings_enum_value(
            settings_click_areas, settings_param_at(directive, 0), CLICK_AREA_CLIENT, "click area");
        button->button = (unsigned int)settings_enum_value(
            settings_mouse_buttons,
            settings_split_mods(settings_param_at(directive, 1), &button->modifiers), BTN_LEFT,
            "button");
        button->callback = action->callback;
        button->argument = settings_get_arg(action->argument_kind, directive, 3);
        count++;
    }
    settings->bindings.mouse_bindings = parsed_buttons;
    settings->bindings.mouse_binding_count = count;
}

static void settings_apply_gestures(const struct scfg_block* block)
{
    GestureBinding* parsed_gestures =
        util_calloc(settings_count_directives(block, "gesture") + 1, sizeof *parsed_gestures);
    size_t count = 0;
    for (size_t i = 0; i < block->directives_len; i++) {
        const struct scfg_directive* directive = &block->directives[i];
        settings_line = directive->lineno;
        const ActionEntry* action;
        if (strcmp(directive->name, "gesture") ||
            !(action = settings_action_by_name(settings_param_at(directive, 2))))
            continue;
        GestureBinding* gesture = &parsed_gestures[count];
        gesture->direction = (unsigned int)settings_enum_value(
            settings_gesture_motions, settings_param_at(directive, 0), GESTURE_SWIPE_LEFT,
            "gesture motion");
        gesture->finger_count = settings_uint_value(settings_param_at(directive, 1), 3);
        gesture->callback = action->callback;
        gesture->argument = settings_get_arg(action->argument_kind, directive, 3);
        count++;
    }
    settings->bindings.gesture_bindings = parsed_gestures;
    settings->bindings.gesture_binding_count = count;
}

static void settings_apply_pad_buttons(const struct scfg_block* block)
{
    TabletPadBinding* parsed_pad_buttons =
        util_calloc(settings_count_directives(block, "padbutton") + 1, sizeof *parsed_pad_buttons);
    size_t count = 0;
    for (size_t i = 0; i < block->directives_len; i++) {
        const struct scfg_directive* directive = &block->directives[i];
        settings_line = directive->lineno;
        const ActionEntry* action;
        if (strcmp(directive->name, "padbutton") ||
            !(action = settings_action_by_name(settings_param_at(directive, 1))))
            continue;
        TabletPadBinding* pad_button = &parsed_pad_buttons[count];
        pad_button->button = settings_uint_value(settings_param_at(directive, 0), 0);
        pad_button->callback = action->callback;
        pad_button->argument = settings_get_arg(action->argument_kind, directive, 2);
        count++;
    }
    settings->bindings.tablet_pad_bindings = parsed_pad_buttons;
    settings->bindings.tablet_pad_binding_count = count;
}

void settings_load_builtin(void)
{
    static struct scfg_block builtin_block;

    if (settings_builtin)
        return;
    char* text = settings_rendered_defaults();
    if (!text)
        return;
    FILE* file = fmemopen(text, strlen(text), "r");
    if (file) {
        if (!scfg_parse_file(&builtin_block, file))
            settings_builtin = &builtin_block;
        fclose(file);
    }
    free(text);
}

/* the file's lines of kind k, or the built-in ones when it has none */
static const struct scfg_block* settings_lines_or_builtin(const struct scfg_block* block,
                                                          const char* key)
{
    return settings_count_directives(block, key) || !settings_builtin ? block : settings_builtin;
}

static void settings_apply_sections(const struct scfg_block* block)
{
    settings_apply_look(settings_find_section(block, "look"));
    settings_apply_windows(settings_find_section(block, "windows"));
    settings_apply_private(settings_find_section(block, "private"));
    settings_apply_bar(settings_find_section(block, "bar"));
    settings_apply_opacity(settings_find_section(block, "opacity"));
    settings_apply_input(settings_find_section(block, "input"));
    settings_apply_misc(settings_find_section(block, "misc"));
}

/* the layouts first: the rest names them by symbol */
void settings_apply(const struct scfg_block* block)
{
    settings_apply_layouts(settings_lines_or_builtin(block, "layout"));
    /* a reload has to undo a setting since left out */
    settings_apply_sections(settings_builtin);
    settings_apply_sections(block);
    settings_apply_rules(settings_lines_or_builtin(block, "rule"));
    settings_apply_monitors(settings_lines_or_builtin(block, "monitor"));
    settings_apply_autostart(settings_lines_or_builtin(block, "autostart"));
    settings_apply_keys(settings_lines_or_builtin(block, "key"));
    settings_apply_buttons(settings_lines_or_builtin(block, "button"));
    settings_apply_gestures(settings_lines_or_builtin(block, "gesture"));
    settings_apply_pad_buttons(settings_lines_or_builtin(block, "padbutton"));
}

static int settings_is_record(const char* name)
{
    for (size_t i = 0; i < LENGTH(settings_records); i++)
        if (!strcmp(settings_records[i], name))
            return 1;
    return 0;
}

static const char* settings_value_kind(const struct scfg_directive* directive)
{
    if (directive->params_len != 1)
        return directive->params_len ? "a list" : "off";
    if (settings_is_bool(directive->params[0]))
        return "true or false";
    if (settings_is_number(directive->params[0], nullptr))
        return "a number";
    return "a string";
}

/* off takes anything, strings and lists any words */
static int settings_has_same_shape(const struct scfg_directive* want,
                                   const struct scfg_directive* got)
{
    const char* kind = settings_value_kind(want);

    if (!want->params_len)
        return 1;
    if (!strcmp(kind, "a list") || !strcmp(kind, "a string"))
        return got->params_len > 0;
    return !strcmp(kind, settings_value_kind(got));
}

void settings_validate(const struct scfg_block* want,
                       const struct scfg_block* got,
                       const char* path,
                       int* problems)
{
    for (size_t i = 0; i < want->directives_len; i++) {
        const struct scfg_directive* wanted = &want->directives[i];
        if (!*path && settings_is_record(wanted->name))
            continue;
        char full_name[256];
        snprintf(full_name, sizeof full_name, "%s%s%s", path, *path ? "." : "", wanted->name);
        const struct scfg_directive* found = settings_find_directive(got, wanted->name);
        if (!found)
            continue;
        if (wanted->children.directives_len) {
            settings_validate(&wanted->children, &found->children, full_name, problems);
        } else if (!settings_has_same_shape(wanted, found)) {
            settings_line = found->lineno;
            settings_warn("%s should be %s", full_name, settings_value_kind(wanted));
            (*problems)++;
        }
    }
    /* a typo, a setting this build was configured without, or a second one */
    for (size_t i = 0; i < got->directives_len; i++) {
        const struct scfg_directive* found = &got->directives[i];
        if (!*path && settings_is_record(found->name)) {
            /* what a rule or a monitor holds */
            const struct scfg_directive* wanted = settings_find_directive(want, found->name);
            if (wanted && wanted->children.directives_len)
                settings_validate(&wanted->children, &found->children, found->name, problems);
            continue;
        }
        const struct scfg_directive* first = settings_find_directive(got, found->name);
        if (first == found && settings_find_directive(want, found->name))
            continue;
        char full_name[256];
        snprintf(full_name, sizeof full_name, "%s%s%s", path, *path ? "." : "", found->name);
        settings_line = found->lineno;
        if (first != found)
            settings_warn("%s is already set on line %d", full_name, first->lineno);
        else
            settings_warn("%s is not a setting this build has", full_name);
        (*problems)++;
    }
}
