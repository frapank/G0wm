/* Taken from https://github.com/djpohly/dwl/issues/466 */
#define COLOR(hex)                                                             \
    { ((hex >> 24) & 0xFF) / 255.0f,                                           \
      ((hex >> 16) & 0xFF) / 255.0f,                                           \
      ((hex >> 8) & 0xFF) / 255.0f,                                            \
      (hex & 0xFF) / 255.0f }

/* appearance */
static const int sloppyfocus               = 1; // focus follows mouse
static const int bypass_surface_visibility = 0; // 1 = idle inhibitors disabled
static const int smartgaps                 = 0; // 1 means no outer gap when there is only one window
static int gaps                            = 1; // 1 means gaps between windows are added
static const unsigned int gappx            = 3; // gap pixel between windows
static const unsigned int borderpx         = 1; // border pixel of windows
static const int showbar                   = 1; // 0 means no bar
static const int topbar                    = 1; // 0 means bottom bar

static const char* cursor_theme = NULL;         // required for cursor_size
static const int cursor_size    = 24;           // xcursor base size, default is 24

static const char* fonts[] = { "monospace:size=10" };
static const float rootcolor[] = COLOR(0x000000ff);

static const float fullscreen_bg[] = { 0.0f, 0.0f, 0.0f, 1.0f };
static const int showsystray                   = 1;  // 0 means no systray
static const unsigned int systrayspacing       = 2;  // systray icon spacing
static const unsigned int systrayiconsize      = 16; // icon size, 0 fills the bar
static const int titlebar                      = 1;  // 0 means no per-window title bar

static const int barwintitle                   = 0;  // Show focused window
static const unsigned int titlepadding         = 6;  // title bar height on top of the font height
static const int shownotifications             = 1;  // 0 means no bar notifications (see README)
static const unsigned int notification_timeout = 5;  // seconds a notification stays in the bar

static uint32_t colors[][3] = {
    /*                   fg          bg        border   */
    [SchemeNorm] = { 0xffffffff, 0x000000ff, 0x000000ff },
    [SchemeSel]  = { 0xffffffff, 0x000000ff, 0x000000ff },
    [SchemeUrg]  = { 0xffffffff, 0x000000ff, 0xff0000ff },

    [SchemeTitle]    = { 0x888888ff, 0x000000ff, 0x000000ff },
    [SchemeTitleSel] = { 0xffffffff, 0x000000ff, 0x000000ff },

    [SchemeNotify] = { 0x000000ff, 0xffffffff, 0xffffffff },
};

/* tagging */
static char* tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

/* logging */
static int log_level = WLR_ERROR;

/* auto-start */
static const char* const autostart[] = {
    /* "swaybg", "-i", "/path/to/your/image", "-m", "fill", NULL, */
    NULL // Terminator
};

static const Rule rules[] = {
    /* app_id             title       tags mask     isfloating   monitor */
    { "Placeholder", NULL, 0, 1, -1 },
};

/* layout(s) */
static const Layout layouts[] = {
    /* symbol     arrange function */
    { "[ ]", tile },    // Normal
    { "< >", NULL },    // Floating
    { "[M]", monocle }, // Mixed
    { "|||", tabbed },  // Tabbed
};

/* monitors */
/* (x=-1, y=-1) is reserved as an "autoconfigure" monitor position indicator
 * WARNING: negative values other than (-1, -1) cause problems with Xwayland
 * clients
 */
static const MonitorRule monrules[] = {
    /* name  mfact  nmaster  scale  layout       rotate/reflect              x   y   width height refresh */
    { NULL,  0.55f, 1,       1,     &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL, -1, -1, 0,    0,     0 },
};

/* keyboard */
static const struct xkb_rule_names xkb_rules = {
    /* can specify fields: rules, model, layout, variant, options 
     * example: .options = "ctrl:nocaps",
     */
    .options = NULL,
};

static const int repeat_rate = 25;
static const int repeat_delay = 600;

/* Trackpad */
static const int tap_to_click            = 1;
static const int tap_and_drag            = 1;
static const int drag_lock               = 1;
static const int natural_scrolling       = 0;
static const int disable_while_typing    = 1;
static const int left_handed             = 0;
static const int middle_button_emulation = 0;

/* You can choose between:
 * LIBINPUT_CONFIG_SCROLL_NO_SCROLL
 * LIBINPUT_CONFIG_SCROLL_2FG
 * LIBINPUT_CONFIG_SCROLL_EDGE
 * LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN
 */
static const enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;

/* You can choose between:
 * LIBINPUT_CONFIG_CLICK_METHOD_NONE
 * LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS
 * LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER
 */
static const enum libinput_config_click_method click_method =
    LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;

/* You can choose between:
 * LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
 * LIBINPUT_CONFIG_SEND_EVENTS_DISABLED
 * LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE
 */
static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

/* You can choose between:
 * LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
 * LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
 */
static const enum libinput_config_accel_profile accel_profile =
    LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static const double accel_speed = 0.0;

/* You can choose between:
 * LIBINPUT_CONFIG_TAP_MAP_LRM -- 1/2/3 finger tap maps to left/right/middle
 * LIBINPUT_CONFIG_TAP_MAP_LMR -- 1/2/3 finger tap maps to left/middle/right
 */
static const enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

static const int hide_cursor_when_typing = 1;

/* If you want to use Alt for MODKEY instead, use WLR_MODIFIER_ALT */
#define MODKEY WLR_MODIFIER_LOGO

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

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd)                                                             \
    {                                                                          \
        .v = (const char*[])                                                   \
        {                                                                      \
            "/bin/sh", "-c", cmd, NULL                                         \
        }                                                                      \
    }

/* commands: replace with whatever terminal/launcher/file manager/browser you
 * have installed */
static const char* dmenucmd[]       = { "wmenu", NULL };
static const char* termcmd[]        = { "foot", NULL };
static const char* menucmd[]        = { "wmenu-run", NULL };
static const char* filemanagercmd[] = { "xterm", "-e", "ranger", NULL };
static const char* browsercmd[]     = { "firefox", NULL };

static const Key keys[] = {
	/* Note that Shift changes certain key codes: 2 -> at, etc. */
	/* modifier                  key                  function          argument */

	/* --- APPLICATIONS AND SYSTEM --- */
	{ MODKEY,                    XKB_KEY_q,           spawn,            {.v = termcmd} },
	{ MODKEY,                    XKB_KEY_f,           spawn,            {.v = filemanagercmd} },
	{ MODKEY,                    XKB_KEY_r,           spawn,            {.v = menucmd} },
	{ MODKEY,                    XKB_KEY_b,           spawn,            {.v = browsercmd} },
	{ MODKEY,                    XKB_KEY_c,           killclient,       {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_f,           togglefullscreen, {0} },
	{ MODKEY,                    XKB_KEY_v,           togglefloating,   {0} },
	{ MODKEY,                    XKB_KEY_g,           togglegaps,       {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_b,           togglebar,        {0} },
	{ 0,                         XKB_KEY_Print,       spawn,            SHCMD("grim -g \"$(slurp)\" - | swappy -f -") },
	{ MODKEY,                    XKB_KEY_t,           toggletabbed,     {.v = &layouts[3]} },
	{ MODKEY,                    XKB_KEY_e,           togglefullscreen, {0} },

	/* --- FOCUS CONTROL --- */
	/* dwl has one master/stack list, not a 2D tree: h/k walk it backwards and
	 * j/l forwards. In the tabbed layout this cycles through the tabs. */
	{ MODKEY,                    XKB_KEY_h,           focusstack,       {.i = -1} },
	{ MODKEY,                    XKB_KEY_j,           focusstack,       {.i = +1} },
	{ MODKEY,                    XKB_KEY_k,           focusstack,       {.i = -1} },
	{ MODKEY,                    XKB_KEY_l,           focusstack,       {.i = +1} },

	/* --- MOVE WINDOW POSITION --- */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_h,           movestack,        {.i = -1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_j,           movestack,        {.i = +1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_k,           movestack,        {.i = -1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_l,           movestack,        {.i = +1} },

	/* --- RESIZE --- */
	/* Floating clients move their edges; tiled ones only react to h/l, which
	 * adjust the master area, as the stack splits the height evenly. */
	{ MODKEY|WLR_MODIFIER_SHIFT|WLR_MODIFIER_ALT, XKB_KEY_h, resizewidth,  {.i = -50} },
	{ MODKEY|WLR_MODIFIER_SHIFT|WLR_MODIFIER_ALT, XKB_KEY_l, resizewidth,  {.i = +50} },
	{ MODKEY|WLR_MODIFIER_SHIFT|WLR_MODIFIER_ALT, XKB_KEY_k, resizeheight, {.i = -50} },
	{ MODKEY|WLR_MODIFIER_SHIFT|WLR_MODIFIER_ALT, XKB_KEY_j, resizeheight, {.i = +50} },

	{ MODKEY|WLR_MODIFIER_CTRL,  XKB_KEY_h,           setmfact,         {.f = -0.05f} },
	{ MODKEY|WLR_MODIFIER_CTRL,  XKB_KEY_l,           setmfact,         {.f = +0.05f} },

	/* --- MEDIA CONTROLS --- */
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

	/* --- dwl defaults --- */
	{ MODKEY,                    XKB_KEY_i,           incnmaster,       {.i = +1} },
	{ MODKEY,                    XKB_KEY_d,           incnmaster,       {.i = -1} },
	{ MODKEY,                    XKB_KEY_Return,      zoom,             {0} },
	{ MODKEY,                    XKB_KEY_Tab,         view,             {0} },
	{ MODKEY,                    XKB_KEY_m,           setlayout,        {.v = &layouts[2]} },
	{ MODKEY,                    XKB_KEY_space,       setlayout,        {0} },
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
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_q,           quit,             {0} },

	/* Ctrl-Alt-Backspace and Ctrl-Alt-Fx used to be handled by X server */
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, quit, {0} },
	/* Ctrl-Alt-Fx is used to switch to another VT, if you don't know what a VT is
	 * do not remove them.
	 */
#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_XF86Switch_VT_##n, chvt, {.ui = (n)} }
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};

static const Button buttons[] = {
	{ ClkLtSymbol, 0,      BTN_LEFT,   setlayout,      {.v = &layouts[0]} },
	{ ClkLtSymbol, 0,      BTN_RIGHT,  setlayout,      {.v = &layouts[2]} },
	{ ClkTitle,    0,      BTN_MIDDLE, zoom,           {0} },
	{ ClkTitle,    0,      BTN_LEFT,   notifyclick,    {0} },
	{ ClkStatus,   0,      BTN_MIDDLE, spawn,          {.v = termcmd} },
	{ ClkClient,   MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },
	{ ClkClient,   MODKEY, BTN_MIDDLE, togglefloating, {0} },
	{ ClkClient,   MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },
	{ ClkTray,     0,      BTN_LEFT,   trayactivate,   {0} },
	{ ClkTray,     0,      BTN_RIGHT,  traymenu,       {0} },
	{ ClkTagBar,   0,      BTN_LEFT,   view,           {0} },
	{ ClkTagBar,   0,      BTN_RIGHT,  toggleview,     {0} },
	{ ClkTagBar,   MODKEY, BTN_LEFT,   tag,            {0} },
	{ ClkTagBar,   MODKEY, BTN_RIGHT,  toggletag,      {0} },
}
