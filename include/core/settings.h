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

#include "core/g0wm.h"
#include "window/layout.h"

/* 0xRRGGBBAA -> the float[4] wlroots wants (dwl issue #466) */
#define COLOR(hex)                                                                                 \
    { ((hex >> 24) & 0xFF) / 255.0f, ((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f,  \
      (hex & 0xFF) / 255.0f }
#define TAGMASK ((1u << tag_count) - 1)

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
enum { CornerNormal, CornerSquircle };                                 /* corner curve */
enum { OpacityNormal, OpacityBlur };                                   /* opacity_type */
enum { SwipeLeft, SwipeRight, SwipeUp, SwipeDown, PinchIn, PinchOut }; /* gesture motions */
enum { RunnerIconsNone, RunnerIconsSelected, RunnerIconsAll };
enum { BarStatusFocus, BarStatusAll, BarStatusMain };
enum { NotifyDotLeft, NotifyDotRight }; /* ends of the free box */
enum : unsigned int {
    ClkTagBar,
    ClkLtSymbol,
    ClkStatus,
    ClkTitle,
    ClkClient,
    ClkRoot,
    ClkTray,
    ClkWinTitle /* a window's title bar, only for the wheel */
}; /* clicks */
enum : unsigned int { WheelUp = 0x10000, WheelDown }; /* Button.button for a wheel notch */

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
extern size_t font_count;
extern uint32_t colors[NumSchemes][3];
extern float root_color[4];
extern float fullscreen_color[4];
#ifdef INTEGRATED_BACKGROUND
extern const char* wallpaper;
#endif
extern char** tags;
extern size_t tag_count;
extern unsigned int border_width;
extern int corner_style;
extern unsigned int corner_radius;
extern int gaps;
extern unsigned int gap_size;
extern int smart_gaps;
extern int title_bar;
extern unsigned int title_padding;
extern int title_center;
extern int title_close;
extern uint32_t title_close_color;
extern uint32_t private_color;
extern float private_round;
extern int always_private;
extern uint32_t public_color;
extern const char* private_title;
extern Layout* layouts;
extern size_t layout_count;
extern Rule* rules;
extern size_t rule_count;
extern int bar_enabled;
extern int bar_top;
extern int bar_window_title;
extern int bar_tag_dots;
extern unsigned int bar_tag_dot_size;
extern unsigned int bar_tag_dot_gap;
extern float bar_tag_dot_width;
extern float bar_tag_dot_round;
extern unsigned int bar_urgent_pulse;
extern int bar_layout_symbol;
extern unsigned int bar_box_radius;
extern unsigned int bar_padding;
extern float bar_height;
extern int bar_single_monitor;
extern int bar_status;
extern int systray_enabled;
extern unsigned int systray_spacing;
extern unsigned int systray_padding;
extern unsigned int systray_icon_size;
extern int notifications_enabled;
extern int private_notifications;
extern unsigned int notification_timeout;
extern unsigned int notification_line_width;
extern unsigned int notification_line_radius;
extern const char* notification_action_sign;
extern const char* notification_private_text;
extern int notification_dot;
extern uint32_t notification_private_color;
extern uint32_t notification_public_color;
extern const char* runner_placeholder;
extern unsigned int runner_max_candidates;
extern unsigned int runner_pill_height;
extern unsigned int runner_pill_padding;
extern float runner_pill_round;
extern int runner_fuzzy;
extern int runner_icons;
extern unsigned int runner_icon_size;
extern float runner_icon_round;
extern unsigned int runner_icon_border;
extern unsigned int runner_history_size;
extern const char* runner_shell;
extern const char* runner_terminal;
extern const char* runner_terminal_prefix;
extern const char* runner_run_prefix;
extern const char* runner_calc_prefix;
extern const char* runner_search_prefix;
extern const char* runner_search_url;
extern const char* runner_window_prefix;
extern const char* runner_tag_prefix;
extern int runner_files;
extern const char* runner_paths;
extern unsigned int runner_calc_precision;
extern int opacity_enabled;
extern float opacity_focus;
extern float opacity_unfocus;
extern float opacity_decorations;
#ifdef INTEGRATED_BACKGROUND
extern int opacity_type;
extern unsigned int blur_radius;
extern unsigned int blur_passes;
extern float blur_saturation;
extern float blur_brightness;
#endif
extern MonitorRule* monitor_rules;
extern size_t monitor_rule_count;
extern int sloppy_focus;
extern int move_threshold;
extern int move_snap;
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
extern enum libinput_config_tap_button_map tap_button_map;
extern const char* tablet_output;
extern int tablet_left_handed;
extern const char* touch_output;
extern const char** autostart;
extern Key* keys;
extern size_t key_count;
extern Button* buttons;
extern size_t button_count;
extern Gesture* gestures;
extern unsigned int pinch_zoom_modifier;
extern size_t gesture_count;
extern PadButton* pad_buttons;
extern size_t pad_button_count;
extern int log_level;
extern int bypass_surface_visibility;
extern int allow_tearing;
extern int auto_vrr;

void settings_load(void);
[[nodiscard]] int settings_write(void);
[[nodiscard]] int settings_make_dirs(char* path);

#endif
