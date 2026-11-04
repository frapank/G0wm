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

/* function declarations */
static int bar_visible(Monitor* m);

/* variables */
char bar_status_text[STATUS_MAX];
/* redraws the bars while an urgent dot pulses */
static struct wl_event_source* bar_pulse_timer;

/* function implementations */
bool bar_accepts_input(struct wlr_scene_buffer*, double*, double*)
{
    return true;
}

/* The first enabled output matched by monitor_rules[], NULL if there is none.
 */
Monitor* bar_main_monitor(void)
{
    Monitor* m;

    for (const MonitorRule* r = monitor_rules;
         r < monitor_rules + monitor_rule_count;
         r++) {
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
Monitor* bar_monitor(void)
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
void bar_draw_pill(Monitor* m,
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
    int stride, sy, cov;
    double fx, fy;

    if (w <= 0 || h <= 0 ||
        !(mask = pixman_image_create_bits(PIXMAN_a8, w, h, nullptr, 0)))
        return;
    r = fmin(fmax(r, 0), fmin(w, h) / 2.0);
    data = (uint8_t*)pixman_image_get_data(mask);
    stride = pixman_image_get_stride(mask);

    /* 4x4 supersampling */
    for (int py = 0; py < h; py++)
        for (int px = 0; px < w; px++) {
            for (cov = 0, sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
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
uint32_t bar_fade(uint32_t rgba)
{
    return (rgba & ~0xffu) | (rgba & 0xffu) / 3;
}

/* the dot marks the windows that differ from always_private */
int bar_private_dot(Client* c, uint32_t* rgba)
{
    if (!c->isprivate == !always_private)
        return 0;
    *rgba = c->isprivate ? private_color : public_color;
    return 1;
}

double bar_private_radius(int d)
{
    return d / 2.0 * (private_round < 0 ? bar_tag_dot_round : private_round);
}

/* scheme with an opaque background */
uint32_t* bar_opaque_scheme(int scheme)
{
    static uint32_t scm[NumSchemes][3];

    memcpy(scm[scheme], colors[scheme], sizeof(scm[scheme]));
    scm[scheme][ColBg] |= 0xff;
    return scm[scheme];
}

/* a pool of one, dropped when the size changes */
Buffer* bar_share_buffer(Buffer** pool, int w, int h)
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
    uint32_t occ = 0, urg = 0;
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
    for (uint32_t i = 0; i < tag_count && bar_tag_dots; i++) {
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
    for (uint32_t i = 0; i < tag_count && !bar_tag_dots; i++) {
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
