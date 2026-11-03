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
    { MODKEY, KEY, view, { .ui = 1 << TAG } },                                 \
        { MODKEY | WLR_MODIFIER_CTRL, KEY, toggleview, { .ui = 1 << TAG } },   \
        { MODKEY | WLR_MODIFIER_SHIFT, SKEY, tag, { .ui = 1 << TAG } },        \
    {                                                                          \
        MODKEY | WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT, SKEY, toggletag,      \
        {                                                                      \
            .ui = 1 << TAG                                                     \
        }                                                                      \
    }

#define CHVT(n)                                                                \
    {                                                                          \
        WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_XF86Switch_VT_##n, chvt, \
        {                                                                      \
            .ui = (n)                                                          \
        }                                                                      \
    }

static const char* fonts_def[] = { "monospace:size=10" };
const char** fonts = fonts_def;
size_t nfonts      = LENGTH(fonts_def);

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

float rootcolor[]     = COLOR(0x000000ff);
float fullscreen_bg[] = { 0.0f, 0.0f, 0.0f, 1.0f };

#ifdef INTEGRATED_BACKGROUND
const char* wallpaper = ""; // empty for no wallpaper
#endif

static char* tags_def[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
char** tags  = tags_def;
size_t ntags = LENGTH(tags_def);

unsigned int borderpx = 1;

int cornerstyle       = CornerNormal; // or CornerSquircle
unsigned int cornerpx = 0;

int gaps                = 1;
unsigned int gappx      = 3;
int smartgaps           = 0;

int titlebar              = 1;
unsigned int titlepadding = 6;
int titlecenter           = 1;
int titleclose            = 1; // shown with the pointer over it
uint32_t titleclosecolor  = 0xff5f57ff;
uint32_t privatecolor     = 0x9b59f6ff;
float privateround        = -1.0f;
int alwaysprivate         = 0;
uint32_t publiccolor      = 0x5bc8f5ff;
const char* privatetitle  = "{app}";

static Layout layouts_def[] = {
    { "[ ]", tile },
    { "|||", tabbed },
};
Layout* layouts = layouts_def;
size_t nlayouts = LENGTH(layouts_def);

/* opacity 0 and private -1 keep the default, monitor -1 is the focused one */
static Rule rules_def[] = {
    { .id              = "Placeholder",
      .title           = nullptr,
      .tags            = 0,
      .isfloating      = 1,
      .isprivate       = 1,
      .opacity_focus   = 0,
      .opacity_unfocus = 0,
      .monitor         = -1 },
};
Rule* rules   = rules_def;
size_t nrules = LENGTH(rules_def);

int showbar     = 1;
int topbar      = 1;
int barwintitle = 0;

int bartagdots  = 1;
unsigned int bartagdotsize = 0;
unsigned int bartagdotgap  = 0;
float bartagdotwidth       = 3.0f;
float bartagdotround       = 1.0f;
unsigned int barurgentpulse = 1200;
int barltsymbol = 0;

unsigned int barboxradius = 2;
unsigned int barpadding   = 0;
float barheight           = 1.1f; // times the font height, 0 for automatic

int barsinglemon = 0;
int barstatus = BarStatusMain;

int showsystray              = 1;
unsigned int systrayspacing  = 2;
unsigned int systraypadding  = 2;
unsigned int systrayiconsize = 16; // 0 fills the bar

int shownotifications             = 1;
unsigned int notification_timeout = 5; // seconds

unsigned int notification_linewidth  = 4;
unsigned int notification_lineradius = 2;

const char* notification_actionsign = "[r]";

const char* runner_placeholder = "Run..."; // shown while nothing is typed
unsigned int runner_max_candidates = 5;
unsigned int runner_pillheight     = 1;
unsigned int runner_pillpadding    = 1;
float runner_pillround             = -1.0f;
int runner_fuzzy                   = 1;
int runner_icons                   = RunnerIconsSelected;
unsigned int runner_iconsize       = 0;
float runner_iconround             = 0.4f;
unsigned int runner_iconborder     = 1;
unsigned int runner_history_size   = 100;
const char* runner_shell           = "/bin/sh";
const char* runner_terminal        = "foot";
const char* runner_termprefix      = "!";
const char* runner_runprefix       = "!!";
const char* runner_calcprefix      = "=";
const char* runner_searchprefix    = "?";
const char* runner_searchurl       = "https://duckduckgo.com/?q=%s";
const char* runner_windowprefix    = "@";
const char* runner_tagprefix       = "#";
int runner_files                   = 1;
const char* runner_paths           = "";
unsigned int runner_calc_precision = 10;

int opacity_enabled = 1;

float opacity_focus   = 1.00f;
float opacity_unfocus = 1.00f;
float opacity_deco    = 1.00f; // bar, title bars and borders

#ifdef INTEGRATED_BACKGROUND
int opacity_type = OpacityNormal; // or OpacityBlur

unsigned int blur_radius = 15;
unsigned int blur_passes = 1;
float blur_saturation    = 1.60f;
float blur_brightness    = 1.00f;
#endif

/* the first matching name wins, keep the nullptr one last */
static MonitorRule monrules_def[] = {
    { .name    = nullptr,
      .mfact   = 0.55f,
      .nmaster = 1,
      .scale   = 1,
      .lt      = &layouts_def[0],
      .rr      = WL_OUTPUT_TRANSFORM_NORMAL,
      .x       = -1,
      .y       = -1,
      .width   = 0,
      .height  = 0,
      .refresh = 0 },
};
MonitorRule* monrules = monrules_def;
size_t nmonrules      = LENGTH(monrules_def);

int sloppyfocus = 1;

int movethreshold = 4;     // px dragged on a title before the window moves
int movesnap      = 12;    // px at which a moved floating window sticks, 0 off

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

enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

/* output names as wlr-randr shows them, nullptr for all of them */
const char* tablet_output = nullptr;
int tablet_left_handed    = 0; // buttons on the right
const char* touch_output  = nullptr; // nullptr: the one udev reports

static const char* termcmd[]        = { "foot", nullptr };
static const char* filemanagercmd[] = { "thunar", nullptr };
static const char* browsercmd[]     = { "firefox", nullptr };


/* run without a shell */
static const char* autostart_def[] = {
    /* "example", "arg1", "arg2", nullptr, */
    nullptr
};
const char** autostart = autostart_def;

static Key keys_def[] = {
	{ MODKEY,                    XKB_KEY_q,           spawn,            {.v = termcmd} },
	{ MODKEY,                    XKB_KEY_f,           spawn,            {.v = filemanagercmd} },
	{ MODKEY,                    XKB_KEY_b,           spawn,            {.v = browsercmd} },
	{ 0,                         XKB_KEY_Print,       spawn,            SHCMD("grim -g \"$(slurp)\" - | swappy -f -") },
	{ MODKEY,                    XKB_KEY_c,           killclient,       {} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_v,           togglefloating,   {} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_p,           toggleprivate,    {} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_g,           togglegaps,       {} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_b,           togglebar,        {} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_t,           toggletitlebar,   {} },
	{ MODKEY,                    XKB_KEY_t,           toggletabbed,     {.v = &layouts_def[1]} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_f,           togglefullscreen, {} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_r,           reloadsettings,   {} },

	{ MODKEY,                    XKB_KEY_r,           runnertoggle,     {} },

	{ MODKEY,                                      XKB_KEY_n, notifyscroll,     {} },
	{ MODKEY|WLR_MODIFIER_SHIFT,                   XKB_KEY_n, notifyopen,       {} },
	{ MODKEY|WLR_MODIFIER_CTRL,                    XKB_KEY_n, notifynext,       {} },
	{ MODKEY|WLR_MODIFIER_ALT,                     XKB_KEY_n, notifyprev,       {} },
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT, XKB_KEY_n, notifydismissall, {} },
	{ MODKEY,                                      XKB_KEY_a, notifyactions,    {} },

	{ MODKEY,                    XKB_KEY_h,           focusstack,       {.i = -1} },
	{ MODKEY,                    XKB_KEY_j,           focusstack,       {.i = +1} },
	{ MODKEY,                    XKB_KEY_k,           focusstack,       {.i = -1} },
	{ MODKEY,                    XKB_KEY_l,           focusstack,       {.i = +1} },

	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_h,           movestack,        {.i = -1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_j,           movestack,        {.i = +1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_k,           movestack,        {.i = -1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_l,           movestack,        {.i = +1} },

	{ MODKEY|WLR_MODIFIER_CTRL, XKB_KEY_h,           resizewidth,  {.i = -50} },
	{ MODKEY|WLR_MODIFIER_CTRL, XKB_KEY_l,           resizewidth,  {.i = +50} },
	{ MODKEY|WLR_MODIFIER_CTRL, XKB_KEY_k,           resizeheight, {.i = -50} },
	{ MODKEY|WLR_MODIFIER_CTRL, XKB_KEY_j,           resizeheight, {.i = +50} },

	{ MODKEY,                                    XKB_KEY_o, setopacityfocus,   {.f = +0.05f} },
	{ MODKEY|WLR_MODIFIER_ALT,                   XKB_KEY_o, setopacityfocus,   {.f = -0.05f} },
	{ MODKEY|WLR_MODIFIER_CTRL,                  XKB_KEY_o, setopacityunfocus, {.f = +0.05f} },
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_o, setopacityunfocus, {.f = -0.05f} },
	{ MODKEY|WLR_MODIFIER_SHIFT,                 XKB_KEY_o, toggleopacity,     {} },

	{ MODKEY|WLR_MODIFIER_ALT,   XKB_KEY_equal,       magnify,          {.f = +0.25f} },
	{ MODKEY|WLR_MODIFIER_ALT,   XKB_KEY_minus,       magnify,          {.f = -0.25f} },
	{ MODKEY|WLR_MODIFIER_ALT,   XKB_KEY_0,           magnify,          {.f = 0} },

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

	{ MODKEY,                    XKB_KEY_i,           incnmaster,       {.i = +1} },
	{ MODKEY,                    XKB_KEY_d,           incnmaster,       {.i = -1} },
	{ MODKEY,                    XKB_KEY_Return,      zoom,             {} },
	{ MODKEY,                    XKB_KEY_Tab,         view,             {} },
	{ MODKEY,                    XKB_KEY_space,       setlayout,        {} },
	{ MODKEY,                    XKB_KEY_0,           view,             {.ui = ~0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_parenright,  tag,              {.ui = ~0} },
	{ MODKEY,                    XKB_KEY_comma,       focusmon,         {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,                    XKB_KEY_period,      focusmon,         {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_less,        tagmon,           {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_greater,     tagmon,           {.i = WLR_DIRECTION_RIGHT} },
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
	{ MODKEY,                    XKB_KEY_Escape,      toggleinhibit,    {} },

	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, quit, {} },
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};
Key* keys    = keys_def;
size_t nkeys = LENGTH(keys_def);

static Button buttons_def[] = {
	{ ClkLtSymbol, 0,      BTN_LEFT,   setlayout,      {.v = &layouts_def[0]} },
	{ ClkLtSymbol, 0,      BTN_RIGHT,  setlayout,      {.v = &layouts_def[1]} },

	{ ClkTitle,    0,      BTN_MIDDLE, zoom,           {} },
	{ ClkTitle,    0,      BTN_LEFT,   notifyopen,     {} },
	{ ClkTitle,    0,      BTN_RIGHT,  notifydismiss,  {} },

	{ ClkStatus,   0,      BTN_MIDDLE, spawn,          {.v = termcmd} },

	{ ClkTray,     0,      BTN_LEFT,   trayactivate,   {} },
	{ ClkTray,     0,      BTN_RIGHT,  traymenu,       {} },

	{ ClkTagBar,   0,      BTN_LEFT,   view,           {} },
	{ ClkTagBar,   0,      BTN_RIGHT,  toggleview,     {} },
	{ ClkTagBar,   MODKEY, BTN_LEFT,   tag,            {} },
	{ ClkTagBar,   MODKEY, BTN_RIGHT,  toggletag,      {} },
	{ ClkTagBar,   0,      WheelUp,    shiftview,      {.i = -1} },
	{ ClkTagBar,   0,      WheelDown,  shiftview,      {.i = +1} },

	{ ClkWinTitle, 0,      WheelUp,    focusstack,     {.i = -1} },
	{ ClkWinTitle, 0,      WheelDown,  focusstack,     {.i = +1} },

	{ ClkClient,   MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },
	{ ClkClient,   MODKEY, BTN_MIDDLE, togglefloating, {} },
	{ ClkClient,   MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },

	{ ClkClient,   MODKEY, WheelUp,    magnify,        {.f = +0.25f} },
	{ ClkClient,   MODKEY, WheelDown,  magnify,        {.f = -0.25f} },
	{ ClkRoot,     MODKEY, WheelUp,    magnify,        {.f = +0.25f} },
	{ ClkRoot,     MODKEY, WheelDown,  magnify,        {.f = -0.25f} },
};
Button* buttons = buttons_def;
size_t nbuttons = LENGTH(buttons_def);

/* a finger count used here is taken from the apps */
static Gesture gestures_def[] = {
	{ SwipeLeft,  3, shiftview, {.i = +1} },
	{ SwipeRight, 3, shiftview, {.i = -1} },
};
Gesture* gestures = gestures_def;
size_t ngestures  = LENGTH(gestures_def);

/* held while pinching on a touchpad to zoom the magnifier, 0 for off */
unsigned int pinchzoom_mod = MODKEY;

/* tablet pad buttons, counted from 0: a bound one is not sent to the app.
 * e.g. { 0, spawn, SHCMD("wtype -M ctrl z -m ctrl") } */
PadButton* padbuttons = nullptr;
size_t npadbuttons    = 0;

int log_level = WLR_ERROR;

/* let a hidden fullscreen window keep the screen awake */
int bypass_surface_visibility = 0;

/* tearing for fullscreen windows that ask for it */
int allow_tearing = 1;

/* VRR for fullscreen games and videos */
int auto_vrr = 1;
