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
static const char* const settings_records[] = { "layout",  "rule",
                                                "monitor", "autostart",
                                                "key",     "button",
                                                "gesture", "padbutton" };

/* function implementations */
[[gnu::format(printf, 1, 2)]] static void settings_warn(const char* fmt, ...)
{
    va_list ap;

    fprintf(stderr, "g0wm: config:%d: ", settings_line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static int settings_enum_value(const Enum* e,
                               const char* name,
                               int fallback,
                               const char* what)
{
    for (const Enum* p = e; p->name; p++)
        if (!strcmp(p->name, name))
            return p->val;
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

static const struct scfg_directive* settings_find(const struct scfg_block* b,
                                                  const char* k)
{
    if (!b)
        return nullptr;
    for (size_t i = 0; i < b->directives_len; i++) {
        if (!strcmp(b->directives[i].name, k)) {
            settings_line = b->directives[i].lineno;
            return &b->directives[i];
        }
    }
    return nullptr;
}

static const struct scfg_block* settings_section(const struct scfg_block* b,
                                                 const char* k)
{
    const struct scfg_directive* d = settings_find(b, k);

    return d ? &d->children : nullptr;
}

static size_t settings_count(const struct scfg_block* b, const char* k)
{
    size_t n = 0;

    for (size_t i = 0; i < b->directives_len; i++)
        n += !strcmp(b->directives[i].name, k);
    return n;
}

static const char* settings_param(const struct scfg_directive* d, size_t i)
{
    return d && i < d->params_len ? d->params[i] : nullptr;
}

static int settings_is_bool(const char* s)
{
    return s && (!strcmp(s, "true") || !strcmp(s, "false"));
}

static int settings_is_number(const char* s, double* v)
{
    char* end;
    double x;

    if (!s || !*s)
        return 0;
    x = strtod(s, &end);
    if (*end)
        return 0;
    if (v)
        *v = x;
    return 1;
}

static double settings_number_value(const char* s, double cur)
{
    double v;

    return settings_is_number(s, &v) ? v : cur;
}

/* a negative one would not convert */
static unsigned int settings_uint_value(const char* s, unsigned int cur)
{
    double v;

    if (!settings_is_number(s, &v))
        return cur;
    if (v < 0 || v > UINT_MAX) {
        settings_warn("%s is out of range", s);
        return cur;
    }
    return (unsigned int)v;
}

static uint32_t settings_hex_value(const char* s, uint32_t cur)
{
    unsigned long v;
    char* end;

    if (!s)
        return cur;
    if (*s == '#' && strlen(s) == 9) {
        v = strtoul(s + 1, &end, 16);
        if (!*end)
            return (uint32_t)v;
    }
    settings_warn("%s is not an #rrggbbaa color", s);
    return cur;
}

/* the getters keep the value config.h gave when the file has nothing usable */
static int settings_get_bool(const struct scfg_block* b, const char* k, int cur)
{
    const char* s = settings_param(settings_find(b, k), 0);

    return settings_is_bool(s) ? *s == 't' : cur;
}

static double settings_get_number(const struct scfg_block* b,
                                  const char* k,
                                  double cur)
{
    return settings_number_value(settings_param(settings_find(b, k), 0), cur);
}

static unsigned int settings_get_uint(const struct scfg_block* b,
                                      const char* k,
                                      unsigned int cur)
{
    return settings_uint_value(settings_param(settings_find(b, k), 0), cur);
}

static const char* settings_get_string(const struct scfg_block* b,
                                       const char* k,
                                       const char* cur)
{
    const struct scfg_directive* d = settings_find(b, k);

    return d ? settings_param(d, 0) : cur;
}

static int settings_get_enum(const struct scfg_block* b,
                             const char* k,
                             const Enum* e,
                             int cur,
                             const char* what)
{
    const char* name = settings_get_string(b, k, nullptr);

    return name ? settings_enum_value(e, name, cur, what) : cur;
}

static uint32_t settings_get_hex(const struct scfg_block* b,
                                 const char* k,
                                 uint32_t cur)
{
    return settings_hex_value(settings_get_string(b, k, nullptr), cur);
}

static void settings_get_rgba(float c[static 4],
                              const struct scfg_block* b,
                              const char* k)
{
    uint32_t v = settings_get_hex(b, k, settings_pack_rgba(c));

    c[0] = ((v >> 24) & 0xFF) / 255.0f;
    c[1] = ((v >> 16) & 0xFF) / 255.0f;
    c[2] = ((v >> 8) & 0xFF) / 255.0f;
    c[3] = (v & 0xFF) / 255.0f;
}

/* the parsed file outlives the settings, so its strings are used in place.
 * One slot to spare: the result is NULL terminated as well as counted. */
static const char** settings_string_list(const struct scfg_directive* d,
                                         size_t from,
                                         size_t* n)
{
    const char** v;

    if (!d || d->params_len < from)
        return nullptr;
    v = util_calloc(d->params_len - from + 1, sizeof *v);
    for (size_t i = from; i < d->params_len; i++)
        v[i - from] = d->params[i];
    if (n)
        *n = d->params_len - from;
    return v;
}

/* logo+shift+q: the mods into *mod, returns q */
static const char* settings_split_mods(const char* s, uint32_t* mod)
{
    const char* plus;
    char name[16];

    *mod = 0;
    while ((plus = strchr(s, '+'))) {
        snprintf(name, sizeof name, "%.*s", (int)(plus - s), s);
        *mod |= (uint32_t)settings_enum_value(
            settings_modifier_names, name, 0, "modifier");
        s = plus + 1;
    }
    return s;
}

static uint32_t settings_get_mods(const struct scfg_block* b,
                                  const char* k,
                                  uint32_t cur)
{
    const char* s = settings_get_string(b, k, nullptr);
    uint32_t mod;

    if (!s)
        return cur;
    if (!strcmp(s, "none"))
        return 0;
    s = settings_split_mods(s, &mod);
    return mod | (uint32_t)settings_enum_value(
                     settings_modifier_names, s, 0, "modifier");
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

static Arg settings_get_arg(char kind, const struct scfg_directive* d, size_t i)
{
    const char* s = settings_param(d, i);
    Arg a = {};

    switch (kind) {
        case 'i':
            a.i = (int)settings_number_value(s, 0);
            break;
        case 'u':
            a.ui = settings_uint_value(s, 0);
            break;
        case 'f':
            a.f = (float)settings_number_value(s, 0);
            break;
        case 'v':
            a.v = settings_string_list(d, i, nullptr);
            break;
        case 'l':
            a.v = settings_layout_by_symbol(s);
            break;
        case 'c':
            a.ui = s ? (uint32_t)settings_enum_value(
                           settings_drag_modes, s, CurMove, "drag mode")
                     : CurMove;
            break;
    }
    return a;
}

static void settings_apply_colors(const struct scfg_block* b)
{
    const struct scfg_directive* d;

    for (size_t i = 0; i < NumSchemes; i++) {
        if (!settings_scheme_names[i] ||
            !(d = settings_find(b, settings_scheme_names[i])))
            continue;
        for (size_t j = 0; j < 3; j++)
            colors[i][j] =
                settings_hex_value(settings_param(d, j), colors[i][j]);
    }
}

static void settings_apply_look(const struct scfg_block* b)
{
    const char** list;

    if (!b)
        return;
    if ((list =
             settings_string_list(settings_find(b, "fonts"), 0, &font_count)))
        fonts = list;
    settings_apply_colors(settings_section(b, "colors"));
    settings_get_rgba(root_color, b, "rootcolor");
    settings_get_rgba(fullscreen_color, b, "fullscreen_bg");
#ifdef INTEGRATED_BACKGROUND
    wallpaper = settings_get_string(b, "wallpaper", wallpaper);
#endif
    if ((list =
             settings_string_list(settings_find(b, "tags"), 0, &tag_count))) {
        tags = (char**)list;
        if (tag_count > 31) { /* TAGMASK has to fit in a uint32_t */
            settings_warn("only the first 31 tags are used");
            tag_count = 31;
        }
    }
}

static void settings_apply_windows(const struct scfg_block* b)
{
    if (!b)
        return;
    border_width = settings_get_uint(b, "borderpx", border_width);
    corner_style = settings_get_enum(
        b, "cornerstyle", settings_corner_styles, corner_style, "corner style");
    corner_radius = settings_get_uint(b, "cornerpx", corner_radius);
    gaps = (int)settings_get_number(b, "gaps", gaps);
    gap_size = settings_get_uint(b, "gappx", gap_size);
    smart_gaps = settings_get_bool(b, "smartgaps", smart_gaps);
    title_bar = settings_get_bool(b, "titlebar", title_bar);
    title_padding = settings_get_uint(b, "titlepadding", title_padding);
    title_center = settings_get_bool(b, "titlecenter", title_center);
    title_close = settings_get_bool(b, "titleclose", title_close);
    title_close_color =
        settings_get_hex(b, "titleclosecolor", title_close_color);
    private_color = settings_get_hex(b, "privatecolor", private_color);
    private_round =
        (float)fmin(settings_get_number(b, "privateround", private_round), 1);
    always_private = settings_get_bool(b, "alwaysprivate", always_private);
    public_color = settings_get_hex(b, "publiccolor", public_color);
    private_title = settings_get_string(b, "privatetitle", private_title);
    move_threshold =
        (int)settings_get_number(b, "movethreshold", move_threshold);
    move_snap = (int)settings_get_number(b, "movesnap", move_snap);
}

static void settings_apply_bar(const struct scfg_block* b)
{
    if (!b)
        return;
    bar_enabled = settings_get_bool(b, "showbar", bar_enabled);
    bar_top = settings_get_bool(b, "topbar", bar_top);
    bar_window_title = settings_get_bool(b, "barwintitle", bar_window_title);
    bar_tag_dots = settings_get_bool(b, "bartagdots", bar_tag_dots);
    bar_tag_dot_size = settings_get_uint(b, "bartagdotsize", bar_tag_dot_size);
    bar_tag_dot_gap = settings_get_uint(b, "bartagdotgap", bar_tag_dot_gap);
    bar_tag_dot_width = (float)fmax(
        settings_get_number(b, "bartagdotwidth", bar_tag_dot_width), 1);
    bar_tag_dot_round = (float)fmin(
        fmax(settings_get_number(b, "bartagdotround", bar_tag_dot_round), 0),
        1);
    bar_urgent_pulse = settings_get_uint(b, "barurgentpulse", bar_urgent_pulse);
    bar_layout_symbol = settings_get_bool(b, "barltsymbol", bar_layout_symbol);
    bar_box_radius = settings_get_uint(b, "barboxradius", bar_box_radius);
    bar_padding = settings_get_uint(b, "barpadding", bar_padding);
    bar_height = (float)settings_get_number(b, "barheight", bar_height);
    bar_single_monitor =
        settings_get_bool(b, "barsinglemon", bar_single_monitor);
    bar_status = settings_get_enum(b,
                                   "barstatus",
                                   settings_bar_statuses,
                                   bar_status,
                                   "bar status setting");
    systray_enabled = settings_get_bool(b, "showsystray", systray_enabled);
    systray_spacing = settings_get_uint(b, "systrayspacing", systray_spacing);
    systray_padding = settings_get_uint(b, "systraypadding", systray_padding);
    systray_icon_size =
        settings_get_uint(b, "systrayiconsize", systray_icon_size);
    notifications_enabled =
        settings_get_bool(b, "shownotifications", notifications_enabled);
    notification_timeout =
        settings_get_uint(b, "notification_timeout", notification_timeout);
    notification_line_width =
        settings_get_uint(b, "notification_linewidth", notification_line_width);
    notification_line_radius = settings_get_uint(
        b, "notification_lineradius", notification_line_radius);
    notification_action_sign = settings_get_string(
        b, "notification_actionsign", notification_action_sign);
    runner_placeholder =
        settings_get_string(b, "runner_placeholder", runner_placeholder);
    runner_max_candidates =
        settings_get_uint(b, "runner_max_candidates", runner_max_candidates);
    runner_pill_height =
        settings_get_uint(b, "runner_pillheight", runner_pill_height);
    runner_pill_padding =
        settings_get_uint(b, "runner_pillpadding", runner_pill_padding);
    runner_pill_round = (float)fmin(
        settings_get_number(b, "runner_pillround", runner_pill_round), 1);
    runner_fuzzy = settings_get_bool(b, "runner_fuzzy", runner_fuzzy);
    runner_icons = settings_get_enum(b,
                                     "runner_icons",
                                     settings_runner_icons,
                                     runner_icons,
                                     "runner icon setting");
    runner_icon_size =
        settings_get_uint(b, "runner_iconsize", runner_icon_size);
    runner_icon_round = (float)fmin(
        fmax(settings_get_number(b, "runner_iconround", runner_icon_round), 0),
        1);
    runner_icon_border =
        settings_get_uint(b, "runner_iconborder", runner_icon_border);
    runner_history_size =
        settings_get_uint(b, "runner_history_size", runner_history_size);
    runner_shell = settings_get_string(b, "runner_shell", runner_shell);
    runner_terminal =
        settings_get_string(b, "runner_terminal", runner_terminal);
    runner_terminal_prefix =
        settings_get_string(b, "runner_termprefix", runner_terminal_prefix);
    runner_run_prefix =
        settings_get_string(b, "runner_runprefix", runner_run_prefix);
    runner_calc_prefix =
        settings_get_string(b, "runner_calcprefix", runner_calc_prefix);
    runner_search_prefix =
        settings_get_string(b, "runner_searchprefix", runner_search_prefix);
    runner_search_url =
        settings_get_string(b, "runner_searchurl", runner_search_url);
    runner_window_prefix =
        settings_get_string(b, "runner_windowprefix", runner_window_prefix);
    runner_tag_prefix =
        settings_get_string(b, "runner_tagprefix", runner_tag_prefix);
    runner_files = settings_get_bool(b, "runner_files", runner_files);
    runner_paths = settings_get_string(b, "runner_paths", runner_paths);
    runner_calc_precision =
        settings_get_uint(b, "runner_calc_precision", runner_calc_precision);
}

static void settings_apply_opacity(const struct scfg_block* b)
{
    if (!b)
        return;
    opacity_enabled = settings_get_bool(b, "opacity_enabled", opacity_enabled);
    opacity_focus =
        (float)settings_get_number(b, "opacity_focus", opacity_focus);
    opacity_unfocus =
        (float)settings_get_number(b, "opacity_unfocus", opacity_unfocus);
    opacity_decorations =
        (float)settings_get_number(b, "opacity_deco", opacity_decorations);
#ifdef INTEGRATED_BACKGROUND
    opacity_type = settings_get_enum(b,
                                     "opacity_type",
                                     settings_opacity_kinds,
                                     opacity_type,
                                     "kind of opacity");
    blur_radius = settings_get_uint(b, "blur_radius", blur_radius);
    blur_passes = settings_get_uint(b, "blur_passes", blur_passes);
    blur_saturation =
        (float)settings_get_number(b, "blur_saturation", blur_saturation);
    blur_brightness =
        (float)settings_get_number(b, "blur_brightness", blur_brightness);
#endif
}

static void settings_apply_input(const struct scfg_block* b)
{
    const struct scfg_block* x;

    if (!b)
        return;
    sloppy_focus = settings_get_bool(b, "sloppyfocus", sloppy_focus);
    if ((x = settings_section(b, "xkb_rules"))) {
        xkb_rules.rules = settings_get_string(x, "rules", xkb_rules.rules);
        xkb_rules.model = settings_get_string(x, "model", xkb_rules.model);
        xkb_rules.layout = settings_get_string(x, "layout", xkb_rules.layout);
        xkb_rules.variant =
            settings_get_string(x, "variant", xkb_rules.variant);
        xkb_rules.options =
            settings_get_string(x, "options", xkb_rules.options);
    }
    repeat_rate = (int)settings_get_number(b, "repeat_rate", repeat_rate);
    repeat_delay = (int)settings_get_number(b, "repeat_delay", repeat_delay);
    cursor_theme = settings_get_string(b, "cursor_theme", cursor_theme);
    cursor_size = (int)settings_get_number(b, "cursor_size", cursor_size);
    hide_cursor_when_typing = settings_get_bool(
        b, "hide_cursor_when_typing", hide_cursor_when_typing);
    tap_to_click = settings_get_bool(b, "tap_to_click", tap_to_click);
    tap_and_drag = settings_get_bool(b, "tap_and_drag", tap_and_drag);
    drag_lock = settings_get_bool(b, "drag_lock", drag_lock);
    natural_scrolling =
        settings_get_bool(b, "natural_scrolling", natural_scrolling);
    disable_while_typing =
        settings_get_bool(b, "disable_while_typing", disable_while_typing);
    left_handed = settings_get_bool(b, "left_handed", left_handed);
    middle_button_emulation = settings_get_bool(
        b, "middle_button_emulation", middle_button_emulation);
    scroll_method = (enum libinput_config_scroll_method)settings_get_enum(
        b,
        "scroll_method",
        settings_scroll_methods,
        scroll_method,
        "scroll method");
    click_method = (enum libinput_config_click_method)settings_get_enum(
        b,
        "click_method",
        settings_click_methods,
        click_method,
        "click method");
    send_events_mode = (uint32_t)settings_get_enum(b,
                                                   "send_events_mode",
                                                   settings_send_events,
                                                   (int)send_events_mode,
                                                   "send events mode");
    accel_profile = (enum libinput_config_accel_profile)settings_get_enum(
        b,
        "accel_profile",
        settings_accel_profiles,
        accel_profile,
        "acceleration profile");
    accel_speed = settings_get_number(b, "accel_speed", accel_speed);
    touchpad_natural_scrolling = settings_get_bool(
        b, "touchpad_natural_scrolling", touchpad_natural_scrolling);
    touchpad_accel_profile =
        (enum libinput_config_accel_profile)settings_get_enum(
            b,
            "touchpad_accel_profile",
            settings_accel_profiles,
            touchpad_accel_profile,
            "acceleration profile");
    touchpad_accel_speed =
        settings_get_number(b, "touchpad_accel_speed", touchpad_accel_speed);
    pinch_zoom_modifier =
        settings_get_mods(b, "pinchzoom_mod", pinch_zoom_modifier);
    tap_button_map = (enum libinput_config_tap_button_map)settings_get_enum(
        b, "button_map", settings_button_maps, tap_button_map, "button map");
    tablet_output = settings_get_string(b, "tablet_output", tablet_output);
    tablet_left_handed =
        settings_get_bool(b, "tablet_left_handed", tablet_left_handed);
    touch_output = settings_get_string(b, "touch_output", touch_output);
}

static void settings_apply_misc(const struct scfg_block* b)
{
    if (!b)
        return;
    log_level = settings_get_enum(
        b, "log_level", settings_log_levels, log_level, "log level");
    bypass_surface_visibility = settings_get_bool(
        b, "bypass_surface_visibility", bypass_surface_visibility);
    allow_tearing = settings_get_bool(b, "allow_tearing", allow_tearing);
    auto_vrr = settings_get_bool(b, "auto_vrr", auto_vrr);
}

/* none keeps the defaults: a monitor needs a layout */
static void settings_apply_layouts(const struct scfg_block* b)
{
    const struct scfg_directive* d;
    Layout* out;
    size_t j, n = 0;

    if (!settings_count(b, "layout"))
        return;
    out = util_calloc(settings_count(b, "layout"), sizeof *out);
    for (size_t i = 0; i < b->directives_len; i++) {
        const char* name;

        d = &b->directives[i];
        settings_line = d->lineno;
        if (strcmp(d->name, "layout"))
            continue;
        out[n].symbol = settings_param(d, 0) ? settings_param(d, 0) : "[ ]";
        name = settings_param(d, 1) ? settings_param(d, 1) : "tile";
        for (j = 0; j < settings_arrange_count; j++)
            if (!strcmp(settings_arranges[j].name, name))
                break;
        if (j < settings_arrange_count) {
            out[n].arrange = settings_arranges[j].arrange;
        } else {
            out[n].arrange = layout_tile;
            settings_warn("%s is not a layout, using tile", name);
        }
        n++;
    }
    layouts = out;
    layout_count = n;
}

static void settings_apply_rules(const struct scfg_block* b)
{
    const struct scfg_block* r;
    Rule* out;
    size_t n = 0;

    out = util_calloc(settings_count(b, "rule") + 1, sizeof *out);
    for (size_t i = 0; i < b->directives_len; i++) {
        if (strcmp(b->directives[i].name, "rule"))
            continue;
        r = &b->directives[i].children;
        out[n].id = settings_get_string(r, "id", nullptr);
        out[n].title = settings_get_string(r, "title", nullptr);
        out[n].tags = settings_get_uint(r, "tags", 0);
        out[n].isfloating = settings_get_bool(r, "isfloating", 0);
        out[n].isprivate = settings_get_bool(r, "private", -1);
        out[n].opacity_focus =
            (float)settings_get_number(r, "opacity_focus", 0);
        out[n].opacity_unfocus =
            (float)settings_get_number(r, "opacity_unfocus", 0);
        out[n].monitor = (int)settings_get_number(r, "monitor", -1);
        n++;
    }
    rules = out;
    rule_count = n;
}

static void settings_apply_monitors(const struct scfg_block* b)
{
    const struct scfg_block* m;
    MonitorRule* out;
    size_t n = 0;

    if (!settings_count(b, "monitor"))
        return;
    out = util_calloc(settings_count(b, "monitor"), sizeof *out);
    for (size_t i = 0; i < b->directives_len; i++) {
        if (strcmp(b->directives[i].name, "monitor"))
            continue;
        m = &b->directives[i].children;
        out[n].name = settings_get_string(m, "name", nullptr);
        out[n].mfact = (float)settings_get_number(m, "mfact", 0.55);
        out[n].nmaster = (int)settings_get_number(m, "nmaster", 1);
        out[n].scale = (float)settings_get_number(m, "scale", 1);
        out[n].lt = settings_layout_by_symbol(
            settings_get_string(m, "layout", nullptr));
        if (!out[n].lt)
            out[n].lt = layouts;
        out[n].rr = (enum wl_output_transform)settings_get_enum(
            m,
            "transform",
            settings_transforms,
            WL_OUTPUT_TRANSFORM_NORMAL,
            "transform");
        out[n].x = (int)settings_get_number(m, "x", -1);
        out[n].y = (int)settings_get_number(m, "y", -1);
        out[n].width = (int)settings_get_number(m, "width", 0);
        out[n].height = (int)settings_get_number(m, "height", 0);
        out[n].refresh = (int)settings_get_number(m, "refresh", 0);
        n++;
    }
    monitor_rules = out;
    monitor_rule_count = n;
}

/* back to one flat run of arguments per command, each NULL terminated */
static void settings_apply_autostart(const struct scfg_block* b)
{
    const struct scfg_directive* d;
    const char** out;
    size_t n = 0;

    for (size_t i = 0; i < b->directives_len; i++)
        if (!strcmp(b->directives[i].name, "autostart"))
            n += b->directives[i].params_len + 1;
    out = util_calloc(n + 1, sizeof *out);

    n = 0;
    for (size_t i = 0; i < b->directives_len; i++) {
        d = &b->directives[i];
        settings_line = d->lineno;
        if (strcmp(d->name, "autostart") || !d->params_len)
            continue;
        for (size_t j = 0; j < d->params_len; j++)
            out[n++] = d->params[j];
        out[n++] = nullptr;
    }
    autostart = out;
}

static void settings_apply_keys(const struct scfg_block* b)
{
    const struct scfg_directive* d;
    const Action* act;
    Key* out;
    size_t n = 0;

    out = util_calloc(settings_count(b, "key") + 1, sizeof *out);
    for (size_t i = 0; i < b->directives_len; i++) {
        const char* name;
        xkb_keysym_t sym;

        d = &b->directives[i];
        settings_line = d->lineno;
        if (strcmp(d->name, "key") ||
            !(act = settings_action_by_name(settings_param(d, 1))))
            continue;
        name = settings_split_mods(settings_param(d, 0), &out[n].mod);
        if ((sym = xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS)) ==
            XKB_KEY_NoSymbol) {
            settings_warn("%s is not a key", name);
            continue;
        }
        out[n].keysym = sym;
        out[n].func = act->func;
        out[n].arg = settings_get_arg(act->arg, d, 2);
        n++;
    }
    keys = out;
    key_count = n;
}

static void settings_apply_buttons(const struct scfg_block* b)
{
    const struct scfg_directive* d;
    const Action* act;
    Button* out;
    size_t n = 0;

    out = util_calloc(settings_count(b, "button") + 1, sizeof *out);
    for (size_t i = 0; i < b->directives_len; i++) {
        d = &b->directives[i];
        settings_line = d->lineno;
        if (strcmp(d->name, "button") ||
            !(act = settings_action_by_name(settings_param(d, 2))))
            continue;
        out[n].click = (unsigned int)settings_enum_value(settings_click_areas,
                                                         settings_param(d, 0),
                                                         ClkClient,
                                                         "click area");
        out[n].button = (unsigned int)settings_enum_value(
            settings_mouse_buttons,
            settings_split_mods(settings_param(d, 1), &out[n].mod),
            BTN_LEFT,
            "button");
        out[n].func = act->func;
        out[n].arg = settings_get_arg(act->arg, d, 3);
        n++;
    }
    buttons = out;
    button_count = n;
}

static void settings_apply_gestures(const struct scfg_block* b)
{
    const struct scfg_directive* d;
    const Action* act;
    Gesture* out;
    size_t n = 0;

    out = util_calloc(settings_count(b, "gesture") + 1, sizeof *out);
    for (size_t i = 0; i < b->directives_len; i++) {
        d = &b->directives[i];
        settings_line = d->lineno;
        if (strcmp(d->name, "gesture") ||
            !(act = settings_action_by_name(settings_param(d, 2))))
            continue;
        out[n].motion =
            (unsigned int)settings_enum_value(settings_gesture_motions,
                                              settings_param(d, 0),
                                              SwipeLeft,
                                              "gesture motion");
        out[n].fingers = settings_uint_value(settings_param(d, 1), 3);
        out[n].func = act->func;
        out[n].arg = settings_get_arg(act->arg, d, 3);
        n++;
    }
    gestures = out;
    gesture_count = n;
}

static void settings_apply_pad_buttons(const struct scfg_block* b)
{
    const struct scfg_directive* d;
    const Action* act;
    PadButton* out;
    size_t n = 0;

    out = util_calloc(settings_count(b, "padbutton") + 1, sizeof *out);
    for (size_t i = 0; i < b->directives_len; i++) {
        d = &b->directives[i];
        settings_line = d->lineno;
        if (strcmp(d->name, "padbutton") ||
            !(act = settings_action_by_name(settings_param(d, 1))))
            continue;
        out[n].button = settings_uint_value(settings_param(d, 0), 0);
        out[n].func = act->func;
        out[n].arg = settings_get_arg(act->arg, d, 2);
        n++;
    }
    pad_buttons = out;
    pad_button_count = n;
}

void settings_load_builtin(void)
{
    static struct scfg_block b;
    char* text;
    FILE* f;

    if (settings_builtin || !(text = settings_rendered_defaults()))
        return;
    if ((f = fmemopen(text, strlen(text), "r"))) {
        if (!scfg_parse_file(&b, f))
            settings_builtin = &b;
        fclose(f);
    }
    free(text);
}

/* the file's lines of kind k, or the built-in ones when it has none */
static const struct scfg_block* settings_lines(const struct scfg_block* b,
                                               const char* k)
{
    return settings_count(b, k) || !settings_builtin ? b : settings_builtin;
}

static void settings_apply_sections(const struct scfg_block* b)
{
    settings_apply_look(settings_section(b, "look"));
    settings_apply_windows(settings_section(b, "windows"));
    settings_apply_bar(settings_section(b, "bar"));
    settings_apply_opacity(settings_section(b, "opacity"));
    settings_apply_input(settings_section(b, "input"));
    settings_apply_misc(settings_section(b, "misc"));
}

/* the layouts first: the rest names them by symbol */
void settings_apply(const struct scfg_block* b)
{
    settings_apply_layouts(settings_lines(b, "layout"));
    /* a reload has to undo a setting since left out */
    settings_apply_sections(settings_builtin);
    settings_apply_sections(b);
    settings_apply_rules(settings_lines(b, "rule"));
    settings_apply_monitors(settings_lines(b, "monitor"));
    settings_apply_autostart(settings_lines(b, "autostart"));
    settings_apply_keys(settings_lines(b, "key"));
    settings_apply_buttons(settings_lines(b, "button"));
    settings_apply_gestures(settings_lines(b, "gesture"));
    settings_apply_pad_buttons(settings_lines(b, "padbutton"));
}

static int settings_is_record(const char* k)
{
    for (size_t i = 0; i < LENGTH(settings_records); i++)
        if (!strcmp(settings_records[i], k))
            return 1;
    return 0;
}

static const char* settings_kind_of(const struct scfg_directive* d)
{
    if (d->params_len != 1)
        return d->params_len ? "a list" : "off";
    if (settings_is_bool(d->params[0]))
        return "true or false";
    if (settings_is_number(d->params[0], nullptr))
        return "a number";
    return "a string";
}

/* off takes anything, strings and lists any words */
static int settings_same_shape(const struct scfg_directive* want,
                               const struct scfg_directive* got)
{
    const char* k = settings_kind_of(want);

    if (!want->params_len)
        return 1;
    if (!strcmp(k, "a list") || !strcmp(k, "a string"))
        return got->params_len > 0;
    return !strcmp(k, settings_kind_of(got));
}

void settings_check(const struct scfg_block* want,
                    const struct scfg_block* got,
                    const char* path,
                    int* bad)
{
    const struct scfg_directive* w;
    const struct scfg_directive* g;
    char sub[256];

    for (size_t i = 0; i < want->directives_len; i++) {
        w = &want->directives[i];
        if (!*path && settings_is_record(w->name))
            continue;
        snprintf(sub, sizeof sub, "%s%s%s", path, *path ? "." : "", w->name);
        if (!(g = settings_find(got, w->name)))
            continue;
        if (w->children.directives_len) {
            settings_check(&w->children, &g->children, sub, bad);
        } else if (!settings_same_shape(w, g)) {
            settings_line = g->lineno;
            settings_warn("%s should be %s", sub, settings_kind_of(w));
            (*bad)++;
        }
    }
    /* a typo, a setting this build was configured without, or a second one */
    for (size_t i = 0; i < got->directives_len; i++) {
        g = &got->directives[i];
        if (!*path && settings_is_record(g->name)) {
            /* what a rule or a monitor holds */
            if ((w = settings_find(want, g->name)) &&
                w->children.directives_len)
                settings_check(&w->children, &g->children, g->name, bad);
            continue;
        }
        w = settings_find(got, g->name);
        if (w == g && settings_find(want, g->name))
            continue;
        snprintf(sub, sizeof sub, "%s%s%s", path, *path ? "." : "", g->name);
        settings_line = g->lineno;
        if (w != g)
            settings_warn("%s is already set on line %d", sub, w->lineno);
        else
            settings_warn("%s is not a setting this build has", sub);
        (*bad)++;
    }
}
