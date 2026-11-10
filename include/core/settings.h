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

enum ColorScheme {
    COLOR_SCHEME_NORMAL,
    COLOR_SCHEME_SELECTED,
    COLOR_SCHEME_URGENT,
    COLOR_SCHEME_TITLE,
    COLOR_SCHEME_TITLE_SELECTED,
    COLOR_SCHEME_STATUS,
    COLOR_SCHEME_NOTIFICATION,
    COLOR_SCHEME_NOTIFICATION_LOW,
    COLOR_SCHEME_NOTIFICATION_CRITICAL,
    COLOR_SCHEME_RUNNER,
    COLOR_SCHEME_RUNNER_SUGGESTION,
    COLOR_SCHEME_RUNNER_SELECTED,
    COLOR_SCHEME_RUNNER_ICON,
    COLOR_SCHEME_COUNT
}; /* color schemes */
enum CornerStyle { CORNER_STYLE_NORMAL, CORNER_STYLE_SQUIRCLE }; /* corner curve */
enum OpacityMode { OPACITY_MODE_NORMAL, OPACITY_MODE_BLUR };     /* opacity_type */
enum GestureDirection {
    GESTURE_SWIPE_LEFT,
    GESTURE_SWIPE_RIGHT,
    GESTURE_SWIPE_UP,
    GESTURE_SWIPE_DOWN,
    GESTURE_PINCH_IN,
    GESTURE_PINCH_OUT
}; /* gesture motions */
enum RunnerIconMode { RUNNER_ICONS_NONE, RUNNER_ICONS_SELECTED, RUNNER_ICONS_ALL };
enum BarStatusMode { BAR_STATUS_FOCUSED, BAR_STATUS_ALL, BAR_STATUS_MAIN };
enum NotificationDotPosition {
    NOTIFICATION_DOT_LEFT,
    NOTIFICATION_DOT_RIGHT
}; /* ends of the free box */
enum ClickArea : unsigned int {
    CLICK_AREA_TAGS,
    CLICK_AREA_LAYOUT_SYMBOL,
    CLICK_AREA_STATUS,
    CLICK_AREA_BAR_TITLE,
    CLICK_AREA_CLIENT,
    CLICK_AREA_ROOT,
    CLICK_AREA_TRAY,
    CLICK_AREA_WINDOW_TITLE /* a window's title bar, only for the wheel */
}; /* clicks */
enum MouseWheelButton : unsigned int {
    MOUSE_WHEEL_UP = 0x10000,
    MOUSE_WHEEL_DOWN
}; /* MouseBinding.button for a wheel notch */

typedef struct {
    unsigned int click_area;
    unsigned int modifiers;
    unsigned int button;
    void (*callback)(const ActionArg*);
    ActionArg argument;
} MouseBinding;

typedef struct {
    unsigned int direction;
    unsigned int finger_count;
    void (*callback)(const ActionArg*);
    ActionArg argument;
} GestureBinding;

typedef struct {
    unsigned int button; /* counted from 0, as libinput does */
    void (*callback)(const ActionArg*);
    ActionArg argument;
} TabletPadBinding;

typedef struct {
    uint32_t modifiers;
    xkb_keysym_t keysym;
    void (*callback)(const ActionArg*);
    ActionArg argument;
} KeyBinding;

typedef struct {
    const char* name;
    float master_factor;
    int master_count;
    float scale;
    const WindowLayout* layout;
    enum wl_output_transform transform;
    int x, y;
    int width, height; /* 0,0 means use the preferred mode */
    int refresh_hz;    /* Hz, ignored when width/height are 0 */
} MonitorRule;

typedef struct {
    const char* app_id;
    const char* title;
    uint32_t tags;
    int is_floating;
    int is_private;
    const char* private_text;
    uint32_t private_background;
    uint32_t private_foreground;
    float focused_opacity;   /* 0 keeps the default from config.h */
    float unfocused_opacity; /* 0 keeps the default from config.h */
    int monitor_index;
} ClientRule;

extern const char** fonts;
extern size_t font_count;
extern uint32_t colors[COLOR_SCHEME_COUNT][3];
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
extern const char* private_text;
extern const char* private_image;
extern unsigned int private_image_size;
extern float private_text_scale;
extern uint32_t private_background;
extern uint32_t private_foreground;
extern WindowLayout* layouts;
extern size_t layout_count;
extern ClientRule* client_rules;
extern size_t client_rule_count;
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
extern int notification_private_hide;
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
extern KeyBinding* key_bindings;
extern size_t key_binding_count;
extern MouseBinding* mouse_bindings;
extern size_t mouse_binding_count;
extern GestureBinding* gesture_bindings;
extern unsigned int pinch_zoom_modifier;
extern size_t gesture_binding_count;
extern TabletPadBinding* tablet_pad_bindings;
extern size_t tablet_pad_binding_count;
extern int log_level;
extern int bypass_surface_visibility;
extern int allow_tearing;
extern int auto_vrr;

void settings_load(void);
[[nodiscard]] int settings_write(void);
[[nodiscard]] int settings_make_dirs(char* path);

#endif
