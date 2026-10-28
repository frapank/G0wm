/*
 * See LICENSE file for copyright and license details.
 *
 * text-input-v3 and input-method-v2: the bridge between apps and an input
 * method, with its keyboard grab and candidate popup
 */
#include "g0wm.h"

typedef struct {
    struct wlr_text_input_v3* wlr;
    struct wl_list link; /* textinputs */
    struct wl_listener enable;
    struct wl_listener commit;
    struct wl_listener disable;
    struct wl_listener destroy;
} TextInput;

typedef struct {
    struct wlr_input_popup_surface_v2* popup;
    struct wlr_scene_tree* tree;
    /* last rectangle sent, an unchanged one makes the IM recommit forever;
     * negative width means none yet */
    struct wlr_box sent;
    struct wl_list link; /* popups */
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
} InputPopup;

typedef struct {
    struct wlr_surface* surface;
    int x, y, found;
} SurfaceFind;

/* function declarations */
static void findsurface(struct wlr_scene_buffer* buffer,
                        int sx,
                        int sy,
                        void* data);
static void imactivate(TextInput* ti);
static void imcommit(struct wl_listener* listener, void* data);
static void imdeactivate(void);
static void imdestroy(struct wl_listener* listener, void* data);
static void imgrabdestroy(struct wl_listener* listener, void* data);
static void imgrabkeyboard(struct wl_listener* listener, void* data);
static void imnewpopup(struct wl_listener* listener, void* data);
static void imstate(TextInput* ti);
static struct wlr_input_method_keyboard_grab_v2* keygrab(KeyboardGroup* g);
static void popupcommit(struct wl_listener* listener, void* data);
static void popupdestroy(struct wl_listener* listener, void* data);
static void popupmap(struct wl_listener* listener, void* data);
static void popupplace(InputPopup* p);
static void popupsupdate(void);
static void popupunmap(struct wl_listener* listener, void* data);
static int surfacecoords(struct wlr_surface* surface, int* x, int* y);
static void ticommit(struct wl_listener* listener, void* data);
static void tidestroy(struct wl_listener* listener, void* data);
static void tidisable(struct wl_listener* listener, void* data);
static void tienable(struct wl_listener* listener, void* data);
static void tisync(struct wlr_surface* focus);
static void tiupdate(TextInput* ti);

/* variables */
static struct wlr_input_method_v2* im;
static TextInput* served; /* the text input the input method is active for */
static struct wl_list textinputs = { &textinputs, &textinputs };
static struct wl_list popups = { &popups, &popups };
static struct wl_listener im_commit = { .notify = imcommit };
static struct wl_listener im_destroy = { .notify = imdestroy };
static struct wl_listener im_grab = { .notify = imgrabkeyboard };
static struct wl_listener im_grab_destroy = { .notify = imgrabdestroy };
static struct wl_listener im_popup = { .notify = imnewpopup };

/* function implementations */

static void findsurface(struct wlr_scene_buffer* buffer,
                        int sx,
                        int sy,
                        void* data)
{
    SurfaceFind* f = data;
    struct wlr_scene_surface* ss = wlr_scene_surface_try_from_buffer(buffer);

    if (f->found || !ss || ss->surface != f->surface)
        return;
    f->x = sx;
    f->y = sy;
    f->found = 1;
}

static int surfacecoords(struct wlr_surface* surface, int* x, int* y)
{
    SurfaceFind f = { .surface = surface };

    wlr_scene_node_for_each_buffer(&scene->tree.node, findsurface, &f);
    *x = f.x;
    *y = f.y;
    return f.found;
}

static void popupplace(InputPopup* p)
{
    struct wlr_surface* focus = served ? served->wlr->focused_surface : NULL;
    struct wlr_box rect = { 0 };
    struct wlr_box box;
    Monitor* m;
    int sx, sy, w, h, x, y;

    if (!focus || !p->popup->surface->mapped ||
        !surfacecoords(focus, &sx, &sy)) {
        wlr_scene_node_set_enabled(&p->tree->node, 0);
        p->sent.width = -1;
        return;
    }
    if (served->wlr->active_features &
        WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE)
        rect = served->wlr->current.cursor_rectangle;
    else
        rect.width = focus->current.width;
    rect.x += sx;
    rect.y += sy;

    if (!(m = xytomon(rect.x, rect.y)))
        m = selmon;
    if (!m) {
        wlr_scene_node_set_enabled(&p->tree->node, 0);
        p->sent.width = -1;
        return;
    }

    w = p->popup->surface->current.width;
    h = p->popup->surface->current.height;
    x = rect.x;
    y = rect.y + rect.height;
    if (x + w > m->m.x + m->m.width &&
        rect.x + rect.width - m->m.x > m->m.x + m->m.width - rect.x)
        x = rect.x + rect.width - w;
    if (y + h > m->m.y + m->m.height &&
        rect.y - m->m.y > m->m.y + m->m.height - (rect.y + rect.height))
        y = rect.y - h;
    x = MAX(m->m.x, MIN(x, m->m.x + m->m.width - w));
    y = MAX(m->m.y, MIN(y, m->m.y + m->m.height - h));

    wlr_scene_node_set_position(&p->tree->node, x, y);
    wlr_scene_node_raise_to_top(&p->tree->node);
    wlr_scene_node_set_enabled(&p->tree->node, 1);

    box = (struct wlr_box){ rect.x - x, rect.y - y, rect.width, rect.height };
    if (!wlr_box_equal(&box, &p->sent)) {
        p->sent = box;
        wlr_input_popup_surface_v2_send_text_input_rectangle(p->popup, &box);
    }
}

static void popupsupdate(void)
{
    InputPopup* p;

    wl_list_for_each(p, &popups, link) popupplace(p);
}

static void popupmap(struct wl_listener* listener, void* data)
{
    InputPopup* p = wl_container_of(listener, p, map);

    popupplace(p);
}

static void popupunmap(struct wl_listener* listener, void* data)
{
    InputPopup* p = wl_container_of(listener, p, unmap);

    popupplace(p);
}

static void popupcommit(struct wl_listener* listener, void* data)
{
    InputPopup* p = wl_container_of(listener, p, commit);

    popupplace(p);
}

static void popupdestroy(struct wl_listener* listener, void* data)
{
    InputPopup* p = wl_container_of(listener, p, destroy);

    wl_list_remove(&p->map.link);
    wl_list_remove(&p->unmap.link);
    wl_list_remove(&p->commit.link);
    wl_list_remove(&p->destroy.link);
    wl_list_remove(&p->link);
    wlr_scene_node_destroy(&p->tree->node);
    free(p);
}

static void imnewpopup(struct wl_listener* listener, void* data)
{
    struct wlr_input_popup_surface_v2* s = data;
    InputPopup* p = ecalloc(1, sizeof(*p));

    p->popup = s;
    p->tree = wlr_scene_tree_create(layers[LyrOverlay]);
    wlr_scene_subsurface_tree_create(p->tree, s->surface);
    wlr_scene_node_set_enabled(&p->tree->node, 0);
    p->sent.width = -1;
    LISTEN(&s->surface->events.map, &p->map, popupmap);
    LISTEN(&s->surface->events.unmap, &p->unmap, popupunmap);
    LISTEN(&s->surface->events.commit, &p->commit, popupcommit);
    LISTEN(&s->events.destroy, &p->destroy, popupdestroy);
    wl_list_insert(&popups, &p->link);
    popupplace(p);
}

static void imstate(TextInput* ti)
{
    struct wlr_text_input_v3* t = ti->wlr;

    if (t->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT)
        wlr_input_method_v2_send_surrounding_text(
            im,
            t->current.surrounding.text ? t->current.surrounding.text : "",
            t->current.surrounding.cursor,
            t->current.surrounding.anchor);
    wlr_input_method_v2_send_text_change_cause(im,
                                               t->current.text_change_cause);
    if (t->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE)
        wlr_input_method_v2_send_content_type(
            im, t->current.content_type.hint, t->current.content_type.purpose);
    popupsupdate();
    wlr_input_method_v2_send_done(im);
}

static void imdeactivate(void)
{
    if (!served)
        return;
    served = NULL;
    if (im) {
        wlr_input_method_v2_send_deactivate(im);
        wlr_input_method_v2_send_done(im);
    }
    popupsupdate();
}

static void imactivate(TextInput* ti)
{
    if (served)
        imdeactivate();
    served = ti;
    wlr_input_method_v2_send_activate(im);
    imstate(ti);
}

static void tiupdate(TextInput* ti)
{
    if (!im || !ti->wlr->focused_surface || !ti->wlr->current_enabled)
        return;
    if (ti != served)
        imactivate(ti);
    else
        imstate(ti);
}

static void tienable(struct wl_listener* listener, void* data)
{
    TextInput* ti = wl_container_of(listener, ti, enable);

    tiupdate(ti);
}

static void ticommit(struct wl_listener* listener, void* data)
{
    TextInput* ti = wl_container_of(listener, ti, commit);

    tiupdate(ti);
}

static void tidisable(struct wl_listener* listener, void* data)
{
    TextInput* ti = wl_container_of(listener, ti, disable);

    if (ti == served)
        imdeactivate();
}

static void tidestroy(struct wl_listener* listener, void* data)
{
    TextInput* ti = wl_container_of(listener, ti, destroy);

    if (ti == served)
        imdeactivate();
    wl_list_remove(&ti->enable.link);
    wl_list_remove(&ti->commit.link);
    wl_list_remove(&ti->disable.link);
    wl_list_remove(&ti->destroy.link);
    wl_list_remove(&ti->link);
    free(ti);
}

/* text inputs follow the keyboard focus while an input method is connected,
 * and only enter a surface of their own client */
static void tisync(struct wlr_surface* focus)
{
    struct wlr_surface* surface = im ? focus : NULL;
    TextInput* ti;

    if (served && (!served->wlr->focused_surface ||
                   served->wlr->focused_surface != surface))
        imdeactivate();
    wl_list_for_each(ti, &textinputs, link)
    {
        if (ti->wlr->focused_surface == surface)
            continue;
        if (ti->wlr->focused_surface)
            wlr_text_input_v3_send_leave(ti->wlr);
        if (surface && wl_resource_get_client(ti->wlr->resource) ==
                           wl_resource_get_client(surface->resource))
            wlr_text_input_v3_send_enter(ti->wlr, surface);
    }
    popupsupdate();
}

static void imcommit(struct wl_listener* listener, void* data)
{
    if (!served || !served->wlr->focused_surface)
        return;
    if (im->current.preedit.text)
        wlr_text_input_v3_send_preedit_string(served->wlr,
                                              im->current.preedit.text,
                                              im->current.preedit.cursor_begin,
                                              im->current.preedit.cursor_end);
    if (im->current.commit_text)
        wlr_text_input_v3_send_commit_string(served->wlr,
                                             im->current.commit_text);
    if (im->current.delete.before_length || im->current.delete.after_length)
        wlr_text_input_v3_send_delete_surrounding_text(
            served->wlr,
            im->current.delete.before_length,
            im->current.delete.after_length);
    wlr_text_input_v3_send_done(served->wlr);
}

static void imgrabkeyboard(struct wl_listener* listener, void* data)
{
    struct wlr_input_method_keyboard_grab_v2* grab = data;
    struct wlr_keyboard* kb = kb_group ? &kb_group->wlr_group->keyboard : NULL;

    wlr_input_method_keyboard_grab_v2_set_keyboard(grab, kb);
    if (kb)
        wlr_input_method_keyboard_grab_v2_send_modifiers(grab, &kb->modifiers);
    wl_signal_add(&grab->events.destroy, &im_grab_destroy);
}

/* resend the real modifiers, the grab may have changed them */
static void imgrabdestroy(struct wl_listener* listener, void* data)
{
    struct wlr_keyboard* kb = wlr_seat_get_keyboard(seat);

    wl_list_remove(&im_grab_destroy.link);
    if (kb)
        wlr_seat_keyboard_notify_modifiers(seat, &kb->modifiers);
}

static void imdestroy(struct wl_listener* listener, void* data)
{
    wl_list_remove(&im_commit.link);
    wl_list_remove(&im_destroy.link);
    wl_list_remove(&im_grab.link);
    wl_list_remove(&im_popup.link);
    im = NULL;
    served = NULL;
    tisync(NULL);
}

void createinputmethod(struct wl_listener* listener, void* data)
{
    if (im) {
        wlr_input_method_v2_send_unavailable(data);
        return;
    }
    im = data;
    wl_signal_add(&im->events.commit, &im_commit);
    wl_signal_add(&im->events.grab_keyboard, &im_grab);
    wl_signal_add(&im->events.new_popup_surface, &im_popup);
    wl_signal_add(&im->events.destroy, &im_destroy);
    tisync(seat->keyboard_state.focused_surface);
}

void createtextinput(struct wl_listener* listener, void* data)
{
    struct wlr_text_input_v3* wlr = data;
    TextInput* ti;

    if (wlr->seat != seat)
        return;
    ti = ecalloc(1, sizeof(*ti));
    ti->wlr = wlr;
    LISTEN(&wlr->events.enable, &ti->enable, tienable);
    LISTEN(&wlr->events.commit, &ti->commit, ticommit);
    LISTEN(&wlr->events.disable, &ti->disable, tidisable);
    LISTEN(&wlr->events.destroy, &ti->destroy, tidestroy);
    wl_list_insert(&textinputs, &ti->link);
    tisync(seat->keyboard_state.focused_surface);
}

/* A window moved or resized: the popup follows the text cursor with it. */
void textinputarrange(void)
{
    popupsupdate();
}

void textinputfocus(struct wl_listener* listener, void* data)
{
    struct wlr_seat_keyboard_focus_change_event* event = data;

    tisync(event->new_surface);
}

static struct wlr_input_method_keyboard_grab_v2* keygrab(KeyboardGroup* g)
{
    struct wlr_input_method_keyboard_grab_v2* grab;

    if (locked || !im || !(grab = im->keyboard_grab))
        return NULL;
    /* the input method types through its own virtual keyboard */
    if (g->vkb && wl_resource_get_client(g->vkb->resource) ==
                      wl_resource_get_client(grab->resource))
        return NULL;
    return grab;
}

/* Returns 1 if the input method took the key. A release goes where its press
 * went, so neither side is left with a key that stays down. */
int textinputkey(KeyboardGroup* g, struct wlr_keyboard_key_event* event)
{
    struct wlr_input_method_keyboard_grab_v2* grab = keygrab(g);
    uint32_t* word;
    uint32_t bit;

    if (event->keycode >= KEY_CNT)
        return 0;
    word = &g->imkeys[event->keycode / 32];
    bit = 1u << (event->keycode % 32);
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        if (!grab)
            return 0;
        *word |= bit;
    } else {
        if (!(*word & bit))
            return 0;
        *word &= ~bit;
        if (!grab)
            return 1;
    }
    wlr_input_method_keyboard_grab_v2_set_keyboard(grab,
                                                   &g->wlr_group->keyboard);
    wlr_input_method_keyboard_grab_v2_send_key(
        grab, event->time_msec, event->keycode, event->state);
    return 1;
}

int textinputmodifiers(KeyboardGroup* g)
{
    struct wlr_input_method_keyboard_grab_v2* grab = keygrab(g);

    if (!grab)
        return 0;
    wlr_input_method_keyboard_grab_v2_set_keyboard(grab,
                                                   &g->wlr_group->keyboard);
    wlr_input_method_keyboard_grab_v2_send_modifiers(
        grab, &g->wlr_group->keyboard.modifiers);
    return 1;
}
