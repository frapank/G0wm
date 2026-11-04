/*
 * SPDX-FileCopyrightText: dwl-patches contributors
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the runner prompt and candidates drawn in the bar
 */

#include <drwl.h>
#include <math.h>
#include <pixman.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "desktop/runner/runner.h"
#include "desktop/systray/icon.h"
#include "internal.h"
#include "output/monitor.h"

/* function implementations */
/* icon scaled to fit size, centered in the bar */
void bar_draw_icon(Monitor* m, Icon* icon, int x, int size)
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
void bar_draw_icon_holder(Monitor* m, int x, int size)
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

int bar_runner_icon_size(Monitor* m)
{
    int size = runner_icon_size
                   ? (int)round(runner_icon_size * m->wlr_output->scale)
                   : m->drw->font->height;

    return MIN(size, (int)m->b.height);
}

/* The candidates after the prompt, a page at a time. The selected one sits
 * in a pill shaped like the selected tag dot. */
void bar_draw_candidates(Monitor* m, int x, int end)
{
    int n = runner_candidate_count(), sel = runner_selected();
    int per = (int)MIN(runner_max_candidates, (unsigned int)n);
    int fh = m->drw->font->height, bh = (int)m->b.height;
    int ph, pad, mg, tw, isz, iw, holder;
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
    for (int i = sel / per * per; i < n && i < sel / per * per + per; i++) {
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
