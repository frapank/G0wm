/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * notifications in the bar, their history and the action picker
 */

#include <dbus/dbus.h>
#include <drwl.h>
#include <math.h>
#include <pixman.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <xkbcommon/xkbcommon.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "desktop/bar/bar.h"
#include "desktop/bar/notification.h"
#include "desktop/notify/notify.h"
#include "desktop/notify/queue.h"
#include "internal.h"
#include "output/monitor.h"
#include "window/client.h"
#include "window/layout.h"

typedef struct {
    int line_x, line_width;
    int icon_x, icon_width, icon_height;
    int text_x, text_width;
    int count_x, count_width;
    char count_text[32];
    int sign_x, sign_width;
    /* while picking, the action labels shown in place of the text */
    int chip_x[NOTIFY_ACTMAX], chip_width[NOTIFY_ACTMAX];
    size_t chip_first, chip_count;
} NotifyBox;

constexpr char bar_notification_more[] = "\xe2\x80\xba"; /* › */

/* function declarations */
static void bar_draw_notification_text(Monitor* monitor,
                                       const NotifyBox* box,
                                       int x,
                                       int width,
                                       const Notification* notification,
                                       const char* text,
                                       uint32_t* scheme);
static size_t bar_fit_prefix(Monitor* monitor, const char* text, int max_width);
static void bar_notification_box(Monitor* monitor,
                                 const Notification* notification,
                                 long hist,
                                 int x,
                                 int width,
                                 NotifyBox* box);
static void bar_notification_browse(dbus_uint32_t id);
static void bar_notification_pick_close(void);
static void bar_notification_pick_run(size_t i);
static void bar_notification_sync(const Notification* notification, long hist);
static int bar_notification_tick(void*);

/* variables */
static dbus_uint32_t bar_notification_shown_id; /* what bar_notification_offset refers to */
static int bar_notification_shown_history;
static size_t bar_notification_offset;
/* history entry being viewed, 0 for live; by id since indices shift */
static dbus_uint32_t bar_notification_view_id;
static uint64_t bar_notification_view_ms; /* last input, the view times out */
static dbus_uint32_t bar_notification_live_id;
/* animates the line and times out the history view */
static struct wl_event_source* bar_notification_timer;
/* picked action, -1 when closed; the picker belongs to bar_notification_pick_id
 */
static long bar_notification_pick_selected = -1;
static dbus_uint32_t bar_notification_pick_id;
static int bar_notification_box_x; /* last drawn x, for hit testing */

/* function implementations */
/* Background and text. drwl_text() paints its own background, so a progress
 * fill is drawn by calling this twice under different clips. */
static void bar_draw_notification_text(Monitor* monitor,
                                       const NotifyBox* box,
                                       int x,
                                       int width,
                                       const Notification* notification,
                                       const char* text,
                                       uint32_t* scheme)
{
    drwl_setscheme(monitor->drw, scheme);
    drwl_rect(monitor->drw, x, 0, (unsigned int)width, monitor->b.height, 1, 1);
    if (box->chip_count) {
        for (size_t i = box->chip_first; i < box->chip_first + box->chip_count; i++)
            drwl_text(monitor->drw, box->chip_x[i], 0, box->chip_width[i], monitor->b.height,
                      monitor->lrpad / 2, notification->act[i].label,
                      (long)i == bar_notification_pick_selected);
        if (box->chip_first || box->chip_first + box->chip_count < notification->nact) {
            const int more_width = (int)drwl_font_getwidth(monitor->drw, bar_notification_more);
            if (more_width <= box->text_width)
                drwl_text(monitor->drw, box->text_x + box->text_width - more_width, 0, more_width,
                          monitor->b.height, 0, bar_notification_more, 0);
        }
    } else if (box->text_width > 0 &&
               (int)drwl_font_getwidth(monitor->drw, text) <= box->text_width) {
        drwl_text(monitor->drw, box->text_x, 0, box->text_width, monitor->b.height, 0, text, 0);
    } else if (box->text_width > 0) {
        /* our own › instead of drwl's …, which covers the last codepoint */
        const int more_width = (int)drwl_font_getwidth(monitor->drw, bar_notification_more);
        const size_t prefix_length = bar_fit_prefix(monitor, text, box->text_width - more_width);
        char buf[NOTIFY_TEXTMAX];
        memcpy(buf, text, prefix_length);
        buf[prefix_length] = '\0';
        if (prefix_length)
            drwl_text(monitor->drw, box->text_x, 0, box->text_width - more_width, monitor->b.height,
                      0, buf, 0);
        if (more_width <= box->text_width)
            drwl_text(monitor->drw, box->text_x + box->text_width - more_width, 0, more_width,
                      monitor->b.height, 0, bar_notification_more, 0);
    }
    if (box->sign_width)
        drwl_text(monitor->drw, box->sign_x, 0, box->sign_width, monitor->b.height, 0,
                  notification_action_sign, 0);
    if (box->count_width)
        drwl_text(monitor->drw, box->count_x, 0, box->count_width, monitor->b.height, 0,
                  box->count_text, 0);
}

/* hist is its history index, -1 for the live one */
void bar_draw_notification(Monitor* monitor,
                           int x,
                           int width,
                           const Notification* notification,
                           long hist)
{
    int color_scheme = notification->urgency == UrgencyLow        ? SchemeNotifyLow
                       : notification->urgency == UrgencyCritical ? SchemeNotifyCrit
                                                                  : SchemeNotify;
    uint32_t line_color = colors[color_scheme][ColBorder];

    bar_notification_sync(notification, hist);
    bar_notification_picking();
    bar_notification_box_x = x;
    NotifyBox box;
    bar_notification_box(monitor, notification, hist, x, width, &box);

    const char* visible_text =
        notification->text +
        (bar_notification_offset < strlen(notification->text) ? bar_notification_offset : 0);

    if (notification->value < 0) {
        bar_draw_notification_text(monitor, &box, x, width, notification, visible_text,
                                   colors[color_scheme]);
    } else {
        /* the fill's background is premixed: a tint drawn over would dye the
         * text too */
        uint32_t filled_colors[3];
        memcpy(filled_colors, colors[color_scheme], sizeof(filled_colors));
        uint32_t background = colors[color_scheme][ColBg];
        uint32_t alpha = (line_color & 0xff) / 3;
        filled_colors[ColBg] = background & 0xff;
        for (int shift = 8; shift < 32; shift += 8) {
            uint32_t mixed_channel =
                (background >> shift & 0xff) * (255 - alpha) + (line_color >> shift & 0xff) * alpha;
            filled_colors[ColBg] |= (mixed_channel / 255) << shift;
        }

        int filled_width = width * notification->value / 100;
        pixman_region32_t clip_region;
        pixman_region32_init_rect(&clip_region, x, 0, (unsigned int)filled_width,
                                  monitor->b.height);
        pixman_image_set_clip_region32(monitor->drw->image, &clip_region);
        pixman_region32_fini(&clip_region);
        bar_draw_notification_text(monitor, &box, x, width, notification, visible_text,
                                   filled_colors);

        pixman_region32_init_rect(&clip_region, x + filled_width, 0,
                                  (unsigned int)(width - filled_width), monitor->b.height);
        pixman_image_set_clip_region32(monitor->drw->image, &clip_region);
        pixman_region32_fini(&clip_region);
        bar_draw_notification_text(monitor, &box, x, width, notification, visible_text,
                                   colors[color_scheme]);

        pixman_region32_init_rect(&clip_region, 0, 0, monitor->b.width, monitor->b.height);
        pixman_image_set_clip_region32(monitor->drw->image, &clip_region);
        pixman_region32_fini(&clip_region);
    }

    uint64_t now_ms = notify_now();
    uint64_t wake_ms = 0;

    /* once, after both passes: they blend with OVER */
    if (box.line_width) {
        int font_height = monitor->drw->font->height;
        int line_length = font_height;
        unsigned int held_ms;

        /* full for sticky and history entries, the latter at half alpha */
        if (hist < 0 && notification->timeout_ms && notify_held(&held_ms)) {
            line_length = (int)((uint64_t)font_height * held_ms / notification->timeout_ms);
        } else if (hist < 0 && notification->timeout_ms) {
            uint64_t end_ms = notification->shown_ms + notification->timeout_ms;
            uint64_t left_ms = end_ms > now_ms ? end_ms - now_ms : 0;
            line_length = (int)((uint64_t)font_height * left_ms / notification->timeout_ms);
            wake_ms = notification->timeout_ms / (unsigned int)(font_height > 0 ? font_height : 1);
            wake_ms = wake_ms < 16 ? 16 : wake_ms;
        }
        if (hist >= 0)
            line_color = (line_color & ~0xffu) | (line_color & 0xffu) / 2;
        int line_top = (monitor->b.height - font_height) / 2;
        bar_draw_pill(monitor, box.line_x, line_top + font_height - line_length, box.line_width,
                      line_length, notification_line_radius * monitor->wlr_output->scale, 0,
                      line_color);
    }

    if (box.icon_width) {
        pixman_image_t* icon_image = notification->icon->img;
        pixman_transform_t icon_transform;
        pixman_transform_init_scale(
            &icon_transform,
            pixman_double_to_fixed((double)pixman_image_get_width(icon_image) / box.icon_width),
            pixman_double_to_fixed((double)pixman_image_get_height(icon_image) / box.icon_height));
        pixman_image_set_transform(icon_image, &icon_transform);
        pixman_image_set_filter(icon_image, PIXMAN_FILTER_BILINEAR, nullptr, 0);
        pixman_image_composite32(PIXMAN_OP_OVER, icon_image, nullptr, monitor->drw->image, 0, 0, 0,
                                 0, box.icon_x, (monitor->b.height - box.icon_height) / 2,
                                 box.icon_width, box.icon_height);
        pixman_image_set_transform(icon_image, nullptr);
    }

    if (bar_notification_view_id) {
        uint64_t end_ms = bar_notification_view_ms + notification_timeout * 1000;
        uint64_t left_ms = end_ms > now_ms ? end_ms - now_ms : 1;
        wake_ms = !wake_ms || left_ms < wake_ms ? left_ms : wake_ms;
    }
    if (wake_ms && !bar_notification_timer)
        bar_notification_timer =
            wl_event_loop_add_timer(event_loop, bar_notification_tick, nullptr);
    if (bar_notification_timer)
        wl_event_source_timer_update(bar_notification_timer, (int)wake_ms);
}

/* bytes of s, cut on a codepoint, that fit in w */
static size_t bar_fit_prefix(Monitor* monitor, const char* text, int max_width)
{
    size_t cut_points[NOTIFY_TEXTMAX], cut_count = 0;
    const size_t length = strlen(text);
    char buf[NOTIFY_TEXTMAX];

    if (length >= sizeof(buf) || max_width <= 0)
        return 0;
    for (size_t i = 1; i <= length; i++)
        if (i == length || (text[i] & 0xC0) != 0x80)
            cut_points[cut_count++] = i;
    /* width grows with length: binary search */
    size_t low = 0;
    for (size_t high = cut_count; low < high;) {
        const size_t mid = (low + high + 1) / 2;
        memcpy(buf, text, cut_points[mid - 1]);
        buf[cut_points[mid - 1]] = '\0';
        if ((int)drwl_font_getwidth(monitor->drw, buf) <= max_width)
            low = mid;
        else
            high = mid - 1;
    }
    return low ? cut_points[low - 1] : 0;
}

/* Shared by drawing, scrolling and hit testing so they always agree. */
static void bar_notification_box(Monitor* monitor,
                                 const Notification* notification,
                                 long hist,
                                 int x,
                                 int width,
                                 NotifyBox* box)
{
    const int pad = monitor->lrpad / 2;
    const int size = monitor->drw->font->height;

    *box = (NotifyBox){};
    int cursor_x = x + pad;
    if (notification_line_width) {
        box->line_x = cursor_x;
        box->line_width = (int)roundf((float)notification_line_width * monitor->wlr_output->scale);
        box->line_width = box->line_width > 0 ? box->line_width : 1;
        cursor_x += box->line_width + pad;
    }
    if (notification->icon && size > 0) {
        const int icon_source_width = pixman_image_get_width(notification->icon->img);
        const int icon_source_height = pixman_image_get_height(notification->icon->img);
        if (icon_source_width > 0 && icon_source_height > 0) {
            box->icon_width = icon_source_width >= icon_source_height
                                  ? size
                                  : (icon_source_width * size / icon_source_height
                                         ? icon_source_width * size / icon_source_height
                                         : 1);
            box->icon_height = icon_source_height >= icon_source_width
                                   ? size
                                   : (icon_source_height * size / icon_source_width
                                          ? icon_source_height * size / icon_source_width
                                          : 1);
            box->icon_x = cursor_x;
            cursor_x += box->icon_width + pad;
        }
    }

    if (hist >= 0)
        snprintf(box->count_text, sizeof(box->count_text), "%ld/%zu", hist + 1,
                 notify_history_length());
    else if (notify_queued())
        snprintf(box->count_text, sizeof(box->count_text), "+%zu", notify_queued());
    int end = x + width - pad;
    if (*box->count_text) {
        box->count_width = (int)drwl_font_getwidth(monitor->drw, box->count_text);
        box->count_x = end - box->count_width;
        end = box->count_x - pad;
    }
    int picking = hist < 0 && bar_notification_pick_selected >= 0 &&
                  notification->id == bar_notification_pick_id;
    if (hist < 0 && notification->nact && !picking && *notification_action_sign) {
        box->sign_width = (int)drwl_font_getwidth(monitor->drw, notification_action_sign);
        box->sign_x = end - box->sign_width;
        end = box->sign_x - pad;
    }
    box->text_x = cursor_x;
    box->text_width = end > cursor_x ? end - cursor_x : 0;

    if (!picking)
        return;
    /* scroll the chips until the picked one fits */
    for (box->chip_first = 0;; box->chip_first++) {
        size_t chip_index = box->chip_first;
        cursor_x = box->text_x;
        for (; chip_index < notification->nact; chip_index++) {
            box->chip_width[chip_index] =
                (int)drwl_font_getwidth(monitor->drw, notification->act[chip_index].label) +
                monitor->lrpad;
            if (cursor_x + box->chip_width[chip_index] > end)
                break;
            box->chip_x[chip_index] = cursor_x;
            cursor_x += box->chip_width[chip_index] + pad;
        }
        box->chip_count = chip_index - box->chip_first;
        if (chip_index > (size_t)bar_notification_pick_selected ||
            box->chip_first >= (size_t)bar_notification_pick_selected)
            break;
    }
}

/* id 0 goes back to the live notification */
static void bar_notification_browse(dbus_uint32_t id)
{
    bar_notification_view_id = id;
    bar_notification_view_ms = notify_now();
    bar_draw_all();
}

/* The history entry being viewed (its index in *hist) or the live one
 * (*hist -1). Also ends the view on timeout or when a new one comes in. */
const Notification* bar_notification_shown(long* hist)
{
    const Notification *current = notify_current(), *entry;

    if (!current) {
        bar_notification_live_id = 0;
    } else if (current->id != bar_notification_live_id) {
        bar_notification_live_id = current->id;
        bar_notification_view_id = 0;
    }
    if (bar_notification_view_id &&
        (bar_notification_pick_selected >= 0 ||
         notify_now() >= bar_notification_view_ms + notification_timeout * 1000))
        bar_notification_view_id = 0;

    for (size_t i = 0; bar_notification_view_id && (entry = notify_history(i)); i++)
        if (entry->id == bar_notification_view_id) {
            *hist = (long)i;
            return entry;
        }
    bar_notification_view_id = 0;
    *hist = -1;
    return current;
}

/* Reset the scroll offset for another notification, or when this one got
 * shorter than where we'd scrolled to. */
static void bar_notification_sync(const Notification* notification, long hist)
{
    if (!notification || bar_notification_shown_id != notification->id ||
        bar_notification_shown_history != (hist >= 0) ||
        bar_notification_offset >= strlen(notification->text)) {
        bar_notification_shown_id = notification ? notification->id : 0;
        bar_notification_shown_history = hist >= 0;
        bar_notification_offset = 0;
    }
}

static int bar_notification_tick(void*)
{
    bar_draw_selected();
    return 0;
}

void bar_notification_finish(void)
{
    if (bar_notification_timer)
        wl_event_source_remove(bar_notification_timer);
    bar_notification_timer = nullptr;
}

/* Also closes the picker once its notification is gone or has no actions. */
int bar_notification_picking(void)
{

    if (bar_notification_pick_selected < 0)
        return 0;
    const Notification* cur = notify_current();
    if (!cur || cur->id != bar_notification_pick_id || !cur->nact) {
        bar_notification_pick_close();
        return 0;
    }
    if ((size_t)bar_notification_pick_selected >= cur->nact) /* updated with fewer */
        bar_notification_pick_selected = (long)cur->nact - 1;
    return 1;
}

static void bar_notification_pick_close(void)
{
    bar_notification_pick_selected = -1;
    notify_hold(0);
}

static void bar_notification_pick_run(size_t i)
{
    bar_notification_pick_selected = -1;
    notify_action(i); /* releases the hold */
    bar_draw_all();
}

/* Toggles the picker; while open it owns the keyboard
 * (bar_notification_pick_key()). */
void bar_notification_actions(const Arg*)
{

    if (!notifications_enabled)
        return;
    const Notification* cur;
    if (bar_notification_picking()) {
        bar_notification_pick_close();
    } else if ((cur = notify_current()) && cur->nact) {
        bar_notification_view_id = 0;
        bar_notification_pick_selected = 0;
        bar_notification_pick_id = cur->id;
        notify_hold(1);
    } else {
        return;
    }
    bar_draw_all();
}

/* left/right, h/l or Tab move, Return or space runs, 1-9 run directly,
 * Escape closes */
void bar_notification_pick_key(xkb_keysym_t sym)
{
    const Notification* current;

    if (!bar_notification_picking() || !(current = notify_current()))
        return;
    long action_count = (long)current->nact;
    switch (sym) {
        case XKB_KEY_Left:
        case XKB_KEY_h:
        case XKB_KEY_ISO_Left_Tab:
            bar_notification_pick_selected =
                (bar_notification_pick_selected + action_count - 1) % action_count;
            break;
        case XKB_KEY_Right:
        case XKB_KEY_l:
        case XKB_KEY_Tab:
            bar_notification_pick_selected = (bar_notification_pick_selected + 1) % action_count;
            break;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
        case XKB_KEY_space:
            bar_notification_pick_run((size_t)bar_notification_pick_selected);
            return;
        case XKB_KEY_Escape:
            bar_notification_pick_close();
            break;
        default:
            if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9 && (long)(sym - XKB_KEY_1) < action_count)
                bar_notification_pick_run((size_t)(sym - XKB_KEY_1));
            return;
    }
    bar_draw_all();
}

/* Scrolls the notification on by one screenful, wrapping to the start once
 * the tail has been shown. */
void bar_notification_scroll(const Arg*)
{
    /* the box is measured on the monitor the bar is drawn on */
    Monitor* monitor = bar_monitor();
    const Notification* notification;
    long hist;

    if (!notifications_enabled || !monitor || !monitor->b.titlew ||
        !(notification = bar_notification_shown(&hist)))
        return;
    bar_notification_sync(notification, hist);
    if (hist >= 0)
        bar_notification_view_ms = notify_now();
    NotifyBox box;
    bar_notification_box(monitor, notification, hist, 0, monitor->b.titlew, &box);

    const char* text = notification->text;
    size_t length = strlen(text);
    size_t offset = bar_notification_offset < length ? bar_notification_offset : 0;
    if (box.text_width <= 0 ||
        (int)drwl_font_getwidth(monitor->drw, text + offset) <= box.text_width) {
        bar_notification_offset = 0;
        bar_draw_all();
        return;
    }

    /* on from what was shown, which left room for the › */
    size_t next_offset =
        offset + bar_fit_prefix(monitor, text + offset,
                                box.text_width -
                                    (int)drwl_font_getwidth(monitor->drw, bar_notification_more));
    if (next_offset == offset) {
        /* not even one codepoint fits: force progress anyway */
        next_offset = offset + 1;
        while (next_offset < length && (text[next_offset] & 0xC0) == 0x80)
            next_offset++;
    }
    bar_notification_offset = next_offset < length ? next_offset : 0;
    bar_draw_all();
}

void bar_notification_prev(const Arg*)
{

    if (!notifications_enabled)
        return;
    long hist;
    bar_notification_shown(&hist);
    const Notification* older;
    if ((older = notify_history((size_t)(hist + 1))))
        bar_notification_browse(older->id);
    else if (hist >= 0) /* the oldest: just refresh the timeout */
        bar_notification_browse(bar_notification_view_id);
}

/* One step forward; on the live one, dismisses it for the next queued. */
void bar_notification_next(const Arg*)
{

    if (!notifications_enabled)
        return;
    long hist;
    bar_notification_shown(&hist);
    if (hist > 0)
        bar_notification_browse(notify_history((size_t)hist - 1)->id);
    else if (hist == 0)
        bar_notification_browse(0);
    else
        notify_dismiss();
}

/* Puts the notification away early, giving the box back to the window title.
 * On a history entry, forgets it; with the picker open, closes the picker. */
void bar_notification_dismiss(const Arg*)
{

    if (!notifications_enabled)
        return;
    if (bar_notification_picking()) {
        bar_notification_pick_close();
        bar_draw_all();
        return;
    }
    long hist;
    bar_notification_shown(&hist);
    if (hist < 0) {
        notify_dismiss();
        return;
    }
    notify_history_remove((size_t)hist);
    const Notification* neighbour;
    if ((neighbour = notify_history((size_t)hist)) ||
        (neighbour = hist ? notify_history((size_t)hist - 1) : nullptr))
        bar_notification_browse(neighbour->id);
    else
        bar_notification_browse(0);
}

/* The live and queued ones go to the history. */
void bar_notification_dismiss_all(const Arg*)
{
    int browsing = bar_notification_view_id != 0;

    if (!notifications_enabled)
        return;
    bar_notification_view_id = 0;
    notify_dismiss_all();
    if (browsing)
        bar_draw_all();
}

/* Opens the notification: sends its default action and focuses the
 * sender's window, since it can't raise itself. From the pointer (arg->ui is
 * x + 1) a click on the action sign opens the picker, and with the picker
 * open a click runs the action under it or closes the picker. */
void bar_notification_open(const Arg* arg)
{
    Monitor* monitor = bar_monitor();
    long hist, pointer_x = arg && arg->ui ? (long)arg->ui - 1 : -1;

    const Notification* notification;
    if (!notifications_enabled || !(notification = bar_notification_shown(&hist)))
        return;
    NotifyBox box;
    if (pointer_x >= 0 && monitor && hist < 0) {
        bar_notification_box(monitor, notification, hist, bar_notification_box_x, monitor->b.titlew,
                             &box);
        if (bar_notification_picking()) {
            for (size_t i = box.chip_first; i < box.chip_first + box.chip_count; i++)
                if (pointer_x >= box.chip_x[i] && pointer_x < box.chip_x[i] + box.chip_width[i]) {
                    bar_notification_pick_run(i);
                    return;
                }
            bar_notification_pick_close();
            bar_draw_all();
            return;
        }
        if (box.sign_width && pointer_x >= box.sign_x - monitor->lrpad / 2 &&
            pointer_x < box.sign_x + box.sign_width + monitor->lrpad / 2) {
            bar_notification_actions(nullptr);
            return;
        }
    }
    const char* id;
    Client* client;
    wl_list_for_each(client, &clients, link) {
        id = client_get_app_id(client);
        if (!client->mon || client_is_unmanaged(client) ||
            (strcasecmp(id, notification->app) &&
             (!*notification->desktop || strcasecmp(id, notification->desktop))))
            continue;
        selected_monitor = client->mon;
        if (!VISIBLEON(client, client->mon))
            layout_view(&(Arg){ .ui = client->tags });
        client_focus(client, 1);
        break;
    }
    if (hist < 0) {
        notify_invoke();
    } else {
        bar_notification_view_id = 0;
        bar_draw_all();
    }
}

/* Vertical walks the history (or the picker), horizontal scrolls the text.
 * Returns whether the wheel was consumed. */
int bar_notification_wheel(Monitor* pointer_monitor, double delta, int horizontal)
{
    /* a mouse notch is 15; touchpads send it in slices */
    constexpr double notch_size = 15;
    constexpr double scroll_step = 5;
    Monitor* bar_mon = bar_monitor();

    /* Scroll received but not yet turned into a notch or a step. It is kept
     * between calls because touchpads send many small deltas. */
    static double vertical_accumulator;
    static double horizontal_accumulator;

    if (!notifications_enabled || !bar_mon || bar_mon != pointer_monitor || !bar_mon->b.titlew)
        return 0;

    long history_index;
    const Notification* notification = bar_notification_shown(&history_index);

    /* While the user is picking an action, the wheel moves the selection
     * between the action labels, like the Left and Right keys do. */
    if (!horizontal && bar_notification_picking()) {
        for (vertical_accumulator += delta; vertical_accumulator >= notch_size;
             vertical_accumulator -= notch_size)
            bar_notification_pick_key(XKB_KEY_Right);
        for (; vertical_accumulator <= -notch_size; vertical_accumulator += notch_size)
            bar_notification_pick_key(XKB_KEY_Left);
        return 1;
    }

    /* Otherwise the wheel walks through the notifications and the history:
     * a notch down shows the next one, a notch up the previous one. */
    if (!horizontal) {
        if (!notification && !notify_history_length())
            return 0;
        for (vertical_accumulator += delta; vertical_accumulator >= notch_size;
             vertical_accumulator -= notch_size)
            bar_notification_next(nullptr);
        for (; vertical_accumulator <= -notch_size; vertical_accumulator += notch_size)
            bar_notification_prev(nullptr);
        return 1;
    }

    if (!notification)
        return 0;

    /* The offset belongs to one notification, so reset it if this is another
     * one. Scrolling counts as input, so the history view doesn't time out
     * while the user is reading. */
    bar_notification_sync(notification, history_index);
    if (history_index >= 0)
        bar_notification_view_ms = notify_now();

    NotifyBox box;
    bar_notification_box(bar_mon, notification, history_index, 0, bar_mon->b.titlew, &box);
    const char* text = notification->text;

    /* Scroll the text right one character per step, but only while the rest
     * of it is wider than the room we have. Continuation bytes (10xxxxxx) are
     * skipped so the offset always lands on the start of a UTF-8 character. */
    for (horizontal_accumulator += delta; horizontal_accumulator >= scroll_step;
         horizontal_accumulator -= scroll_step)
        if ((int)drwl_font_getwidth(bar_mon->drw, text + bar_notification_offset) > box.text_width)
            do
                bar_notification_offset++;
            while ((text[bar_notification_offset] & 0xC0) == 0x80);

    /* Scroll back to the left the same way, stopping at the start. */
    for (; horizontal_accumulator <= -scroll_step; horizontal_accumulator += scroll_step)
        while (bar_notification_offset && (text[--bar_notification_offset] & 0xC0) == 0x80)
            ;

    bar_draw_all();
    return 1;
}
