/*
 * See LICENSE file for copyright and license details.
 *
 * private windows, masked in screen capture but not on screen
 */
#include "g0wm.h"
#include <wlr/interfaces/wlr_ext_image_capture_source_v1.h>

#define MAXBOXES 64

typedef struct {
    struct wlr_ext_image_capture_source_v1_interface impl;
    const struct wlr_ext_image_capture_source_v1_interface* orig;
    struct wlr_ext_image_capture_source_v1* source;
    struct wlr_output* output;
    Client* client;
    struct wl_listener destroy;
} Shield;

typedef struct {
    struct wlr_screencopy_frame_v1* frame;
    struct wlr_buffer* buf;
    struct wlr_box box;
} Copy;

/* function declarations */
static int blackout(struct wlr_buffer* buf, struct wlr_box* b, int n);
static void copyframe(struct wlr_ext_image_capture_source_v1* source,
                      struct wlr_ext_image_copy_capture_frame_v1* frame,
                      struct wlr_ext_image_capture_source_v1_frame_event* ev);
static int privateboxes(Monitor* m, struct wlr_box* out);
static void shield(struct wlr_ext_image_capture_source_v1* source,
                   struct wlr_output* output,
                   Client* c);
static void shielddestroy(struct wl_listener* listener, void* data);
static void wipe(struct wlr_buffer* buf);

/* variables */
static struct wl_array copies;
static struct wlr_box masks[MAXBOXES];
static int nmasks;

/* function implementations */

static int blackout(struct wlr_buffer* buf, struct wlr_box* b, int n)
{
    struct wlr_box all = { 0, 0, buf->width, buf->height };
    struct wlr_render_pass* pass;
    uint32_t format, px, *row;
    size_t stride;
    void* data;
    int i, x, y;

    for (i = 0; i < n; i++)
        if (!wlr_box_intersection(&b[i], &b[i], &all))
            b[i] = (struct wlr_box){ 0 };

    if (wlr_buffer_begin_data_ptr_access(
            buf, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &data, &format, &stride)) {
        switch (format) {
            case DRM_FORMAT_ARGB8888:
            case DRM_FORMAT_XRGB8888:
            case DRM_FORMAT_ABGR8888:
            case DRM_FORMAT_XBGR8888:
                px = 0xff000000;
                break;
            case DRM_FORMAT_ARGB2101010:
            case DRM_FORMAT_XRGB2101010:
            case DRM_FORMAT_ABGR2101010:
            case DRM_FORMAT_XBGR2101010:
                px = 0xc0000000;
                break;
            default:
                wlr_buffer_end_data_ptr_access(buf);
                return 0;
        }
        for (i = 0; i < n; i++)
            for (y = b[i].y; y < b[i].y + b[i].height; y++) {
                row = (uint32_t*)((uint8_t*)data + y * stride) + b[i].x;
                for (x = 0; x < b[i].width; x++)
                    row[x] = px;
            }
        wlr_buffer_end_data_ptr_access(buf);
        return 1;
    }

    if (!(pass = wlr_renderer_begin_buffer_pass(drw, buf, NULL)))
        return 0;
    for (i = 0; i < n; i++)
        if (!wlr_box_empty(&b[i]))
            wlr_render_pass_add_rect(
                pass,
                &(struct wlr_render_rect_options){ .box = b[i],
                                                   .color = { 0, 0, 0, 1 } });
    return wlr_render_pass_submit(pass);
}

/* fail the frame rather than leak it */
static void copyframe(struct wlr_ext_image_capture_source_v1* source,
                      struct wlr_ext_image_copy_capture_frame_v1* frame,
                      struct wlr_ext_image_capture_source_v1_frame_event* ev)
{
    Shield* s = wl_container_of(source->impl, s, impl);
    struct wlr_box b[MAXBOXES];
    struct timespec now;
    Monitor* m;
    int n;

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (s->client) {
        if (!s->client->isprivate) {
            s->orig->copy_frame(source, frame, ev);
            return;
        }
        b[0] = (struct wlr_box){
            0, 0, frame->buffer->width, frame->buffer->height
        };
        if (!blackout(frame->buffer, b, 1))
            goto fail;
        wlr_ext_image_copy_capture_frame_v1_ready(
            frame, WL_OUTPUT_TRANSFORM_NORMAL, &now);
        return;
    }

    if (!(m = s->output->data))
        goto fail;
    if (!(n = privateboxes(m, b))) {
        s->orig->copy_frame(source, frame, ev);
        return;
    }
    if (!m->shown ||
        !wlr_ext_image_copy_capture_frame_v1_copy_buffer(
            frame, m->shown, drw) ||
        !blackout(frame->buffer, b, n))
        goto fail;
    wlr_ext_image_copy_capture_frame_v1_ready(
        frame, s->output->transform, &now);
    return;

fail:
    wlr_ext_image_copy_capture_frame_v1_fail(
        frame, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN);
}

/* screencopy copies during the commit: mask before ready is sent */
void privacyafter(Monitor* m)
{
    struct wlr_screencopy_frame_v1* f;
    struct wlr_box b[MAXBOXES];
    Copy* cp;
    int i, gone;

    m->shown = NULL;
    wl_array_for_each(cp, &copies)
    {
        gone = 1;
        wl_list_for_each(f, &screencopy_mgr->frames, link)
        {
            if (f == cp->frame)
                gone = 0;
        }
        if (gone) {
            for (i = 0; i < nmasks; i++) {
                b[i] = masks[i];
                b[i].x -= cp->box.x;
                b[i].y -= cp->box.y;
            }
            if (!blackout(cp->buf, b, nmasks))
                wipe(cp->buf);
        }
        wlr_buffer_unlock(cp->buf);
    }
    copies.size = 0;
}

int privacybefore(Monitor* m, struct wlr_output_state* state)
{
    struct wlr_screencopy_frame_v1* f;
    Copy* cp;

    m->shown = state->buffer;
    copies.size = 0;
    if (!screencopy_mgr || !(nmasks = privateboxes(m, masks)))
        return 1;
    wl_list_for_each(f, &screencopy_mgr->frames, link)
    {
        if (f->output != m->wlr_output || !f->buffer)
            continue;
        if (!(cp = wl_array_add(&copies, sizeof(*cp)))) {
            privacyafter(m);
            return 0;
        }
        /* ready must not be flushed before privacyafter() */
        wl_client_flush(wl_resource_get_client(f->resource));
        *cp = (Copy){ f, wlr_buffer_lock(f->buffer), f->box };
    }
    return 1;
}

void privacycapture(struct wl_listener* listener, void* data)
{
    struct wlr_ext_image_copy_capture_session_v1* session = data;
    struct wlr_output* o =
        wlr_output_try_from_ext_image_capture_source_v1(session->source);

    if (o)
        shield(session->source, o, NULL);
}

void privacydamage(void)
{
    Monitor* m;

    wl_list_for_each(m, &mons, link)
    {
        wlr_damage_ring_add_whole(&m->scene_output->damage_ring);
        wlr_output_schedule_frame(m->wlr_output);
    }
}

void privacyfini(void)
{
    wl_array_release(&copies);
}

/* contents of private windows on m, in buffer pixels */
static int privateboxes(Monitor* m, struct wlr_box* out)
{
    struct wlr_output* o = m->wlr_output;
    struct wlr_box b, all;
    float sc = o->scale;
    Client* c;
    int n = 0, tw, th, t, x0, y0, x1, y1;

    wlr_output_transformed_resolution(o, &tw, &th);
    all = (struct wlr_box){ 0, 0, tw, th };
    wl_list_for_each(c, &clients, link)
    {
        if (!c->isprivate || !c->scene->node.enabled ||
            !c->scene_surface->node.enabled)
            continue;
        t = titleheight(c);
        b = (struct wlr_box){ c->geom.x + (int)c->bw,
                              c->geom.y + (int)c->bw + t,
                              c->geom.width - 2 * (int)c->bw,
                              c->geom.height - 2 * (int)c->bw - t };
        x0 = (int)floor((b.x - m->m.x) * sc);
        y0 = (int)floor((b.y - m->m.y) * sc);
        x1 = (int)ceil((b.x + b.width - m->m.x) * sc);
        y1 = (int)ceil((b.y + b.height - m->m.y) * sc);
        b = (struct wlr_box){ x0, y0, x1 - x0, y1 - y0 };
        if (!wlr_box_intersection(&b, &b, &all))
            continue;
        wlr_box_transform(
            &b, &b, wlr_output_transform_invert(o->transform), tw, th);
        if (m->zoom > 1 && m->zoomtex) {
            x0 = (int)floor((b.x - m->zoomsrc.x) * m->zoom);
            y0 = (int)floor((b.y - m->zoomsrc.y) * m->zoom);
            x1 = (int)ceil((b.x + b.width - m->zoomsrc.x) * m->zoom);
            y1 = (int)ceil((b.y + b.height - m->zoomsrc.y) * m->zoom);
            b = (struct wlr_box){ x0, y0, x1 - x0, y1 - y0 };
        }
        /* too many, mask everything */
        if (n == MAXBOXES) {
            out[0] = (struct wlr_box){ 0, 0, o->width, o->height };
            return 1;
        }
        out[n++] = b;
    }
    return n;
}

static void shield(struct wlr_ext_image_capture_source_v1* source,
                   struct wlr_output* output,
                   Client* c)
{
    Shield* s;

    if (source->impl->copy_frame == copyframe || !(s = ecalloc(1, sizeof(*s))))
        return;
    s->orig = source->impl;
    s->impl = *source->impl;
    s->impl.copy_frame = copyframe;
    s->source = source;
    s->output = output;
    s->client = c;
    source->impl = &s->impl;
    LISTEN(&source->events.destroy, &s->destroy, shielddestroy);
}

void shieldclient(Client* c)
{
    if (c->capture)
        shield(c->capture, NULL, c);
}

/* the source is still used after this signal */
static void shielddestroy(struct wl_listener* listener, void* data)
{
    Shield* s = wl_container_of(listener, s, destroy);

    s->source->impl = s->orig;
    wl_list_remove(&s->destroy.link);
    free(s);
}

/* title that others get to see */
const char* showntitle(Client* c)
{
    static char buf[256];
    const char* s;
    size_t n = 0;

    if (!c->isprivate)
        return client_get_title(c);
    for (s = privatetitle; *s && n < sizeof(buf) - 1; s++) {
        if (!strncmp(s, "{app}", 5)) {
            n += snprintf(buf + n, sizeof(buf) - n, "%s", client_get_appid(c));
            n = MIN(n, sizeof(buf) - 1);
            s += 4;
        } else {
            buf[n++] = *s;
        }
    }
    buf[n] = '\0';
    return buf;
}

void toggleprivate(const Arg* arg)
{
    Client* sel = focustop(selmon);

    if (!sel)
        return;
    sel->isprivate = !sel->isprivate;
    toplevelupdate(sel);
    privacydamage();
    drawbars();
}

static void wipe(struct wlr_buffer* buf)
{
    struct wlr_render_pass* pass;
    uint32_t format;
    size_t stride;
    void* data;

    if (wlr_buffer_begin_data_ptr_access(
            buf, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &data, &format, &stride)) {
        memset(data, 0, stride * (size_t)buf->height);
        wlr_buffer_end_data_ptr_access(buf);
        return;
    }
    if (!(pass = wlr_renderer_begin_buffer_pass(drw, buf, NULL))) {
        wlr_log(WLR_ERROR, "privacy: cannot black out a screencopy frame");
        return;
    }
    wlr_render_pass_add_rect(pass,
                             &(struct wlr_render_rect_options){
                                 .box = { 0, 0, buf->width, buf->height },
                                 .color = { 0, 0, 0, 1 } });
    if (!wlr_render_pass_submit(pass))
        wlr_log(WLR_ERROR, "privacy: cannot black out a screencopy frame");
}
