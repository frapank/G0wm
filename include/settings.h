/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_SETTINGS_H
#define G0WM_SETTINGS_H

#include <libinput.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include "g0wm.h"
#include "layout.h"

/* 0xRRGGBBAA -> the float[4] wlroots wants (dwl issue #466) */
#define COLOR(hex)                                                             \
    { ((hex >> 24) & 0xFF) / 255.0f,                                           \
      ((hex >> 16) & 0xFF) / 255.0f,                                           \
      ((hex >> 8) & 0xFF) / 255.0f,                                            \
      (hex & 0xFF) / 255.0f }
#define TAGMASK ((1u << ntags) - 1)

enum {
    SchemeNorm,
    SchemeSel,
    SchemeUrg,
    SchemeTitle,
    SchemeTitleSel,
    SchemeStatus,
    SchemeNotify,
    SchemeNotifyLow,
    SchemeNotifyCrit,
    SchemeRunner,
    SchemeRunnerSuggest,
    SchemeRunnerSel,
    SchemeRunnerIcon,
    NumSchemes
}; /* color schemes */
enum { CornerNormal, CornerSquircle }; /* corner curve */
enum { OpacityNormal, OpacityBlur };   /* opacity_type */
enum {
    SwipeLeft,
    SwipeRight,
    SwipeUp,
    SwipeDown,
    PinchIn,
    PinchOut
}; /* gesture motions */
enum { RunnerIconsNone, RunnerIconsSelected, RunnerIconsAll };
enum { BarStatusFocus, BarStatusAll, BarStatusMain };
enum {
    ClkTagBar,
    ClkLtSymbol,
    ClkStatus,
    ClkTitle,
    ClkClient,
    ClkRoot,
    ClkTray,
    ClkWinTitle /* a window's title bar, only for the wheel */
}; /* clicks */
enum { WheelUp = 0x10000, WheelDown }; /* Button.button for a wheel notch */

typedef struct {
    unsigned int click;
    unsigned int mod;
    unsigned int button;
    void (*func)(const Arg*);
    Arg arg;
} Button;

typedef struct {
    unsigned int motion;
    unsigned int fingers;
    void (*func)(const Arg*);
    Arg arg;
} Gesture;

typedef struct {
    unsigned int button; /* counted from 0, as libinput does */
    void (*func)(const Arg*);
    Arg arg;
} PadButton;

typedef struct {
    uint32_t mod;
    xkb_keysym_t keysym;
    void (*func)(const Arg*);
    Arg arg;
} Key;

typedef struct {
    const char* name;
    float mfact;
    int nmaster;
    float scale;
    const Layout* lt;
    enum wl_output_transform rr;
    int x, y;
    int width, height; /* 0,0 means use the preferred mode */
    int refresh;       /* Hz, ignored when width/height are 0 */
} MonitorRule;

typedef struct {
    const char* id;
    const char* title;
    uint32_t tags;
    int isfloating;
    int isprivate;
    float opacity_focus;   /* 0 keeps the default from config.h */
    float opacity_unfocus; /* 0 keeps the default from config.h */
    int monitor;
} Rule;

extern const char** fonts;
extern size_t nfonts;
extern uint32_t colors[NumSchemes][3];
extern float rootcolor[4];
extern float fullscreen_bg[4];
#ifdef INTEGRATED_BACKGROUND
extern const char* wallpaper;
#endif
extern char** tags;
extern size_t ntags;
extern unsigned int borderpx;
extern int cornerstyle;
extern unsigned int cornerpx;
extern int gaps;
extern unsigned int gappx;
extern int smartgaps;
extern int titlebar;
extern unsigned int titlepadding;
extern int titlecenter;
extern int titleclose;
extern uint32_t titleclosecolor;
extern uint32_t privatecolor;
extern float privateround;
extern int alwaysprivate;
extern uint32_t publiccolor;
extern const char* privatetitle;
extern Layout* layouts;
extern size_t nlayouts;
extern Rule* rules;
extern size_t nrules;
extern int showbar;
extern int topbar;
extern int barwintitle;
extern int bartagdots;
extern unsigned int bartagdotsize;
extern unsigned int bartagdotgap;
extern float bartagdotwidth;
extern float bartagdotround;
extern unsigned int barurgentpulse;
extern int barltsymbol;
extern unsigned int barboxradius;
extern unsigned int barpadding;
extern float barheight;
extern int barsinglemon;
extern int barstatus;
extern int showsystray;
extern unsigned int systrayspacing;
extern unsigned int systraypadding;
extern unsigned int systrayiconsize;
extern int shownotifications;
extern unsigned int notification_timeout;
extern unsigned int notification_linewidth;
extern unsigned int notification_lineradius;
extern const char* notification_actionsign;
extern const char* runner_placeholder;
extern unsigned int runner_max_candidates;
extern unsigned int runner_pillheight;
extern unsigned int runner_pillpadding;
extern float runner_pillround;
extern int runner_fuzzy;
extern int runner_icons;
extern unsigned int runner_iconsize;
extern float runner_iconround;
extern unsigned int runner_iconborder;
extern unsigned int runner_history_size;
extern const char* runner_shell;
extern const char* runner_terminal;
extern const char* runner_termprefix;
extern const char* runner_runprefix;
extern const char* runner_calcprefix;
extern const char* runner_searchprefix;
extern const char* runner_searchurl;
extern const char* runner_windowprefix;
extern const char* runner_tagprefix;
extern int runner_files;
extern const char* runner_paths;
extern unsigned int runner_calc_precision;
extern int opacity_enabled;
extern float opacity_focus;
extern float opacity_unfocus;
extern float opacity_deco;
#ifdef INTEGRATED_BACKGROUND
extern int opacity_type;
extern unsigned int blur_radius;
extern unsigned int blur_passes;
extern float blur_saturation;
extern float blur_brightness;
#endif
extern MonitorRule* monrules;
extern size_t nmonrules;
extern int sloppyfocus;
extern int movethreshold;
extern int movesnap;
extern struct xkb_rule_names xkb_rules;
extern int repeat_rate;
extern int repeat_delay;
extern const char* cursor_theme;
extern int cursor_size;
extern int hide_cursor_when_typing;
extern int tap_to_click;
extern int tap_and_drag;
extern int drag_lock;
extern int natural_scrolling;
extern int disable_while_typing;
extern int left_handed;
extern int middle_button_emulation;
extern enum libinput_config_scroll_method scroll_method;
extern enum libinput_config_click_method click_method;
extern uint32_t send_events_mode;
extern enum libinput_config_accel_profile accel_profile;
extern double accel_speed;
extern int touchpad_natural_scrolling;
extern enum libinput_config_accel_profile touchpad_accel_profile;
extern double touchpad_accel_speed;
extern enum libinput_config_tap_button_map button_map;
extern const char* tablet_output;
extern int tablet_left_handed;
extern const char* touch_output;
extern const char** autostart;
extern Key* keys;
extern size_t nkeys;
extern Button* buttons;
extern size_t nbuttons;
extern Gesture* gestures;
extern unsigned int pinchzoom_mod;
extern size_t ngestures;
extern PadButton* padbuttons;
extern size_t npadbuttons;
extern int log_level;
extern int bypass_surface_visibility;
extern int allow_tearing;
extern int auto_vrr;

void settingsload(void);
int settingswrite(void);
int mkdirp(char* path);

#endif
