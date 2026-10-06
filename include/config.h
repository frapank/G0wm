/* Default values, used to write a new settings.json. Once the file exists
 * it is the one that counts. */

#define MODKEY WLR_MODIFIER_LOGO

#define SHCMD(cmd)                                                             \
    {                                                                          \
        .v = (const char*[])                                                   \
        {                                                                      \
            "/bin/sh", "-c", cmd, NULL                                         \
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

#ifdef TITLEBAR
int titlebar              = 1;
unsigned int titlepadding = 6;
#endif

static Layout layouts_def[] = {
    { "[ ]", tile },
    { "< >", NULL },
    { "[M]", monocle },
    { "|||", tabbed },
};
Layout* layouts = layouts_def;
size_t nlayouts = LENGTH(layouts_def);

/* opacity 0 keeps the default, monitor -1 is the focused one */
static Rule rules_def[] = {
    { .id              = "Placeholder",
      .title           = NULL,
      .tags            = 0,
      .isfloating      = 1,
      .opacity_focus   = 0,
      .opacity_unfocus = 0,
      .monitor         = -1 },
};
Rule* rules   = rules_def;
size_t nrules = LENGTH(rules_def);

int showbar     = 1;
int topbar      = 1;
int barwintitle = 0;

int bartagdots  = 1; // dots instead of the tag names

unsigned int barboxradius = 2;
unsigned int barpadding   = 0;
float barheight           = 1.1f; // times the font height, 0 for automatic

/* one bar only, on the first monitor */
int barsinglemon = 0;

#ifdef SYSTRAY
int showsystray              = 1;
unsigned int systrayspacing  = 2;
unsigned int systraypadding  = 2;
unsigned int systrayiconsize = 16; // 0 fills the bar
#endif

#ifdef NOTIFICATIONS
int shownotifications             = 1;
unsigned int notification_timeout = 5; // seconds

unsigned int notification_linewidth  = 4;
unsigned int notification_lineradius = 2;

const char* notification_actionsign = "[r]";
#endif

int opacity_enabled = 1;

float opacity_focus   = 1.00f;
float opacity_unfocus = 1.00f;
float opacity_deco    = 1.00f; // bar, title bars and borders

/* empty means every app */
int opacity_exclusion_type = 0; // 0 only these, 1 all but these
static const char* opacity_apps_def[] = {
    NULL
};
const char** opacity_apps = opacity_apps_def;

#ifdef INTEGRATED_BACKGROUND
int opacity_type = OpacityNormal; // or OpacityBlur

unsigned int blur_radius = 15;
unsigned int blur_passes = 1;
float blur_saturation    = 1.60f;
float blur_brightness    = 1.00f;
#endif

/* the first matching name wins, keep the NULL one last */
static MonitorRule monrules_def[] = {
    { .name    = NULL,
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

struct xkb_rule_names xkb_rules = {
    .options = NULL,
};

int repeat_rate  = 25;
int repeat_delay = 600;

const char* cursor_theme = NULL;
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

enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

static const char* termcmd[]        = { "foot", NULL };
static const char* filemanagercmd[] = { "thunar", NULL };
static const char* browsercmd[]     = { "firefox", NULL };

#ifndef RUNNER
static const char* menucmd[]        = { "wmenu-run", NULL };
#endif

/* run without a shell */
static const char* autostart_def[] = {
    /* "example", "arg1", "arg2", NULL, */
    NULL
};
const char** autostart = autostart_def;

static Key keys_def[] = {
	{ MODKEY,                    XKB_KEY_q,           spawn,            {.v = termcmd} },
	{ MODKEY,                    XKB_KEY_f,           spawn,            {.v = filemanagercmd} },
	{ MODKEY,                    XKB_KEY_b,           spawn,            {.v = browsercmd} },
	{ 0,                         XKB_KEY_Print,       spawn,            SHCMD("grim -g \"$(slurp)\" - | swappy -f -") },
	{ MODKEY,                    XKB_KEY_c,           killclient,       {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_v,           togglefloating,   {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_g,           togglegaps,       {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_b,           togglebar,        {0} },
#ifdef TITLEBAR
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_t,           toggletitlebar,   {0} },
#endif
	{ MODKEY,                    XKB_KEY_t,           toggletabbed,     {.v = &layouts_def[3]} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_f,           togglefullscreen, {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_r,           reloadsettings,   {0} },

#ifdef RUNNER
	{ MODKEY,                    XKB_KEY_r,           runnertoggle,     {0} },
#else
	{ MODKEY,                    XKB_KEY_r,           spawn,            {.v = menucmd} },
#endif

#ifdef NOTIFICATIONS
	{ MODKEY,                                      XKB_KEY_n, notifyscroll,     {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT,                   XKB_KEY_n, notifyopen,       {0} },
	{ MODKEY|WLR_MODIFIER_CTRL,                    XKB_KEY_n, notifynext,       {0} },
	{ MODKEY|WLR_MODIFIER_ALT,                     XKB_KEY_n, notifyprev,       {0} },
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT, XKB_KEY_n, notifydismissall, {0} },
	{ MODKEY,                                      XKB_KEY_a, notifyactions,    {0} },
#endif

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
	{ MODKEY|WLR_MODIFIER_SHIFT,                 XKB_KEY_o, toggleopacity,     {0} },

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
	{ MODKEY,                    XKB_KEY_Return,      zoom,             {0} },
	{ MODKEY,                    XKB_KEY_Tab,         view,             {0} },
	{ MODKEY,                    XKB_KEY_m,           setlayout,        {.v = &layouts_def[2]} },
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
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Escape,      quit,             {0} },

	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, quit, {0} },
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};
Key* keys    = keys_def;
size_t nkeys = LENGTH(keys_def);

static Button buttons_def[] = {
	{ ClkLtSymbol, 0,      BTN_LEFT,   setlayout,      {.v = &layouts_def[0]} },
	{ ClkLtSymbol, 0,      BTN_RIGHT,  setlayout,      {.v = &layouts_def[2]} },

	{ ClkTitle,    0,      BTN_MIDDLE, zoom,           {0} },
#ifdef NOTIFICATIONS
	{ ClkTitle,    0,      BTN_LEFT,   notifyopen,     {0} },
	{ ClkTitle,    0,      BTN_RIGHT,  notifydismiss,  {0} },
#endif

	{ ClkStatus,   0,      BTN_MIDDLE, spawn,          {.v = termcmd} },

#ifdef SYSTRAY
	{ ClkTray,     0,      BTN_LEFT,   trayactivate,   {0} },
	{ ClkTray,     0,      BTN_RIGHT,  traymenu,       {0} },
#endif

	{ ClkTagBar,   0,      BTN_LEFT,   view,           {0} },
	{ ClkTagBar,   0,      BTN_RIGHT,  toggleview,     {0} },
	{ ClkTagBar,   MODKEY, BTN_LEFT,   tag,            {0} },
	{ ClkTagBar,   MODKEY, BTN_RIGHT,  toggletag,      {0} },

	{ ClkClient,   MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },
	{ ClkClient,   MODKEY, BTN_MIDDLE, togglefloating, {0} },
	{ ClkClient,   MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },
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

int log_level = WLR_ERROR;

/* let a hidden fullscreen window keep the screen awake */
int bypass_surface_visibility = 0;
