/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the bar with its tags, layout symbol and window title
 */

#include <drwl.h>
#include <math.h>
#include <pixman.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/bar/bar.h"
#include "desktop/bar/status.h"
#include "desktop/notify/notify.h"
#include "desktop/runner/calc.h"
#include "desktop/runner/runner.h"
#include "desktop/systray/icon.h"
#include "desktop/systray/tray.h"
#include "internal.h"
#include "output/monitor.h"
#include "output/privacy.h"
#include "render/buffer.h"
#include "render/corner.h"
#include "render/opacity.h"
#include "window/client.h"

constexpr int PULSE_FRAME_MS = 40;

/* variables */
char bar_status_text[STATUS_MAX];
/* redraws the bars while an urgent dot pulses */
static struct wl_event_source* bar_pulse_timer;
struct PillMask {
    int width, height;
    double radius;
    bool hollow;
    pixman_image_t* image;
};
static struct PillMask pill_masks[16];
static size_t pill_mask_next;

static void bar_draw_contents(Monitor* monitor, bool draw_titles);

/* function implementations */
bool bar_accepts_input(struct wlr_scene_buffer*, double*, double*)
{
    return true;
}

/* The first enabled output matched by rules.monitor_rules, NULL if there is none.
 */
Monitor* bar_main_monitor(void)
{
    for (const MonitorRule* rule = settings->rules.monitor_rules;
         rule < settings->rules.monitor_rules + settings->rules.monitor_rule_count; rule++) {
        Monitor* monitor;

        /* monitors grows at its head, so backwards is the order the outputs
         * showed up in: under the catch-all row the oldest one wins */
        wl_list_for_each_reverse(monitor, &monitors, link) {
            if (monitor->wlr_output->enabled &&
                (!rule->name || strstr(monitor->wlr_output->name, rule->name)))
                return monitor;
        }
    }
    return nullptr;
}

/* The monitor the bar is drawn on: the main one with bar.single_monitor, the
 * focused one without it. */
Monitor* bar_monitor(void)
{
    return settings->bar.single_monitor ? bar_main_monitor() : selected_monitor;
}

/* Whether monitor carries a bar. bar.single_monitor leaves every other monitor
 * without one, and monitor_arrange_layers() reads that off the scene node to
 * free the height. */
static bool bar_visible(const Monitor* monitor)
{
    return monitor->wlr_output->enabled && settings->bar.enabled &&
           (!settings->bar.single_monitor || monitor == bar_monitor());
}

/* whether (x, y) falls in a width by height box with corners of radius */
static bool bar_in_pill(double x, double y, int width, int height, double radius)
{
    if (x > width || y > height)
        return false;
    radius = fmax(radius, 0);
    const double outside_x = fmax(fmax(radius - x, x - (width - radius)), 0);
    const double outside_y = fmax(fmax(radius - y, y - (height - radius)), 0);
    return outside_x * outside_x + outside_y * outside_y <= radius * radius;
}

/* Antialiased rounded rectangle, blended over the buffer. hollow keeps a
 * one pixel outline, as drwl_rect() does when not filled. */
void bar_draw_pill(Monitor* monitor,
                   int x,
                   int y,
                   int width,
                   int height,
                   double radius,
                   bool hollow,
                   uint32_t rgba)
{
    if (width <= 0 || height <= 0)
        return;
    radius = fmin(fmax(radius, 0), fmin(width, height) / 2.0);
    pixman_image_t* mask = nullptr;
    for (size_t i = 0; i < LENGTH(pill_masks); i++) {
        const struct PillMask* entry = &pill_masks[i];
        if (entry->image && entry->width == width && entry->height == height &&
            entry->radius == radius && entry->hollow == hollow) {
            mask = entry->image;
            break;
        }
    }
    if (!mask) {
        mask = pixman_image_create_bits(PIXMAN_a8, width, height, nullptr, 0);
        if (!mask)
            return;
        uint8_t* data = (uint8_t*)pixman_image_get_data(mask);
        const int stride = pixman_image_get_stride(mask);

        /* 4x4 supersampling */
        for (int py = 0; py < height; py++)
            for (int px = 0; px < width; px++) {
                int coverage = 0;
                for (int sub_y = 0; sub_y < 4; sub_y++)
                    for (int sub_x = 0; sub_x < 4; sub_x++) {
                        const double sample_x = px + (sub_x + 0.5) / 4;
                        const double sample_y = py + (sub_y + 0.5) / 4;
                        coverage += bar_in_pill(sample_x, sample_y, width, height, radius) &&
                                    !(hollow && sample_x > 1 && sample_y > 1 &&
                                      bar_in_pill(sample_x - 1, sample_y - 1, width - 2, height - 2,
                                                  radius - 1));
                    }
                data[py * stride + px] = (uint8_t)(coverage * 255 / 16);
            }

        struct PillMask* entry = &pill_masks[pill_mask_next++ % LENGTH(pill_masks)];
        if (entry->image)
            pixman_image_unref(entry->image);
        *entry = (struct PillMask){ width, height, radius, hollow, mask };
    }
    const pixman_color_t color = convert_color(rgba);
    pixman_image_t* source = pixman_image_create_solid_fill(&color);
    if (source) {
        pixman_image_composite32(PIXMAN_OP_OVER, source, mask, monitor->draw_context->image, 0, 0,
                                 0, 0, x, y, width, height);
        pixman_image_unref(source);
    }
}

/* the same colour at a third of its alpha */
uint32_t bar_fade(uint32_t rgba)
{
    return (rgba & ~0xffu) | (rgba & 0xffu) / 3;
}

/* the dot marks the windows that differ from always_private */
bool bar_private_dot(const WindowClient* client, uint32_t* rgba)
{
    if (!client->is_private == !settings->windows.always_private)
        return false;
    *rgba = client->is_private ? settings->windows.private_color : settings->windows.public_color;
    return true;
}

double bar_private_radius(int diameter)
{
    return diameter / 2.0 *
           (settings->windows.private_round < 0 ? settings->bar.tag_dot_round
                                                : settings->windows.private_round);
}

/* scheme with an opaque background */
uint32_t* bar_opaque_scheme(int scheme)
{
    static uint32_t schemes[COLOR_SCHEME_COUNT][3];

    memcpy(schemes[scheme], settings->look.colors[scheme], sizeof(schemes[scheme]));
    schemes[scheme][ColBg] |= 0xff;
    return schemes[scheme];
}

/* a pool of one, dropped when the size changes */
RenderBuffer* bar_share_buffer(RenderBuffer** pool, int width, int height)
{
    if (*pool && ((*pool)->base.width != width || (*pool)->base.height != height))
        buffer_pool_drop(pool, 1);
    return buffer_get(pool, 1, width, height);
}

/* the urgent colour breathing between full and a quarter of its alpha, one
 * cycle every bar.urgent_pulse ms */
static uint32_t bar_pulse(uint32_t rgba)
{
    const double phase =
        (double)(notify_now() % settings->bar.urgent_pulse) / settings->bar.urgent_pulse;
    const double brightness = 0.25 + 0.75 * (0.5 + 0.5 * cos(2 * 3.14159265358979 * phase));

    return (rgba & ~0xffu) | (uint32_t)((rgba & 0xffu) * brightness);
}

static int bar_pulse_tick(void*)
{
    Monitor* monitor;
    wl_list_for_each(monitor, &monitors, link)
        bar_draw_contents(monitor, false);
    return 0;
}

/* One shot: each bar_draw() that still shows an urgent dot asks for the next
 * frame, so the timer dies with the last urgent window. */
static void bar_pulse_arm(void)
{
    if (!bar_pulse_timer)
        bar_pulse_timer = wl_event_loop_add_timer(event_loop, bar_pulse_tick, nullptr);
    if (bar_pulse_timer)
        wl_event_source_timer_update(bar_pulse_timer, PULSE_FRAME_MS);
}

void bar_finish(void)
{
    for (size_t i = 0; i < LENGTH(pill_masks); i++) {
        if (pill_masks[i].image)
            pixman_image_unref(pill_masks[i].image);
        pill_masks[i] = (struct PillMask){ 0 };
    }
    pill_mask_next = 0;
    if (bar_pulse_timer)
        wl_event_source_remove(bar_pulse_timer);
    bar_pulse_timer = nullptr;
}

static int bar_dot_size(const Monitor* monitor)
{
    return settings->bar.tag_dot_size
               ? MAX((int)round(settings->bar.tag_dot_size * monitor->wlr_output->scale), 1)
               : MAX(monitor->draw_context->font->height * 2 / 5, 4);
}

/* the dot of tag, a pill when selected; returns its cell's width */
static int bar_tag_dot(const Monitor* monitor,
                       const Monitor* target,
                       uint32_t tag,
                       int* dot_size,
                       int* dot_lead,
                       int* dot_width)
{
    const double scale = monitor->wlr_output->scale;

    *dot_size = bar_dot_size(monitor);
    const int gap = settings->bar.tag_dot_gap ? (int)round(settings->bar.tag_dot_gap * scale)
                                              : *dot_size * 3 / 2;
    *dot_width = target->tagsets[target->selected_tagset_index] & 1 << tag
                     ? (int)round(*dot_size * settings->bar.tag_dot_width)
                     : *dot_size;
    *dot_lead = tag ? gap / 2 : monitor->horizontal_padding / 2;
    return *dot_lead + *dot_width +
           (tag == settings->look.tag_count - 1 ? monitor->horizontal_padding / 2 : gap - gap / 2);
}

/* at the notification_dot end of the free box; returns the width it takes */
static int bar_draw_notification_dot(Monitor* monitor, int x, int free_width)
{
    const int dot_size = bar_dot_size(monitor);
    const int width = dot_size + monitor->horizontal_padding;
    uint32_t dot_color;

    if (!settings->notifications.enabled || free_width < width ||
        !bar_notification_private_dot(&dot_color))
        return 0;
    if (settings->notifications.dot == NOTIFICATION_DOT_RIGHT)
        x += free_width - width;
    drwl_setscheme(monitor->draw_context, settings->look.colors[COLOR_SCHEME_NORMAL]);
    drwl_rect(monitor->draw_context, x, 0, width, monitor->bar_metrics.height_px, 1, 1);
    bar_draw_pill(monitor, x + monitor->horizontal_padding / 2,
                  (monitor->bar_metrics.height_px - dot_size) / 2, dot_size, dot_size,
                  bar_private_radius(dot_size), false, dot_color);
    return width;
}

/* title of client in the bar */
static void bar_draw_window_title(Monitor* monitor,
                                  WindowClient* client,
                                  int x,
                                  int width,
                                  bool selected,
                                  bool shared)
{
    const int box_inset = monitor->draw_context->font->height / 9;
    const int box_size = monitor->draw_context->font->height / 6 + 2;
    const int box_y =
        (monitor->bar_metrics.height_px - monitor->draw_context->font->height) / 2 + box_inset;
    const double box_radius = settings->bar.box_radius * monitor->wlr_output->scale;
    const int scheme = selected ? COLOR_SCHEME_SELECTED : COLOR_SCHEME_NORMAL;

    drwl_setscheme(monitor->draw_context,
                   shared ? bar_opaque_scheme(scheme) : settings->look.colors[scheme]);
    drwl_text(monitor->draw_context, x, 0, width, monitor->bar_metrics.height_px,
              monitor->horizontal_padding / 2,
              shared ? privacy_share_title(client) : client_get_title(client), 0);
    if (client->is_floating)
        bar_draw_pill(monitor, x + box_inset, box_y, box_size, box_size, box_radius, true,
                      monitor->draw_context->scheme[ColFg]);
    uint32_t dot_color;
    if (bar_private_dot(client, &dot_color))
        bar_draw_pill(monitor, x + box_inset + (client->is_floating ? box_size + box_inset : 0),
                      box_y, box_size, box_size, bar_private_radius(box_size), false, dot_color);
}

/* What screen sharing gets in place of a private notification: a placeholder,
 * or with notifications.private_hide what the box holds without it. */
static void bar_draw_shared_notification(Monitor* monitor,
                                         WindowClient* focused,
                                         const Notification* notification,
                                         long history_index,
                                         int width,
                                         bool selected)
{
    if (!settings->notifications.private_hide) {
        bar_draw_notification(monitor, 0, width, notification, history_index, true);
    } else if (settings->bar.window_title && focused) {
        bar_draw_window_title(monitor, focused, 0, width, selected, true);
    } else {
        drwl_setscheme(monitor->draw_context, bar_opaque_scheme(COLOR_SCHEME_NORMAL));
        drwl_rect(monitor->draw_context, 0, 0, width, monitor->bar_metrics.height_px, 1, 1);
    }
}

/* shared with input_bar_click() */
int bar_tag_width(Monitor* monitor, Monitor* target, uint32_t tag)
{

    if (!settings->bar.tag_dots)
        return (int)TEXTW(monitor, settings->look.tags[tag]);
    int dot_size;
    int dot_lead;
    int dot_width;
    return bar_tag_dot(monitor, target, tag, &dot_size, &dot_lead, &dot_width);
}

void bar_draw(Monitor* monitor)
{
    bar_draw_contents(monitor, true);
}

static void bar_draw_contents(Monitor* monitor, bool draw_titles)
{
    /* what the bar reports on: its own monitor, or the focused one when
     * bar.single_monitor leaves a single bar standing in for the lot */
    Monitor* target = settings->bar.single_monitor && selected_monitor ? selected_monitor : monitor;
    bool is_selected = target == selected_monitor;

    /* Title bars are refreshed on the same events as the bar */
    WindowClient* client;
    wl_list_for_each(client, &clients, link)
        if (draw_titles && client->monitor == monitor)
            bar_draw_title_bar(client);

    monitor->capture_width = 0;
    if (!monitor->bar_scene_buffer->node.enabled) {
#ifdef INTEGRATED_BACKGROUND
        /* a hidden bar has no backdrop either, and bar_toggle() comes through
         * here rather than through the tail of this function */
        opacity_blur_bar(monitor);
#endif
        return;
    }

    RenderBuffer* buffer =
        buffer_get(monitor->bar_buffer_pool, LENGTH(monitor->bar_buffer_pool),
                   monitor->bar_metrics.width_px, monitor->bar_metrics.height_px);
    if (!buffer)
        return;
    drwl_setimage(monitor->draw_context, buffer->image);
    int corner_r = (int)roundf((float)corner_bar_radius(monitor) * monitor->wlr_output->scale);
    corner_clear(buffer->pixels, monitor->bar_metrics.width_px, monitor->bar_metrics.height_px,
                 corner_r);
    int tray_w = tray_get_width(monitor->tray);

    /* draw status first so it can be overdrawn by tags later */
    int status_w = 0;
    if (bar_status_shown(monitor)) {
        status_w = STATUSW(monitor);
        bar_draw_status(monitor, bar_status_text,
                        monitor->bar_metrics.width_px - (status_w + tray_w), status_w, 1);
    }

    uint32_t occupied = 0;
    uint32_t urgent = 0;
    wl_list_for_each(client, &clients, link) {
        if (client->monitor != target)
            continue;
        occupied |= client->tags;
        if (client->is_urgent)
            urgent |= client->tags;
    }

    /* dots mode: each tag is a dot (a pill when selected) colored by its state,
     * with no number. numbers mode is the loop below */
    WindowClient* focused = client_top(target);
    int cur_x = 0;
    bool pulsing = false;
    for (uint32_t i = 0; i < settings->look.tag_count && settings->bar.tag_dots; i++) {
        uint32_t tag_bit = 1u << i;
        int dot_size;
        int dot_lead;
        int dot_width;
        int tag_w = bar_tag_dot(monitor, target, i, &dot_size, &dot_lead, &dot_width);
        uint32_t color;

        drwl_setscheme(monitor->draw_context, settings->look.colors[COLOR_SCHEME_NORMAL]);
        drwl_rect(monitor->draw_context, cur_x, 0, tag_w, monitor->bar_metrics.height_px, 1, 1);
        if (urgent & tag_bit) {
            color = settings->look.colors[COLOR_SCHEME_URGENT][ColBorder];
            if (settings->bar.urgent_pulse) {
                color = bar_pulse(color);
                pulsing = true;
            }
        } else if (target->tagsets[target->selected_tagset_index] & tag_bit)
            color = settings->look.colors[COLOR_SCHEME_SELECTED][ColFg];
        else if (occupied & tag_bit)
            color = settings->look.colors[COLOR_SCHEME_NORMAL][ColFg];
        else /* empty */
            color = bar_fade(settings->look.colors[COLOR_SCHEME_NORMAL][ColFg]);
        bar_draw_pill(monitor, cur_x + dot_lead, (monitor->bar_metrics.height_px - dot_size) / 2,
                      dot_width, dot_size, dot_size / 2.0 * settings->bar.tag_dot_round, 0, color);
        cur_x += tag_w;
    }
    if (pulsing)
        bar_pulse_arm();

    /* occupied-tag squares in the corner of the tag number (numbers mode)
     * placed from the text's centered offset so they line up with it */
    int text_inset = monitor->draw_context->font->height / 9;
    int marker_size = monitor->draw_context->font->height / 6 + 2;
    int text_inset_y =
        (monitor->bar_metrics.height_px - monitor->draw_context->font->height) / 2 + text_inset;
    double marker_radius = settings->bar.box_radius * monitor->wlr_output->scale;
    for (uint32_t i = 0; i < settings->look.tag_count && !settings->bar.tag_dots; i++) {
        uint32_t tag_bit = 1u << i;
        int tag_w = TEXTW(monitor, settings->look.tags[i]);

        drwl_setscheme(
            monitor->draw_context,
            settings->look.colors[target->tagsets[target->selected_tagset_index] & tag_bit
                                      ? COLOR_SCHEME_SELECTED
                                      : COLOR_SCHEME_NORMAL]);
        drwl_text(monitor->draw_context, cur_x, 0, tag_w, monitor->bar_metrics.height_px,
                  monitor->horizontal_padding / 2, settings->look.tags[i], urgent & tag_bit);
        if (occupied & tag_bit)
            bar_draw_pill(monitor, cur_x + tag_w - text_inset - marker_size, text_inset_y,
                          marker_size, marker_size, marker_radius,
                          !(is_selected && focused && focused->tags & tag_bit),
                          monitor->draw_context->scheme[urgent & tag_bit ? ColBg : ColFg]);
        cur_x += tag_w;
    }
    if (settings->bar.layout_symbol) {
        int symbol_w = TEXTW(monitor, target->layout_symbol);

        drwl_setscheme(monitor->draw_context, settings->look.colors[COLOR_SCHEME_NORMAL]);
        cur_x = drwl_text(monitor->draw_context, cur_x, 0, symbol_w, monitor->bar_metrics.height_px,
                          monitor->horizontal_padding / 2, target->layout_symbol, 0);
    }

    /* Remember the free box for bar_notification_scroll(): the title and the
     * notification share it, so its geometry must be measured in exactly one
     * place. */
    int free_w = monitor->bar_metrics.width_px - (status_w + cur_x + tray_w);
    if (is_selected) {
        const int dot_w = bar_draw_notification_dot(monitor, cur_x, free_w);
        if (settings->notifications.dot == NOTIFICATION_DOT_LEFT)
            cur_x += dot_w;
        free_w -= dot_w;
    }
    monitor->bar_metrics.content_width_px = free_w > monitor->bar_metrics.height_px ? free_w : 0;

    if (monitor->bar_metrics.content_width_px) {
        /* The prompt takes the box over the same way a notification does,
         * and outranks one if both would want it at once. */
        if (runner_active && is_selected) {
            const char* suggestion = runner_suggest();
            int query_len = runner_length - runner_query();
            /* the caret scales with the font, which is loaded at the output's
             * dpi, so it keeps its proportions on every monitor */
            int font_h = monitor->draw_context->font->height;
            int caret_w = MAX(font_h / 12, 2);
            int free_end = cur_x + free_w;
            int icon_size = bar_runner_icon_size(monitor);
            int text_x = cur_x;

            drwl_setscheme(monitor->draw_context, settings->look.colors[COLOR_SCHEME_RUNNER]);
            drwl_rect(monitor->draw_context, cur_x, 0, free_w, monitor->bar_metrics.height_px, 1,
                      1);
            /* the slot stays in every mode, so the text never jumps */
            if (settings->runner.icons != RUNNER_ICONS_NONE &&
                icon_size + monitor->horizontal_padding < free_w) {
                RasterIcon* icon;
                if (suggestion && (icon = runner_icon(runner_selected())))
                    bar_draw_icon(monitor, icon, cur_x + monitor->horizontal_padding / 2,
                                  icon_size);
                else
                    bar_draw_icon_holder(monitor, cur_x + monitor->horizontal_padding / 2,
                                         icon_size);
                text_x += icon_size + monitor->horizontal_padding / 2;
            }
            drwl_text(monitor->draw_context, text_x, 0, free_end - text_x,
                      monitor->bar_metrics.height_px, monitor->horizontal_padding / 2,
                      runner_buffer, 0);
            int hint_x = text_x + monitor->horizontal_padding / 2 +
                         drwl_font_getwidth(monitor->draw_context, runner_buffer);

            /* the caret tracks the cursor, which need not be at the end of
             * the text; measure just the part before it */
            char saved_char = runner_buffer[runner_cursor];
            runner_buffer[runner_cursor] = '\0';
            int caret_x = text_x + monitor->horizontal_padding / 2 +
                          drwl_font_getwidth(monitor->draw_context, runner_buffer);
            runner_buffer[runner_cursor] = saved_char;

            if (caret_x + caret_w <= free_end)
                bar_draw_pill(monitor, caret_x, text_inset_y, caret_w, font_h - 2 * text_inset,
                              caret_w / 2.0, 0, settings->look.colors[COLOR_SCHEME_RUNNER][ColFg]);
            hint_x += caret_w;

            drwl_setscheme(monitor->draw_context,
                           settings->look.colors[COLOR_SCHEME_RUNNER_SUGGESTION]);
            if (!runner_length && *settings->runner.placeholder && hint_x < free_end) {
                drwl_text(monitor->draw_context, hint_x, 0, free_end - hint_x,
                          monitor->bar_metrics.height_px, 0, settings->runner.placeholder, 0);
            } else if (!settings->runner.max_candidates && suggestion &&
                       (size_t)query_len < strlen(suggestion) &&
                       !strncmp(suggestion, runner_buffer + runner_length - query_len, query_len) &&
                       hint_x < free_end) {
                drwl_text(monitor->draw_context, hint_x, 0, free_end - hint_x,
                          monitor->bar_metrics.height_px, 0, suggestion + query_len, 0);
            } else if (!suggestion && hint_x < free_end) {
                /* Not a completion of what's typed, so it can't reuse the
                 * "suggestion + query_len" tail above: it's an unrelated
                 * answer, appended rather than spliced in. */
                double calc_value;
                char calc_text[48];
                if (runner_calc(&calc_value)) {
                    memcpy(calc_text, "= ", 2);
                    runner_calc_format(calc_text + 2, sizeof calc_text - 2, calc_value);
                    drwl_text(monitor->draw_context, hint_x, 0, free_end - hint_x,
                              monitor->bar_metrics.height_px, 0, calc_text, 0);
                }
            }
            if (settings->runner.max_candidates && runner_candidate_count())
                bar_draw_candidates(monitor, hint_x + monitor->horizontal_padding / 2, free_end);
        } else {
            /* A notification takes the box over for as long as it lasts, so the
             * window title (if bar.window_title is on) steps aside and comes
             * back once the notification expires or is dismissed. */
            long history_index = -1;
            const Notification* notification = settings->notifications.enabled && is_selected
                                                   ? bar_notification_shown(&history_index)
                                                   : nullptr;
            RenderBuffer* share_buf;
            if (notification) {
                bar_draw_notification(monitor, cur_x, free_w, notification, history_index, false);
                if (bar_notification_private() &&
                    (share_buf = bar_share_buffer(monitor->capture_buffer_pool, free_w,
                                                  monitor->bar_metrics.height_px))) {
                    drwl_setimage(monitor->draw_context, share_buf->image);
                    bar_draw_shared_notification(monitor, focused, notification, history_index,
                                                 free_w, is_selected);
                    drwl_setimage(monitor->draw_context, buffer->image);
                    wlr_buffer_unlock(&share_buf->base);
                    monitor->capture_x = cur_x;
                    monitor->capture_width = free_w;
                }
            } else if (settings->bar.window_title && focused) {
                bar_draw_window_title(monitor, focused, cur_x, free_w, is_selected, false);
                if (focused->is_private &&
                    (share_buf = bar_share_buffer(monitor->capture_buffer_pool, free_w,
                                                  monitor->bar_metrics.height_px))) {
                    drwl_setimage(monitor->draw_context, share_buf->image);
                    bar_draw_window_title(monitor, focused, 0, free_w, is_selected, true);
                    drwl_setimage(monitor->draw_context, buffer->image);
                    wlr_buffer_unlock(&share_buf->base);
                    monitor->capture_x = cur_x;
                    monitor->capture_width = free_w;
                }
            } else {
                drwl_setscheme(monitor->draw_context, settings->look.colors[COLOR_SCHEME_NORMAL]);
                drwl_rect(monitor->draw_context, cur_x, 0, free_w, monitor->bar_metrics.height_px,
                          1, 1);
            }
        }
    }

    if (tray_w > 0)
        pixman_image_composite32(
            PIXMAN_OP_SRC, monitor->tray->image, nullptr, monitor->draw_context->image, 0, 0, 0, 0,
            monitor->bar_metrics.width_px - tray_w, 0, tray_w, monitor->bar_metrics.height_px);

    corner_cut(buffer->pixels, monitor->bar_metrics.width_px, monitor->bar_metrics.height_px,
               corner_r);

    wlr_scene_buffer_set_opacity(monitor->bar_scene_buffer, opacity_decorations_current());
    wlr_scene_buffer_set_dest_size(monitor->bar_scene_buffer, monitor->bar_metrics.logical_width,
                                   monitor->bar_metrics.logical_height);
    wlr_scene_node_set_position(
        &monitor->bar_scene_buffer->node, monitor->geometry.x + (int)settings->bar.padding,
        monitor->geometry.y +
            (settings->bar.top ? (int)settings->bar.padding
                               : monitor->geometry.height - monitor->bar_metrics.logical_height -
                                     (int)settings->bar.padding));
    wlr_scene_buffer_set_buffer(monitor->bar_scene_buffer, &buffer->base);
    wlr_buffer_unlock(&buffer->base);
#ifdef INTEGRATED_BACKGROUND
    opacity_blur_bar(monitor);
#endif
}

void bar_draw_all(void)
{
    Monitor* monitor = nullptr;

    wl_list_for_each(monitor, &monitors, link)
        bar_draw(monitor);
}

/* Redraws the bar that shows what selected_monitor is up to, which
 * bar.single_monitor can keep on another monitor entirely. */
void bar_draw_selected(void)
{
    Monitor* monitor = bar_monitor();

    if (monitor)
        bar_draw(monitor);
}

void bar_toggle(const ActionArg*)
{
    /* under bar.single_monitor this hides the one bar from any monitor */
    Monitor* monitor = bar_monitor();

    if (!monitor)
        return;
    wlr_scene_node_set_enabled(&monitor->bar_scene_buffer->node,
                               !monitor->bar_scene_buffer->node.enabled);
    monitor_arrange_layers(monitor);
    bar_draw_all();
}

void bar_update(Monitor* monitor)
{
    int output_w;
    int output_h;
    wlr_output_transformed_resolution(monitor->wlr_output, &output_w, &output_h);

    /* the bar is narrower than the output by its padding on both sides */
    int padding_px = (int)roundf((float)settings->bar.padding * monitor->wlr_output->scale);
    monitor->bar_metrics.width_px = MAX(1, output_w - 2 * padding_px);
    monitor->bar_metrics.logical_width =
        (int)((float)monitor->bar_metrics.width_px / monitor->wlr_output->scale);

    wlr_scene_node_set_enabled(&monitor->bar_scene_buffer->node, bar_visible(monitor));

    buffer_pool_drop(monitor->bar_buffer_pool, LENGTH(monitor->bar_buffer_pool));
    buffer_pool_drop(monitor->capture_buffer_pool, LENGTH(monitor->capture_buffer_pool));

    if (monitor->bar_metrics.scale == monitor->wlr_output->scale && monitor->draw_context)
        return;

    drwl_font_destroy(monitor->draw_context->font);
    char font_attrs[12];
    snprintf(font_attrs, sizeof(font_attrs), "dpi=%.2f", 96. * monitor->wlr_output->scale);
    if (!(drwl_font_create(monitor->draw_context, settings->look.font_count, settings->look.fonts,
                           font_attrs)))
        util_die("Could not load font");

    monitor->bar_metrics.scale = monitor->wlr_output->scale;
    monitor->horizontal_padding = monitor->draw_context->font->height;
    /* the automatic height follows the font, which is loaded at the output's
     * dpi, so bar.height scales it the same on every monitor */
    monitor->bar_metrics.height_px = monitor->draw_context->font->height + 2;
    if (settings->bar.height > 0)
        monitor->bar_metrics.height_px =
            MAX((int)((float)monitor->bar_metrics.height_px * settings->bar.height + 0.5f),
                monitor->draw_context->font->height);
    monitor->bar_metrics.logical_height =
        (int)((float)monitor->bar_metrics.height_px / monitor->wlr_output->scale);
    monitor->titlebar_metrics.height_px =
        monitor->draw_context->font->height + (int)settings->windows.title_padding;
    monitor->titlebar_metrics.logical_height =
        (int)((float)monitor->titlebar_metrics.height_px / monitor->wlr_output->scale);

    if (settings->bar.enabled && settings->systray.enabled && tray_watcher.running) {
        if (monitor->tray)
            tray_destroy(monitor->tray);

        /* systray.icon_size is unscaled, like cursor_size; 0 fills the bar */
        int tray_icon_size =
            settings->systray.icon_size
                ? (int)((float)settings->systray.icon_size * monitor->wlr_output->scale)
                : monitor->bar_metrics.height_px;
        monitor->tray =
            tray_create(monitor, monitor->bar_metrics.height_px,
                        MIN(tray_icon_size, monitor->bar_metrics.height_px),
                        (int)((float)settings->systray.spacing * monitor->wlr_output->scale),
                        (int)((float)settings->systray.padding * monitor->wlr_output->scale),
                        settings->look.colors[COLOR_SCHEME_NORMAL], settings->look.fonts,
                        font_attrs, &bar_tray_notify, &tray_watcher);

        if (!monitor->tray)
            util_die("Couldn't create tray for monitor");

        wl_list_insert(&tray_watcher.trays, &monitor->tray->link);
    }
}
