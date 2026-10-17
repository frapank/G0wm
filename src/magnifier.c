/*
 * See LICENSE file for copyright and license details.
 *
 * magnifier: zooms a monitor in around the pointer. While zoomed the scene is
 * rendered unzoomed into buffers of our own, and the part on show is drawn
 * from there, scaled, onto the output.
 */
#include "g0wm.h"

#define ZOOMMAX 16.0f

/* function declarations */
static void wholedamage(struct wlr_output* o, struct wlr_output_state* state);

/* function implementations */
void magnifierdrop(Monitor* m)
{
    wlr_texture_destroy(m->zoomtex);
    wlr_buffer_unlock(m->zoombuf);
    wlr_swapchain_destroy(m->zoomchain);
    m->zoomtex = NULL;
    m->zoombuf = NULL;
    m->zoomchain = NULL;
}

/* The frame of a zoomed monitor, or of one just leaving the zoom. 0 when
 * there is nothing new to show. */
int magnifierframe(Monitor* m, struct wlr_output_state* state)
{
    struct wlr_output* o = m->wlr_output;
    struct wlr_scene_output_state_options opts = { 0 };
    struct wlr_render_pass* pass;
    struct wlr_fbox src;
    double fx, fy;
    int tw, th;

    if (m->zoom <= 1) {
        /* the output's own buffers still hold zoomed frames */
        magnifierdrop(m);
        wlr_damage_ring_add_whole(&m->scene_output->damage_ring);
        if (!wlr_scene_output_build_state(m->scene_output, state, NULL))
            return 0;
        wholedamage(o, state);
        return 1;
    }

    if (!wlr_output_configure_primary_swapchain(o, state, &o->swapchain))
        return 0;
    if (!m->zoomchain || m->zoomchain->width != o->width ||
        m->zoomchain->height != o->height) {
        magnifierdrop(m);
        /* ponytail: the output's format, taken to be one the renderer can
         * sample too; intersect with its texture formats if one is not */
        if (!(m->zoomchain = wlr_swapchain_create(
                  alloc, o->width, o->height, &o->swapchain->format)))
            return 0;
    }

    /* the part on show, worked out upright and then turned the way the
     * output's buffers are. It is placed in proportion to the pointer, which
     * so stays over what it is over unzoomed: input needs no remapping. */
    wlr_output_transformed_resolution(o, &tw, &th);
    fx = (cursor->x - m->m.x) / m->m.width;
    fy = (cursor->y - m->m.y) / m->m.height;
    src.width = tw / m->zoom;
    src.height = th / m->zoom;
    src.x = (fx < 0 ? 0 : fx > 1 ? 1 : fx) * (tw - src.width);
    src.y = (fy < 0 ? 0 : fy > 1 ? 1 : fy) * (th - src.height);
    wlr_fbox_transform(
        &src, &src, wlr_output_transform_invert(o->transform), tw, th);

    if (m->zoomtex && !wlr_scene_output_needs_frame(m->scene_output)) {
        if (wlr_fbox_equal(&src, &m->zoomsrc))
            return 0;
    } else {
        opts.swapchain = m->zoomchain;
        if (!wlr_scene_output_build_state(m->scene_output, state, &opts) ||
            !state->buffer)
            return 0;
        wlr_texture_destroy(m->zoomtex);
        wlr_buffer_unlock(m->zoombuf);
        m->zoombuf = wlr_buffer_lock(state->buffer);
        if (!(m->zoomtex = wlr_texture_from_buffer(drw, m->zoombuf))) {
            wlr_log(WLR_ERROR, "magnifier: cannot draw from the frame");
            return 0;
        }
        /* they belong to the frame, not to the zoomed copy made below */
        state->committed &= ~(WLR_OUTPUT_STATE_WAIT_TIMELINE |
                              WLR_OUTPUT_STATE_SIGNAL_TIMELINE);
    }
    m->zoomsrc = src;

    if (!(pass = wlr_output_begin_render_pass(o, state, NULL)))
        return 0;
    wlr_render_pass_add_texture(
        pass,
        &(struct wlr_render_texture_options){
            .texture = m->zoomtex,
            .src_box = src,
            .dst_box = { .width = o->width, .height = o->height },
            .filter_mode = WLR_SCALE_FILTER_BILINEAR,
        });
    if (!wlr_render_pass_submit(pass))
        return 0;
    wholedamage(o, state);
    return 1;
}

void magnify(const Arg* arg)
{
    Monitor* m = xytomon(cursor->x, cursor->y);
    float z;

    if (!m && !(m = selmon))
        return;

    /* a step and its opposite cancel out: +0.25 is x1.25, -0.25 is /1.25 */
    if (arg->f == 0)
        z = 1;
    else if (arg->f > 0)
        z = m->zoom * (1 + arg->f);
    else
        z = m->zoom / (1 - arg->f);
    z = z < 1.01f ? 1 : z > ZOOMMAX ? ZOOMMAX : z;
    if (z == m->zoom)
        return;

    /* while zoomed the pointer is drawn into the frame, to be zoomed with it,
     * and no window may go straight to the screen past the zoom */
    if ((z > 1) != (m->zoom > 1)) {
        wlr_output_lock_software_cursors(m->wlr_output, z > 1);
        wlr_output_lock_attach_render(m->wlr_output, z > 1);
    }
    m->zoom = z;
    wlr_output_schedule_frame(m->wlr_output);
}

static void wholedamage(struct wlr_output* o, struct wlr_output_state* state)
{
    pixman_region32_t damage;

    pixman_region32_init_rect(&damage, 0, 0, o->width, o->height);
    wlr_output_state_set_damage(state, &damage);
    pixman_region32_fini(&damage);
}
