/* The values a fresh settings.json is written from; the file wins over them
 * once it exists. Changing anything here needs a rebuild. */

/* == 0. MACROS ============================================================ */

#define MODKEY WLR_MODIFIER_LOGO // WLR_MODIFIER_ALT to use Alt instead

/* spawn a shell command, pre dwm-5.0 fashion */
#define SHCMD(cmd)                                                             \
    {                                                                          \
        .v = (const char*[])                                                   \
        {                                                                      \
            "/bin/sh", "-c", cmd, NULL                                         \
        }                                                                      \
    }

/* view, toggleview, tag, toggletag for one tag */
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

/* switch VT; keep the CHVT() bindings in section 9 unless you know better */
#define CHVT(n)                                                                \
    {                                                                          \
        WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_XF86Switch_VT_##n, chvt, \
        {                                                                      \
            .ui = (n)                                                          \
        }                                                                      \
    }

/* == 1. LOOK ============================================================== */

static const char* fonts_def[] = { "monospace:size=10" };
const char** fonts = fonts_def;
size_t nfonts      = LENGTH(fonts_def);

/* { fg, bg, border } as 0xRRGGBBAA, indexed by the Scheme* enum in src/g0wm.c */
uint32_t colors[NumSchemes][3] = {
    /*                          fg          bg          border   */
    [SchemeNorm]          = { 0xffffffff, 0x000000ff, 0x000000ff }, // unfocused
    [SchemeSel]           = { 0xffffffff, 0x000000ff, 0x000000ff }, // focused
    [SchemeUrg]           = { 0xffffffff, 0x000000ff, 0xff0000ff }, // urgent

    [SchemeTitle]         = { 0x888888ff, 0x000000ff, 0x000000ff }, // title bar
    [SchemeTitleSel]      = { 0xffffffff, 0x000000ff, 0x000000ff }, // ... focused

    [SchemeStatus]        = { 0xffffffff, 0x000000ff, 0x000000ff }, // status text, overridden by ^c#/^b#/^d^
    [SchemeNotify]        = { 0x000000ff, 0xffffffff, 0xffffffff }, // notification

    [SchemeRunner]        = { 0xffffffff, 0x000000ff, 0x000000ff }, // MODKEY+r prompt
    [SchemeRunnerSuggest] = { 0xaaaaaaff, 0x000000ff, 0x000000ff }, // ... its completion, same bg
};

float rootcolor[]     = COLOR(0x000000ff);        // behind the windows
float fullscreen_bg[] = { 0.0f, 0.0f, 0.0f, 1.0f }; // fullscreen letterbox

/* --- INTEGRATED_BACKGROUND (--no-integrated-background: use swaybg) --- */
#ifdef INTEGRATED_BACKGROUND
const char* wallpaper = ""; // image path, empty for just rootcolor
#endif
/* --- end INTEGRATED_BACKGROUND --- */

static char* tags_def[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
char** tags  = tags_def;
size_t ntags = LENGTH(tags_def);

/* == 2. WINDOWS =========================================================== */

unsigned int borderpx = 1; // 0 for no border

/* The border carries the curve and the contents stay square, so a large
 * radius widens the border on its own: past 3.4 * borderpx, 6.2 with
 * CornerSquircle. */
int cornerstyle       = CornerNormal; // or CornerSquircle
unsigned int cornerpx = 0; // corner radius, 0 for square corners

/* gaps multiplies gappx, it is not a bool: MODKEY+g sets it to !gaps, so
 * anything above 1 collapses to 1 on the first toggle. */
int gaps                = 1; // runtime: MODKEY+g
unsigned int gappx      = 3; // gap size, times gaps
int smartgaps           = 0; // 1 drops the outer gap with one window

/* --- TITLEBAR: per-window title bars, not barwintitle --- */
#ifdef TITLEBAR
int titlebar              = 1; // runtime: MODKEY+Shift+t
unsigned int titlepadding = 6; // on top of the font height
#endif
/* --- end TITLEBAR --- */

static Layout layouts_def[] = {
    /* symbol     arrange function */
    { "[ ]", tile },    // Normal
    { "< >", NULL },    // Floating
    { "[M]", monocle }, // Mixed
    { "|||", tabbed },  // Tabbed
};
Layout* layouts = layouts_def;
size_t nlayouts = LENGTH(layouts_def);

/* Substring match on app id and title, NULL matches everything. opacity 0
 * keeps the section 4 default, monitor -1 the focused one. Fields are named so
 * a new one in Rule (src/g0wm.c) cannot silently shift the rows below. */
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

/* == 3. BAR =============================================================== */

int showbar     = 1; // 0 means no bar
int topbar      = 1; // 0 means bottom bar
int barwintitle = 0; // focused window title in the bar

/* Gap between the bar and the edges of its output, for a floating bar. The
 * layout leaves the same gap on the side the bar faces. */
unsigned int barpadding = 0; // 0 puts the bar against the edges

/* Bar height, times the automatic one (the font height plus two pixels), so
 * it holds at any dpi: 1.1 is a tenth taller, 0 leaves it alone. */
float barheight = 1.1f;

/* 1 keeps one bar on the first monitor matched by monrules[], with only the
 * catch-all row the first output that came up. It never moves, the others get
 * none, and it reports on whichever monitor is focused. */
int barsinglemon = 0;

/* --- SYSTRAY: clicks are ClkTray (10) --- */
#ifdef SYSTRAY
int showsystray              = 1;
unsigned int systrayspacing  = 2;  // gap between two icons
unsigned int systraypadding  = 2;  // gap at both ends, the right edge included
unsigned int systrayiconsize = 16; // 0 fills the bar
#endif
/* --- end SYSTRAY --- */

/* --- NOTIFICATIONS: share the bar's title box, clicks are ClkTitle (10) --- */
#ifdef NOTIFICATIONS
int shownotifications             = 1;
unsigned int notification_timeout = 5; // seconds one stays up
#endif
/* --- end NOTIFICATIONS --- */

/* == 4. OPACITY (1.0 is opaque, i.e. off) ================================= */

int opacity_enabled = 1; // runtime: MODKEY+Alt+o

float opacity_focus   = 1.00f; // rules[] overrides these per client
float opacity_unfocus = 1.00f;
float opacity_deco    = 1.00f; // the bar, title bars and borders

/* app ids matched like rules[]; an empty list means every app */
int opacity_exclusion_type = 0; // 0 only these, 1 all but these
static const char* opacity_apps_def[] = {
    NULL // Terminator
};
const char** opacity_apps = opacity_apps_def;

/* --- BLUR: the frosted glass is a copy of the wallpaper, so it needs one --- */
#ifdef INTEGRATED_BACKGROUND
/* what a transparent window shows through itself: whatever happens to be
 * behind it, or a frosted copy of the wallpaper */
int opacity_type = OpacityNormal; // OpacityNormal or OpacityBlur

unsigned int blur_radius = 15; // how far the wallpaper smears
unsigned int blur_passes = 1;  // more is rounder, 3 is plenty
float blur_saturation    = 1.60f; // 1.0 leaves the colors alone
float blur_brightness    = 1.00f; // below 1 for darker glass
#endif
/* --- end BLUR --- */

/* == 5. MONITORS ==========================================================
 * First row whose name is a substring of the output wins, so keep the
 * .name = NULL catch-all last; wlr-randr lists the names. x/y are logical
 * pixels (resolution / scale), (-1,-1) autoconfigures and other negatives
 * break Xwayland. width/height/refresh 0 means the preferred mode. */

static MonitorRule monrules_def[] = {
    { .name    = NULL, // catch-all for every monitor
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

/* == 6. INPUT ============================================================= */

int sloppyfocus = 1; // focus follows the mouse

struct xkb_rule_names xkb_rules = {
    /* also takes .rules, .model, .layout, .variant; e.g. .options = "ctrl:nocaps" */
    .options = NULL,
};

int repeat_rate  = 25;
int repeat_delay = 600;

const char* cursor_theme = NULL; // naming one is required for the size
int cursor_size    = 24;   // base size, times the monitor scale
int hide_cursor_when_typing = 1;

/* trackpad */
int tap_to_click            = 1;
int tap_and_drag            = 1;
int drag_lock               = 1;
int natural_scrolling       = 0;
int disable_while_typing    = 1;
int left_handed             = 0;
int middle_button_emulation = 0;

/* NO_SCROLL, 2FG, EDGE, ON_BUTTON_DOWN */
enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;

/* NONE, BUTTON_AREAS, CLICKFINGER */
enum libinput_config_click_method click_method =
    LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;

/* ENABLED, DISABLED, DISABLED_ON_EXTERNAL_MOUSE */
uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

/* FLAT, ADAPTIVE */
enum libinput_config_accel_profile accel_profile =
    LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
double accel_speed = 0.0;

/* LRM or LMR: 1/2/3 finger tap -> left/right/middle or left/middle/right */
enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

/* == 7. PROGRAMS ==========================================================
 * execvp() argument lists: no shell, so use SHCMD() in section 9 for pipes. */

static const char* termcmd[]        = { "foot", NULL };
static const char* filemanagercmd[] = { "thunar", NULL };
static const char* browsercmd[]     = { "firefox", NULL };

/* --- RUNNER: the launcher MODKEY+r spawns without g0wm's own prompt --- */
#ifndef RUNNER
static const char* menucmd[]        = { "wmenu-run", NULL };
#endif
/* --- end RUNNER --- */

/* == 8. AUTOSTART =========================================================
 * Started with g0wm, killed on exit. One NULL-terminated argument list each,
 * run through execvp(): no shell, so no ~, no $VAR, no globs. */

static const char* autostart_def[] = {
    /* "example", "arg1", "arg2", NULL, */
    NULL // Terminator
};
const char** autostart = autostart_def;

/* == 9. KEYS ==============================================================
 * { modifier, key, function, argument }. Shift changes key codes: 2 -> at. */

static Key keys_def[] = {
	/* --- APPLICATIONS AND SYSTEM --- */
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
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_r,           reloadsettings,   {0} }, // re-read settings.json

	/* --- RUNNER: g0wm's own prompt, or menucmd without it --- */
#ifdef RUNNER
	{ MODKEY,                    XKB_KEY_r,           runnertoggle,     {0} },
#else
	{ MODKEY,                    XKB_KEY_r,           spawn,            {.v = menucmd} },
#endif
	/* --- end RUNNER --- */

	/* --- NOTIFICATIONS: n scrolls a long one by a screenful, Shift+n opens it --- */
#ifdef NOTIFICATIONS
	{ MODKEY,                    XKB_KEY_n,           notifyscroll,     {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_n,           notifyopen,       {0} },
#endif
	/* --- end NOTIFICATIONS --- */

	/* --- FOCUS CONTROL --- */
	/* one master/stack list, not a 2D tree: h/k walk it backwards, j/l
	 * forwards. In the tabbed layout this cycles through the tabs. */
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
	/* same hjkl as focus/move, one more modifier: floating clients move
	 * their edges; tiled ones only react to h/l, which adjust the master
	 * area, as the stack splits the height evenly */
	{ MODKEY|WLR_MODIFIER_CTRL, XKB_KEY_h,           resizewidth,  {.i = -50} },
	{ MODKEY|WLR_MODIFIER_CTRL, XKB_KEY_l,           resizewidth,  {.i = +50} },
	{ MODKEY|WLR_MODIFIER_CTRL, XKB_KEY_k,           resizeheight, {.i = -50} },
	{ MODKEY|WLR_MODIFIER_CTRL, XKB_KEY_j,           resizeheight, {.i = +50} },

	/* --- OPACITY: focused window, then the same window once unfocused.
	 * shift is always the toggle group, ctrl is always the unfocused
	 * plane, alt is always the decrement --- */
	{ MODKEY,                                    XKB_KEY_o, setopacityfocus,   {.f = +0.05f} },
	{ MODKEY|WLR_MODIFIER_ALT,                   XKB_KEY_o, setopacityfocus,   {.f = -0.05f} },
	{ MODKEY|WLR_MODIFIER_CTRL,                  XKB_KEY_o, setopacityunfocus, {.f = +0.05f} },
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_o, setopacityunfocus, {.f = -0.05f} },
	{ MODKEY|WLR_MODIFIER_SHIFT,                 XKB_KEY_o, toggleopacity,     {0} }, // off everywhere

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

	/* --- g0wm defaults --- */
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

	/* --- VT SWITCHING --- */
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, quit, {0} },
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};
Key* keys    = keys_def;
size_t nkeys = LENGTH(keys_def);

/* == 10. MOUSE ============================================================
 * { where, modifier, button, function, argument }. ClkTitle is the shared bar
 * box holding the window title, notifications and the runner. */

static Button buttons_def[] = {
	{ ClkLtSymbol, 0,      BTN_LEFT,   setlayout,      {.v = &layouts_def[0]} },
	{ ClkLtSymbol, 0,      BTN_RIGHT,  setlayout,      {.v = &layouts_def[2]} },

	{ ClkTitle,    0,      BTN_MIDDLE, zoom,           {0} },
	/* --- NOTIFICATIONS: left opens, right dismisses, the wheel scrolls --- */
#ifdef NOTIFICATIONS
	{ ClkTitle,    0,      BTN_LEFT,   notifyopen,     {0} },
	{ ClkTitle,    0,      BTN_RIGHT,  notifydismiss,  {0} },
#endif
	/* --- end NOTIFICATIONS --- */

	{ ClkStatus,   0,      BTN_MIDDLE, spawn,          {.v = termcmd} },

	/* --- SYSTRAY: left activates an item, right opens its menu --- */
#ifdef SYSTRAY
	{ ClkTray,     0,      BTN_LEFT,   trayactivate,   {0} },
	{ ClkTray,     0,      BTN_RIGHT,  traymenu,       {0} },
#endif
	/* --- end SYSTRAY --- */

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

/* == 11. MISC ============================================================= */

/* WLR_SILENT, WLR_ERROR, WLR_INFO, WLR_DEBUG; -d forces WLR_DEBUG */
int log_level = WLR_ERROR; // runtime: the -d flag

/* 1 lets a hidden fullscreen client still keep the screen awake */
int bypass_surface_visibility = 0;
