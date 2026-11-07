/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * loading and reloading ~/.config/g0wm/config, and the tables of names
 */

#include <errno.h>
#include <libinput.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <scfg.h>
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

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/bar/bar.h"
#include "desktop/bar/notification.h"
#include "desktop/bar/title.h"
#include "desktop/bar/tray.h"
#include "desktop/notify/notify.h"
#include "desktop/runner/runner.h"
#include "input/keyboard.h"
#include "input/pointer.h"
#include "input/tablet.h"
#include "internal.h"
#include "output/magnifier.h"
#include "output/monitor.h"
#include "output/privacy.h"
#include "render/corner.h"
#include "render/opacity.h"
#include "window/client.h"
#include "window/layout.h"
#include "window/toplevel.h"

/* function declarations */
static void settings_reload(const Arg*);

/* the only file that includes it: these are the definitions */
#include "config.h"

/* variables */
/* tables end on { 0, nullptr }; a real zero value is listed before it */
/* in combo order: logo+shift+q */
const Enum settings_modifier_names[] = {
    { WLR_MODIFIER_LOGO, "logo" },   { WLR_MODIFIER_CTRL, "ctrl" }, { WLR_MODIFIER_ALT, "alt" },
    { WLR_MODIFIER_SHIFT, "shift" }, { WLR_MODIFIER_CAPS, "caps" }, { WLR_MODIFIER_MOD2, "mod2" },
    { WLR_MODIFIER_MOD3, "mod3" },   { WLR_MODIFIER_MOD5, "mod5" }, { 0, nullptr }
};
const Enum settings_drag_modes[] = { { CurMove, "move" }, { CurResize, "resize" }, { 0, nullptr } };
const Enum settings_click_areas[] = {
    { ClkTagBar, "tagbar" }, { ClkLtSymbol, "ltsymbol" }, { ClkStatus, "status" },
    { ClkTitle, "title" },   { ClkClient, "client" },     { ClkRoot, "root" },
    { ClkTray, "tray" },     { ClkWinTitle, "wintitle" }, { 0, nullptr },
};
const Enum settings_mouse_buttons[] = { { BTN_LEFT, "left" },        { BTN_MIDDLE, "middle" },
                                        { BTN_RIGHT, "right" },      { WheelUp, "wheel-up" },
                                        { WheelDown, "wheel-down" }, { 0, nullptr } };
const Enum settings_gesture_motions[] = { { SwipeLeft, "swipe-left" },
                                          { SwipeRight, "swipe-right" },
                                          { SwipeUp, "swipe-up" },
                                          { SwipeDown, "swipe-down" },
                                          { PinchIn, "pinch-in" },
                                          { PinchOut, "pinch-out" },
                                          { 0, nullptr } };
const Enum settings_runner_icons[] = { { RunnerIconsNone, "none" },
                                       { RunnerIconsSelected, "selected" },
                                       { RunnerIconsAll, "all" },
                                       { 0, nullptr } };
const Enum settings_bar_statuses[] = { { BarStatusFocus, "focus" },
                                       { BarStatusAll, "all" },
                                       { BarStatusMain, "main" },
                                       { 0, nullptr } };
const Enum settings_corner_styles[] = { { CornerNormal, "normal" },
                                        { CornerSquircle, "squircle" },
                                        { 0, nullptr } };
#ifdef INTEGRATED_BACKGROUND
const Enum settings_opacity_kinds[] = { { OpacityNormal, "normal" },
                                        { OpacityBlur, "blur" },
                                        { 0, nullptr } };
#endif
const Enum settings_transforms[] = { { WL_OUTPUT_TRANSFORM_NORMAL, "normal" },
                                     { WL_OUTPUT_TRANSFORM_90, "90" },
                                     { WL_OUTPUT_TRANSFORM_180, "180" },
                                     { WL_OUTPUT_TRANSFORM_270, "270" },
                                     { WL_OUTPUT_TRANSFORM_FLIPPED, "flipped" },
                                     { WL_OUTPUT_TRANSFORM_FLIPPED_90, "flipped-90" },
                                     { WL_OUTPUT_TRANSFORM_FLIPPED_180, "flipped-180" },
                                     { WL_OUTPUT_TRANSFORM_FLIPPED_270, "flipped-270" },
                                     { 0, nullptr } };
const Enum settings_scroll_methods[] = { { LIBINPUT_CONFIG_SCROLL_NO_SCROLL, "none" },
                                         { LIBINPUT_CONFIG_SCROLL_2FG, "2fg" },
                                         { LIBINPUT_CONFIG_SCROLL_EDGE, "edge" },
                                         { LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN,
                                           "on-button-down" },
                                         { 0, nullptr } };
const Enum settings_click_methods[] = { { LIBINPUT_CONFIG_CLICK_METHOD_NONE, "none" },
                                        { LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS,
                                          "button-areas" },
                                        { LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER, "clickfinger" },
                                        { 0, nullptr } };
const Enum settings_send_events[] = { { LIBINPUT_CONFIG_SEND_EVENTS_ENABLED, "enabled" },
                                      { LIBINPUT_CONFIG_SEND_EVENTS_DISABLED, "disabled" },
                                      { LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE,
                                        "disabled-on-external-mouse" },
                                      { 0, nullptr } };
const Enum settings_accel_profiles[] = { { LIBINPUT_CONFIG_ACCEL_PROFILE_NONE, "none" },
                                         { LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT, "flat" },
                                         { LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE, "adaptive" },
                                         { 0, nullptr } };
const Enum settings_button_maps[] = { { LIBINPUT_CONFIG_TAP_MAP_LRM, "lrm" },
                                      { LIBINPUT_CONFIG_TAP_MAP_LMR, "lmr" },
                                      { 0, nullptr } };
const Enum settings_log_levels[] = { { WLR_SILENT, "silent" },
                                     { WLR_ERROR, "error" },
                                     { WLR_INFO, "info" },
                                     { WLR_DEBUG, "debug" },
                                     { 0, nullptr } };
const Arrange settings_arranges[] = { { "tile", layout_tile }, { "tabbed", layout_tabbed } };
const size_t settings_arrange_count = LENGTH(settings_arranges);
const Action settings_actions[] = {
    { change_virtual_console, "chvt", 'u' },
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
    { bar_notification_toggle_private, "toggleprivatenotify", 0 },
    { layout_toggle_tabbed, "toggletabbed", 'l' },
    { layout_toggle_tag, "toggletag", 'u' },
    { bar_toggle_title_bars, "toggletitlebar", 0 },
    { layout_toggle_view, "toggleview", 'u' },
    { bar_tray_activate, "trayactivate", 0 },
    { bar_tray_menu, "traymenu", 0 },
    { layout_view, "view", 'u' },
    { layout_zoom, "zoom", 0 },
};
const size_t settings_action_count = LENGTH(settings_actions);
/* the holes keep every name on its Scheme* index */
const char* const settings_scheme_names[] = {
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
/* config.h's settings, read back: what the file is checked against and what
 * fills in for what it leaves out */
const struct scfg_block* settings_builtin;

/* function implementations */
uint32_t settings_pack_rgba(const float color[static 4])
{
    return ((uint32_t)(color[0] * 255 + 0.5f) << 24) | ((uint32_t)(color[1] * 255 + 0.5f) << 16) |
           ((uint32_t)(color[2] * 255 + 0.5f) << 8) | (uint32_t)(color[3] * 255 + 0.5f);
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
    for (char* separator = path + 1; *separator; separator++) {
        if (*separator != '/')
            continue;
        *separator = '\0';
        if (mkdir(path, 0755) < 0 && errno != EEXIST)
            return -1;
        *separator = '/';
    }
    return mkdir(path, 0755) < 0 && errno != EEXIST ? -1 : 0;
}

/* -c: write the defaults when there are none, read back whatever is there,
 * then print what g0wm ends up with. Notes go to stderr, the settings to
 * stdout, so the two can be told apart. */
int settings_write(void)
{
    const char* path = settings_path();

    if (!path) {
        fprintf(stderr, "g0wm: neither XDG_CONFIG_HOME nor HOME is set\n");
        return 1;
    }

    /* never over the user's own file: a reinstall must not undo it */
    if (!access(path, F_OK)) {
        fprintf(stderr, "g0wm: reading the config in %s\n", path);
    } else {
        char dir[PATH_MAX];
        snprintf(dir, sizeof dir, "%s", path);
        char* last_slash = strrchr(dir, '/');
        if (last_slash)
            *last_slash = '\0';
        if (settings_make_dirs(dir) < 0) {
            fprintf(stderr, "g0wm: cannot create %s: %s\n", dir, strerror(errno));
            return 1;
        }
        char* defaults = settings_rendered_defaults();
        if (!defaults)
            return 1;
        FILE* file = fopen(path, "w");
        if (!file) {
            fprintf(stderr, "g0wm: cannot write %s: %s\n", path, strerror(errno));
            free(defaults);
            return 1;
        }
        fputs(defaults, file);
        free(defaults);
        if (fclose(file) != 0) {
            fprintf(stderr, "g0wm: cannot write %s: %s\n", path, strerror(errno));
            return 1;
        }
        fprintf(stderr, "g0wm: wrote %s\n", path);
    }

    settings_load();
    char* effective = settings_rendered_defaults();
    if (!effective)
        return 1;
    fputs(effective, stdout);
    free(effective);
    return 0;
}

void settings_load(void)
{
    const char* path = settings_path();

    if (!path) {
        fprintf(stderr, "g0wm: neither XDG_CONFIG_HOME nor HOME is set\n");
        return;
    }
    if (access(path, F_OK) < 0) {
        fprintf(stderr, "g0wm: no settings in %s, run 'g0wm -c' to write them\n", path);
        return;
    }
    struct scfg_block* tree = util_calloc(1, sizeof *tree);
    if (scfg_load_file(tree, path) < 0) {
        fprintf(stderr, "g0wm: cannot read %s\n", path);
        scfg_block_finish(tree);
        free(tree);
        return;
    }

    settings_load_builtin();
    int problems = 0;
    if (settings_builtin)
        settings_validate(settings_builtin, tree, "", &problems);
    /* never freed: the settings point into its strings */
    settings_apply(tree);
    if (problems)
        fprintf(stderr, "g0wm: %s has %d problem%s\n", path, problems, problems == 1 ? "" : "s");
}

/* the keymap and the repeat rate are otherwise only set when a keyboard
 * shows up; the trackpad settings are per device and still need a restart */
static void settings_reload_keyboard(void)
{
    if (!keyboard_group)
        return;
    struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (context) {
        struct xkb_keymap* keymap =
            xkb_keymap_new_from_names(context, &xkb_rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (keymap) {
            wlr_keyboard_set_keymap(&keyboard_group->wlr_group->keyboard, keymap);
            xkb_keymap_unref(keymap);
        }
        xkb_context_unref(context);
    }
    wlr_keyboard_set_repeat_info(&keyboard_group->wlr_group->keyboard, repeat_rate, repeat_delay);
}

static void settings_reload(const Arg*)
{
    const int was_private = always_private;

    settings_load();

    /* fewer tags than before would leave windows on bits nothing shows */
    Monitor* monitor;
    wl_list_for_each(monitor, &monitors, link) {
        if (!(monitor->tagset[monitor->seltags] & TAGMASK))
            monitor->tagset[monitor->seltags] = 1;
        wlr_scene_rect_set_color(monitor->fullscreen_bg, fullscreen_color);
    }
    /* what a client copied out of the settings when it was mapped: its
     * border, its own opacity and the title shown when private */
    Client* client;
    wl_list_for_each(client, &clients, link) {
        if (!(client->tags & TAGMASK))
            client->tags = 1;
        client->opacity = client->opacity_unfocus = opacity_unfocus;
        client->opacity_focus = opacity_focus;
        if (!client->isfullscreen)
            client->bw = client_is_unmanaged(client) ? 0 : corner_border_width();
        if (always_private != was_private)
            client->isprivate = always_private;
        toplevel_update(client);
    }
    if (always_private != was_private)
        privacy_damage();
    wlr_scene_rect_set_color(root_background, root_color);

    settings_reload_keyboard();
    tablet_remap();
    monitor_reload_rules();
    monitor_update_layout(nullptr, nullptr);

    /* a floating client keeps its own geometry, so a new border or title bar
     * only reaches it through client_resize() */
    wl_list_for_each(client, &clients, link) {
        if (client->mon && client->isfloating && !client->isfullscreen)
            client_resize(client, client->geom, 1);
    }

    opacity_reload();
    notify_set_timeout(notification_timeout);
    bar_draw_all();
}
