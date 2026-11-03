/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ~/.config/g0wm/config: written from config.h, read back over it
 */
#include <drwl.h>
#include <errno.h>
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
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "bar.h"
#include "client.h"
#include "corner.h"
#include "g0wm.h"
#include "input.h"
#include "layout.h"
#include "magnifier.h"
#include "monitor.h"
#include "notify.h"
#include "opacity.h"
#include "privacy.h"
#include "runner.h"
#include "settings.h"
#include "tablet.h"
#include "toplevel.h"
#include "util.h"

static void settings_reload(const Arg* arg);

/* the only file that includes it: these are the definitions */
#include "config.h"

typedef struct {
    int val;
    const char* name;
} Enum;

/* tables end on { 0, nullptr }; a real zero value is listed before it */
/* in combo order: logo+shift+q */
static const Enum settings_modifier_names[] = { { WLR_MODIFIER_LOGO, "logo" },
                                                { WLR_MODIFIER_CTRL, "ctrl" },
                                                { WLR_MODIFIER_ALT, "alt" },
                                                { WLR_MODIFIER_SHIFT, "shift" },
                                                { WLR_MODIFIER_CAPS, "caps" },
                                                { WLR_MODIFIER_MOD2, "mod2" },
                                                { WLR_MODIFIER_MOD3, "mod3" },
                                                { WLR_MODIFIER_MOD5, "mod5" },
                                                { 0, nullptr } };

static const Enum settings_drag_modes[] = { { CurMove, "move" },
                                            { CurResize, "resize" },
                                            { 0, nullptr } };

static const Enum settings_click_areas[] = {
    { ClkTagBar, "tagbar" }, { ClkLtSymbol, "ltsymbol" },
    { ClkStatus, "status" }, { ClkTitle, "title" },
    { ClkClient, "client" }, { ClkRoot, "root" },
    { ClkTray, "tray" },     { ClkWinTitle, "wintitle" },
    { 0, nullptr },
};

static const Enum settings_mouse_buttons[] = {
    { BTN_LEFT, "left" },        { BTN_MIDDLE, "middle" },
    { BTN_RIGHT, "right" },      { WheelUp, "wheel-up" },
    { WheelDown, "wheel-down" }, { 0, nullptr }
};

static const Enum settings_gesture_motions[] = { { SwipeLeft, "swipe-left" },
                                                 { SwipeRight, "swipe-right" },
                                                 { SwipeUp, "swipe-up" },
                                                 { SwipeDown, "swipe-down" },
                                                 { PinchIn, "pinch-in" },
                                                 { PinchOut, "pinch-out" },
                                                 { 0, nullptr } };

static const Enum settings_runner_icons[] = { { RunnerIconsNone, "none" },
                                              { RunnerIconsSelected,
                                                "selected" },
                                              { RunnerIconsAll, "all" },
                                              { 0, nullptr } };

static const Enum settings_bar_statuses[] = { { BarStatusFocus, "focus" },
                                              { BarStatusAll, "all" },
                                              { BarStatusMain, "main" },
                                              { 0, nullptr } };

static const Enum settings_corner_styles[] = { { CornerNormal, "normal" },
                                               { CornerSquircle, "squircle" },
                                               { 0, nullptr } };

#ifdef INTEGRATED_BACKGROUND
static const Enum settings_opacity_kinds[] = { { OpacityNormal, "normal" },
                                               { OpacityBlur, "blur" },
                                               { 0, nullptr } };
#endif /* INTEGRATED_BACKGROUND */

static const Enum settings_transforms[] = {
    { WL_OUTPUT_TRANSFORM_NORMAL, "normal" },
    { WL_OUTPUT_TRANSFORM_90, "90" },
    { WL_OUTPUT_TRANSFORM_180, "180" },
    { WL_OUTPUT_TRANSFORM_270, "270" },
    { WL_OUTPUT_TRANSFORM_FLIPPED, "flipped" },
    { WL_OUTPUT_TRANSFORM_FLIPPED_90, "flipped-90" },
    { WL_OUTPUT_TRANSFORM_FLIPPED_180, "flipped-180" },
    { WL_OUTPUT_TRANSFORM_FLIPPED_270, "flipped-270" },
    { 0, nullptr }
};

static const Enum settings_scroll_methods[] = {
    { LIBINPUT_CONFIG_SCROLL_NO_SCROLL, "none" },
    { LIBINPUT_CONFIG_SCROLL_2FG, "2fg" },
    { LIBINPUT_CONFIG_SCROLL_EDGE, "edge" },
    { LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN, "on-button-down" },
    { 0, nullptr }
};

static const Enum settings_click_methods[] = {
    { LIBINPUT_CONFIG_CLICK_METHOD_NONE, "none" },
    { LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS, "button-areas" },
    { LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER, "clickfinger" },
    { 0, nullptr }
};

static const Enum settings_send_events[] = {
    { LIBINPUT_CONFIG_SEND_EVENTS_ENABLED, "enabled" },
    { LIBINPUT_CONFIG_SEND_EVENTS_DISABLED, "disabled" },
    { LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE,
      "disabled-on-external-mouse" },
    { 0, nullptr }
};

static const Enum settings_accel_profiles[] = {
    { LIBINPUT_CONFIG_ACCEL_PROFILE_NONE, "none" },
    { LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT, "flat" },
    { LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE, "adaptive" },
    { 0, nullptr }
};

static const Enum settings_button_maps[] = {
    { LIBINPUT_CONFIG_TAP_MAP_LRM, "lrm" },
    { LIBINPUT_CONFIG_TAP_MAP_LMR, "lmr" },
    { 0, nullptr }
};

static const Enum settings_log_levels[] = { { WLR_SILENT, "silent" },
                                            { WLR_ERROR, "error" },
                                            { WLR_INFO, "info" },
                                            { WLR_DEBUG, "debug" },
                                            { 0, nullptr } };

static const struct {
    const char* name;
    void (*arrange)(Monitor*);
} settings_arranges[] = { { "tile", layout_tile },
                          { "tabbed", layout_tabbed } };

/* which member of the Arg a binding uses: 'i' int, 'u' unsigned, 'f' float,
 * 'v' an execvp() list, 'l' a layout symbol, 'c' a drag mode, 0 none */

typedef struct {
    void (*func)(const Arg*);
    const char* name;
    char arg;
} Action;

static const Action settings_actions[] = {
    { change_vt, "chvt", 'u' },
    { monitor_focus, "focusmon", 'i' },
    { layout_focus_stack, "focusstack", 'i' },
    { layout_inc_master, "incnmaster", 'i' },
    { client_kill, "killclient", 0 },
    { magnifier_magnify, "magnify", 'f' },
    { input_move_resize, "moveresize", 'c' },
    { layout_move_stack, "movestack", 'i' },
    { bar_notification_actions, "notifyactions", 0 },
    { bar_notification_dismiss, "notifydismiss", 0 },
    { bar_notification_dismiss_all, "notifydismissall", 0 },
    { bar_notification_next, "notifynext", 0 },
    { bar_notification_open, "notifyopen", 0 },
    { bar_notification_prev, "notifyprev", 0 },
    { bar_notification_scroll, "notifyscroll", 0 },
    { quit, "quit", 0 },
    { settings_reload, "reloadsettings", 0 },
    { layout_resize_height, "resizeheight", 'i' },
    { layout_resize_width, "resizewidth", 'i' },
    { runner_toggle, "runnertoggle", 0 },
    { layout_set, "setlayout", 'l' },
    { layout_set_master_factor, "setmfact", 'f' },
    { opacity_change_focused, "setopacityfocus", 'f' },
    { opacity_change_unfocused, "setopacityunfocus", 'f' },
    { layout_shift_tag, "shifttag", 'i' },
    { layout_shift_view, "shiftview", 'i' },
    { spawn, "spawn", 'v' },
    { layout_tag, "tag", 'u' },
    { monitor_send_client, "tagmon", 'i' },
    { bar_toggle, "togglebar", 0 },
    { client_toggle_floating, "togglefloating", 0 },
    { client_toggle_fullscreen, "togglefullscreen", 0 },
    { layout_toggle_gaps, "togglegaps", 0 },
    { input_toggle_inhibit, "toggleinhibit", 0 },
    { opacity_toggle, "toggleopacity", 0 },
    { privacy_toggle, "toggleprivate", 0 },
    { layout_toggle_tabbed, "toggletabbed", 'l' },
    { layout_toggle_tag, "toggletag", 'u' },
    { bar_toggle_title_bars, "toggletitlebar", 0 },
    { layout_toggle_view, "toggleview", 'u' },
    { bar_tray_activate, "trayactivate", 0 },
    { bar_tray_menu, "traymenu", 0 },
    { layout_view, "view", 'u' },
    { layout_zoom, "zoom", 0 },
};

static int settings_line; /* of the directive being read */

[[gnu::format(printf, 1, 2)]] static void settings_warn(const char* fmt, ...)
{
    va_list ap;

    fprintf(stderr, "g0wm: config:%d: ", settings_line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static const char* settings_enum_name(const Enum* e, int val)
{
    for (; e->name; e++)
        if (e->val == val)
            return e->name;
    return "unknown";
}

static int settings_enum_value(const Enum* e,
                               const char* name,
                               int fallback,
                               const char* what)
{
    const Enum* p;

    for (p = e; p->name; p++)
        if (!strcmp(p->name, name))
            return p->val;
    settings_warn("%s is not a %s", name, what);
    return fallback;
}

static const Action* settings_action_by_func(void (*func)(const Arg*))
{
    size_t i;

    for (i = 0; i < LENGTH(settings_actions); i++)
        if (settings_actions[i].func == func)
            return &settings_actions[i];
    return nullptr;
}

static const Action* settings_action_by_name(const char* name)
{
    size_t i;

    if (!name)
        return nullptr;
    for (i = 0; i < LENGTH(settings_actions); i++)
        if (!strcmp(settings_actions[i].name, name))
            return &settings_actions[i];
    settings_warn("%s is not an action", name);
    return nullptr;
}

static const char* settings_arrange_name(void (*fn)(Monitor*))
{
    size_t i;

    for (i = 0; i < LENGTH(settings_arranges); i++)
        if (settings_arranges[i].arrange == fn)
            return settings_arranges[i].name;
    return "tile";
}

static uint32_t settings_pack_rgba(const float c[static 4])
{
    return ((uint32_t)(c[0] * 255 + 0.5f) << 24) |
           ((uint32_t)(c[1] * 255 + 0.5f) << 16) |
           ((uint32_t)(c[2] * 255 + 0.5f) << 8) | (uint32_t)(c[3] * 255 + 0.5f);
}

/* the holes keep every name on its Scheme* index */
static const char* const settings_scheme_names[] = {
    [SchemeNorm] = "norm",
    [SchemeSel] = "sel",
    [SchemeUrg] = "urg",
    [SchemeTitle] = "title",
    [SchemeTitleSel] = "titlesel",
    [SchemeStatus] = "status",
    [SchemeNotify] = "notify",
    [SchemeNotifyLow] = "notifylow",
    [SchemeNotifyCrit] = "notifycrit",
    [SchemeRunner] = "runner",
    [SchemeRunnerSuggest] = "runnersuggest",
    [SchemeRunnerSel] = "runnersel",
    [SchemeRunnerIcon] = "runnericon",
};
static_assert(LENGTH(settings_scheme_names) == NumSchemes);

/* repeatable, so never missing */
static const char* const settings_records[] = { "layout",  "rule",
                                                "monitor", "autostart",
                                                "key",     "button",
                                                "gesture", "padbutton" };

static FILE* settings_file;
static int settings_depth;
static const char* settings_previous; /* last top level directive */

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
    for (i = 0; i < LENGTH(settings_scheme_names); i++) {
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
static char* settings_rendered_defaults(void)
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

static const struct scfg_directive* settings_find(const struct scfg_block* b,
                                                  const char* k)
{
    size_t i;

    if (!b)
        return nullptr;
    for (i = 0; i < b->directives_len; i++) {
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
    size_t i, n = 0;

    for (i = 0; i < b->directives_len; i++)
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
    size_t i;

    if (!d || d->params_len < from)
        return nullptr;
    v = util_calloc(d->params_len - from + 1, sizeof *v);
    for (i = from; i < d->params_len; i++)
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
    size_t i;

    if (!symbol)
        return nullptr;
    for (i = 0; i < layout_count; i++)
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
    size_t i, j;

    for (i = 0; i < LENGTH(settings_scheme_names); i++) {
        if (!settings_scheme_names[i] ||
            !(d = settings_find(b, settings_scheme_names[i])))
            continue;
        for (j = 0; j < 3; j++)
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
    size_t i, j, n = 0;

    if (!settings_count(b, "layout"))
        return;
    out = util_calloc(settings_count(b, "layout"), sizeof *out);
    for (i = 0; i < b->directives_len; i++) {
        const char* name;

        d = &b->directives[i];
        settings_line = d->lineno;
        if (strcmp(d->name, "layout"))
            continue;
        out[n].symbol = settings_param(d, 0) ? settings_param(d, 0) : "[ ]";
        name = settings_param(d, 1) ? settings_param(d, 1) : "tile";
        for (j = 0; j < LENGTH(settings_arranges); j++)
            if (!strcmp(settings_arranges[j].name, name))
                break;
        if (j < LENGTH(settings_arranges)) {
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
    size_t i, n = 0;

    out = util_calloc(settings_count(b, "rule") + 1, sizeof *out);
    for (i = 0; i < b->directives_len; i++) {
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
    size_t i, n = 0;

    if (!settings_count(b, "monitor"))
        return;
    out = util_calloc(settings_count(b, "monitor"), sizeof *out);
    for (i = 0; i < b->directives_len; i++) {
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
    size_t i, j, n = 0;

    for (i = 0; i < b->directives_len; i++)
        if (!strcmp(b->directives[i].name, "autostart"))
            n += b->directives[i].params_len + 1;
    out = util_calloc(n + 1, sizeof *out);

    n = 0;
    for (i = 0; i < b->directives_len; i++) {
        d = &b->directives[i];
        settings_line = d->lineno;
        if (strcmp(d->name, "autostart") || !d->params_len)
            continue;
        for (j = 0; j < d->params_len; j++)
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
    size_t i, n = 0;

    out = util_calloc(settings_count(b, "key") + 1, sizeof *out);
    for (i = 0; i < b->directives_len; i++) {
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
    size_t i, n = 0;

    out = util_calloc(settings_count(b, "button") + 1, sizeof *out);
    for (i = 0; i < b->directives_len; i++) {
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
    size_t i, n = 0;

    out = util_calloc(settings_count(b, "gesture") + 1, sizeof *out);
    for (i = 0; i < b->directives_len; i++) {
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
    size_t i, n = 0;

    out = util_calloc(settings_count(b, "padbutton") + 1, sizeof *out);
    for (i = 0; i < b->directives_len; i++) {
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

/* config.h's settings, read back: what the file is checked against and what
 * fills in for what it leaves out */
static const struct scfg_block* settings_builtin;

static void settings_load_builtin(void)
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
static void settings_apply(const struct scfg_block* b)
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

static const char* settings_path(void)
{
    static char path[PATH_MAX];
    const char* dir = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");

    if (dir && *dir)
        snprintf(path, sizeof path, "%s/g0wm/config", dir);
    else if (home && *home)
        snprintf(path, sizeof path, "%s/.config/g0wm/config", home);
    else
        return nullptr;
    return path;
}

/* mkdir -p, on a copy the caller owns */
int settings_make_dirs(char* path)
{
    char* p;

    for (p = path + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(path, 0755) < 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }
    return mkdir(path, 0755) < 0 && errno != EEXIST ? -1 : 0;
}

static int settings_is_record(const char* k)
{
    size_t i;

    for (i = 0; i < LENGTH(settings_records); i++)
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

static void settings_check(const struct scfg_block* want,
                           const struct scfg_block* got,
                           const char* path,
                           int* bad)
{
    const struct scfg_directive* w;
    const struct scfg_directive* g;
    char sub[256];
    size_t i;

    for (i = 0; i < want->directives_len; i++) {
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
    for (i = 0; i < got->directives_len; i++) {
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

/* -c: write the defaults when there are none, read back whatever is there,
 * then print what g0wm ends up with. Notes go to stderr, the settings to
 * stdout, so the two can be told apart. */
int settings_write(void)
{
    const char* path = settings_path();
    char dir[PATH_MAX];
    char* slash;
    char* text;
    FILE* f;

    if (!path) {
        fprintf(stderr, "g0wm: neither XDG_CONFIG_HOME nor HOME is set\n");
        return 1;
    }

    /* never over the user's own file: a reinstall must not undo it */
    if (!access(path, F_OK)) {
        fprintf(stderr, "g0wm: reading the config in %s\n", path);
    } else {
        snprintf(dir, sizeof dir, "%s", path);
        if ((slash = strrchr(dir, '/')))
            *slash = '\0';
        if (settings_make_dirs(dir) < 0) {
            fprintf(
                stderr, "g0wm: cannot create %s: %s\n", dir, strerror(errno));
            return 1;
        }
        if (!(text = settings_rendered_defaults()))
            return 1;
        if (!(f = fopen(path, "w"))) {
            fprintf(
                stderr, "g0wm: cannot write %s: %s\n", path, strerror(errno));
            free(text);
            return 1;
        }
        fputs(text, f);
        free(text);
        if (fclose(f) != 0) {
            fprintf(
                stderr, "g0wm: cannot write %s: %s\n", path, strerror(errno));
            return 1;
        }
        fprintf(stderr, "g0wm: wrote %s\n", path);
    }

    settings_load();
    if (!(text = settings_rendered_defaults()))
        return 1;
    fputs(text, stdout);
    free(text);
    return 0;
}

void settings_load(void)
{
    const char* path = settings_path();
    struct scfg_block* tree;
    int bad = 0;

    if (!path) {
        fprintf(stderr, "g0wm: neither XDG_CONFIG_HOME nor HOME is set\n");
        return;
    }
    if (access(path, F_OK) < 0) {
        fprintf(stderr,
                "g0wm: no settings in %s, run 'g0wm -c' to write them\n",
                path);
        return;
    }
    tree = util_calloc(1, sizeof *tree);
    if (scfg_load_file(tree, path) < 0) {
        fprintf(stderr, "g0wm: cannot read %s\n", path);
        scfg_block_finish(tree);
        free(tree);
        return;
    }

    settings_load_builtin();
    if (settings_builtin)
        settings_check(settings_builtin, tree, "", &bad);
    /* never freed: the settings point into its strings */
    settings_apply(tree);
    if (bad)
        fprintf(stderr,
                "g0wm: %s has %d problem%s\n",
                path,
                bad,
                bad == 1 ? "" : "s");
}

/* the keymap and the repeat rate are otherwise only set when a keyboard
 * shows up; the trackpad settings are per device and still need a restart */
static void settings_reload_keyboard(void)
{
    struct xkb_context* context;
    struct xkb_keymap* keymap;

    if (!keyboard_group)
        return;
    if ((context = xkb_context_new(XKB_CONTEXT_NO_FLAGS))) {
        if ((keymap = xkb_keymap_new_from_names(
                 context, &xkb_rules, XKB_KEYMAP_COMPILE_NO_FLAGS))) {
            wlr_keyboard_set_keymap(&keyboard_group->wlr_group->keyboard,
                                    keymap);
            xkb_keymap_unref(keymap);
        }
        xkb_context_unref(context);
    }
    wlr_keyboard_set_repeat_info(
        &keyboard_group->wlr_group->keyboard, repeat_rate, repeat_delay);
}

static void settings_reload(const Arg*)
{
    Monitor* m;
    Client* c;
    int wasprivate = always_private;

    settings_load();

    /* fewer tags than before would leave windows on bits nothing shows */
    wl_list_for_each(m, &monitors, link)
    {
        if (!(m->tagset[m->seltags] & TAGMASK))
            m->tagset[m->seltags] = 1;
        wlr_scene_rect_set_color(m->fullscreen_bg, fullscreen_color);
    }
    /* what a client copied out of the settings when it was mapped: its
     * border, its own opacity and the title shown when private */
    wl_list_for_each(c, &clients, link)
    {
        if (!(c->tags & TAGMASK))
            c->tags = 1;
        c->opacity = c->opacity_unfocus = opacity_unfocus;
        c->opacity_focus = opacity_focus;
        if (!c->isfullscreen)
            c->bw = client_is_unmanaged(c) ? 0 : corner_border_width();
        if (always_private != wasprivate)
            c->isprivate = always_private;
        toplevel_update(c);
    }
    if (always_private != wasprivate)
        privacy_damage();
    wlr_scene_rect_set_color(root_background, root_color);

    settings_reload_keyboard();
    tablet_remap();
    monitor_reload_rules();
    monitor_update_layout(nullptr, nullptr);

    /* a floating client keeps its own geometry, so a new border or title bar
     * only reaches it through client_resize() */
    wl_list_for_each(c, &clients, link)
    {
        if (c->mon && c->isfloating && !c->isfullscreen)
            client_resize(c, c->geom, 1);
    }

    opacity_reload();
    notify_set_timeout(notification_timeout);
    bar_draw_all();
}
