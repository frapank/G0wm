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
void bar_draw_icon(Monitor* monitor, Icon* icon, int start_x, int size)
{
    pixman_image_t* image = icon->img;
    int source_width = pixman_image_get_width(image);
    int source_height = pixman_image_get_height(image);

    if (source_width <= 0 || source_height <= 0 || size <= 0)
        return;

    int width = source_width >= source_height ? size : MAX(source_width * size / source_height, 1);
    int height = source_height >= source_width ? size : MAX(source_height * size / source_width, 1);

    pixman_transform_t transform;
    pixman_transform_init_scale(&transform, pixman_double_to_fixed((double)source_width / width),
                                pixman_double_to_fixed((double)source_height / height));
    pixman_image_set_transform(image, &transform);
    pixman_image_set_filter(image, PIXMAN_FILTER_BILINEAR, nullptr, 0);
    pixman_image_composite32(PIXMAN_OP_OVER, image, nullptr, monitor->drw->image, 0, 0, 0, 0,
                             start_x + (size - width) / 2, ((int)monitor->b.height - height) / 2,
                             width, height);
    pixman_image_set_transform(image, nullptr);
}

/* a rounded square where an icon is missing, colored by runner_icon */
void bar_draw_icon_holder(Monitor* monitor, int start_x, int size)
{
    int top_y = ((int)monitor->b.height - size) / 2;
    int frame_width = MIN((int)round(runner_icon_border * monitor->wlr_output->scale), size / 2);
    double radius = size / 2.0 * runner_icon_round;

    if (frame_width > 0)
        bar_draw_pill(monitor, start_x, top_y, size, size, radius, 0,
                      colors[SchemeRunnerIcon][ColBorder]);
    bar_draw_pill(monitor, start_x + frame_width, top_y + frame_width, size - 2 * frame_width,
                  size - 2 * frame_width, MAX(radius - frame_width, 0), 0,
                  colors[SchemeRunnerIcon][ColBg]);
}

int bar_runner_icon_size(Monitor* monitor)
{
    int size = runner_icon_size ? (int)round(runner_icon_size * monitor->wlr_output->scale)
                                : monitor->drw->font->height;

    return MIN(size, (int)monitor->b.height);
}

/* The candidates after the prompt, a page at a time. The selected one sits
 * in a pill shaped like the selected tag dot. */
void bar_draw_candidates(Monitor* monitor, int start_x, int end_x)
{
    int count = runner_candidate_count();
    int selected = runner_selected();
    int per_page = (int)MIN(runner_max_candidates, (unsigned int)count);
    int font_height = monitor->drw->font->height;
    int bar_px = (int)monitor->b.height;
    double scale = monitor->wlr_output->scale;

    int pill_height =
        runner_pill_height ? (int)round(runner_pill_height * scale) : font_height * 4 / 3;
    pill_height = MIN(MAX(pill_height, font_height), bar_px);
    int padding =
        runner_pill_padding ? (int)round(runner_pill_padding * scale) : monitor->lrpad / 2;
    double radius =
        pill_height / 2.0 * (runner_pill_round < 0 ? bar_tag_dot_round : runner_pill_round);

    /* drwl_text() paints behind the text too, keep that off the round ends */
    int margin = (pill_height - font_height) / 2;
    if (margin < radius)
        padding =
            MAX(padding,
                (int)ceil(radius - sqrt(radius * radius - (radius - margin) * (radius - margin))));

    int icon_size = MIN(bar_runner_icon_size(monitor), pill_height);
    int page_start = selected / per_page * per_page;
    for (int i = page_start; i < count && i < page_start + per_page; i++) {
        const char* text = runner_candidate(i);
        Icon* icon = runner_icons == RunnerIconsAll ? runner_icon(i) : nullptr;
        bool show_holder = runner_icons == RunnerIconsAll && !icon && runner_has_icons();
        int icon_width = icon || show_holder ? icon_size + padding / 2 : 0;
        int text_width = MIN((int)drwl_font_getwidth(monitor->drw, text),
                             end_x - start_x - 2 * padding - icon_width);
        if (text_width <= 0)
            break;
        if (i == selected)
            bar_draw_pill(monitor, start_x, (bar_px - pill_height) / 2,
                          icon_width + text_width + 2 * padding, pill_height, radius, 0,
                          colors[SchemeRunnerSel][ColBg]);
        if (icon)
            bar_draw_icon(monitor, icon, start_x + padding, icon_size);
        else if (show_holder)
            bar_draw_icon_holder(monitor, start_x + padding, icon_size);
        drwl_setscheme(monitor->drw, colors[i == selected ? SchemeRunnerSel : SchemeRunnerSuggest]);
        drwl_text(monitor->drw, start_x + padding + icon_width, (bar_px - font_height) / 2,
                  text_width, font_height, 0, text, 0);
        start_x += icon_width + text_width + 2 * padding;
    }
}
