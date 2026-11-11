/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Default values, used to write a new config. Once the file exists
 * it is the one that counts. */

#define MODKEY WLR_MODIFIER_LOGO

#define SHCMD(cmd)                                                                                 \
    {                                                                                              \
        .pointer = (const char*[])                                                                 \
        {                                                                                          \
            "/bin/sh", "-c", cmd, nullptr                                                          \
        }                                                                                          \
    }

#define TAGKEYS(KEY, SKEY, TAG)                                                                    \
    { MODKEY, KEY, layout_view, { .unsigned_integer = 1 << TAG } },                                \
        { MODKEY | WLR_MODIFIER_CTRL, KEY, layout_toggle_view, { .unsigned_integer = 1 << TAG } }, \
        { MODKEY | WLR_MODIFIER_SHIFT, SKEY, layout_tag, { .unsigned_integer = 1 << TAG } },       \
    {                                                                                              \
        MODKEY | WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT, SKEY, layout_toggle_tag,                  \
        {                                                                                          \
            .unsigned_integer = 1 << TAG                                                           \
        }                                                                                          \
    }

#define CHVT(n)                                                                                    \
    {                                                                                              \
        WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_XF86Switch_VT_##n, change_virtual_console,   \
        {                                                                                          \
            .unsigned_integer = (n)                                                                \
        }                                                                                          \
    }

static const char* default_fonts[] = { "monospace:size=10" };

static char* default_tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

static WindowLayout default_layouts[] = {
    { "[ ]", layout_tile },
    { "|||", layout_tabbed },
};

/* opacity 0 and private -1 keep the default, monitor -1 is the focused one */
static ClientRule default_rules[] = {
    { .app_id = "Placeholder",
      .title = nullptr,
      .tags = 0,
      .is_floating = 1,
      .is_private = 1,
      .focused_opacity = 0,
      .unfocused_opacity = 0,
      .monitor_index = -1 },
};

/* the first matching name wins, keep the nullptr one last */
static MonitorRule default_monitor_rules[] = {
    { .name = nullptr,
      .master_factor = 0.55f,
      .master_count = 1,
      .scale = 1,
      .layout = &default_layouts[0],
      .transform = WL_OUTPUT_TRANSFORM_NORMAL,
      .x = -1,
      .y = -1,
      .width = 0,
      .height = 0,
      .refresh_hz = 0 },
};

static const char* terminal_command[] = { "foot", nullptr };
static const char* file_manager_command[] = { "thunar", nullptr };
static const char* browser_command[] = { "firefox", nullptr };

/* run without a shell */
static const char* default_autostart[] = {
    /* "example", "arg1", "arg2", nullptr, */
    nullptr
};

static KeyBinding default_keys[] = {
    { MODKEY, XKB_KEY_q, spawn, { .pointer = terminal_command } },
    { MODKEY, XKB_KEY_f, spawn, { .pointer = file_manager_command } },
    { MODKEY, XKB_KEY_b, spawn, { .pointer = browser_command } },
    { 0, XKB_KEY_Print, spawn, SHCMD("grim -g \"$(slurp)\" - | swappy -f -") },
    { MODKEY, XKB_KEY_c, client_kill, {} },
    { MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_v, client_toggle_floating, {} },
    { MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_p, privacy_toggle, {} },
    { MODKEY | WLR_MODIFIER_CTRL, XKB_KEY_p, bar_notification_toggle_private, {} },
    { MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_g, layout_toggle_gaps, {} },
    { MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_b, bar_toggle, {} },
    { MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_t, bar_toggle_title_bars, {} },
    { MODKEY, XKB_KEY_t, layout_toggle_tabbed, { .pointer = &default_layouts[1] } },
    { MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_f, client_toggle_fullscreen, {} },
    { MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_r, settings_reload, {} },

    { MODKEY, XKB_KEY_r, runner_toggle, {} },

    { MODKEY, XKB_KEY_n, bar_notification_scroll, {} },
    { MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_n, bar_notification_open, {} },
    { MODKEY | WLR_MODIFIER_CTRL, XKB_KEY_n, bar_notification_next, {} },
    { MODKEY | WLR_MODIFIER_ALT, XKB_KEY_n, bar_notification_prev, {} },
    { MODKEY | WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT,
      XKB_KEY_n,
      bar_notification_dismiss_all,
      {} },
    { MODKEY, XKB_KEY_a, bar_notification_actions, {} },

    { MODKEY, XKB_KEY_h, layout_focus_stack, { .integer = -1 } },
    { MODKEY, XKB_KEY_j, layout_focus_stack, { .integer = +1 } },
    { MODKEY, XKB_KEY_k, layout_focus_stack, { .integer = -1 } },
    { MODKEY, XKB_KEY_l, layout_focus_stack, { .integer = +1 } },

    { MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_h, layout_move_stack, { .integer = -1 } },
    { MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_j, layout_move_stack, { .integer = +1 } },
    { MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_k, layout_move_stack, { .integer = -1 } },
    { MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_l, layout_move_stack, { .integer = +1 } },

    { MODKEY | WLR_MODIFIER_CTRL, XKB_KEY_h, layout_resize_width, { .integer = -50 } },
    { MODKEY | WLR_MODIFIER_CTRL, XKB_KEY_l, layout_resize_width, { .integer = +50 } },
    { MODKEY | WLR_MODIFIER_CTRL, XKB_KEY_k, layout_resize_height, { .integer = -50 } },
    { MODKEY | WLR_MODIFIER_CTRL, XKB_KEY_j, layout_resize_height, { .integer = +50 } },

    { MODKEY, XKB_KEY_o, opacity_change_focused, { .floating = +0.05f } },
    { MODKEY | WLR_MODIFIER_ALT, XKB_KEY_o, opacity_change_focused, { .floating = -0.05f } },
    { MODKEY | WLR_MODIFIER_CTRL, XKB_KEY_o, opacity_change_unfocused, { .floating = +0.05f } },
    { MODKEY | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
      XKB_KEY_o,
      opacity_change_unfocused,
      { .floating = -0.05f } },
    { MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_o, opacity_toggle, {} },

    { MODKEY | WLR_MODIFIER_ALT, XKB_KEY_equal, magnifier_magnify, { .floating = +0.25f } },
    { MODKEY | WLR_MODIFIER_ALT, XKB_KEY_minus, magnifier_magnify, { .floating = -0.25f } },
    { MODKEY | WLR_MODIFIER_ALT, XKB_KEY_0, magnifier_magnify, { .floating = 0 } },

    { 0, XKB_KEY_XF86AudioRaiseVolume, spawn, SHCMD("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+") },
    { 0, XKB_KEY_XF86AudioLowerVolume, spawn, SHCMD("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-") },
    { 0, XKB_KEY_XF86AudioMute, spawn, SHCMD("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle") },
    { 0, XKB_KEY_XF86AudioMicMute, spawn, SHCMD("wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle") },
    { 0, XKB_KEY_XF86MonBrightnessUp, spawn, SHCMD("brightnessctl s 10%+") },
    { 0, XKB_KEY_XF86MonBrightnessDown, spawn, SHCMD("brightnessctl s 10%-") },
    { 0, XKB_KEY_XF86AudioNext, spawn, SHCMD("playerctl next") },
    { 0, XKB_KEY_XF86AudioPause, spawn, SHCMD("playerctl play-pause") },
    { 0, XKB_KEY_XF86AudioPlay, spawn, SHCMD("playerctl play-pause") },
    { 0, XKB_KEY_XF86AudioPrev, spawn, SHCMD("playerctl previous") },

    { MODKEY, XKB_KEY_i, layout_inc_master, { .integer = +1 } },
    { MODKEY, XKB_KEY_d, layout_inc_master, { .integer = -1 } },
    { MODKEY, XKB_KEY_Return, layout_zoom, {} },
    { MODKEY, XKB_KEY_Tab, layout_view, {} },
    { MODKEY, XKB_KEY_space, layout_set, {} },
    { MODKEY, XKB_KEY_0, layout_view, { .unsigned_integer = ~0 } },
    { MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_parenright, layout_tag, { .unsigned_integer = ~0 } },
    { MODKEY, XKB_KEY_comma, monitor_focus, { .integer = WLR_DIRECTION_LEFT } },
    { MODKEY, XKB_KEY_period, monitor_focus, { .integer = WLR_DIRECTION_RIGHT } },
    { MODKEY | WLR_MODIFIER_SHIFT,
      XKB_KEY_less,
      monitor_send_client,
      { .integer = WLR_DIRECTION_LEFT } },
    { MODKEY | WLR_MODIFIER_SHIFT,
      XKB_KEY_greater,
      monitor_send_client,
      { .integer = WLR_DIRECTION_RIGHT } },
    TAGKEYS(XKB_KEY_1, XKB_KEY_exclam, 0),
    TAGKEYS(XKB_KEY_2, XKB_KEY_at, 1),
    TAGKEYS(XKB_KEY_3, XKB_KEY_numbersign, 2),
    TAGKEYS(XKB_KEY_4, XKB_KEY_dollar, 3),
    TAGKEYS(XKB_KEY_5, XKB_KEY_percent, 4),
    TAGKEYS(XKB_KEY_6, XKB_KEY_asciicircum, 5),
    TAGKEYS(XKB_KEY_7, XKB_KEY_ampersand, 6),
    TAGKEYS(XKB_KEY_8, XKB_KEY_asterisk, 7),
    TAGKEYS(XKB_KEY_9, XKB_KEY_parenleft, 8),
    { MODKEY | WLR_MODIFIER_SHIFT, XKB_KEY_Escape, quit, {} },
    { MODKEY, XKB_KEY_Escape, input_toggle_inhibit, {} },

    { WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_Terminate_Server, quit, {} },
    CHVT(1),
    CHVT(2),
    CHVT(3),
    CHVT(4),
    CHVT(5),
    CHVT(6),
    CHVT(7),
    CHVT(8),
    CHVT(9),
    CHVT(10),
    CHVT(11),
    CHVT(12),
};

static MouseBinding default_buttons[] = {
    { CLICK_AREA_LAYOUT_SYMBOL, 0, BTN_LEFT, layout_set, { .pointer = &default_layouts[0] } },
    { CLICK_AREA_LAYOUT_SYMBOL, 0, BTN_RIGHT, layout_set, { .pointer = &default_layouts[1] } },

    { CLICK_AREA_BAR_TITLE, 0, BTN_MIDDLE, layout_zoom, {} },
    { CLICK_AREA_BAR_TITLE, 0, BTN_LEFT, bar_notification_open, {} },
    { CLICK_AREA_BAR_TITLE, 0, BTN_RIGHT, bar_notification_dismiss, {} },

    { CLICK_AREA_STATUS, 0, BTN_MIDDLE, spawn, { .pointer = terminal_command } },

    { CLICK_AREA_TRAY, 0, BTN_LEFT, bar_tray_activate, {} },
    { CLICK_AREA_TRAY, 0, BTN_RIGHT, bar_tray_menu, {} },

    { CLICK_AREA_TAGS, 0, BTN_LEFT, layout_view, {} },
    { CLICK_AREA_TAGS, 0, BTN_RIGHT, layout_toggle_view, {} },
    { CLICK_AREA_TAGS, MODKEY, BTN_LEFT, layout_tag, {} },
    { CLICK_AREA_TAGS, MODKEY, BTN_RIGHT, layout_toggle_tag, {} },
    { CLICK_AREA_TAGS, 0, MOUSE_WHEEL_UP, layout_shift_view, { .integer = -1 } },
    { CLICK_AREA_TAGS, 0, MOUSE_WHEEL_DOWN, layout_shift_view, { .integer = +1 } },

    { CLICK_AREA_WINDOW_TITLE, 0, MOUSE_WHEEL_UP, layout_focus_stack, { .integer = -1 } },
    { CLICK_AREA_WINDOW_TITLE, 0, MOUSE_WHEEL_DOWN, layout_focus_stack, { .integer = +1 } },

    { CLICK_AREA_CLIENT,
      MODKEY,
      BTN_LEFT,
      input_move_resize,
      { .unsigned_integer = CURSOR_MODE_MOVE } },
    { CLICK_AREA_CLIENT, MODKEY, BTN_MIDDLE, client_toggle_floating, {} },
    { CLICK_AREA_CLIENT,
      MODKEY,
      BTN_RIGHT,
      input_move_resize,
      { .unsigned_integer = CURSOR_MODE_RESIZE } },

    { CLICK_AREA_CLIENT, MODKEY, MOUSE_WHEEL_UP, magnifier_magnify, { .floating = +0.25f } },
    { CLICK_AREA_CLIENT, MODKEY, MOUSE_WHEEL_DOWN, magnifier_magnify, { .floating = -0.25f } },
    { CLICK_AREA_ROOT, MODKEY, MOUSE_WHEEL_UP, magnifier_magnify, { .floating = +0.25f } },
    { CLICK_AREA_ROOT, MODKEY, MOUSE_WHEEL_DOWN, magnifier_magnify, { .floating = -0.25f } },
};

/* a finger count used here is taken from the apps */
static GestureBinding default_gestures[] = {
    { GESTURE_SWIPE_LEFT, 3, layout_shift_view, { .integer = +1 } },
    { GESTURE_SWIPE_RIGHT, 3, layout_shift_view, { .integer = -1 } },
};

static Settings default_settings = {
    .look = {
        .fonts = default_fonts,
        .font_count = LENGTH(default_fonts),

        .colors = {
            [COLOR_SCHEME_NORMAL] = { 0xffffffff, 0x000000ff, 0x000000ff },
            [COLOR_SCHEME_SELECTED] = { 0xffffffff, 0x000000ff, 0x000000ff },
            [COLOR_SCHEME_URGENT] = { 0xffffffff, 0x000000ff, 0xff0000ff },

            [COLOR_SCHEME_TITLE] = { 0x888888ff, 0x000000ff, 0x000000ff },
            [COLOR_SCHEME_TITLE_SELECTED] = { 0xffffffff, 0x000000ff, 0x000000ff },

            [COLOR_SCHEME_STATUS] = { 0xffffffff, 0x000000ff, 0x000000ff },
            /* the border color is the urgency line */
            [COLOR_SCHEME_NOTIFICATION] = { 0x000000ff, 0xffffffff, 0xe5a50aff },
            [COLOR_SCHEME_NOTIFICATION_LOW] = { 0x000000ff, 0xffffffff, 0x2ec27eff },
            [COLOR_SCHEME_NOTIFICATION_CRITICAL] = { 0x000000ff, 0xffffffff, 0xe01b24ff },

            [COLOR_SCHEME_RUNNER] = { 0xffffffff, 0x000000ff, 0x000000ff },
            [COLOR_SCHEME_RUNNER_SUGGESTION] = { 0xaaaaaaff, 0x000000ff, 0x000000ff },
            [COLOR_SCHEME_RUNNER_SELECTED] = { 0x000000ff, 0xffffffff, 0x000000ff },
            /* the stand in for a missing icon */
            [COLOR_SCHEME_RUNNER_ICON] = { 0xffffffff, 0x2e2e2eff, 0x4a4a4aff },
        },
        .root_color = COLOR(0x000000ff),
        .fullscreen_color = { 0.0f, 0.0f, 0.0f, 1.0f },
#ifdef INTEGRATED_BACKGROUND
        .wallpaper = "", // empty for no wallpaper
#endif
        .tags = default_tags,
        .tag_count = LENGTH(default_tags),
    },
    .windows = {
        .border_width = 1,
        .corner_style = CORNER_STYLE_NORMAL, // or CORNER_STYLE_SQUIRCLE
        .corner_radius = 0,
        .gaps = 1,
        .gap_size = 3,
        .smart_gaps = 0,
        .title_bar = 1,
        .title_padding = 6,
        .title_center = 1,
        .title_close = 1, // shown with the pointer over it
        .title_close_color = 0xff5f57ff,
        .private_color = 0x9b59f6ff,
        .private_round = -1.0f,
        .always_private = 0,
        .public_color = 0x5bc8f5ff,
        .private_title = "{app}",
    },
    .privacy = {
        .text = "",
        .image = nullptr,
        .image_size = 64,
        .text_scale = 1.2f,
        .background = 0x000000ff,
        .foreground = 0xffffffff,
    },
    .rules = {
        .layouts = default_layouts,
        .layout_count = LENGTH(default_layouts),
        .client_rules = default_rules,
        .client_rule_count = LENGTH(default_rules),
        .monitor_rules = default_monitor_rules,
        .monitor_rule_count = LENGTH(default_monitor_rules),
    },
    .bar = {
        .enabled = 1,
        .top = 1,
        .window_title = 0,
        .tag_dots = 1,
        .tag_dot_size = 0,
        .tag_dot_gap = 0,
        .tag_dot_width = 3.0f,
        .tag_dot_round = 1.0f,
        .urgent_pulse = 1200,
        .layout_symbol = 0,
        .box_radius = 2,
        .padding = 0,
        .height = 1.1f, // times the font height, 0 for automatic
        .single_monitor = 0,
        .status = BAR_STATUS_MAIN,
    },
    .systray = {
        .enabled = 1,
        .spacing = 2,
        .padding = 2,
        .icon_size = 16, // 0 fills the bar
    },
    .notifications = {
        .enabled = 1,
        .private = 1,
        .timeout = 5, // seconds
        .line_width = 4,
        .line_radius = 2,
        .action_sign = "[r]",
        /* {app} for application name */
        .private_text = "G0wm is hiding this notification",
        .private_hide = 0, /* left out of screen sharing, no placeholder */
        .dot = NOTIFICATION_DOT_RIGHT, /* or NOTIFICATION_DOT_LEFT */
        .private_color = 0, /* 0 follows windows.private_color */
        .public_color = 0, /* 0 follows windows.public_color */
    },
    .runner = {
        .placeholder = "Run...", // shown while nothing is typed
        .max_candidates = 5,
        .pill_height = 1,
        .pill_padding = 1,
        .pill_round = -1.0f,
        .fuzzy = 1,
        .icons = RUNNER_ICONS_SELECTED,
        .icon_size = 0,
        .icon_round = 0.4f,
        .icon_border = 1,
        .history_size = 100,
        .shell = "/bin/sh",
        .terminal = "foot",
        .terminal_prefix = "!",
        .run_prefix = "!!",
        .calc_prefix = "=",
        .search_prefix = "?",
        .search_url = "https://duckduckgo.com/?q=%s",
        .window_prefix = "@",
        .tag_prefix = "#",
        .files = 1,
        .paths = "",
        .calc_precision = 10,
    },
    .opacity = {
        .enabled = 1,
        .focus = 1.00f,
        .unfocus = 1.00f,
        .decorations = 1.00f, // bar, title bars and borders
#ifdef INTEGRATED_BACKGROUND
        .type = OPACITY_MODE_NORMAL, // or OPACITY_MODE_BLUR
        .blur_radius = 15,
        .blur_passes = 1,
        .blur_saturation = 1.60f,
        .blur_brightness = 1.00f,
#endif
    },
    .input = {
        .sloppy_focus = 1,
        .move_threshold = 4, // px dragged on a title before the window moves
        .move_snap = 12, // px at which a moved floating window sticks, 0 off
        .xkb_rules = {
            .options = nullptr,
        },
        .repeat_rate = 25,
        .repeat_delay = 600,
        .cursor_theme = nullptr,
        .cursor_size = 24,
        .hide_cursor_when_typing = 1,
        .tap_to_click = 1,
        .tap_and_drag = 1,
        .drag_lock = 1,
        .natural_scrolling = 0,
        .disable_while_typing = 1,
        .left_handed = 0,
        .middle_button_emulation = 0,
        .scroll_method = LIBINPUT_CONFIG_SCROLL_2FG,
        .click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS,
        .send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED,
        .accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE,
        .accel_speed = 0.0,
        .touchpad_natural_scrolling = 0,
        .touchpad_accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE,
        .touchpad_accel_speed = 0.0,
        .tap_button_map = LIBINPUT_CONFIG_TAP_MAP_LRM,
        .tablet_output = nullptr,
        .tablet_left_handed = 0, // buttons on the right
        .touch_output = nullptr, // nullptr: the one udev reports
    },
    .bindings = {
        .autostart = default_autostart,
        .key_bindings = default_keys,
        .key_binding_count = LENGTH(default_keys),
        .mouse_bindings = default_buttons,
        .mouse_binding_count = LENGTH(default_buttons),
        .gesture_bindings = default_gestures,
        .pinch_zoom_modifier = MODKEY, /* held while pinching to zoom, 0 for off */
        .gesture_binding_count = LENGTH(default_gestures),
        /* tablet pad buttons, counted from 0 */
        .tablet_pad_bindings = nullptr,
        .tablet_pad_binding_count = 0,
    },
    .misc = {
        .log_level = WLR_ERROR,
        /* let a hidden fullscreen window keep the screen awake */
        .bypass_surface_visibility = 0,
        /* tearing for fullscreen windows that ask for it */
        .allow_tearing = 1,
        /* VRR for fullscreen games and videos */
        .auto_vrr = 1,
    },
};
