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
    int linex, linew;
    int iconx, iconw, iconh; /* iconw 0 without one */
    int textx, textw;        /* the › included */
    int countx, countw;      /* queue or history position */
    char count[32];
    int signx, signw; /* notification_action_sign */
    /* while picking, the action labels shown in place of the text */
    int chipx[NOTIFY_ACTMAX], chipw[NOTIFY_ACTMAX];
    size_t chip0, nchip;
} NotifyBox;

constexpr char bar_notification_more[] = "\xe2\x80\xba"; /* › */

/* function declarations */
static void bar_draw_notification_text(Monitor* m,
                                       const NotifyBox* b,
                                       int x,
                                       int w,
                                       const Notification* n,
                                       const char* text,
                                       uint32_t* scm);
static size_t bar_fit_prefix(Monitor* m, const char* s, int w);
static void bar_notification_box(Monitor* m,
                                 const Notification* n,
                                 long hist,
                                 int x,
                                 int w,
                                 NotifyBox* b);
static void bar_notification_browse(dbus_uint32_t id);
static void bar_notification_pick_close(void);
static void bar_notification_pick_run(size_t i);
static void bar_notification_sync(const Notification* n, long hist);
static int bar_notification_tick(void* data);

/* variables */
static dbus_uint32_t
    bar_notification_shown_id; /* what bar_notification_offset refers to */
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
static void bar_draw_notification_text(Monitor* m,
                                       const NotifyBox* b,
                                       int x,
                                       int w,
                                       const Notification* n,
                                       const char* text,
                                       uint32_t* scm)
{
    char buf[NOTIFY_TEXTMAX];
    size_t k;
    int aw;

    drwl_setscheme(m->drw, scm);
    drwl_rect(m->drw, x, 0, (unsigned int)w, m->b.height, 1, 1);
    if (b->nchip) {
        for (size_t i = b->chip0; i < b->chip0 + b->nchip; i++)
            drwl_text(m->drw,
                      b->chipx[i],
                      0,
                      b->chipw[i],
                      m->b.height,
                      m->lrpad / 2,
                      n->act[i].label,
                      (long)i == bar_notification_pick_selected);
        if (b->chip0 || b->chip0 + b->nchip < n->nact) {
            aw = (int)drwl_font_getwidth(m->drw, bar_notification_more);
            if (aw <= b->textw)
                drwl_text(m->drw,
                          b->textx + b->textw - aw,
                          0,
                          aw,
                          m->b.height,
                          0,
                          bar_notification_more,
                          0);
        }
    } else if (b->textw > 0 &&
               (int)drwl_font_getwidth(m->drw, text) <= b->textw) {
        drwl_text(m->drw, b->textx, 0, b->textw, m->b.height, 0, text, 0);
    } else if (b->textw > 0) {
        /* our own › instead of drwl's …, which covers the last codepoint */
        aw = (int)drwl_font_getwidth(m->drw, bar_notification_more);
        k = bar_fit_prefix(m, text, b->textw - aw);
        memcpy(buf, text, k);
        buf[k] = '\0';
        if (k)
            drwl_text(
                m->drw, b->textx, 0, b->textw - aw, m->b.height, 0, buf, 0);
        if (aw <= b->textw)
            drwl_text(m->drw,
                      b->textx + b->textw - aw,
                      0,
                      aw,
                      m->b.height,
                      0,
                      bar_notification_more,
                      0);
    }
    if (b->signw)
        drwl_text(m->drw,
                  b->signx,
                  0,
                  b->signw,
                  m->b.height,
                  0,
                  notification_action_sign,
                  0);
    if (b->countw)
        drwl_text(m->drw, b->countx, 0, b->countw, m->b.height, 0, b->count, 0);
}

/* hist is its history index, -1 for the live one */
void bar_draw_notification(Monitor* m,
                           int x,
                           int w,
                           const Notification* n,
                           long hist)
{
    int scheme = n->urgency == UrgencyLow        ? SchemeNotifyLow
                 : n->urgency == UrgencyCritical ? SchemeNotifyCrit
                                                 : SchemeNotify;
    int full = m->drw->font->height, len = full, top, fw, i;
    uint64_t now = notify_now(), wake = 0, end, left;
    uint32_t line = colors[scheme][ColBorder], filled[3], a, bg, tint;
    unsigned int held;
    const char* text;
    pixman_image_t* img;
    pixman_transform_t t;
    pixman_region32_t clip;
    NotifyBox b;

    bar_notification_sync(n, hist);
    bar_notification_picking();
    bar_notification_box_x = x;
    bar_notification_box(m, n, hist, x, w, &b);
    text = n->text + (bar_notification_offset < strlen(n->text)
                          ? bar_notification_offset
                          : 0);

    if (n->value < 0) {
        bar_draw_notification_text(m, &b, x, w, n, text, colors[scheme]);
    } else {
        /* the fill's background is premixed: a tint drawn over would dye the
         * text too */
        memcpy(filled, colors[scheme], sizeof(filled));
        bg = colors[scheme][ColBg];
        a = (line & 0xff) / 3;
        for (filled[ColBg] = bg & 0xff, i = 8; i < 32; i += 8) {
            tint = (bg >> i & 0xff) * (255 - a) + (line >> i & 0xff) * a;
            filled[ColBg] |= (tint / 255) << i;
        }
        fw = w * n->value / 100;
        pixman_region32_init_rect(&clip, x, 0, (unsigned int)fw, m->b.height);
        pixman_image_set_clip_region32(m->drw->image, &clip);
        pixman_region32_fini(&clip);
        bar_draw_notification_text(m, &b, x, w, n, text, filled);
        pixman_region32_init_rect(
            &clip, x + fw, 0, (unsigned int)(w - fw), m->b.height);
        pixman_image_set_clip_region32(m->drw->image, &clip);
        pixman_region32_fini(&clip);
        bar_draw_notification_text(m, &b, x, w, n, text, colors[scheme]);
        pixman_region32_init_rect(&clip, 0, 0, m->b.width, m->b.height);
        pixman_image_set_clip_region32(m->drw->image, &clip);
        pixman_region32_fini(&clip);
    }

    /* once, after both passes: they blend with OVER */
    if (b.linew) {
        /* full for sticky and history entries, the latter at half alpha */
        if (hist < 0 && n->timeout_ms && notify_held(&held)) {
            len = (int)((uint64_t)full * held / n->timeout_ms);
        } else if (hist < 0 && n->timeout_ms) {
            end = n->shown_ms + n->timeout_ms;
            left = end > now ? end - now : 0;
            len = (int)((uint64_t)full * left / n->timeout_ms);
            wake = n->timeout_ms / (unsigned int)(full > 0 ? full : 1);
            wake = wake < 16 ? 16 : wake;
        }
        if (hist >= 0)
            line = (line & ~0xffu) | (line & 0xffu) / 2;
        top = (m->b.height - full) / 2;
        bar_draw_pill(m,
                      b.linex,
                      top + full - len,
                      b.linew,
                      len,
                      notification_line_radius * m->wlr_output->scale,
                      0,
                      line);
    }

    if (b.iconw) {
        img = n->icon->img;
        pixman_transform_init_scale(
            &t,
            pixman_double_to_fixed((double)pixman_image_get_width(img) /
                                   b.iconw),
            pixman_double_to_fixed((double)pixman_image_get_height(img) /
                                   b.iconh));
        pixman_image_set_transform(img, &t);
        pixman_image_set_filter(img, PIXMAN_FILTER_BILINEAR, nullptr, 0);
        pixman_image_composite32(PIXMAN_OP_OVER,
                                 img,
                                 nullptr,
                                 m->drw->image,
                                 0,
                                 0,
                                 0,
                                 0,
                                 b.iconx,
                                 (m->b.height - b.iconh) / 2,
                                 b.iconw,
                                 b.iconh);
        pixman_image_set_transform(img, nullptr);
    }

    if (bar_notification_view_id) {
        end = bar_notification_view_ms + notification_timeout * 1000;
        left = end > now ? end - now : 1;
        wake = !wake || left < wake ? left : wake;
    }
    if (wake && !bar_notification_timer)
        bar_notification_timer =
            wl_event_loop_add_timer(event_loop, bar_notification_tick, nullptr);
    if (bar_notification_timer)
        wl_event_source_timer_update(bar_notification_timer, (int)wake);
}

/* bytes of s, cut on a codepoint, that fit in w */
static size_t bar_fit_prefix(Monitor* m, const char* s, int w)
{
    size_t cut[NOTIFY_TEXTMAX], ncut = 0, len = strlen(s), lo, hi, mid;
    char buf[NOTIFY_TEXTMAX];

    if (len >= sizeof(buf) || w <= 0)
        return 0;
    for (size_t i = 1; i <= len; i++)
        if (i == len || (s[i] & 0xC0) != 0x80)
            cut[ncut++] = i;
    /* width grows with length: binary search */
    for (lo = 0, hi = ncut; lo < hi;) {
        mid = (lo + hi + 1) / 2;
        memcpy(buf, s, cut[mid - 1]);
        buf[cut[mid - 1]] = '\0';
        if ((int)drwl_font_getwidth(m->drw, buf) <= w)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo ? cut[lo - 1] : 0;
}

/* Shared by drawing, scrolling and hit testing so they always agree. */
static void bar_notification_box(Monitor* m,
                                 const Notification* n,
                                 long hist,
                                 int x,
                                 int w,
                                 NotifyBox* b)
{
    int pad = m->lrpad / 2, size = m->drw->font->height, cx, end, iw, ih;
    int picking;
    size_t i;

    *b = (NotifyBox){};
    cx = x + pad;
    if (notification_line_width) {
        b->linex = cx;
        b->linew =
            (int)roundf((float)notification_line_width * m->wlr_output->scale);
        b->linew = b->linew > 0 ? b->linew : 1;
        cx += b->linew + pad;
    }
    if (n->icon && size > 0) {
        iw = pixman_image_get_width(n->icon->img);
        ih = pixman_image_get_height(n->icon->img);
        if (iw > 0 && ih > 0) {
            b->iconw = iw >= ih ? size : (iw * size / ih ? iw * size / ih : 1);
            b->iconh = ih >= iw ? size : (ih * size / iw ? ih * size / iw : 1);
            b->iconx = cx;
            cx += b->iconw + pad;
        }
    }

    if (hist >= 0)
        snprintf(b->count,
                 sizeof(b->count),
                 "%ld/%zu",
                 hist + 1,
                 notify_history_length());
    else if (notify_queued())
        snprintf(b->count, sizeof(b->count), "+%zu", notify_queued());
    end = x + w - pad;
    if (*b->count) {
        b->countw = (int)drwl_font_getwidth(m->drw, b->count);
        b->countx = end - b->countw;
        end = b->countx - pad;
    }
    picking = hist < 0 && bar_notification_pick_selected >= 0 &&
              n->id == bar_notification_pick_id;
    if (hist < 0 && n->nact && !picking && *notification_action_sign) {
        b->signw = (int)drwl_font_getwidth(m->drw, notification_action_sign);
        b->signx = end - b->signw;
        end = b->signx - pad;
    }
    b->textx = cx;
    b->textw = end > cx ? end - cx : 0;

    if (!picking)
        return;
    /* scroll the chips until the picked one fits */
    for (b->chip0 = 0;; b->chip0++) {
        for (i = b->chip0, cx = b->textx; i < n->nact; i++) {
            b->chipw[i] =
                (int)drwl_font_getwidth(m->drw, n->act[i].label) + m->lrpad;
            if (cx + b->chipw[i] > end)
                break;
            b->chipx[i] = cx;
            cx += b->chipw[i] + pad;
        }
        b->nchip = i - b->chip0;
        if (i > (size_t)bar_notification_pick_selected ||
            b->chip0 >= (size_t)bar_notification_pick_selected)
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
    const Notification *cur = notify_current(), *n;

    if (!cur) {
        bar_notification_live_id = 0;
    } else if (cur->id != bar_notification_live_id) {
        bar_notification_live_id = cur->id;
        bar_notification_view_id = 0;
    }
    if (bar_notification_view_id &&
        (bar_notification_pick_selected >= 0 ||
         notify_now() >=
             bar_notification_view_ms + notification_timeout * 1000))
        bar_notification_view_id = 0;

    for (size_t i = 0; bar_notification_view_id && (n = notify_history(i)); i++)
        if (n->id == bar_notification_view_id) {
            *hist = (long)i;
            return n;
        }
    bar_notification_view_id = 0;
    *hist = -1;
    return cur;
}

/* Reset the scroll offset for another notification, or when this one got
 * shorter than where we'd scrolled to. */
static void bar_notification_sync(const Notification* n, long hist)
{
    if (!n || bar_notification_shown_id != n->id ||
        bar_notification_shown_history != (hist >= 0) ||
        bar_notification_offset >= strlen(n->text)) {
        bar_notification_shown_id = n ? n->id : 0;
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
    const Notification* cur;

    if (bar_notification_pick_selected < 0)
        return 0;
    cur = notify_current();
    if (!cur || cur->id != bar_notification_pick_id || !cur->nact) {
        bar_notification_pick_close();
        return 0;
    }
    if ((size_t)bar_notification_pick_selected >=
        cur->nact) /* updated with fewer */
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
    const Notification* cur;

    if (!notifications_enabled)
        return;
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
    const Notification* cur;
    long n;

    if (!bar_notification_picking() || !(cur = notify_current()))
        return;
    n = (long)cur->nact;
    switch (sym) {
        case XKB_KEY_Left:
        case XKB_KEY_h:
        case XKB_KEY_ISO_Left_Tab:
            bar_notification_pick_selected =
                (bar_notification_pick_selected + n - 1) % n;
            break;
        case XKB_KEY_Right:
        case XKB_KEY_l:
        case XKB_KEY_Tab:
            bar_notification_pick_selected =
                (bar_notification_pick_selected + 1) % n;
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
            if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9 &&
                (long)(sym - XKB_KEY_1) < n)
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
    Monitor* m = bar_monitor();
    const Notification* n;
    const char* text;
    NotifyBox b;
    long hist;
    size_t len, off, good;

    if (!notifications_enabled || !m || !m->b.titlew ||
        !(n = bar_notification_shown(&hist)))
        return;
    bar_notification_sync(n, hist);
    if (hist >= 0)
        bar_notification_view_ms = notify_now();
    bar_notification_box(m, n, hist, 0, m->b.titlew, &b);

    text = n->text;
    len = strlen(text);
    off = bar_notification_offset < len ? bar_notification_offset : 0;
    if (b.textw <= 0 ||
        (int)drwl_font_getwidth(m->drw, text + off) <= b.textw) {
        bar_notification_offset = 0;
        bar_draw_all();
        return;
    }

    /* on from what was shown, which left room for the › */
    good = off + bar_fit_prefix(m,
                                text + off,
                                b.textw - (int)drwl_font_getwidth(
                                              m->drw, bar_notification_more));
    if (good == off) {
        /* not even one codepoint fits: force progress anyway */
        good = off + 1;
        while (good < len && (text[good] & 0xC0) == 0x80)
            good++;
    }
    bar_notification_offset = good < len ? good : 0;
    bar_draw_all();
}

void bar_notification_prev(const Arg*)
{
    const Notification* h;
    long hist;

    if (!notifications_enabled)
        return;
    bar_notification_shown(&hist);
    if ((h = notify_history((size_t)(hist + 1))))
        bar_notification_browse(h->id);
    else if (hist >= 0) /* the oldest: just refresh the timeout */
        bar_notification_browse(bar_notification_view_id);
}

/* One step forward; on the live one, dismisses it for the next queued. */
void bar_notification_next(const Arg*)
{
    long hist;

    if (!notifications_enabled)
        return;
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
    const Notification* h;
    long hist;

    if (!notifications_enabled)
        return;
    if (bar_notification_picking()) {
        bar_notification_pick_close();
        bar_draw_all();
        return;
    }
    bar_notification_shown(&hist);
    if (hist < 0) {
        notify_dismiss();
        return;
    }
    notify_history_remove((size_t)hist);
    if ((h = notify_history((size_t)hist)) ||
        (h = hist ? notify_history((size_t)hist - 1) : nullptr))
        bar_notification_browse(h->id);
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
    const Notification* n;
    const char* id;
    Monitor* m = bar_monitor();
    NotifyBox b;
    Client* c;
    long hist, px = arg && arg->ui ? (long)arg->ui - 1 : -1;

    if (!notifications_enabled || !(n = bar_notification_shown(&hist)))
        return;
    if (px >= 0 && m && hist < 0) {
        bar_notification_box(
            m, n, hist, bar_notification_box_x, m->b.titlew, &b);
        if (bar_notification_picking()) {
            for (size_t i = b.chip0; i < b.chip0 + b.nchip; i++)
                if (px >= b.chipx[i] && px < b.chipx[i] + b.chipw[i]) {
                    bar_notification_pick_run(i);
                    return;
                }
            bar_notification_pick_close();
            bar_draw_all();
            return;
        }
        if (b.signw && px >= b.signx - m->lrpad / 2 &&
            px < b.signx + b.signw + m->lrpad / 2) {
            bar_notification_actions(nullptr);
            return;
        }
    }
    wl_list_for_each(c, &clients, link)
    {
        id = client_get_app_id(c);
        if (!c->mon || client_is_unmanaged(c) ||
            (strcasecmp(id, n->app) &&
             (!*n->desktop || strcasecmp(id, n->desktop))))
            continue;
        selected_monitor = c->mon;
        if (!VISIBLEON(c, c->mon))
            layout_view(&(Arg){ .ui = c->tags });
        client_focus(c, 1);
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
int bar_notification_wheel(Monitor* pm, double delta, int horizontal)
{
    /* a mouse notch is 15; touchpads send it in slices */
    constexpr double notch = 15;
    constexpr double step = 5;
    static double acc, hacc;
    Monitor* m = bar_monitor();
    const Notification* n;
    const char* text;
    NotifyBox b;
    long hist;

    if (!notifications_enabled || !m || m != pm || !m->b.titlew)
        return 0;
    n = bar_notification_shown(&hist);

    if (!horizontal && bar_notification_picking()) {
        for (acc += delta; acc >= notch; acc -= notch)
            bar_notification_pick_key(XKB_KEY_Right);
        for (; acc <= -notch; acc += notch)
            bar_notification_pick_key(XKB_KEY_Left);
        return 1;
    }
    if (!horizontal) {
        if (!n && !notify_history_length())
            return 0;
        for (acc += delta; acc >= notch; acc -= notch)
            bar_notification_next(nullptr);
        for (; acc <= -notch; acc += notch)
            bar_notification_prev(nullptr);
        return 1;
    }

    if (!n)
        return 0;
    bar_notification_sync(n, hist);
    if (hist >= 0)
        bar_notification_view_ms = notify_now();
    bar_notification_box(m, n, hist, 0, m->b.titlew, &b);
    text = n->text;
    for (hacc += delta; hacc >= step; hacc -= step)
        if ((int)drwl_font_getwidth(m->drw, text + bar_notification_offset) >
            b.textw)
            do
                bar_notification_offset++;
            while ((text[bar_notification_offset] & 0xC0) == 0x80);
    for (; hacc <= -step; hacc += step)
        while (bar_notification_offset &&
               (text[--bar_notification_offset] & 0xC0) == 0x80)
            ;
    bar_draw_all();
    return 1;
}
