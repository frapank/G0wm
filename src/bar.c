/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the bar, the status text, the window title bars and the tray
 */
#include <dbus/dbus.h>
#include <drwl.h>
#include <errno.h>
#include <math.h>
#include <pixman.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <xkbcommon/xkbcommon.h>

#include "bar.h"
#include "buffer.h"
#include "client.h"
#include "corner.h"
#include "g0wm.h"
#include "input.h"
#include "layout.h"
#include "monitor.h"
#include "notify.h"
#include "opacity.h"
#include "privacy.h"
#include "runner.h"
#include "settings.h"
#include "systray/icon.h"
#include "systray/tray.h"
#include "traypopup.h"
#include "util.h"

/* function declarations */
static Monitor* bar_main_monitor(void);
static Monitor* bar_monitor(void);
static int bar_visible(Monitor* m);
static int bar_status_escape(const char* p, uint32_t* scm);
static void bar_draw_pill(Monitor* m,
                          int x,
                          int y,
                          int w,
                          int h,
                          double r,
                          int hollow,
                          uint32_t rgba);
static void bar_tray_notify(void* data);
static void bar_draw_candidates(Monitor* m, int x, int end);
static void bar_draw_icon(Monitor* m, Icon* icon, int x, int size);
static void bar_draw_icon_holder(Monitor* m, int x, int size);
static int bar_runner_icon_size(Monitor* m);
static void bar_draw_title_bar(Client* c);
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

static void bar_draw_notification(Monitor* m,
                                  int x,
                                  int w,
                                  const Notification* n,
                                  long hist);
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
static const Notification* bar_notification_shown(long* hist);
static void bar_notification_sync(const Notification* n, long hist);
static int bar_notification_tick(void* data);
static void bar_status_stop(void);

/* variables */
char bar_status_text[STATUS_MAX];
constexpr char bar_notification_more[] = "\xe2\x80\xba"; /* › */
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
/* redraws the bars while an urgent dot pulses */
static struct wl_event_source* bar_pulse_timer;
constexpr int PULSE_FRAME_MS = 40;
/* picked action, -1 when closed; the picker belongs to bar_notification_pick_id
 */
static long bar_notification_pick_selected = -1;
static dbus_uint32_t bar_notification_pick_id;
static int bar_notification_box_x; /* last drawn x, for hit testing */
static Client* bar_hovered;        /* whose close button is under the pointer */

/* function implementations */
bool bar_accepts_input(struct wlr_scene_buffer*, double*, double*)
{
    return true;
}

/* The first enabled output matched by monitor_rules[], NULL if there is none.
 */
static Monitor* bar_main_monitor(void)
{
    const MonitorRule* r;
    Monitor* m;

    for (r = monitor_rules; r < monitor_rules + monitor_rule_count; r++) {
        /* monitors grows at its head, so backwards is the order the outputs
         * showed up in: under the catch-all row the oldest one wins */
        wl_list_for_each_reverse(m, &monitors, link)
        {
            if (m->wlr_output->enabled &&
                (!r->name || strstr(m->wlr_output->name, r->name)))
                return m;
        }
    }
    return nullptr;
}

/* The monitor the bar is drawn on: the main one with bar_single_monitor, the
 * focused one without it. */
static Monitor* bar_monitor(void)
{
    return bar_single_monitor ? bar_main_monitor() : selected_monitor;
}

/* Whether m carries a bar. bar_single_monitor leaves every other monitor
 * without one, and monitor_arrange_layers() reads that off the scene node to
 * free the height. */
static int bar_visible(Monitor* m)
{
    return m->wlr_output->enabled && bar_enabled &&
           (!bar_single_monitor || m == bar_monitor());
}

/* whether (x, y) falls in a w by h box with corners of radius r */
static int bar_in_pill(double x, double y, int w, int h, double r)
{
    double dx, dy;

    if (x > w || y > h)
        return 0;
    r = fmax(r, 0);
    dx = fmax(fmax(r - x, x - (w - r)), 0);
    dy = fmax(fmax(r - y, y - (h - r)), 0);
    return dx * dx + dy * dy <= r * r;
}

/* Antialiased rounded rectangle, blended over the buffer. hollow keeps a
 * one pixel outline, as drwl_rect() does when not filled. */
static void bar_draw_pill(Monitor* m,
                          int x,
                          int y,
                          int w,
                          int h,
                          double r,
                          int hollow,
                          uint32_t rgba)
{
    pixman_image_t *mask, *src;
    pixman_color_t clr;
    uint8_t* data;
    int stride, px, py, sx, sy, cov;
    double fx, fy;

    if (w <= 0 || h <= 0 ||
        !(mask = pixman_image_create_bits(PIXMAN_a8, w, h, nullptr, 0)))
        return;
    r = fmin(fmax(r, 0), fmin(w, h) / 2.0);
    data = (uint8_t*)pixman_image_get_data(mask);
    stride = pixman_image_get_stride(mask);

    /* 4x4 supersampling */
    for (py = 0; py < h; py++)
        for (px = 0; px < w; px++) {
            for (cov = 0, sy = 0; sy < 4; sy++)
                for (sx = 0; sx < 4; sx++) {
                    fx = px + (sx + 0.5) / 4;
                    fy = py + (sy + 0.5) / 4;
                    cov += bar_in_pill(fx, fy, w, h, r) &&
                           !(hollow && fx > 1 && fy > 1 &&
                             bar_in_pill(fx - 1, fy - 1, w - 2, h - 2, r - 1));
                }
            data[py * stride + px] = (uint8_t)(cov * 255 / 16);
        }

    clr = convert_color(rgba);
    if ((src = pixman_image_create_solid_fill(&clr))) {
        pixman_image_composite32(
            PIXMAN_OP_OVER, src, mask, m->drw->image, 0, 0, 0, 0, x, y, w, h);
        pixman_image_unref(src);
    }
    pixman_image_unref(mask);
}

/* the same colour at a third of its alpha */
static uint32_t bar_fade(uint32_t rgba)
{
    return (rgba & ~0xffu) | (rgba & 0xffu) / 3;
}

/* the dot marks the windows that differ from always_private */
static int bar_private_dot(Client* c, uint32_t* rgba)
{
    if (!c->isprivate == !always_private)
        return 0;
    *rgba = c->isprivate ? private_color : public_color;
    return 1;
}

static double bar_private_radius(int d)
{
    return d / 2.0 * (private_round < 0 ? bar_tag_dot_round : private_round);
}

/* scheme with an opaque background */
static uint32_t* bar_opaque_scheme(int scheme)
{
    static uint32_t scm[NumSchemes][3];

    memcpy(scm[scheme], colors[scheme], sizeof(scm[scheme]));
    scm[scheme][ColBg] |= 0xff;
    return scm[scheme];
}

/* a pool of one, dropped when the size changes */
static Buffer* bar_share_buffer(Buffer** pool, int w, int h)
{
    if (*pool && ((*pool)->base.width != w || (*pool)->base.height != h))
        buffer_pool_drop(pool, 1);
    return buffer_get(pool, 1, w, h);
}

/* the urgent colour breathing between full and a quarter of its alpha, one
 * cycle every bar_urgent_pulse ms */
static uint32_t bar_pulse(uint32_t rgba)
{
    double t = (double)(notify_now() % bar_urgent_pulse) / bar_urgent_pulse;
    double k = 0.25 + 0.75 * (0.5 + 0.5 * cos(2 * 3.14159265358979 * t));

    return (rgba & ~0xffu) | (uint32_t)((rgba & 0xffu) * k);
}

static int bar_pulse_tick(void*)
{
    bar_draw_all();
    return 0;
}

/* One shot: each bar_draw() that still shows an urgent dot asks for the next
 * frame, so the timer dies with the last urgent window. */
static void bar_pulse_arm(void)
{
    if (!bar_pulse_timer)
        bar_pulse_timer =
            wl_event_loop_add_timer(event_loop, bar_pulse_tick, nullptr);
    if (bar_pulse_timer)
        wl_event_source_timer_update(bar_pulse_timer, PULSE_FRAME_MS);
}

void bar_finish(void)
{
    if (bar_pulse_timer)
        wl_event_source_remove(bar_pulse_timer);
    bar_pulse_timer = nullptr;
}

/* icon scaled to fit size, centered in the bar */
static void bar_draw_icon(Monitor* m, Icon* icon, int x, int size)
{
    pixman_image_t* img = icon->img;
    pixman_transform_t t;
    int iw = pixman_image_get_width(img), ih = pixman_image_get_height(img);
    int w, h;

    if (iw <= 0 || ih <= 0 || size <= 0)
        return;
    w = iw >= ih ? size : MAX(iw * size / ih, 1);
    h = ih >= iw ? size : MAX(ih * size / iw, 1);
    pixman_transform_init_scale(&t,
                                pixman_double_to_fixed((double)iw / w),
                                pixman_double_to_fixed((double)ih / h));
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
                             x + (size - w) / 2,
                             ((int)m->b.height - h) / 2,
                             w,
                             h);
    pixman_image_set_transform(img, nullptr);
}

/* a rounded square where an icon is missing, colored by runner_icon */
static void bar_draw_icon_holder(Monitor* m, int x, int size)
{
    int y = ((int)m->b.height - size) / 2;
    int bw =
        MIN((int)round(runner_icon_border * m->wlr_output->scale), size / 2);
    double r = size / 2.0 * runner_icon_round;

    if (bw > 0)
        bar_draw_pill(
            m, x, y, size, size, r, 0, colors[SchemeRunnerIcon][ColBorder]);
    bar_draw_pill(m,
                  x + bw,
                  y + bw,
                  size - 2 * bw,
                  size - 2 * bw,
                  MAX(r - bw, 0),
                  0,
                  colors[SchemeRunnerIcon][ColBg]);
}

static int bar_runner_icon_size(Monitor* m)
{
    int size = runner_icon_size
                   ? (int)round(runner_icon_size * m->wlr_output->scale)
                   : m->drw->font->height;

    return MIN(size, (int)m->b.height);
}

/* The candidates after the prompt, a page at a time. The selected one sits
 * in a pill shaped like the selected tag dot. */
static void bar_draw_candidates(Monitor* m, int x, int end)
{
    int n = runner_candidate_count(), sel = runner_selected();
    int per = (int)MIN(runner_max_candidates, (unsigned int)n);
    int fh = m->drw->font->height, bh = (int)m->b.height;
    int i, ph, pad, mg, tw, isz, iw, holder;
    double scale = m->wlr_output->scale, r;
    const char* s;
    Icon* icon;

    ph = runner_pill_height ? (int)round(runner_pill_height * scale)
                            : fh * 4 / 3;
    ph = MIN(MAX(ph, fh), bh);
    pad = runner_pill_padding ? (int)round(runner_pill_padding * scale)
                              : m->lrpad / 2;
    r = ph / 2.0 *
        (runner_pill_round < 0 ? bar_tag_dot_round : runner_pill_round);
    /* drwl_text() paints behind the text too, keep that off the round ends */
    mg = (ph - fh) / 2;
    if (mg < r)
        pad = MAX(pad, (int)ceil(r - sqrt(r * r - (r - mg) * (r - mg))));

    isz = MIN(bar_runner_icon_size(m), ph);
    for (i = sel / per * per; i < n && i < sel / per * per + per; i++) {
        s = runner_candidate(i);
        icon = runner_icons == RunnerIconsAll ? runner_icon(i) : nullptr;
        holder = runner_icons == RunnerIconsAll && !icon && runner_has_icons();
        iw = icon || holder ? isz + pad / 2 : 0;
        tw = MIN((int)drwl_font_getwidth(m->drw, s), end - x - 2 * pad - iw);
        if (tw <= 0)
            break;
        if (i == sel)
            bar_draw_pill(m,
                          x,
                          (bh - ph) / 2,
                          iw + tw + 2 * pad,
                          ph,
                          r,
                          0,
                          colors[SchemeRunnerSel][ColBg]);
        if (icon)
            bar_draw_icon(m, icon, x + pad, isz);
        else if (holder)
            bar_draw_icon_holder(m, x + pad, isz);
        drwl_setscheme(
            m->drw, colors[i == sel ? SchemeRunnerSel : SchemeRunnerSuggest]);
        drwl_text(m->drw, x + pad + iw, (bh - fh) / 2, tw, fh, 0, s, 0);
        x += iw + tw + 2 * pad;
    }
}

/* the dot of tag i, a pill when selected; returns its cell's width */
static int bar_tag_dot(Monitor* m,
                       Monitor* s,
                       uint32_t i,
                       int* d,
                       int* lead,
                       int* sw)
{
    int gap;
    double scale = m->wlr_output->scale;

    *d = bar_tag_dot_size ? MAX((int)round(bar_tag_dot_size * scale), 1)
                          : MAX(m->drw->font->height * 2 / 5, 4);
    gap = bar_tag_dot_gap ? (int)round(bar_tag_dot_gap * scale) : *d * 3 / 2;
    *sw = s->tagset[s->seltags] & 1 << i ? (int)round(*d * bar_tag_dot_width)
                                         : *d;
    *lead = i ? gap / 2 : m->lrpad / 2;
    return *lead + *sw + (i == tag_count - 1 ? m->lrpad / 2 : gap - gap / 2);
}

/* title of c in the bar */
static void bar_draw_window_title(Monitor* m,
                                  Client* c,
                                  int x,
                                  int w,
                                  int sel,
                                  int share)
{
    int boxs = m->drw->font->height / 9;
    int boxw = m->drw->font->height / 6 + 2;
    int boxy = (m->b.height - m->drw->font->height) / 2 + boxs;
    double boxr = bar_box_radius * m->wlr_output->scale;
    int scheme = sel ? SchemeSel : SchemeNorm;
    uint32_t pc;

    drwl_setscheme(m->drw, share ? bar_opaque_scheme(scheme) : colors[scheme]);
    drwl_text(m->drw,
              x,
              0,
              w,
              m->b.height,
              m->lrpad / 2,
              share ? privacy_share_title(c) : client_get_title(c),
              0);
    if (c->isfloating)
        bar_draw_pill(
            m, x + boxs, boxy, boxw, boxw, boxr, 1, m->drw->scheme[ColFg]);
    if (bar_private_dot(c, &pc))
        bar_draw_pill(m,
                      x + boxs + (c->isfloating ? boxw + boxs : 0),
                      boxy,
                      boxw,
                      boxw,
                      bar_private_radius(boxw),
                      0,
                      pc);
}

/* shared with input_bar_click() */
int bar_tag_width(Monitor* m, Monitor* s, uint32_t i)
{
    int d, lead, sw;

    if (!bar_tag_dots)
        return (int)TEXTW(m, tags[i]);
    return bar_tag_dot(m, s, i, &d, &lead, &sw);
}

void bar_draw(Monitor* m)
{
    int x, w, tw = 0, traywidth = 0;
    int boxs = m->drw->font->height / 9;
    int boxw = m->drw->font->height / 6 + 2;
    /* the little squares follow the text, which drwl centers */
    int boxy = (m->b.height - m->drw->font->height) / 2 + boxs;
    double boxr = bar_box_radius * m->wlr_output->scale;
    uint32_t i, occ = 0, urg = 0;
    int pulsing = 0;
    Client* c;
    Buffer *buf, *share;
    /* what the bar reports on: its own monitor, or the focused one when
     * bar_single_monitor leaves a single bar standing in for the lot */
    Monitor* s = bar_single_monitor && selected_monitor ? selected_monitor : m;
    int sel = s == selected_monitor;
    int r = (int)roundf((float)corner_bar_radius(m) * m->wlr_output->scale);

    /* Title bars are refreshed on the same events as the bar */
    wl_list_for_each(c, &clients, link) if (c->mon == m) bar_draw_title_bar(c);

    m->sharew = 0;
    if (!m->scene_buffer->node.enabled) {
#ifdef INTEGRATED_BACKGROUND
        /* a hidden bar has no backdrop either, and bar_toggle() comes through
         * here rather than through the tail of this function */
        opacity_blur_bar(m);
#endif
        return;
    }
    if (!(buf = buffer_get(m->pool, LENGTH(m->pool), m->b.width, m->b.height)))
        return;
    drwl_setimage(m->drw, buf->image);
    corner_clear(buf->data, m->b.width, m->b.height, r);
    traywidth = tray_get_width(m->tray);

    /* draw status first so it can be overdrawn by tags later */
    if (bar_status_shown(m)) {
        tw = STATUSW(m);
        bar_draw_status(
            m, bar_status_text, m->b.width - (tw + traywidth), tw, 1);
    }

    wl_list_for_each(c, &clients, link)
    {
        if (c->mon != s)
            continue;
        occ |= c->tags;
        if (c->isurgent)
            urg |= c->tags;
    }
    x = 0;
    c = client_top(s);
    for (i = 0; i < tag_count && bar_tag_dots; i++) {
        int d, lead, sw;
        uint32_t clr;

        w = bar_tag_dot(m, s, i, &d, &lead, &sw);
        drwl_setscheme(m->drw, colors[SchemeNorm]);
        drwl_rect(m->drw, x, 0, w, m->b.height, 1, 1);
        if (urg & 1 << i) {
            clr = colors[SchemeUrg][ColBorder];
            if (bar_urgent_pulse) {
                clr = bar_pulse(clr);
                pulsing = 1;
            }
        } else if (s->tagset[s->seltags] & 1 << i)
            clr = colors[SchemeSel][ColFg];
        else if (occ & 1 << i)
            clr = colors[SchemeNorm][ColFg];
        else /* empty */
            clr = bar_fade(colors[SchemeNorm][ColFg]);
        bar_draw_pill(m,
                      x + lead,
                      (m->b.height - d) / 2,
                      sw,
                      d,
                      d / 2.0 * bar_tag_dot_round,
                      0,
                      clr);
        x += w;
    }
    if (pulsing)
        bar_pulse_arm();
    for (i = 0; i < tag_count && !bar_tag_dots; i++) {
        w = TEXTW(m, tags[i]);
        drwl_setscheme(
            m->drw,
            colors[s->tagset[s->seltags] & 1 << i ? SchemeSel : SchemeNorm]);
        drwl_text(
            m->drw, x, 0, w, m->b.height, m->lrpad / 2, tags[i], urg & 1 << i);
        if (occ & 1 << i)
            bar_draw_pill(m,
                          x + w - boxs - boxw,
                          boxy,
                          boxw,
                          boxw,
                          boxr,
                          !(sel && c && c->tags & 1 << i),
                          m->drw->scheme[urg & 1 << i ? ColBg : ColFg]);
        x += w;
    }
    if (bar_layout_symbol) {
        w = TEXTW(m, s->ltsymbol);
        drwl_setscheme(m->drw, colors[SchemeNorm]);
        x = drwl_text(
            m->drw, x, 0, w, m->b.height, m->lrpad / 2, s->ltsymbol, 0);
    }

    /* Remember the free box for bar_notification_scroll(): the title and the
     * notification share it, so its geometry must be measured in exactly one
     * place. */
    w = m->b.width - (tw + x + traywidth);
    m->b.titlew = w > m->b.height ? w : 0;

    if (m->b.titlew) {
        /* The prompt takes the box over the same way a notification does,
         * and outranks one if both would want it at once. */
        if (runner_active && sel) {
            const char* sug = runner_suggest();
            int qlen = runner_length - runner_query();
            /* the caret scales with the font, which is loaded at the output's
             * dpi, so it keeps its proportions on every monitor */
            int fh = m->drw->font->height, cw = MAX(fh / 12, 2);
            int tx, cx, end = x + w, isz = bar_runner_icon_size(m), px = x;
            Icon* icon;
            char save;

            drwl_setscheme(m->drw, colors[SchemeRunner]);
            drwl_rect(m->drw, x, 0, w, m->b.height, 1, 1);
            /* the slot stays in every mode, so the text never jumps */
            if (runner_icons != RunnerIconsNone && isz + m->lrpad < w) {
                if (sug && (icon = runner_icon(runner_selected())))
                    bar_draw_icon(m, icon, x + m->lrpad / 2, isz);
                else
                    bar_draw_icon_holder(m, x + m->lrpad / 2, isz);
                px += isz + m->lrpad / 2;
            }
            drwl_text(m->drw,
                      px,
                      0,
                      end - px,
                      m->b.height,
                      m->lrpad / 2,
                      runner_buffer,
                      0);
            tx = px + m->lrpad / 2 + drwl_font_getwidth(m->drw, runner_buffer);

            /* the caret tracks the cursor, which need not be at the end of
             * the text; measure just the part before it */
            save = runner_buffer[runner_cursor];
            runner_buffer[runner_cursor] = '\0';
            cx = px + m->lrpad / 2 + drwl_font_getwidth(m->drw, runner_buffer);
            runner_buffer[runner_cursor] = save;

            if (cx + cw <= end)
                bar_draw_pill(m,
                              cx,
                              boxy,
                              cw,
                              fh - 2 * boxs,
                              cw / 2.0,
                              0,
                              colors[SchemeRunner][ColFg]);
            tx += cw;

            drwl_setscheme(m->drw, colors[SchemeRunnerSuggest]);
            if (!runner_length && *runner_placeholder && tx < end) {
                drwl_text(m->drw,
                          tx,
                          0,
                          end - tx,
                          m->b.height,
                          0,
                          runner_placeholder,
                          0);
            } else if (!runner_max_candidates && sug &&
                       (size_t)qlen < strlen(sug) &&
                       !strncmp(
                           sug, runner_buffer + runner_length - qlen, qlen) &&
                       tx < end) {
                drwl_text(
                    m->drw, tx, 0, end - tx, m->b.height, 0, sug + qlen, 0);
            } else if (!sug && tx < end) {
                /* Not a completion of what's typed, so it can't reuse the
                 * "sug + qlen" tail above: it's an unrelated answer,
                 * appended rather than spliced in. */
                double calcval;
                char calcbuf[48];
                if (runner_calc(&calcval)) {
                    memcpy(calcbuf, "= ", 2);
                    runner_calc_format(
                        calcbuf + 2, sizeof calcbuf - 2, calcval);
                    drwl_text(
                        m->drw, tx, 0, end - tx, m->b.height, 0, calcbuf, 0);
                }
            }
            if (runner_max_candidates && runner_candidate_count())
                bar_draw_candidates(m, tx + m->lrpad / 2, end);
        } else {
            /* A notification takes the box over for as long as it lasts, so the
             * window title (if bar_window_title is on) steps aside and comes
             * back once the notification expires or is dismissed. */
            long hist = -1;
            const Notification* n = notifications_enabled && sel
                                        ? bar_notification_shown(&hist)
                                        : nullptr;
            if (n) {
                bar_draw_notification(m, x, w, n, hist);
            } else if (bar_window_title && c) {
                bar_draw_window_title(m, c, x, w, sel, 0);
                if (c->isprivate &&
                    (share = bar_share_buffer(m->sharepool, w, m->b.height))) {
                    drwl_setimage(m->drw, share->image);
                    bar_draw_window_title(m, c, 0, w, sel, 1);
                    drwl_setimage(m->drw, buf->image);
                    wlr_buffer_unlock(&share->base);
                    m->sharex = x;
                    m->sharew = w;
                }
            } else {
                drwl_setscheme(m->drw, colors[SchemeNorm]);
                drwl_rect(m->drw, x, 0, w, m->b.height, 1, 1);
            }
        }
    }

    if (traywidth > 0)
        pixman_image_composite32(PIXMAN_OP_SRC,
                                 m->tray->image,
                                 nullptr,
                                 m->drw->image,
                                 0,
                                 0,
                                 0,
                                 0,
                                 m->b.width - traywidth,
                                 0,
                                 traywidth,
                                 m->b.height);

    corner_cut(buf->data, m->b.width, m->b.height, r);

    wlr_scene_buffer_set_opacity(m->scene_buffer,
                                 opacity_decorations_current());
    wlr_scene_buffer_set_dest_size(
        m->scene_buffer, m->b.real_width, m->b.real_height);
    wlr_scene_node_set_position(
        &m->scene_buffer->node,
        m->m.x + (int)bar_padding,
        m->m.y + (bar_top ? (int)bar_padding
                          : m->m.height - m->b.real_height - (int)bar_padding));
    wlr_scene_buffer_set_buffer(m->scene_buffer, &buf->base);
    wlr_buffer_unlock(&buf->base);
#ifdef INTEGRATED_BACKGROUND
    opacity_blur_bar(m);
#endif
}

/* whether the bar on m shows the status text */
int bar_status_shown(Monitor* m)
{
    if (bar_status == BarStatusMain)
        return m == bar_main_monitor();
    if (bar_status == BarStatusFocus)
        return bar_single_monitor || m == selected_monitor;
    return 1;
}

void bar_draw_all(void)
{
    Monitor* m = nullptr;

    wl_list_for_each(m, &monitors, link) bar_draw(m);
}

/* Redraws the bar that shows what selected_monitor is up to, which
 * bar_single_monitor can keep on another monitor entirely. */
void bar_draw_selected(void)
{
    Monitor* m = bar_monitor();

    if (m)
        bar_draw(m);
}

/* One escape at p: ^c#RRGGBB^ and ^b#RRGGBB^ set the foreground and the
 * background of what follows (an eight digit form carries alpha too), ^d^
 * goes back to SchemeNorm, ^sN!cmd^ opens a clickable area for button N
 * (1 left, 2 right, 3 middle, 4/5 wheel up/down; ^s!^ is 1) and ^e^ closes
 * it. Returns the bytes it takes, and applies a colour to scm when that is
 * given; zero means p does not start one. */
static int bar_status_escape(const char* p, uint32_t* scm)
{
    static constexpr char hex[] = "0123456789abcdef";
    const char* d;
    uint32_t clr = 0;
    int i;

    if (p[0] != '^')
        return 0;
    if (p[1] == 's') {
        i = p[2] >= '1' && p[2] <= '0' + STATUS_BUTTONS ? 3 : 2;
        if (p[i++] != '!' || !p[i] || p[i] == '^')
            return 0;
        for (; p[i] && p[i] != '^'; i++)
            ;
        return p[i] == '^' ? i + 1 : 0;
    }
    if (p[1] == 'e' && p[2] == '^')
        return 3;
    if (p[1] == 'e' && p[2] == '!' && p[3] == '^')
        return 4;
    if (p[1] == 'd' && p[2] == '^') {
        if (scm) {
            scm[ColFg] = colors[SchemeStatus][ColFg];
            scm[ColBg] = colors[SchemeStatus][ColBg];
        }
        return 3;
    }
    if ((p[1] != 'c' && p[1] != 'b') || p[2] != '#')
        return 0;

    /* | 0x20 lowercases a letter and leaves a digit alone */
    for (i = 0; i < 8 && p[3 + i] && (d = strchr(hex, p[3 + i] | 0x20)); i++)
        clr = clr << 4 | (uint32_t)(d - hex);
    if ((i != 6 && i != 8) || p[3 + i] != '^')
        return 0;
    if (i == 6)
        clr = clr << 8 | 0xff; /* the short form is opaque */

    if (scm)
        scm[p[1] == 'c' ? ColFg : ColBg] = clr;
    return i + 4;
}

/* Draws the status text one colour run at a time, and returns its width
 * without the escapes. With render off it only measures, which is what
 * places the box before there is anything to draw in it. */
int bar_draw_status(Monitor* m, const char* text, int x, int w, int render)
{
    uint32_t scm[3];
    char seg[sizeof(bar_status_text)];
    const char* p = text;
    int total = 0, sw, len, xend = x + w;
    size_t n;

    if (!m || !text)
        return 0;
    memcpy(scm, colors[SchemeStatus], sizeof(scm));

    /* the runs cover the glyphs only, so the padding around them would keep
     * whatever the buffer held */
    if (render) {
        drwl_setscheme(m->drw, colors[SchemeStatus]);
        drwl_rect(m->drw, x, 0, w, m->b.height, 1, 1);
    }

    while (*p) {
        for (n = 0; *p && n + 1 < sizeof(seg);) {
            if (*p == '^') {
                if (p[1] == '^') {
                    seg[n++] = *p;
                    p += 2;
                    continue;
                }
                if (bar_status_escape(p, nullptr))
                    break;
            }
            seg[n++] = *p++;
        }
        seg[n] = '\0';

        sw = (int)drwl_font_getwidth(m->drw, seg);
        total += sw;
        if (render && sw > 0 && x < xend) {
            if (x + sw > xend) /* drwl_text() ellipsizes what it cannot fit */
                sw = xend - x;
            drwl_setscheme(m->drw, scm);
            drwl_text(m->drw, x, 0, sw, m->b.height, 0, seg, 0);
            x += sw;
        }

        if ((len = bar_status_escape(p, scm)))
            p += len;
    }

    return total;
}

/* Copies into cmd the command for button at x pixels into the status text.
 * Must walk the text the same way bar_draw_status() does. */
static int bar_status_area(Monitor* m,
                           const char* text,
                           int x,
                           int button,
                           char* cmd,
                           size_t sz)
{
    char seg[sizeof(bar_status_text)];
    const char* p = text;
    const char* area[STATUS_BUTTONS] = {};
    int sx = 0, len;
    size_t n;

    while (*p) {
        for (n = 0; *p && n + 1 < sizeof(seg);) {
            if (*p == '^') {
                if (p[1] == '^') {
                    seg[n++] = *p;
                    p += 2;
                    continue;
                }
                if (bar_status_escape(p, nullptr))
                    break;
            }
            seg[n++] = *p++;
        }
        seg[n] = '\0';

        sx += (int)drwl_font_getwidth(m->drw, seg);
        if (x < sx) {
            if (!area[button - 1])
                return 0;
            n = strcspn(area[button - 1], "^");
            if (n >= sz)
                n = sz - 1;
            memcpy(cmd, area[button - 1], n);
            cmd[n] = '\0';
            return 1;
        }

        if ((len = bar_status_escape(p, nullptr))) {
            if (p[1] == 's' && p[2] == '!')
                area[0] = p + 3;
            else if (p[1] == 's')
                area[p[2] - '1'] = p + 4;
            else if (p[1] == 'e')
                memset(area, 0, sizeof(area));
            p += len;
        }
    }
    return 0;
}

static int bar_status_command(Monitor* pm,
                              int x,
                              int button,
                              char* cmd,
                              size_t sz)
{
    return pm && bar_status_shown(pm) && button >= 1 &&
           button <= STATUS_BUTTONS &&
           bar_status_area(pm, bar_status_text, x, button, cmd, sz);
}

static void bar_status_run(char* cmd)
{
    char* argv[] = { "/bin/sh", "-c", cmd, nullptr };

    spawn(&(Arg){ .v = argv });
}

int bar_status_click(Monitor* pm, int x, int button)
{
    char cmd[sizeof(bar_status_text)];

    if (!bar_status_command(pm, x, button, cmd, sizeof(cmd)))
        return 0;
    bar_status_run(cmd);
    return 1;
}

int bar_status_scroll(Monitor* pm, int x, double delta)
{
    /* a mouse notch is 15; touchpads send it in slices */
    constexpr double notch = 15;
    static double acc;
    char cmd[sizeof(bar_status_text)];

    if (delta == 0 ||
        !bar_status_command(pm, x, delta < 0 ? 4 : 5, cmd, sizeof(cmd)))
        return 0;
    if ((acc < 0) != (delta < 0))
        acc = 0;
    for (acc += delta; acc >= notch; acc -= notch)
        bar_status_run(cmd);
    for (; acc <= -notch; acc += notch)
        bar_status_run(cmd);
    return 1;
}

static void bar_tray_notify(void* data)
{
    bar_draw((Monitor*)data);
}

void bar_tray_activate(const Arg* arg)
{
    Monitor* m = bar_monitor(); /* the tray belongs to the bar clicked */

    if (!m)
        return;
    tray_left_clicked(m->tray, arg->ui);
}

void bar_tray_menu(const Arg* arg)
{
    Monitor* m = bar_monitor();

    if (!m)
        return;
    tray_right_clicked(m->tray, arg->ui, traypopup_present);
}

/* the top tab, or the focused client */
static int bar_title_selected(Client* c)
{
    Monitor* m = c->mon;

    return c == (m->lt[m->sellt]->arrange == layout_tabbed && !c->isfloating
                     ? layout_tab_top(m)
                     : client_top(m));
}

/* x of the close button, w without one */
static int bar_title_close_x(Client* c, int w, int* d)
{
    Monitor* m = c->mon;
    int x;

    *d = MAX(m->drw->font->height * 3 / 5, 4);
    if (!title_close)
        return w;
    x = w - m->lrpad / 2 - *d;
    return x > w / 2 ? x : w;
}

/* draws the title bar of c into buf */
static void bar_paint_title_bar(Client* c, Buffer* buf, int share)
{
    Monitor* m = c->mon;
    const char* title;
    int w = buf->base.width, h = buf->base.height;
    int sel, scheme, tab, end, lead, tw, d, fd, pd, dots;
    uint32_t fg, pc;

    sel = bar_title_selected(c);
    scheme = sel ? SchemeTitleSel : SchemeTitle;
    tab = m->lt[m->sellt]->arrange == layout_tabbed && !c->isfloating &&
          c->titlew < c->geom.width - 2 * (int)c->bw;
    fg = colors[scheme][ColFg];
    title = share ? privacy_share_title(c) : client_get_title(c);
    end = bar_title_close_x(c, w, &d);
    fd = MAX(m->drw->font->height / 3, 3);

    pd = bar_private_dot(c, &pc);
    dots = !!c->isfloating + pd;
    lead = m->lrpad / 2 + dots * (fd + m->lrpad / 3);
    if (title_center) {
        tw = (int)drwl_font_getwidth(m->drw, title);
        lead = MAX(MIN((w - tw) / 2, end - m->lrpad / 2 - tw), lead);
    }

    drwl_setimage(m->drw, buf->image);
    drwl_setscheme(m->drw, share ? bar_opaque_scheme(scheme) : colors[scheme]);
    drwl_rect(m->drw, 0, 0, w, h, 1, 1);
    if (end > lead)
        drwl_text(m->drw, 0, 0, end, h, lead, title, 0);

    if (pd)
        bar_draw_pill(m,
                      lead - fd - m->lrpad / 3,
                      (h - fd) / 2,
                      fd,
                      fd,
                      bar_private_radius(fd),
                      0,
                      pc);
    if (c->isfloating)
        bar_draw_pill(m,
                      lead - dots * (fd + m->lrpad / 3),
                      (h - fd) / 2,
                      fd,
                      fd,
                      fd / 2.0,
                      0,
                      fg);
    if (tab && c->titlex)
        bar_draw_pill(m,
                      0,
                      h / 4,
                      (int)MAX(m->wlr_output->scale, 1),
                      h - h / 2,
                      0,
                      0,
                      bar_fade(colors[SchemeTitle][ColFg]));
    if (end < w && c == bar_hovered)
        bar_draw_pill(m, end, (h - d) / 2, d, d, d / 2.0, 0, title_close_color);
}

/* Renders the client's own title bar. In the tabbed layout every client of the
 * group shares one row, so these end up drawn side by side as tabs. */
static void bar_draw_title_bar(Client* c)
{
    Monitor* m = c->mon;
    Buffer *buf, *share;
    int w, h;

    if (!c->title)
        return;
    bar_place_title_bar(c);
    if (!m || !title_bar || c->isfullscreen || c->titlew <= 0 ||
        !c->scene->node.enabled) {
        wlr_scene_node_set_enabled(&c->title->node, 0);
        return;
    }

    w = (int)((float)c->titlew * m->wlr_output->scale);
    h = m->t.height;
    if (w != c->titlebufw) {
        buffer_pool_drop(c->titlepool, LENGTH(c->titlepool));
        c->titlebufw = w;
    }
    if (!(buf = buffer_get(c->titlepool, LENGTH(c->titlepool), w, h)))
        return;
    bar_paint_title_bar(c, buf, 0);
    if (c->isprivate && (share = bar_share_buffer(c->sharepool, w, h))) {
        bar_paint_title_bar(c, share, 1);
        wlr_buffer_unlock(&share->base);
    }

    wlr_scene_node_set_enabled(&c->title->node, 1);
    wlr_scene_buffer_set_opacity(c->title, opacity_decorations_current());
    wlr_scene_buffer_set_buffer(c->title, &buf->base);
    wlr_buffer_unlock(&buf->base);
}

/* whether (x, y) is on c's close button */
static int bar_on_close_button(Client* c, double x, double y)
{
    Monitor* m = c->mon;
    int bx, d;
    double lx, ly;

    if (!m || !c->title || !c->title->node.enabled)
        return 0;
    lx = (x - c->geom.x - c->bw - c->titlex) * m->wlr_output->scale;
    ly = (y - c->geom.y - c->bw) * m->wlr_output->scale;
    bx = bar_title_close_x(c, c->titlebufw, &d);
    return bx < c->titlebufw && ly >= 0 && ly < m->t.height &&
           lx >= bx - m->lrpad / 2 && lx < c->titlebufw;
}

/* whether (x, y) is on c's title bar */
int bar_on_title_bar(Client* c, double x, double y)
{
    Monitor* m = c->mon;
    double lx, ly;

    if (!m || !c->title || !c->title->node.enabled)
        return 0;
    lx = (x - c->geom.x - c->bw - c->titlex) * m->wlr_output->scale;
    ly = (y - c->geom.y - c->bw) * m->wlr_output->scale;
    return lx >= 0 && lx < c->titlebufw && ly >= 0 && ly < m->t.height;
}

int bar_title_click(Client* c, double x, double y)
{
    if (bar_on_close_button(c, x, y))
        client_send_close(c);
    else if (bar_on_title_bar(c, x, y))
        input_title_grab(c);
    else
        return 0;
    return 1;
}

void bar_title_hover(Client* c, double x, double y)
{
    Client *h = c && bar_on_close_button(c, x, y) ? c : nullptr,
           *old = bar_hovered, *w;

    if (h == old)
        return;
    bar_hovered = h;
    /* old may be gone */
    wl_list_for_each(w, &clients, link) if (w == old || w == h)
        bar_draw_title_bar(w);
}

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
    size_t k, i;
    int aw;

    drwl_setscheme(m->drw, scm);
    drwl_rect(m->drw, x, 0, (unsigned int)w, m->b.height, 1, 1);
    if (b->nchip) {
        for (i = b->chip0; i < b->chip0 + b->nchip; i++)
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
static void bar_draw_notification(Monitor* m,
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
    size_t cut[NOTIFY_TEXTMAX], ncut = 0, len = strlen(s), i, lo, hi, mid;
    char buf[NOTIFY_TEXTMAX];

    if (len >= sizeof(buf) || w <= 0)
        return 0;
    for (i = 1; i <= len; i++)
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
static const Notification* bar_notification_shown(long* hist)
{
    const Notification *cur = notify_current(), *n;
    size_t i;

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

    for (i = 0; bar_notification_view_id && (n = notify_history(i)); i++)
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
    size_t i;

    if (!notifications_enabled || !(n = bar_notification_shown(&hist)))
        return;
    if (px >= 0 && m && hist < 0) {
        bar_notification_box(
            m, n, hist, bar_notification_box_x, m->b.titlew, &b);
        if (bar_notification_picking()) {
            for (i = b.chip0; i < b.chip0 + b.nchip; i++)
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

/* Sizes and places the client's title bar. It spans the whole window, except
 * in the tabbed layout where each client of the group only gets its own slice
 * of the shared row - which is what turns the title bars into tabs. */
void bar_place_title_bar(Client* c)
{
    Monitor* m = c->mon;
    Client* w;
    int i = 0, n = 0, x = 0, tw;

    if (!c->title || !m)
        return;

    tw = c->geom.width - 2 * (int)c->bw;
    if (m->lt[m->sellt]->arrange == layout_tabbed && !c->isfloating &&
        !c->isfullscreen) {
        wl_list_for_each(w, &clients, link)
        {
            if (!VISIBLEON(w, m) || w->isfloating || w->isfullscreen)
                continue;
            if (w == c)
                i = n;
            n++;
        }
        if (n) {
            x = tw * i / n;
            tw = tw * (i + 1) / n - x;
        }
    }

    c->titlex = x;
    c->titlew = tw;
    wlr_scene_node_set_position(&c->title->node, (int)c->bw + x, (int)c->bw);
    wlr_scene_buffer_set_dest_size(c->title, MAX(tw, 1), bar_title_height(c));
}

int bar_title_height(Client* c)
{
    return title_bar && c->mon && !c->isfullscreen ? c->mon->t.real_height : 0;
}

int bar_handle_status_input(int fd, unsigned int mask, void*)
{
    static char buf[STATUS_MAX];
    static size_t len;
    static int skip; /* in a line too long for buf, up to its newline */
    char *p, *nl, *line = nullptr;
    ssize_t n;

    if (mask & WL_EVENT_ERROR)
        util_die("status in event error");
    if (mask & WL_EVENT_HANGUP) {
        bar_status_stop();
        return 0;
    }

    n = read(fd, buf + len, sizeof(buf) - 1 - len);
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
            return 0;
        util_die("read:");
    }
    /* EOF: the status process is gone. The fd stays readable forever, so
     * keeping the source alive would spin the event loop at full speed and
     * starve rendering and input. */
    if (n == 0) {
        bar_status_stop();
        return 0;
    }

    len += (size_t)n;
    buf[len] = '\0';
    for (p = buf; (nl = strchr(p, '\n')); p = nl + 1) {
        *nl = '\0';
        if (skip)
            skip = 0;
        else
            line = p;
    }
    if (line) {
        snprintf(bar_status_text, sizeof(bar_status_text), "%s", line);
        bar_draw_all();
    }

    /* keep the start of the next line */
    len = strlen(p);
    memmove(buf, p, len + 1);
    if (len == sizeof(buf) - 1) {
        len = 0;
        skip = 1;
    }

    return 0;
}

/* Detach the bar status text from stdin, for good. */
static void bar_status_stop(void)
{
    if (!status_event_source)
        return;

    wl_event_source_remove(status_event_source);
    status_event_source = nullptr;
}

void bar_toggle(const Arg*)
{
    /* under bar_single_monitor this hides the one bar from any monitor */
    Monitor* m = bar_monitor();

    if (!m)
        return;
    wlr_scene_node_set_enabled(&m->scene_buffer->node,
                               !m->scene_buffer->node.enabled);
    monitor_arrange_layers(m);
    bar_draw_all();
}

/* layout_arrange() leaves floating clients alone, and no client redraws a title
 * bar, so both are done by hand here */
void bar_toggle_title_bars(const Arg*)
{
    Monitor* m;
    Client* c;

    title_bar = !title_bar;

    wl_list_for_each(m, &monitors, link) layout_arrange(m);
    wl_list_for_each(c, &clients, link)
    {
        if (c->mon && c->isfloating && !c->isfullscreen)
            client_resize(c, c->geom, 1);
    }
    bar_draw_all();

    wl_list_for_each(m, &monitors, link)
    {
        if (m->wlr_output->enabled)
            wlr_output_schedule_frame(m->wlr_output);
    }
}

void bar_update(Monitor* m)
{
    int rw, rh;
    char fontattrs[12];
    int iconsize;

    wlr_output_transformed_resolution(m->wlr_output, &rw, &rh);
    /* the bar is narrower than the output by its padding on both sides */
    m->b.width =
        MAX(1, rw - 2 * (int)roundf((float)bar_padding * m->wlr_output->scale));
    m->b.real_width = (int)((float)m->b.width / m->wlr_output->scale);

    wlr_scene_node_set_enabled(&m->scene_buffer->node, bar_visible(m));

    buffer_pool_drop(m->pool, LENGTH(m->pool));
    buffer_pool_drop(m->sharepool, LENGTH(m->sharepool));

    if (m->b.scale == m->wlr_output->scale && m->drw)
        return;

    drwl_font_destroy(m->drw->font);
    snprintf(
        fontattrs, sizeof(fontattrs), "dpi=%.2f", 96. * m->wlr_output->scale);
    if (!(drwl_font_create(m->drw, font_count, fonts, fontattrs)))
        util_die("Could not load font");

    m->b.scale = m->wlr_output->scale;
    m->lrpad = m->drw->font->height;
    /* the automatic height follows the font, which is loaded at the output's
     * dpi, so bar_height scales it the same on every monitor */
    m->b.height = m->drw->font->height + 2;
    if (bar_height > 0)
        m->b.height = MAX((int)((float)m->b.height * bar_height + 0.5f),
                          m->drw->font->height);
    m->b.real_height = (int)((float)m->b.height / m->wlr_output->scale);
    m->t.height = m->drw->font->height + (int)title_padding;
    m->t.real_height = (int)((float)m->t.height / m->wlr_output->scale);

    if (bar_enabled && systray_enabled && tray_watcher.running) {
        if (m->tray)
            tray_destroy(m->tray);
        /* systray_icon_size is unscaled, like cursor_size; 0 fills the bar */
        iconsize = systray_icon_size
                       ? (int)((float)systray_icon_size * m->wlr_output->scale)
                       : m->b.height;
        m->tray =
            tray_create(m,
                        m->b.height,
                        MIN(iconsize, m->b.height),
                        (int)((float)systray_spacing * m->wlr_output->scale),
                        (int)((float)systray_padding * m->wlr_output->scale),
                        colors[SchemeNorm],
                        fonts,
                        fontattrs,
                        &bar_tray_notify,
                        &tray_watcher);
        if (!m->tray)
            util_die("Couldn't create tray for monitor");
        wl_list_insert(&tray_watcher.trays, &m->tray->link);
    }
}
