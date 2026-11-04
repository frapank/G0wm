/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Default values, used to write a new config. Once the file exists
 * it is the one that counts. */

#define MODKEY WLR_MODIFIER_LOGO

#define SHCMD(cmd)                                                             \
    {                                                                          \
        .v = (const char*[])                                                   \
        {                                                                      \
            "/bin/sh", "-c", cmd, nullptr                                      \
        }                                                                      \
    }

#define TAGKEYS(KEY, SKEY, TAG)                                                \
    { MODKEY, KEY, layout_view, { .ui = 1 << TAG } },                                 \
        { MODKEY | WLR_MODIFIER_CTRL, KEY, layout_toggle_view, { .ui = 1 << TAG } },   \
        { MODKEY | WLR_MODIFIER_SHIFT, SKEY, layout_tag, { .ui = 1 << TAG } },        \
    {                                                                          \
        MODKEY | WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT, SKEY, layout_toggle_tag,      \
        {                                                                      \
            .ui = 1 << TAG                                                     \
        }                                                                      \
    }

#define CHVT(n)                                                                \
    {                                                                          \
        WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_XF86Switch_VT_##n, change_vt, \
        {                                                                      \
            .ui = (n)                                                          \
        }                                                                      \
    }

static const char* default_fonts[] = { "monospace:size=10" };
const char** fonts = default_fonts;
size_t font_count      = LENGTH(default_fonts);

/* fg, bg, border */
uint32_t colors[NumSchemes][3] = {
    [SchemeNorm]          = { 0xffffffff, 0x000000ff, 0x000000ff },
    [SchemeSel]           = { 0xffffffff, 0x000000ff, 0x000000ff },
    [SchemeUrg]           = { 0xffffffff, 0x000000ff, 0xff0000ff },

    [SchemeTitle]         = { 0x888888ff, 0x000000ff, 0x000000ff },
    [SchemeTitleSel]      = { 0xffffffff, 0x000000ff, 0x000000ff },

    [SchemeStatus]        = { 0xffffffff, 0x000000ff, 0x000000ff },
    /* the border color is the urgency line */
    [SchemeNotify]        = { 0x000000ff, 0xffffffff, 0xe5a50aff },
    [SchemeNotifyLow]     = { 0x000000ff, 0xffffffff, 0x2ec27eff },
    [SchemeNotifyCrit]    = { 0x000000ff, 0xffffffff, 0xe01b24ff },

    [SchemeRunner]        = { 0xffffffff, 0x000000ff, 0x000000ff },
    [SchemeRunnerSuggest] = { 0xaaaaaaff, 0x000000ff, 0x000000ff },
    [SchemeRunnerSel]     = { 0x000000ff, 0xffffffff, 0x000000ff },
    /* the stand in for a missing icon */
    [SchemeRunnerIcon]    = { 0xffffffff, 0x2e2e2eff, 0x4a4a4aff },
};

float root_color[]     = COLOR(0x000000ff);
float fullscreen_color[] = { 0.0f, 0.0f, 0.0f, 1.0f };

#ifdef INTEGRATED_BACKGROUND
const char* wallpaper = ""; // empty for no wallpaper
#endif

static char* default_tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
char** tags  = default_tags;
size_t tag_count = LENGTH(default_tags);

unsigned int border_width = 1;

int corner_style       = CornerNormal; // or CornerSquircle
unsigned int corner_radius = 0;

int gaps                = 1;
unsigned int gap_size      = 3;
int smart_gaps           = 0;

int title_bar              = 1;
unsigned int title_padding = 6;
int title_center           = 1;
int title_close            = 1; // shown with the pointer over it
uint32_t title_close_color  = 0xff5f57ff;
uint32_t private_color     = 0x9b59f6ff;
float private_round        = -1.0f;
int always_private         = 0;
uint32_t public_color      = 0x5bc8f5ff;
const char* private_title  = "{app}";

static Layout default_layouts[] = {
    { "[ ]", layout_tile },
    { "|||", layout_tabbed },
};
Layout* layouts = default_layouts;
size_t layout_count = LENGTH(default_layouts);

/* opacity 0 and private -1 keep the default, monitor -1 is the focused one */
static Rule default_rules[] = {
    { .id              = "Placeholder",
      .title           = nullptr,
      .tags            = 0,
      .isfloating      = 1,
      .isprivate       = 1,
      .opacity_focus   = 0,
      .opacity_unfocus = 0,
      .monitor         = -1 },
};
Rule* rules   = default_rules;
size_t rule_count = LENGTH(default_rules);

int bar_enabled     = 1;
int bar_top      = 1;
int bar_window_title = 0;

int bar_tag_dots  = 1;
unsigned int bar_tag_dot_size = 0;
unsigned int bar_tag_dot_gap  = 0;
float bar_tag_dot_width       = 3.0f;
float bar_tag_dot_round       = 1.0f;
unsigned int bar_urgent_pulse = 1200;
int bar_layout_symbol = 0;

unsigned int bar_box_radius = 2;
unsigned int bar_padding   = 0;
float bar_height           = 1.1f; // times the font height, 0 for automatic

int bar_single_monitor = 0;
int bar_status = BarStatusMain;

int systray_enabled              = 1;
unsigned int systray_spacing  = 2;
unsigned int systray_padding  = 2;
unsigned int systray_icon_size = 16; // 0 fills the bar

int notifications_enabled             = 1;
unsigned int notification_timeout = 5; // seconds

unsigned int notification_line_width  = 4;
unsigned int notification_line_radius = 2;

const char* notification_action_sign = "[r]";

const char* runner_placeholder = "Run..."; // shown while nothing is typed
unsigned int runner_max_candidates = 5;
unsigned int runner_pill_height     = 1;
unsigned int runner_pill_padding    = 1;
float runner_pill_round             = -1.0f;
int runner_fuzzy                   = 1;
int runner_icons                   = RunnerIconsSelected;
unsigned int runner_icon_size       = 0;
float runner_icon_round             = 0.4f;
unsigned int runner_icon_border     = 1;
unsigned int runner_history_size   = 100;
const char* runner_shell           = "/bin/sh";
const char* runner_terminal        = "foot";
const char* runner_terminal_prefix      = "!";
const char* runner_run_prefix       = "!!";
const char* runner_calc_prefix      = "=";
const char* runner_search_prefix    = "?";
const char* runner_search_url       = "https://duckduckgo.com/?q=%s";
const char* runner_window_prefix    = "@";
const char* runner_tag_prefix       = "#";
int runner_files                   = 1;
const char* runner_paths           = "";
unsigned int runner_calc_precision = 10;

int opacity_enabled = 1;

float opacity_focus   = 1.00f;
float opacity_unfocus = 1.00f;
float opacity_decorations    = 1.00f; // bar, title bars and borders

#ifdef INTEGRATED_BACKGROUND
int opacity_type = OpacityNormal; // or OpacityBlur

unsigned int blur_radius = 15;
unsigned int blur_passes = 1;
float blur_saturation    = 1.60f;
float blur_brightness    = 1.00f;
#endif

/* the first matching name wins, keep the nullptr one last */
static MonitorRule default_monitor_rules[] = {
    { .name    = nullptr,
      .mfact   = 0.55f,
      .nmaster = 1,
      .scale   = 1,
      .lt      = &default_layouts[0],
      .rr      = WL_OUTPUT_TRANSFORM_NORMAL,
      .x       = -1,
      .y       = -1,
      .width   = 0,
      .height  = 0,
      .refresh = 0 },
};
MonitorRule* monitor_rules = default_monitor_rules;
size_t monitor_rule_count      = LENGTH(default_monitor_rules);

int sloppy_focus = 1;

int move_threshold = 4;     // px dragged on a title before the window moves
int move_snap      = 12;    // px at which a moved floating window sticks, 0 off

struct xkb_rule_names xkb_rules = {
    .options = nullptr,
};

int repeat_rate  = 25;
int repeat_delay = 600;

const char* cursor_theme = nullptr;
int cursor_size    = 24;
int hide_cursor_when_typing = 1;

int tap_to_click            = 1;
int tap_and_drag            = 1;
int drag_lock               = 1;
int natural_scrolling       = 0;
int disable_while_typing    = 1;
int left_handed             = 0;
int middle_button_emulation = 0;

enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;

enum libinput_config_click_method click_method =
    LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;

uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

enum libinput_config_accel_profile accel_profile =
    LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
double accel_speed = 0.0;

/* natural_scrolling, accel_profile and accel_speed for touchpads */
int touchpad_natural_scrolling = 0;
enum libinput_config_accel_profile touchpad_accel_profile =
    LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
double touchpad_accel_speed = 0.0;

enum libinput_config_tap_button_map tap_button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

/* output names as wlr-randr shows them, nullptr for all of them */
const char* tablet_output = nullptr;
int tablet_left_handed    = 0; // buttons on the right
const char* touch_output  = nullptr; // nullptr: the one udev reports

static const char* terminal_command[]        = { "foot", nullptr };
static const char* file_manager_command[] = { "thunar", nullptr };
static const char* browser_command[]     = { "firefox", nullptr };


/* run without a shell */
static const char* default_autostart[] = {
    /* "example", "arg1", "arg2", nullptr, */
    nullptr
};
const char** autostart = default_autostart;

static Key default_keys[] = {
	{ MODKEY,                    XKB_KEY_q,           spawn,            {.v = terminal_command} },
	{ MODKEY,                    XKB_KEY_f,           spawn,            {.v = file_manager_command} },
	{ MODKEY,                    XKB_KEY_b,           spawn,            {.v = browser_command} },
	{ 0,                         XKB_KEY_Print,       spawn,            SHCMD("grim -g \"$(slurp)\" - | swappy -f -") },
	{ MODKEY,                    XKB_KEY_c,           client_kill,       {} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_v,           client_toggle_floating,   {} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_p,           privacy_toggle,    {} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_g,           layout_toggle_gaps,       {} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_b,           bar_toggle,        {} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_t,           bar_toggle_title_bars,   {} },
	{ MODKEY,                    XKB_KEY_t,           layout_toggle_tabbed,     {.v = &default_layouts[1]} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_f,           client_toggle_fullscreen, {} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_r,           settings_reload,   {} },

	{ MODKEY,                    XKB_KEY_r,           runner_toggle,     {} },

	{ MODKEY,                                      XKB_KEY_n, bar_notification_scroll,     {} },
	{ MODKEY|WLR_MODIFIER_SHIFT,                   XKB_KEY_n, bar_notification_open,       {} },
	{ MODKEY|WLR_MODIFIER_CTRL,                    XKB_KEY_n, bar_notification_next,       {} },
	{ MODKEY|WLR_MODIFIER_ALT,                     XKB_KEY_n, bar_notification_prev,       {} },
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT, XKB_KEY_n, bar_notification_dismiss_all, {} },
	{ MODKEY,                                      XKB_KEY_a, bar_notification_actions,    {} },

	{ MODKEY,                    XKB_KEY_h,           layout_focus_stack,       {.i = -1} },
	{ MODKEY,                    XKB_KEY_j,           layout_focus_stack,       {.i = +1} },
	{ MODKEY,                    XKB_KEY_k,           layout_focus_stack,       {.i = -1} },
	{ MODKEY,                    XKB_KEY_l,           layout_focus_stack,       {.i = +1} },

	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_h,           layout_move_stack,        {.i = -1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_j,           layout_move_stack,        {.i = +1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_k,           layout_move_stack,        {.i = -1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_l,           layout_move_stack,        {.i = +1} },

	{ MODKEY|WLR_MODIFIER_CTRL, XKB_KEY_h,           layout_resize_width,  {.i = -50} },
	{ MODKEY|WLR_MODIFIER_CTRL, XKB_KEY_l,           layout_resize_width,  {.i = +50} },
	{ MODKEY|WLR_MODIFIER_CTRL, XKB_KEY_k,           layout_resize_height, {.i = -50} },
	{ MODKEY|WLR_MODIFIER_CTRL, XKB_KEY_j,           layout_resize_height, {.i = +50} },

	{ MODKEY,                                    XKB_KEY_o, opacity_change_focused,   {.f = +0.05f} },
	{ MODKEY|WLR_MODIFIER_ALT,                   XKB_KEY_o, opacity_change_focused,   {.f = -0.05f} },
	{ MODKEY|WLR_MODIFIER_CTRL,                  XKB_KEY_o, opacity_change_unfocused, {.f = +0.05f} },
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_o, opacity_change_unfocused, {.f = -0.05f} },
	{ MODKEY|WLR_MODIFIER_SHIFT,                 XKB_KEY_o, opacity_toggle,     {} },

	{ MODKEY|WLR_MODIFIER_ALT,   XKB_KEY_equal,       magnifier_magnify,          {.f = +0.25f} },
	{ MODKEY|WLR_MODIFIER_ALT,   XKB_KEY_minus,       magnifier_magnify,          {.f = -0.25f} },
	{ MODKEY|WLR_MODIFIER_ALT,   XKB_KEY_0,           magnifier_magnify,          {.f = 0} },

	{ 0, XKB_KEY_XF86AudioRaiseVolume,  spawn, SHCMD("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+") },
	{ 0, XKB_KEY_XF86AudioLowerVolume,  spawn, SHCMD("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-") },
	{ 0, XKB_KEY_XF86AudioMute,         spawn, SHCMD("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle") },
	{ 0, XKB_KEY_XF86AudioMicMute,      spawn, SHCMD("wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle") },
	{ 0, XKB_KEY_XF86MonBrightnessUp,   spawn, SHCMD("brightnessctl s 10%+") },
	{ 0, XKB_KEY_XF86MonBrightnessDown, spawn, SHCMD("brightnessctl s 10%-") },
	{ 0, XKB_KEY_XF86AudioNext,         spawn, SHCMD("playerctl next") },
	{ 0, XKB_KEY_XF86AudioPause,        spawn, SHCMD("playerctl play-pause") },
	{ 0, XKB_KEY_XF86AudioPlay,         spawn, SHCMD("playerctl play-pause") },
	{ 0, XKB_KEY_XF86AudioPrev,         spawn, SHCMD("playerctl previous") },

	{ MODKEY,                    XKB_KEY_i,           layout_inc_master,       {.i = +1} },
	{ MODKEY,                    XKB_KEY_d,           layout_inc_master,       {.i = -1} },
	{ MODKEY,                    XKB_KEY_Return,      layout_zoom,             {} },
	{ MODKEY,                    XKB_KEY_Tab,         layout_view,             {} },
	{ MODKEY,                    XKB_KEY_space,       layout_set,        {} },
	{ MODKEY,                    XKB_KEY_0,           layout_view,             {.ui = ~0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_parenright,  layout_tag,              {.ui = ~0} },
	{ MODKEY,                    XKB_KEY_comma,       monitor_focus,         {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,                    XKB_KEY_period,      monitor_focus,         {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_less,        monitor_send_client,           {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_greater,     monitor_send_client,           {.i = WLR_DIRECTION_RIGHT} },
	TAGKEYS(          XKB_KEY_1, XKB_KEY_exclam,                        0),
	TAGKEYS(          XKB_KEY_2, XKB_KEY_at,                            1),
	TAGKEYS(          XKB_KEY_3, XKB_KEY_numbersign,                    2),
	TAGKEYS(          XKB_KEY_4, XKB_KEY_dollar,                        3),
	TAGKEYS(          XKB_KEY_5, XKB_KEY_percent,                       4),
	TAGKEYS(          XKB_KEY_6, XKB_KEY_asciicircum,                   5),
	TAGKEYS(          XKB_KEY_7, XKB_KEY_ampersand,                     6),
	TAGKEYS(          XKB_KEY_8, XKB_KEY_asterisk,                      7),
	TAGKEYS(          XKB_KEY_9, XKB_KEY_parenleft,                     8),
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Escape,      quit,             {} },
	{ MODKEY,                    XKB_KEY_Escape,      input_toggle_inhibit,    {} },

	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, quit, {} },
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};
Key* keys    = default_keys;
size_t key_count = LENGTH(default_keys);

static Button default_buttons[] = {
	{ ClkLtSymbol, 0,      BTN_LEFT,   layout_set,      {.v = &default_layouts[0]} },
	{ ClkLtSymbol, 0,      BTN_RIGHT,  layout_set,      {.v = &default_layouts[1]} },

	{ ClkTitle,    0,      BTN_MIDDLE, layout_zoom,           {} },
	{ ClkTitle,    0,      BTN_LEFT,   bar_notification_open,     {} },
	{ ClkTitle,    0,      BTN_RIGHT,  bar_notification_dismiss,  {} },

	{ ClkStatus,   0,      BTN_MIDDLE, spawn,          {.v = terminal_command} },

	{ ClkTray,     0,      BTN_LEFT,   bar_tray_activate,   {} },
	{ ClkTray,     0,      BTN_RIGHT,  bar_tray_menu,       {} },

	{ ClkTagBar,   0,      BTN_LEFT,   layout_view,           {} },
	{ ClkTagBar,   0,      BTN_RIGHT,  layout_toggle_view,     {} },
	{ ClkTagBar,   MODKEY, BTN_LEFT,   layout_tag,            {} },
	{ ClkTagBar,   MODKEY, BTN_RIGHT,  layout_toggle_tag,      {} },
	{ ClkTagBar,   0,      WheelUp,    layout_shift_view,      {.i = -1} },
	{ ClkTagBar,   0,      WheelDown,  layout_shift_view,      {.i = +1} },

	{ ClkWinTitle, 0,      WheelUp,    layout_focus_stack,     {.i = -1} },
	{ ClkWinTitle, 0,      WheelDown,  layout_focus_stack,     {.i = +1} },

	{ ClkClient,   MODKEY, BTN_LEFT,   input_move_resize,     {.ui = CurMove} },
	{ ClkClient,   MODKEY, BTN_MIDDLE, client_toggle_floating, {} },
	{ ClkClient,   MODKEY, BTN_RIGHT,  input_move_resize,     {.ui = CurResize} },

	{ ClkClient,   MODKEY, WheelUp,    magnifier_magnify,        {.f = +0.25f} },
	{ ClkClient,   MODKEY, WheelDown,  magnifier_magnify,        {.f = -0.25f} },
	{ ClkRoot,     MODKEY, WheelUp,    magnifier_magnify,        {.f = +0.25f} },
	{ ClkRoot,     MODKEY, WheelDown,  magnifier_magnify,        {.f = -0.25f} },
};
Button* buttons = default_buttons;
size_t button_count = LENGTH(default_buttons);

/* a finger count used here is taken from the apps */
static Gesture default_gestures[] = {
	{ SwipeLeft,  3, layout_shift_view, {.i = +1} },
	{ SwipeRight, 3, layout_shift_view, {.i = -1} },
};
Gesture* gestures = default_gestures;
size_t gesture_count  = LENGTH(default_gestures);

/* held while pinching on a touchpad to zoom the magnifier, 0 for off */
unsigned int pinch_zoom_modifier = MODKEY;

/* tablet pad buttons, counted from 0: a bound one is not sent to the app.
 * e.g. { 0, spawn, SHCMD("wtype -M ctrl z -m ctrl") } */
PadButton* pad_buttons = nullptr;
size_t pad_button_count    = 0;

int log_level = WLR_ERROR;

/* let a hidden fullscreen window keep the screen awake */
int bypass_surface_visibility = 0;

/* tearing for fullscreen windows that ask for it */
int allow_tearing = 1;

/* VRR for fullscreen games and videos */
int auto_vrr = 1;
