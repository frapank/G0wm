/*
 * See LICENSE file for copyright and license details.
 *
 * drawing tablets (pens and pads, over tablet-v2) and touchscreens
 */
#include "g0wm.h"

typedef struct {
    struct wlr_tablet* wlr_tablet;
    struct wlr_tablet_v2_tablet* v2; /* NULL if creation failed */
    struct wl_list link;             /* tablets */
    struct wl_listener destroy;
} Tablet;

typedef struct {
    struct wlr_tablet_v2_tablet_tool* v2; /* NULL for a totem */
    double ox, oy;         /* layout position of the surface it is on */
    double tilt_x, tilt_y; /* wlroots only reports the axis that changed */
    int emulating;         /* the tip went down as a left click */
    uint32_t emubuttons;   /* 1 << (button & 31), sent as pointer buttons */
    struct wl_listener set_cursor;
    struct wl_listener destroy;
} TabletTool;

typedef struct {
    struct wlr_tablet_v2_tablet_pad* v2;
    struct wlr_input_device* device;
    Tablet* tablet; /* NULL until its tablet shows up */
    struct wlr_surface* focus;
    struct wl_list link; /* pads */
    struct wl_listener button;
    struct wl_listener ring;
    struct wl_listener strip;
    struct wl_listener focus_destroy;
    struct wl_listener destroy;
} TabletPad;

typedef struct {
    struct wlr_touch* wlr_touch;
    struct wl_list link; /* touchdevs */
    struct wl_listener destroy;
} Touch;

/* function declarations */
static void emulate(uint32_t time, uint32_t button, int pressed);
static struct wlr_output* outputnamed(const char* name);
static void padattach(TabletPad* p);
static void padbutton(struct wl_listener* listener, void* data);
static void paddestroy(struct wl_listener* listener, void* data);
static void padfocus(TabletPad* p, struct wlr_surface* surface);
static void padring(struct wl_listener* listener, void* data);
static void padstrip(struct wl_listener* listener, void* data);
static void padunfocus(struct wl_listener* listener, void* data);
static int samegroup(struct wlr_input_device* a, struct wlr_input_device* b);
static uint32_t stylusbutton(uint32_t button);
static void tabletconfigure(Tablet* t);
static void tabletdestroy(struct wl_listener* listener, void* data);
static int tipbound(void);
static TabletTool* tool(struct wlr_tablet_tool* wlr_tool);
static void tooldestroy(struct wl_listener* listener, void* data);
static void toolmotion(TabletTool* t,
                       Tablet* tb,
                       uint32_t time,
                       double x,
                       double y);
static void toolsetcursor(struct wl_listener* listener, void* data);
static void touchdestroy(struct wl_listener* listener, void* data);
static int touchtaken(struct wlr_surface* surface);

/* variables */
struct wlr_tablet_manager_v2* tablet_mgr;
static struct wl_list tablets = { &tablets, &tablets };
static struct wl_list pads = { &pads, &pads };
static struct wl_list touchdevs = { &touchdevs, &touchdevs };
/* surface origin of each touch point */
/* ponytail: indexed by id & 15, libinput hands out small slot numbers */
static struct {
    double ox, oy;
} points[16];
static int32_t emutouch = -1; /* the touch point driving the pointer */

/* function implementations */
void createtablet(struct wlr_tablet* wlr_tablet)
{
    Tablet* t = ecalloc(1, sizeof(*t));
    TabletPad* p;

    t->wlr_tablet = wlr_tablet;
    t->v2 = wlr_tablet_create(tablet_mgr, seat, &wlr_tablet->base);
    wlr_tablet->data = t;
    wl_list_insert(&tablets, &t->link);
    LISTEN(&wlr_tablet->base.events.destroy, &t->destroy, tabletdestroy);

    wlr_cursor_attach_input_device(cursor, &wlr_tablet->base);
    tabletconfigure(t);
    wl_list_for_each(p, &pads, link) if (!p->tablet) padattach(p);
}

void createtabletpad(struct wlr_tablet_pad* wlr_pad)
{
    TabletPad* p = ecalloc(1, sizeof(*p));

    p->device = &wlr_pad->base;
    p->v2 = wlr_tablet_pad_create(tablet_mgr, seat, p->device);
    wl_list_insert(&pads, &p->link);
    wl_list_init(&p->focus_destroy.link);
    LISTEN(&wlr_pad->events.button, &p->button, padbutton);
    LISTEN(&wlr_pad->events.ring, &p->ring, padring);
    LISTEN(&wlr_pad->events.strip, &p->strip, padstrip);
    LISTEN(&p->device->events.destroy, &p->destroy, paddestroy);
    padattach(p);
}

void createtouch(struct wlr_touch* wlr_touch)
{
    Touch* d = ecalloc(1, sizeof(*d));

    d->wlr_touch = wlr_touch;
    wl_list_insert(&touchdevs, &d->link);
    LISTEN(&wlr_touch->base.events.destroy, &d->destroy, touchdestroy);
    wlr_cursor_attach_input_device(cursor, &wlr_touch->base);
    tabletremap();
}

static void emulate(uint32_t time, uint32_t button, int pressed)
{
    struct wlr_pointer_button_event synth = {
        .time_msec = time,
        .button = button,
        .state = pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                         : WL_POINTER_BUTTON_STATE_RELEASED,
    };
    buttonpress(NULL, &synth);
    wlr_seat_pointer_notify_frame(seat);
}

int hastouch(void)
{
    return !wl_list_empty(&touchdevs);
}

/* NULL if missing or disabled */
static struct wlr_output* outputnamed(const char* name)
{
    Monitor* m;

    if (!name)
        return NULL;
    wl_list_for_each(m, &mons, link)
    {
        if (m->wlr_output->enabled && !strcmp(m->wlr_output->name, name))
            return m->wlr_output;
    }
    return NULL;
}

static void padattach(TabletPad* p)
{
    Tablet* t;

    padfocus(p, NULL);
    p->tablet = NULL;
    wl_list_for_each(t, &tablets, link)
    {
        if (samegroup(p->device, &t->wlr_tablet->base)) {
            p->tablet = t;
            break;
        }
    }
    padfocus(p, seat->keyboard_state.focused_surface);
}

static void padbutton(struct wl_listener* listener, void* data)
{
    TabletPad* p = wl_container_of(listener, p, button);
    struct wlr_tablet_pad_button_event* event = data;
    const PadButton* b;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    /* bound buttons are not forwarded */
    for (b = padbuttons; !locked && b < padbuttons + npadbuttons; b++) {
        if (b->button == event->button && b->func) {
            if (event->state == WLR_BUTTON_PRESSED)
                b->func(&b->arg);
            return;
        }
    }
    wlr_tablet_v2_tablet_pad_notify_mode(
        p->v2, event->group, event->mode, event->time_msec);
    wlr_tablet_v2_tablet_pad_notify_button(
        p->v2,
        event->button,
        event->time_msec,
        event->state == WLR_BUTTON_PRESSED
            ? ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED
            : ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED);
}

static void paddestroy(struct wl_listener* listener, void* data)
{
    TabletPad* p = wl_container_of(listener, p, destroy);

    wl_list_remove(&p->focus_destroy.link);
    wl_list_remove(&p->button.link);
    wl_list_remove(&p->ring.link);
    wl_list_remove(&p->strip.link);
    wl_list_remove(&p->destroy.link);
    wl_list_remove(&p->link);
    free(p);
}

/* pads follow keyboard focus */
static void padfocus(TabletPad* p, struct wlr_surface* surface)
{
    if (surface == p->focus)
        return;
    if (p->focus) {
        wlr_tablet_v2_tablet_pad_notify_leave(p->v2, p->focus);
        wl_list_remove(&p->focus_destroy.link);
        wl_list_init(&p->focus_destroy.link);
        p->focus = NULL;
    }
    if (!surface || !p->tablet || !p->tablet->v2 ||
        !wlr_surface_accepts_tablet_v2(surface, p->tablet->v2))
        return;
    wlr_tablet_v2_tablet_pad_notify_enter(p->v2, p->tablet->v2, surface);
    p->focus = surface;
    LISTEN(&surface->events.destroy, &p->focus_destroy, padunfocus);
}

void padkeyboardfocus(struct wl_listener* listener, void* data)
{
    struct wlr_seat_keyboard_focus_change_event* event = data;
    TabletPad* p;

    wl_list_for_each(p, &pads, link) padfocus(p, event->new_surface);
}

static void padring(struct wl_listener* listener, void* data)
{
    TabletPad* p = wl_container_of(listener, p, ring);
    struct wlr_tablet_pad_ring_event* event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_tablet_v2_tablet_pad_notify_ring(p->v2,
                                         event->ring,
                                         event->position,
                                         event->source ==
                                             WLR_TABLET_PAD_RING_SOURCE_FINGER,
                                         event->time_msec);
}

static void padstrip(struct wl_listener* listener, void* data)
{
    TabletPad* p = wl_container_of(listener, p, strip);
    struct wlr_tablet_pad_strip_event* event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_tablet_v2_tablet_pad_notify_strip(
        p->v2,
        event->strip,
        event->position,
        event->source == WLR_TABLET_PAD_STRIP_SOURCE_FINGER,
        event->time_msec);
}

static void padunfocus(struct wl_listener* listener, void* data)
{
    TabletPad* p = wl_container_of(listener, p, focus_destroy);

    wl_list_remove(&p->focus_destroy.link);
    wl_list_init(&p->focus_destroy.link);
    p->focus = NULL;
}

/* no device groups outside libinput (nested): match any tablet */
static int samegroup(struct wlr_input_device* a, struct wlr_input_device* b)
{
    if (!wlr_input_device_is_libinput(a) || !wlr_input_device_is_libinput(b))
        return 1;
    return libinput_device_get_device_group(
               wlr_libinput_get_device_handle(a)) ==
           libinput_device_get_device_group(wlr_libinput_get_device_handle(b));
}

static uint32_t stylusbutton(uint32_t button)
{
    switch (button) {
        case BTN_STYLUS:
            return BTN_MIDDLE;
        case BTN_STYLUS2:
            return BTN_RIGHT;
        case BTN_STYLUS3:
            return BTN_SIDE;
        default: /* a tablet mouse has real ones */
            return button;
    }
}

void tabletaxis(struct wl_listener* listener, void* data)
{
    struct wlr_tablet_tool_axis_event* event = data;
    TabletTool* t = tool(event->tool);
    uint32_t axes = event->updated_axes;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    /* An axis event only carries the axes it actually updated; NAN tells
     * wlroots to keep the current coordinate for the others. */
    if (axes & (WLR_TABLET_TOOL_AXIS_X | WLR_TABLET_TOOL_AXIS_Y))
        toolmotion(t,
                   event->tablet->data,
                   event->time_msec,
                   axes & WLR_TABLET_TOOL_AXIS_X ? event->x : NAN,
                   axes & WLR_TABLET_TOOL_AXIS_Y ? event->y : NAN);

    if (!t->v2 || !t->v2->focused_surface)
        return;
    if (axes & WLR_TABLET_TOOL_AXIS_PRESSURE)
        wlr_tablet_v2_tablet_tool_notify_pressure(t->v2, event->pressure);
    if (axes & WLR_TABLET_TOOL_AXIS_DISTANCE)
        wlr_tablet_v2_tablet_tool_notify_distance(t->v2, event->distance);
    if (axes & WLR_TABLET_TOOL_AXIS_TILT_X)
        t->tilt_x = event->tilt_x;
    if (axes & WLR_TABLET_TOOL_AXIS_TILT_Y)
        t->tilt_y = event->tilt_y;
    if (axes & (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y))
        wlr_tablet_v2_tablet_tool_notify_tilt(t->v2, t->tilt_x, t->tilt_y);
    if (axes & WLR_TABLET_TOOL_AXIS_ROTATION)
        wlr_tablet_v2_tablet_tool_notify_rotation(t->v2, event->rotation);
    if (axes & WLR_TABLET_TOOL_AXIS_SLIDER)
        wlr_tablet_v2_tablet_tool_notify_slider(t->v2, event->slider);
    if (axes & WLR_TABLET_TOOL_AXIS_WHEEL)
        wlr_tablet_v2_tablet_tool_notify_wheel(t->v2, event->wheel_delta, 0);
}

void tabletbutton(struct wl_listener* listener, void* data)
{
    struct wlr_tablet_tool_button_event* event = data;
    TabletTool* t = tool(event->tool);
    uint32_t bit = 1u << (event->button & 31);
    int pressed = event->state == WLR_BUTTON_PRESSED;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    /* released the same way it was pressed */
    if (pressed ? !t->v2 || !t->v2->focused_surface || t->emulating
                : t->emubuttons & bit) {
        if (pressed)
            t->emubuttons |= bit;
        else
            t->emubuttons &= ~bit;
        emulate(event->time_msec, stylusbutton(event->button), pressed);
        return;
    }
    if (!t->v2)
        return;
    wlr_tablet_v2_tablet_tool_notify_button(
        t->v2,
        event->button,
        pressed ? ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED
                : ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED);
    wlr_tablet_tool_v2_start_implicit_grab(t->v2);
}

static void tabletconfigure(Tablet* t)
{
    struct wlr_input_device* device = &t->wlr_tablet->base;
    struct libinput_device* li;

    wlr_cursor_map_input_to_output(cursor, device, outputnamed(tablet_output));
    if (wlr_input_device_is_libinput(device) &&
        (li = wlr_libinput_get_device_handle(device)) &&
        libinput_device_config_left_handed_is_available(li))
        libinput_device_config_left_handed_set(li, tablet_left_handed);
}

static void tabletdestroy(struct wl_listener* listener, void* data)
{
    Tablet* t = wl_container_of(listener, t, destroy);
    TabletPad* p;

    wl_list_remove(&t->destroy.link);
    wl_list_remove(&t->link);
    wl_list_for_each(p, &pads, link) if (p->tablet == t) padattach(p);
    free(t);
}

void tabletproximity(struct wl_listener* listener, void* data)
{
    struct wlr_tablet_tool_proximity_event* event = data;
    TabletTool* t = tool(event->tool);

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    if (event->state == WLR_TABLET_TOOL_PROXIMITY_IN)
        toolmotion(
            t, event->tablet->data, event->time_msec, event->x, event->y);
    else if (t->v2)
        wlr_tablet_v2_tablet_tool_notify_proximity_out(t->v2);
}

void tabletremap(void)
{
    Tablet* t;
    Touch* d;

    wl_list_for_each(t, &tablets, link) tabletconfigure(t);
    wl_list_for_each(d, &touchdevs, link)
    {
        wlr_cursor_map_input_to_output(
            cursor,
            &d->wlr_touch->base,
            outputnamed(touch_output ? touch_output
                                     : d->wlr_touch->output_name));
    }
}

void tablettip(struct wl_listener* listener, void* data)
{
    struct wlr_tablet_tool_tip_event* event = data;
    TabletTool* t = tool(event->tool);
    Client* c;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    if (event->state == WLR_TABLET_TOOL_TIP_UP) {
        if (t->emulating) {
            t->emulating = 0;
            emulate(event->time_msec, BTN_LEFT, 0);
            toolmotion(t, event->tablet->data, event->time_msec, NAN, NAN);
        } else if (t->v2) {
            wlr_tablet_v2_tablet_tool_notify_up(t->v2);
        }
        return;
    }

    if (t->v2 && t->v2->focused_surface && !tipbound()) {
        /* a stroke focuses like a click */
        selmon = xytomon(cursor->x, cursor->y);
        xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
        if (!locked && c && (!client_is_unmanaged(c) || client_wants_focus(c)))
            focusclient(c, 1);
        wlr_tablet_v2_tablet_tool_notify_down(t->v2);
        wlr_tablet_tool_v2_start_implicit_grab(t->v2);
        return;
    }
    /* bound click or no tablet-v2 surface: act as the pointer until tip up */
    if (t->v2 && t->v2->focused_surface) {
        wlr_tablet_v2_tablet_tool_notify_proximity_out(t->v2);
        motionnotify(event->time_msec, &event->tablet->base, 0, 0, 0, 0);
    }
    t->emulating = 1;
    emulate(event->time_msec, BTN_LEFT, 1);
}

static int tipbound(void)
{
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
    const Button* b;

    if (locked)
        return 0;
    for (b = buttons; b < buttons + nbuttons; b++)
        if (b->click == ClkClient && b->button == BTN_LEFT && b->func &&
            CLEANMASK(mods) == CLEANMASK(b->mod))
            return 1;
    return 0;
}

static TabletTool* tool(struct wlr_tablet_tool* wlr_tool)
{
    TabletTool* t = wlr_tool->data;

    if (t)
        return t;
    t = ecalloc(1, sizeof(*t));
    wl_list_init(&t->set_cursor.link);
    if ((t->v2 = wlr_tablet_tool_create(tablet_mgr, seat, wlr_tool)))
        LISTEN(&t->v2->events.set_cursor, &t->set_cursor, toolsetcursor);
    LISTEN(&wlr_tool->events.destroy, &t->destroy, tooldestroy);
    wlr_tool->data = t;
    return t;
}

static void tooldestroy(struct wl_listener* listener, void* data)
{
    TabletTool* t = wl_container_of(listener, t, destroy);

    wl_list_remove(&t->set_cursor.link);
    wl_list_remove(&t->destroy.link);
    free(t);
}

int toolfocusedby(struct wlr_tablet_v2_tablet_tool* tool,
                  struct wlr_seat_client* client)
{
    return tool && tool->focused_surface && client &&
           wl_resource_get_client(tool->focused_surface->resource) ==
               client->client;
}

/* tablet-v2 surfaces get the pen, anything else gets it as the pointer */
static void toolmotion(TabletTool* t,
                       Tablet* tb,
                       uint32_t time,
                       double x,
                       double y)
{
    struct wlr_input_device* device = &tb->wlr_tablet->base;
    struct wlr_surface* surface = NULL;
    Client* c = NULL;
    double lx, ly, sx = 0, sy = 0;
    int grabbed = t->v2 && wlr_tablet_tool_v2_has_implicit_grab(t->v2);

    wlr_cursor_absolute_to_layout_coords(cursor, device, x, y, &lx, &ly);
    if (t->v2 && tb->v2 && !t->emulating && cursor_mode == CurNormal &&
        !traypopup_active())
        xytonode(lx, ly, &surface, &c, NULL, &sx, &sy);
    if (surface && !wlr_surface_accepts_tablet_v2(surface, tb->v2))
        surface = NULL;

    if (!surface && !grabbed) {
        if (t->v2)
            wlr_tablet_v2_tablet_tool_notify_proximity_out(t->v2);
        motionnotify(time,
                     device,
                     lx - cursor->x,
                     ly - cursor->y,
                     lx - cursor->x,
                     ly - cursor->y);
        /* Tablets emit no frame event of their own, and clients hold
         * pointer events back until one arrives. */
        wlr_seat_pointer_notify_frame(seat);
        return;
    }

    wlr_cursor_warp_closest(cursor, device, lx, ly);
    cursormoved();
    titlehover(c, cursor->x, cursor->y);
    if (!grabbed) {
        /* the pen and the pointer are not both on an app at once */
        if (seat->pointer_state.focused_surface)
            wlr_seat_pointer_notify_clear_focus(seat);
        if (surface != t->v2->focused_surface && sloppyfocus && c &&
            !client_is_unmanaged(c))
            focusclient(c, 0);
        t->ox = lx - sx;
        t->oy = ly - sy;
    }
    if (surface)
        wlr_tablet_v2_tablet_tool_notify_proximity_in(t->v2, tb->v2, surface);
    else
        wlr_tablet_v2_tablet_tool_notify_proximity_out(t->v2);
    wlr_tablet_v2_tablet_tool_notify_motion(
        t->v2, cursor->x - t->ox, cursor->y - t->oy);
}

static void toolsetcursor(struct wl_listener* listener, void* data)
{
    TabletTool* t = wl_container_of(listener, t, set_cursor);
    struct wlr_tablet_v2_event_cursor* event = data;

    if (cursor_mode == CurNormal && toolfocusedby(t->v2, event->seat_client))
        setclientcursor(event->surface, event->hotspot_x, event->hotspot_y);
}

void touchcancel(struct wl_listener* listener, void* data)
{
    struct wlr_touch_cancel_event* event = data;
    struct wlr_touch_point* point;

    if (event->touch_id == emutouch) {
        emutouch = -1;
        emulate(event->time_msec, BTN_LEFT, 0);
    } else if ((point = wlr_seat_touch_get_point(seat, event->touch_id))) {
        wlr_seat_touch_notify_cancel(seat, point->client);
    }
}

static void touchdestroy(struct wl_listener* listener, void* data)
{
    Touch* d = wl_container_of(listener, d, destroy);

    wl_list_remove(&d->destroy.link);
    wl_list_remove(&d->link);
    free(d);
    seatcaps();
}

void touchdown(struct wl_listener* listener, void* data)
{
    struct wlr_touch_down_event* event = data;
    struct wlr_surface* surface = NULL;
    Client* c = NULL;
    double lx, ly, sx, sy;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_cursor_absolute_to_layout_coords(
        cursor, &event->touch->base, event->x, event->y, &lx, &ly);
    if (cursor_mode == CurNormal && !traypopup_active())
        xytonode(lx, ly, &surface, &c, NULL, &sx, &sy);

    if (surface && touchtaken(surface) &&
        wlr_seat_touch_notify_down(
            seat, surface, event->time_msec, event->touch_id, sx, sy)) {
        points[event->touch_id & 15].ox = lx - sx;
        points[event->touch_id & 15].oy = ly - sy;
        selmon = xytomon(lx, ly);
        if (!locked && c && (!client_is_unmanaged(c) || client_wants_focus(c)))
            focusclient(c, 1);
        return;
    }
    /* no touch support there: emulate a click, one finger at a time */
    if (emutouch >= 0)
        return;
    emutouch = event->touch_id;
    motionnotify(event->time_msec,
                 &event->touch->base,
                 lx - cursor->x,
                 ly - cursor->y,
                 lx - cursor->x,
                 ly - cursor->y);
    emulate(event->time_msec, BTN_LEFT, 1);
}

void touchframe(struct wl_listener* listener, void* data)
{
    wlr_seat_touch_notify_frame(seat);
}

void touchmotion(struct wl_listener* listener, void* data)
{
    struct wlr_touch_motion_event* event = data;
    double lx, ly;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    wlr_cursor_absolute_to_layout_coords(
        cursor, &event->touch->base, event->x, event->y, &lx, &ly);
    if (event->touch_id == emutouch) {
        motionnotify(event->time_msec,
                     &event->touch->base,
                     lx - cursor->x,
                     ly - cursor->y,
                     lx - cursor->x,
                     ly - cursor->y);
        wlr_seat_pointer_notify_frame(seat);
        return;
    }
    if (seat->drag && seat->drag->grab_type == WLR_DRAG_GRAB_KEYBOARD_TOUCH &&
        seat->drag->touch_id == event->touch_id) {
        struct wlr_surface* surface = NULL;
        double sx = 0, sy = 0;

        wlr_scene_node_set_position(
            &drag_icon->node, (int)round(lx), (int)round(ly));
        xytonode(lx, ly, &surface, NULL, NULL, &sx, &sy);
        if (surface)
            wlr_seat_touch_point_focus(
                seat, surface, event->time_msec, event->touch_id, sx, sy);
        else
            wlr_seat_touch_point_clear_focus(
                seat, event->time_msec, event->touch_id);
        wlr_seat_touch_notify_motion(
            seat, event->time_msec, event->touch_id, sx, sy);
        return;
    }
    wlr_seat_touch_notify_motion(seat,
                                 event->time_msec,
                                 event->touch_id,
                                 lx - points[event->touch_id & 15].ox,
                                 ly - points[event->touch_id & 15].oy);
}

static int touchtaken(struct wlr_surface* surface)
{
    struct wlr_seat_client* client = wlr_seat_client_for_wl_client(
        seat, wl_resource_get_client(surface->resource));

    return client && !wl_list_empty(&client->touches);
}

void touchup(struct wl_listener* listener, void* data)
{
    struct wlr_touch_up_event* event = data;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    if (event->touch_id == emutouch) {
        emutouch = -1;
        emulate(event->time_msec, BTN_LEFT, 0);
        return;
    }
    if (wlr_seat_touch_get_point(seat, event->touch_id))
        wlr_seat_touch_notify_up(seat, event->time_msec, event->touch_id);
}
