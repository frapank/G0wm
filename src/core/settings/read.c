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

static int settings_enum_value(const Enum* table, const char* name, int fallback, const char* what)
{
    for (const Enum* entry = table; entry->name; entry++)
        if (!strcmp(entry->name, name))
            return entry->val;
    settings_warn("%s is not a %s", name, what);
    return fallback;
}

static const Action* settings_action_by_name(const char* name)
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
                             const Enum* enum_table,
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

static const Layout* settings_layout_by_symbol(const char* symbol)
{
    if (!symbol)
        return nullptr;
    for (size_t i = 0; i < layout_count; i++)
        if (!strcmp(layouts[i].symbol, symbol))
            return &layouts[i];
    settings_warn("no layout draws %s", symbol);
    return nullptr;
}

static Arg settings_get_arg(char kind, const struct scfg_directive* directive, size_t param_index)
{
    const char* text = settings_param_at(directive, param_index);
    Arg arg = {};

    switch (kind) {
        case 'i':
            arg.i = (int)settings_number_value(text, 0);
            break;
        case 'u':
            arg.ui = settings_uint_value(text, 0);
            break;
        case 'f':
            arg.f = (float)settings_number_value(text, 0);
            break;
        case 'v':
            arg.v = settings_collect_params(directive, param_index, nullptr);
            break;
        case 'l':
            arg.v = settings_layout_by_symbol(text);
            break;
        case 'c':
            arg.ui = text ? (uint32_t)settings_enum_value(settings_drag_modes, text, CurMove,
                                                          "drag mode")
                          : CurMove;
            break;
    }
    return arg;
}

static void settings_apply_colors(const struct scfg_block* block)
{
    for (size_t scheme = 0; scheme < NumSchemes; scheme++) {
        if (!settings_scheme_names[scheme])
            continue;
        const struct scfg_directive* directive =
            settings_find_directive(block, settings_scheme_names[scheme]);
        if (!directive)
            continue;
        for (size_t color = 0; color < 3; color++)
            colors[scheme][color] =
                settings_hex_value(settings_param_at(directive, color), colors[scheme][color]);
    }
}

static void settings_apply_look(const struct scfg_block* block)
{
    static const char** owned_fonts;
    static const char** owned_tags;

    if (!block)
        return;
    const char** font_list =
        settings_collect_params(settings_find_directive(block, "fonts"), 0, &font_count);
    if (font_list) {
        free(owned_fonts);
        fonts = owned_fonts = font_list;
    }
    settings_apply_colors(settings_find_section(block, "colors"));
    settings_get_rgba(root_color, block, "rootcolor");
    settings_get_rgba(fullscreen_color, block, "fullscreen_bg");
#ifdef INTEGRATED_BACKGROUND
    wallpaper = settings_get_string(block, "wallpaper", wallpaper);
#endif
    const char** tag_list =
        settings_collect_params(settings_find_directive(block, "tags"), 0, &tag_count);
    if (tag_list) {
        free(owned_tags);
        owned_tags = tag_list;
        tags = (char**)tag_list;
        if (tag_count > 31) { /* TAGMASK has to fit in a uint32_t */
            settings_warn("only the first 31 tags are used");
            tag_count = 31;
        }
    }
}

static void settings_apply_windows(const struct scfg_block* block)
{
    if (!block)
        return;
    border_width = settings_get_uint(block, "borderpx", border_width);
    corner_style = settings_get_enum(block, "cornerstyle", settings_corner_styles, corner_style,
                                     "corner style");
    corner_radius = settings_get_uint(block, "cornerpx", corner_radius);
    gaps = (int)settings_get_number(block, "gaps", gaps);
    gap_size = settings_get_uint(block, "gappx", gap_size);
    smart_gaps = settings_get_bool(block, "smartgaps", smart_gaps);
    title_bar = settings_get_bool(block, "titlebar", title_bar);
    title_padding = settings_get_uint(block, "titlepadding", title_padding);
    title_center = settings_get_bool(block, "titlecenter", title_center);
    title_close = settings_get_bool(block, "titleclose", title_close);
    title_close_color = settings_get_hex(block, "titleclosecolor", title_close_color);
    private_color = settings_get_hex(block, "privatecolor", private_color);
    private_round = (float)fmin(settings_get_number(block, "privateround", private_round), 1);
    always_private = settings_get_bool(block, "alwaysprivate", always_private);
    public_color = settings_get_hex(block, "publiccolor", public_color);
    private_title = settings_get_string(block, "privatetitle", private_title);
    move_threshold = (int)settings_get_number(block, "movethreshold", move_threshold);
    move_snap = (int)settings_get_number(block, "movesnap", move_snap);
}

static void settings_apply_private(const struct scfg_block* block)
{
    if (!block)
        return;
    private_text = settings_get_string(block, "text", private_text);
    private_image = settings_get_string(block, "image", private_image);
    private_image_size = settings_get_uint(block, "imagesize", private_image_size);
    private_background = settings_get_hex(block, "color", private_background);
    private_foreground = settings_get_hex(block, "textcolor", private_foreground);
}

static void settings_apply_bar(const struct scfg_block* block)
{
    if (!block)
        return;
    bar_enabled = settings_get_bool(block, "showbar", bar_enabled);
    bar_top = settings_get_bool(block, "topbar", bar_top);
    bar_window_title = settings_get_bool(block, "barwintitle", bar_window_title);
    bar_tag_dots = settings_get_bool(block, "bartagdots", bar_tag_dots);
    bar_tag_dot_size = settings_get_uint(block, "bartagdotsize", bar_tag_dot_size);
    bar_tag_dot_gap = settings_get_uint(block, "bartagdotgap", bar_tag_dot_gap);
    bar_tag_dot_width =
        (float)fmax(settings_get_number(block, "bartagdotwidth", bar_tag_dot_width), 1);
    bar_tag_dot_round =
        (float)fmin(fmax(settings_get_number(block, "bartagdotround", bar_tag_dot_round), 0), 1);
    bar_urgent_pulse = settings_get_uint(block, "barurgentpulse", bar_urgent_pulse);
    bar_layout_symbol = settings_get_bool(block, "barltsymbol", bar_layout_symbol);
    bar_box_radius = settings_get_uint(block, "barboxradius", bar_box_radius);
    bar_padding = settings_get_uint(block, "barpadding", bar_padding);
    bar_height = (float)settings_get_number(block, "barheight", bar_height);
    bar_single_monitor = settings_get_bool(block, "barsinglemon", bar_single_monitor);
    bar_status = settings_get_enum(block, "barstatus", settings_bar_statuses, bar_status,
                                   "bar status setting");
    systray_enabled = settings_get_bool(block, "showsystray", systray_enabled);
    systray_spacing = settings_get_uint(block, "systrayspacing", systray_spacing);
    systray_padding = settings_get_uint(block, "systraypadding", systray_padding);
    systray_icon_size = settings_get_uint(block, "systrayiconsize", systray_icon_size);
    notifications_enabled = settings_get_bool(block, "shownotifications", notifications_enabled);
    private_notifications = settings_get_bool(block, "privatenotify", private_notifications);
    notification_timeout = settings_get_uint(block, "notification_timeout", notification_timeout);
    notification_line_width =
        settings_get_uint(block, "notification_linewidth", notification_line_width);
    notification_line_radius =
        settings_get_uint(block, "notification_lineradius", notification_line_radius);
    notification_action_sign =
        settings_get_string(block, "notification_actionsign", notification_action_sign);
    notification_private_text =
        settings_get_string(block, "notification_privatetext", notification_private_text);
    notification_private_hide =
        settings_get_bool(block, "notification_privatehide", notification_private_hide);
    notification_dot = settings_get_enum(block, "notification_dot", settings_notification_dots,
                                         notification_dot, "notification dot setting");
    notification_private_color =
        settings_get_hex(block, "notification_privatecolor", notification_private_color);
    notification_public_color =
        settings_get_hex(block, "notification_publiccolor", notification_public_color);
    runner_placeholder = settings_get_string(block, "runner_placeholder", runner_placeholder);
    runner_max_candidates =
        settings_get_uint(block, "runner_max_candidates", runner_max_candidates);
    runner_pill_height = settings_get_uint(block, "runner_pillheight", runner_pill_height);
    runner_pill_padding = settings_get_uint(block, "runner_pillpadding", runner_pill_padding);
    runner_pill_round =
        (float)fmin(settings_get_number(block, "runner_pillround", runner_pill_round), 1);
    runner_fuzzy = settings_get_bool(block, "runner_fuzzy", runner_fuzzy);
    runner_icons = settings_get_enum(block, "runner_icons", settings_runner_icons, runner_icons,
                                     "runner icon setting");
    runner_icon_size = settings_get_uint(block, "runner_iconsize", runner_icon_size);
    runner_icon_round =
        (float)fmin(fmax(settings_get_number(block, "runner_iconround", runner_icon_round), 0), 1);
    runner_icon_border = settings_get_uint(block, "runner_iconborder", runner_icon_border);
    runner_history_size = settings_get_uint(block, "runner_history_size", runner_history_size);
    runner_shell = settings_get_string(block, "runner_shell", runner_shell);
    runner_terminal = settings_get_string(block, "runner_terminal", runner_terminal);
    runner_terminal_prefix =
        settings_get_string(block, "runner_termprefix", runner_terminal_prefix);
    runner_run_prefix = settings_get_string(block, "runner_runprefix", runner_run_prefix);
    runner_calc_prefix = settings_get_string(block, "runner_calcprefix", runner_calc_prefix);
    runner_search_prefix = settings_get_string(block, "runner_searchprefix", runner_search_prefix);
    runner_search_url = settings_get_string(block, "runner_searchurl", runner_search_url);
    runner_window_prefix = settings_get_string(block, "runner_windowprefix", runner_window_prefix);
    runner_tag_prefix = settings_get_string(block, "runner_tagprefix", runner_tag_prefix);
    runner_files = settings_get_bool(block, "runner_files", runner_files);
    runner_paths = settings_get_string(block, "runner_paths", runner_paths);
    runner_calc_precision =
        settings_get_uint(block, "runner_calc_precision", runner_calc_precision);
}

static void settings_apply_opacity(const struct scfg_block* block)
{
    if (!block)
        return;
    opacity_enabled = settings_get_bool(block, "opacity_enabled", opacity_enabled);
    opacity_focus = (float)settings_get_number(block, "opacity_focus", opacity_focus);
    opacity_unfocus = (float)settings_get_number(block, "opacity_unfocus", opacity_unfocus);
    opacity_decorations = (float)settings_get_number(block, "opacity_deco", opacity_decorations);
#ifdef INTEGRATED_BACKGROUND
    opacity_type = settings_get_enum(block, "opacity_type", settings_opacity_kinds, opacity_type,
                                     "kind of opacity");
    blur_radius = settings_get_uint(block, "blur_radius", blur_radius);
    blur_passes = settings_get_uint(block, "blur_passes", blur_passes);
    blur_saturation = (float)settings_get_number(block, "blur_saturation", blur_saturation);
    blur_brightness = (float)settings_get_number(block, "blur_brightness", blur_brightness);
#endif
}

static void settings_apply_input(const struct scfg_block* block)
{
    if (!block)
        return;
    sloppy_focus = settings_get_bool(block, "sloppyfocus", sloppy_focus);
    const struct scfg_block* xkb_section = settings_find_section(block, "xkb_rules");
    if (xkb_section) {
        xkb_rules.rules = settings_get_string(xkb_section, "rules", xkb_rules.rules);
        xkb_rules.model = settings_get_string(xkb_section, "model", xkb_rules.model);
        xkb_rules.layout = settings_get_string(xkb_section, "layout", xkb_rules.layout);
        xkb_rules.variant = settings_get_string(xkb_section, "variant", xkb_rules.variant);
        xkb_rules.options = settings_get_string(xkb_section, "options", xkb_rules.options);
    }
    repeat_rate = (int)settings_get_number(block, "repeat_rate", repeat_rate);
    repeat_delay = (int)settings_get_number(block, "repeat_delay", repeat_delay);
    cursor_theme = settings_get_string(block, "cursor_theme", cursor_theme);
    cursor_size = (int)settings_get_number(block, "cursor_size", cursor_size);
    hide_cursor_when_typing =
        settings_get_bool(block, "hide_cursor_when_typing", hide_cursor_when_typing);
    tap_to_click = settings_get_bool(block, "tap_to_click", tap_to_click);
    tap_and_drag = settings_get_bool(block, "tap_and_drag", tap_and_drag);
    drag_lock = settings_get_bool(block, "drag_lock", drag_lock);
    natural_scrolling = settings_get_bool(block, "natural_scrolling", natural_scrolling);
    disable_while_typing = settings_get_bool(block, "disable_while_typing", disable_while_typing);
    left_handed = settings_get_bool(block, "left_handed", left_handed);
    middle_button_emulation =
        settings_get_bool(block, "middle_button_emulation", middle_button_emulation);
    scroll_method = (enum libinput_config_scroll_method)settings_get_enum(
        block, "scroll_method", settings_scroll_methods, scroll_method, "scroll method");
    click_method = (enum libinput_config_click_method)settings_get_enum(
        block, "click_method", settings_click_methods, click_method, "click method");
    send_events_mode = (uint32_t)settings_get_enum(block, "send_events_mode", settings_send_events,
                                                   (int)send_events_mode, "send events mode");
    accel_profile = (enum libinput_config_accel_profile)settings_get_enum(
        block, "accel_profile", settings_accel_profiles, accel_profile, "acceleration profile");
    accel_speed = settings_get_number(block, "accel_speed", accel_speed);
    touchpad_natural_scrolling =
        settings_get_bool(block, "touchpad_natural_scrolling", touchpad_natural_scrolling);
    touchpad_accel_profile = (enum libinput_config_accel_profile)settings_get_enum(
        block, "touchpad_accel_profile", settings_accel_profiles, touchpad_accel_profile,
        "acceleration profile");
    touchpad_accel_speed = settings_get_number(block, "touchpad_accel_speed", touchpad_accel_speed);
    pinch_zoom_modifier = settings_get_mods(block, "pinchzoom_mod", pinch_zoom_modifier);
    tap_button_map = (enum libinput_config_tap_button_map)settings_get_enum(
        block, "button_map", settings_button_maps, tap_button_map, "button map");
    tablet_output = settings_get_string(block, "tablet_output", tablet_output);
    tablet_left_handed = settings_get_bool(block, "tablet_left_handed", tablet_left_handed);
    touch_output = settings_get_string(block, "touch_output", touch_output);
}

static void settings_apply_misc(const struct scfg_block* block)
{
    if (!block)
        return;
    log_level = settings_get_enum(block, "log_level", settings_log_levels, log_level, "log level");
    bypass_surface_visibility =
        settings_get_bool(block, "bypass_surface_visibility", bypass_surface_visibility);
    allow_tearing = settings_get_bool(block, "allow_tearing", allow_tearing);
    auto_vrr = settings_get_bool(block, "auto_vrr", auto_vrr);
}

/* none keeps the defaults: a monitor needs a layout */
static void settings_apply_layouts(const struct scfg_block* block)
{
    if (!settings_count_directives(block, "layout"))
        return;
    Layout* parsed_layouts =
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
            parsed_layouts[count].arrange = settings_arranges[arrange_index].arrange;
        } else {
            parsed_layouts[count].arrange = layout_tile;
            settings_warn("%s is not a layout, using tile", arrange_name);
        }
        count++;
    }
    layouts = parsed_layouts;
    layout_count = count;
}

static void settings_apply_rules(const struct scfg_block* block)
{
    Rule* parsed_rules =
        util_calloc(settings_count_directives(block, "rule") + 1, sizeof *parsed_rules);
    size_t count = 0;
    for (size_t i = 0; i < block->directives_len; i++) {
        if (strcmp(block->directives[i].name, "rule"))
            continue;
        const struct scfg_block* rule_block = &block->directives[i].children;
        Rule* rule = &parsed_rules[count];
        rule->id = settings_get_string(rule_block, "id", nullptr);
        rule->title = settings_get_string(rule_block, "title", nullptr);
        rule->tags = settings_get_uint(rule_block, "tags", 0);
        rule->isfloating = settings_get_bool(rule_block, "isfloating", 0);
        rule->isprivate = settings_get_bool(rule_block, "private", -1);
        rule->private_text = settings_get_string(rule_block, "privatetext", nullptr);
        rule->private_background = settings_get_hex(rule_block, "privatecolor", 0);
        rule->private_foreground = settings_get_hex(rule_block, "privatetextcolor", 0);
        rule->opacity_focus = (float)settings_get_number(rule_block, "opacity_focus", 0);
        rule->opacity_unfocus = (float)settings_get_number(rule_block, "opacity_unfocus", 0);
        rule->monitor = (int)settings_get_number(rule_block, "monitor", -1);
        count++;
    }
    rules = parsed_rules;
    rule_count = count;
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
        rule->mfact = (float)settings_get_number(monitor_block, "mfact", 0.55);
        rule->nmaster = (int)settings_get_number(monitor_block, "nmaster", 1);
        rule->scale = (float)settings_get_number(monitor_block, "scale", 1);
        rule->lt = settings_layout_by_symbol(settings_get_string(monitor_block, "layout", nullptr));
        if (!rule->lt)
            rule->lt = layouts;
        rule->rr = (enum wl_output_transform)settings_get_enum(
            monitor_block, "transform", settings_transforms, WL_OUTPUT_TRANSFORM_NORMAL,
            "transform");
        rule->x = (int)settings_get_number(monitor_block, "x", -1);
        rule->y = (int)settings_get_number(monitor_block, "y", -1);
        rule->width = (int)settings_get_number(monitor_block, "width", 0);
        rule->height = (int)settings_get_number(monitor_block, "height", 0);
        rule->refresh = (int)settings_get_number(monitor_block, "refresh", 0);
        count++;
    }
    monitor_rules = parsed_rules;
    monitor_rule_count = count;
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
    autostart = flat_commands;
}

static void settings_apply_keys(const struct scfg_block* block)
{
    Key* parsed_keys =
        util_calloc(settings_count_directives(block, "key") + 1, sizeof *parsed_keys);
    size_t count = 0;
    for (size_t i = 0; i < block->directives_len; i++) {
        const struct scfg_directive* directive = &block->directives[i];
        settings_line = directive->lineno;
        const Action* action;
        if (strcmp(directive->name, "key") ||
            !(action = settings_action_by_name(settings_param_at(directive, 1))))
            continue;
        const char* key_name =
            settings_split_mods(settings_param_at(directive, 0), &parsed_keys[count].mod);
        const xkb_keysym_t keysym = xkb_keysym_from_name(key_name, XKB_KEYSYM_NO_FLAGS);
        if (keysym == XKB_KEY_NoSymbol) {
            settings_warn("%s is not a key", key_name);
            continue;
        }
        parsed_keys[count].keysym = keysym;
        parsed_keys[count].func = action->func;
        parsed_keys[count].arg = settings_get_arg(action->arg, directive, 2);
        count++;
    }
    keys = parsed_keys;
    key_count = count;
}

static void settings_apply_buttons(const struct scfg_block* block)
{
    Button* parsed_buttons =
        util_calloc(settings_count_directives(block, "button") + 1, sizeof *parsed_buttons);
    size_t count = 0;
    for (size_t i = 0; i < block->directives_len; i++) {
        const struct scfg_directive* directive = &block->directives[i];
        settings_line = directive->lineno;
        const Action* action;
        if (strcmp(directive->name, "button") ||
            !(action = settings_action_by_name(settings_param_at(directive, 2))))
            continue;
        Button* button = &parsed_buttons[count];
        button->click = (unsigned int)settings_enum_value(
            settings_click_areas, settings_param_at(directive, 0), ClkClient, "click area");
        button->button = (unsigned int)settings_enum_value(
            settings_mouse_buttons,
            settings_split_mods(settings_param_at(directive, 1), &button->mod), BTN_LEFT, "button");
        button->func = action->func;
        button->arg = settings_get_arg(action->arg, directive, 3);
        count++;
    }
    buttons = parsed_buttons;
    button_count = count;
}

static void settings_apply_gestures(const struct scfg_block* block)
{
    Gesture* parsed_gestures =
        util_calloc(settings_count_directives(block, "gesture") + 1, sizeof *parsed_gestures);
    size_t count = 0;
    for (size_t i = 0; i < block->directives_len; i++) {
        const struct scfg_directive* directive = &block->directives[i];
        settings_line = directive->lineno;
        const Action* action;
        if (strcmp(directive->name, "gesture") ||
            !(action = settings_action_by_name(settings_param_at(directive, 2))))
            continue;
        Gesture* gesture = &parsed_gestures[count];
        gesture->motion = (unsigned int)settings_enum_value(
            settings_gesture_motions, settings_param_at(directive, 0), SwipeLeft, "gesture motion");
        gesture->fingers = settings_uint_value(settings_param_at(directive, 1), 3);
        gesture->func = action->func;
        gesture->arg = settings_get_arg(action->arg, directive, 3);
        count++;
    }
    gestures = parsed_gestures;
    gesture_count = count;
}

static void settings_apply_pad_buttons(const struct scfg_block* block)
{
    PadButton* parsed_pad_buttons =
        util_calloc(settings_count_directives(block, "padbutton") + 1, sizeof *parsed_pad_buttons);
    size_t count = 0;
    for (size_t i = 0; i < block->directives_len; i++) {
        const struct scfg_directive* directive = &block->directives[i];
        settings_line = directive->lineno;
        const Action* action;
        if (strcmp(directive->name, "padbutton") ||
            !(action = settings_action_by_name(settings_param_at(directive, 1))))
            continue;
        PadButton* pad_button = &parsed_pad_buttons[count];
        pad_button->button = settings_uint_value(settings_param_at(directive, 0), 0);
        pad_button->func = action->func;
        pad_button->arg = settings_get_arg(action->arg, directive, 2);
        count++;
    }
    pad_buttons = parsed_pad_buttons;
    pad_button_count = count;
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
