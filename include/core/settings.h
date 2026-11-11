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
#define TAGMASK ((1u << settings->look.tag_count) - 1)

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

typedef struct {
    const char** fonts;
    size_t font_count;
    uint32_t colors[COLOR_SCHEME_COUNT][3];
    float root_color[4];
    float fullscreen_color[4];
#ifdef INTEGRATED_BACKGROUND
    const char* wallpaper;
#endif
    char** tags;
    size_t tag_count;
} LookSettings;

typedef struct {
    unsigned int border_width;
    int corner_style;
    unsigned int corner_radius;
    int gaps;
    unsigned int gap_size;
    int smart_gaps;
    int title_bar;
    unsigned int title_padding;
    int title_center;
    int title_close;
    uint32_t title_close_color;
    uint32_t private_color;
    float private_round;
    int always_private;
    uint32_t public_color;
    const char* private_title;
} WindowsSettings;

typedef struct {
    const char* text;
    const char* image;
    unsigned int image_size;
    float text_scale;
    uint32_t background;
    uint32_t foreground;
} PrivacySettings;

typedef struct {
    WindowLayout* layouts;
    size_t layout_count;
    ClientRule* client_rules;
    size_t client_rule_count;
    MonitorRule* monitor_rules;
    size_t monitor_rule_count;
} RulesSettings;

typedef struct {
    int enabled;
    int top;
    int window_title;
    int tag_dots;
    unsigned int tag_dot_size;
    unsigned int tag_dot_gap;
    float tag_dot_width;
    float tag_dot_round;
    unsigned int urgent_pulse;
    int layout_symbol;
    unsigned int box_radius;
    unsigned int padding;
    float height;
    int single_monitor;
    int status;
} BarSettings;

typedef struct {
    int enabled;
    unsigned int spacing;
    unsigned int padding;
    unsigned int icon_size;
} SystraySettings;

typedef struct {
    int enabled;
    int private;
    unsigned int timeout;
    unsigned int line_width;
    unsigned int line_radius;
    const char* action_sign;
    const char* private_text;
    int private_hide;
    int dot;
    uint32_t private_color;
    uint32_t public_color;
} NotificationsSettings;

typedef struct {
    const char* placeholder;
    unsigned int max_candidates;
    unsigned int pill_height;
    unsigned int pill_padding;
    float pill_round;
    int fuzzy;
    int icons;
    unsigned int icon_size;
    float icon_round;
    unsigned int icon_border;
    unsigned int history_size;
    const char* shell;
    const char* terminal;
    const char* terminal_prefix;
    const char* run_prefix;
    const char* calc_prefix;
    const char* search_prefix;
    const char* search_url;
    const char* window_prefix;
    const char* tag_prefix;
    int files;
    const char* paths;
    unsigned int calc_precision;
} RunnerSettings;

typedef struct {
    int enabled;
    float focus;
    float unfocus;
    float decorations;
#ifdef INTEGRATED_BACKGROUND
    int type;
    unsigned int blur_radius;
    unsigned int blur_passes;
    float blur_saturation;
    float blur_brightness;
#endif
} OpacitySettings;

typedef struct {
    int sloppy_focus;
    int move_threshold;
    int move_snap;
    struct xkb_rule_names xkb_rules;
    int repeat_rate;
    int repeat_delay;
    const char* cursor_theme;
    int cursor_size;
    int hide_cursor_when_typing;
    int tap_to_click;
    int tap_and_drag;
    int drag_lock;
    int natural_scrolling;
    int disable_while_typing;
    int left_handed;
    int middle_button_emulation;
    enum libinput_config_scroll_method scroll_method;
    enum libinput_config_click_method click_method;
    uint32_t send_events_mode;
    enum libinput_config_accel_profile accel_profile;
    double accel_speed;
    int touchpad_natural_scrolling;
    enum libinput_config_accel_profile touchpad_accel_profile;
    double touchpad_accel_speed;
    enum libinput_config_tap_button_map tap_button_map;
    const char* tablet_output;
    int tablet_left_handed;
    const char* touch_output;
} InputSettings;

typedef struct {
    const char** autostart;
    KeyBinding* key_bindings;
    size_t key_binding_count;
    MouseBinding* mouse_bindings;
    size_t mouse_binding_count;
    GestureBinding* gesture_bindings;
    unsigned int pinch_zoom_modifier;
    size_t gesture_binding_count;
    TabletPadBinding* tablet_pad_bindings;
    size_t tablet_pad_binding_count;
} BindingsSettings;

typedef struct {
    int log_level;
    int bypass_surface_visibility;
    int allow_tearing;
    int auto_vrr;
} MiscSettings;

typedef struct {
    LookSettings look;
    WindowsSettings windows;
    PrivacySettings privacy;
    RulesSettings rules;
    BarSettings bar;
    SystraySettings systray;
    NotificationsSettings notifications;
    RunnerSettings runner;
    OpacitySettings opacity;
    InputSettings input;
    BindingsSettings bindings;
    MiscSettings misc;
} Settings;

extern Settings* settings;

void settings_load(void);
[[nodiscard]] int settings_write(void);
[[nodiscard]] int settings_make_dirs(char* path);

#endif
