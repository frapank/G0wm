/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: unixchad
 * SPDX-FileCopyrightText: Ben Collerson
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * pointer motion, buttons, scrolling, grabs and constraints
 */

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/box.h>
#include <wlr/util/region.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "core/util.h"
#include "desktop/bar/bar.h"
#include "desktop/bar/notification.h"
#include "desktop/bar/status.h"
#include "desktop/bar/title.h"
#include "desktop/systray/tray.h"
#include "desktop/traypopup.h"
#include "input/pointer.h"
#include "input/tablet.h"
#include "internal.h"
#include "output/lock.h"
#include "output/monitor.h"
#include "window/client.h"
#include "window/layout.h"

typedef struct {
    struct wlr_pointer_constraint_v1* constraint;
    struct wl_listener destroy;
} PointerConstraint;

/* function declarations */
static unsigned int input_bar_click(Monitor* pm, Arg* arg);
static void input_cursor_constrain(
    struct wlr_pointer_constraint_v1* constraint);
static void input_cursor_warp_to_hint(void);
static void input_handle_pointer_constraint_destroy(
    struct wl_listener* listener,
    void* data);
static void input_grab(Client* c, unsigned int mode);
static void input_cursor_activity(void);
static void input_magnet(struct wlr_box* b);
static void input_magnet_edge(int pos, int size, int edge, int* dist, int* to);
static void input_pointer_focus(Client* c,
                                struct wlr_surface* surface,
                                double sx,
                                double sy,
                                uint32_t time);
static void input_handle_last_cursor_destroy(struct wl_listener* listener,
                                             void* data);
static int input_wheel_binding(const struct wlr_pointer_axis_event* event);

/* variables */
struct last_cursor_state input_last_cursor;
unsigned int input_cursor_mode;
Client* input_grab_client;
static struct wlr_pointer_constraint_v1* input_active_constraint;
bool input_cursor_hidden = false;
static int input_grab_x, input_grab_y; /* client-relative */
static double input_press_x, input_press_y;

/* function implementations */
/* The part of pm's bar the cursor is over, ClkRoot if it is not over it.
 * arg picks up the tag or tray item the part stands for. */
static unsigned int input_bar_click(Monitor* pm, Arg* arg)
{
    unsigned int i = 0, x = 0;
    double cx;
    int traywidth = 0, statusw;
    struct wlr_scene_node* node;
    struct wlr_scene_buffer* buffer;
    /* under bar_single_monitor the bar shows the focused monitor's tags */
    Monitor* s = bar_single_monitor && selected_monitor ? selected_monitor : pm;

    if (exclusive_focus || !pm ||
        !(node = wlr_scene_node_at(&layers[LyrBottom]->node,
                                   cursor->x,
                                   cursor->y,
                                   nullptr,
                                   nullptr)) ||
        !(buffer = wlr_scene_buffer_from_node(node)) ||
        buffer != pm->scene_buffer)
        return ClkRoot;

    cx = (cursor->x - pm->m.x - bar_padding) * pm->wlr_output->scale;
    traywidth = tray_get_width(pm->tray);
    statusw = bar_status_shown(pm) ? STATUSW(pm) : 0;
    do
        x += bar_tag_width(pm, s, i);
    while (cx >= x && ++i < tag_count);
    if (i < tag_count) {
        arg->ui = 1 << i;
        return ClkTagBar;
    } else if (bar_layout_symbol && cx < x + TEXTW(pm, s->ltsymbol))
        return ClkLtSymbol;
    else if (traywidth && cx > pm->b.width - traywidth) {
        arg->ui = tray_index_at(pm->tray, cx - (pm->b.width - traywidth));
        return ClkTray;
    } else if (cx > pm->b.width - (statusw + traywidth)) {
        arg->i = (int)(cx - (pm->b.width - (statusw + traywidth)));
        return ClkStatus;
    }
    /* x + 1, so 0 still means "from the keyboard" */
    arg->ui = (unsigned int)cx + 1;
    return ClkTitle;
}

void input_handle_axis(struct wl_listener*, void* data)
{
    /* This event is forwarded by the cursor when a pointer emits an axis event,
     * for example when you move the scroll wheel. */
    struct wlr_pointer_axis_event* event = data;
    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    input_cursor_activity();
    {
        Client* c;
        Arg arg;
        Monitor* pm = monitor_at(cursor->x, cursor->y);
        struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
        int horizontal =
            event->orientation == WL_POINTER_AXIS_HORIZONTAL_SCROLL ||
            (keyboard &&
             wlr_keyboard_get_modifiers(keyboard) & WLR_MODIFIER_SHIFT);

        input_node_at(
            cursor->x, cursor->y, nullptr, &c, nullptr, nullptr, nullptr);
        if (!lock_active && !c && input_bar_click(pm, &arg) == ClkTitle &&
            bar_notification_wheel(pm, event->delta, horizontal))
            return;
    }
    if (!lock_active && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        Client* c;
        Arg arg;
        Monitor* pm = monitor_at(cursor->x, cursor->y);

        input_node_at(
            cursor->x, cursor->y, nullptr, &c, nullptr, nullptr, nullptr);
        if (!c && input_bar_click(pm, &arg) == ClkStatus &&
            bar_status_scroll(pm, arg.i, event->delta))
            return;
    }
    if (!lock_active && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL &&
        input_wheel_binding(event))
        return;
    /* Notify the client with pointer focus of the axis event. */
    wlr_seat_pointer_notify_axis(seat,
                                 event->time_msec,
                                 event->orientation,
                                 event->delta,
                                 event->delta_discrete,
                                 event->source,
                                 event->relative_direction);
}

void input_handle_button(struct wl_listener*, void* data)
{
    unsigned int click;
    struct wlr_pointer_button_event* event = data;
    struct wlr_keyboard* keyboard;
    Monitor* pm; /* the monitor the pointer is on, bar included */
    uint32_t mods;
    Arg arg = {
        .v = nullptr
    }; /* .v is the widest member: zeroes the whole union */
    Client* c;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    input_cursor_activity();

    /* the open menu owns the pointer, nothing under it sees the click */
    if (!lock_active && traypopup_active()) {
        if (event->state == WL_POINTER_BUTTON_STATE_PRESSED)
            traypopup_click(cursor->x, cursor->y);
        return;
    }

    click = ClkRoot;
    input_node_at(cursor->x, cursor->y, nullptr, &c, nullptr, nullptr, nullptr);
    if (c)
        click = ClkClient;

    switch (event->state) {
        case WL_POINTER_BUTTON_STATE_PRESSED:
            input_cursor_mode = CurPressed;
            pm = monitor_at(cursor->x, cursor->y);
            if (lock_active) {
                selected_monitor = pm;
                break;
            }

            if (!c && (click = input_bar_click(pm, &arg)) != ClkRoot) {
                /* The bar belongs to pm, but under bar_single_monitor it stands
                 * in for the focused monitor: leaving selected_monitor alone is
                 * what lands the click on the monitor whose tags are on show.
                 */
                if (!bar_single_monitor || !selected_monitor)
                    selected_monitor = pm;
            } else {
                selected_monitor = pm;
            }

            /* Change focus if the button was _pressed_ over a client */
            input_node_at(
                cursor->x, cursor->y, nullptr, &c, nullptr, nullptr, nullptr);
            if (click == ClkClient &&
                (!client_is_unmanaged(c) || client_wants_focus(c)))
                client_focus(c, 1);

            if (click == ClkStatus &&
                bar_status_click(pm,
                                 arg.i,
                                 event->button == BTN_LEFT     ? 1
                                 : event->button == BTN_RIGHT  ? 2
                                 : event->button == BTN_MIDDLE ? 3
                                                               : 0))
                return;

            keyboard = wlr_seat_get_keyboard(seat);
            mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
            if (click == ClkClient && !CLEANMASK(mods) &&
                event->button == BTN_LEFT &&
                bar_title_click(c, cursor->x, cursor->y))
                return;
            for (const Button* b = buttons; b < buttons + button_count; b++) {
                if (CLEANMASK(mods) == CLEANMASK(b->mod) &&
                    event->button == b->button && click == b->click &&
                    b->func) {
                    b->func((click == ClkTagBar || click == ClkTray ||
                             click == ClkTitle) &&
                                    b->arg.i == 0
                                ? &arg
                                : &b->arg);
                    return;
                }
            }
            break;
        case WL_POINTER_BUTTON_STATE_RELEASED:
            /* If you released any buttons, we exit interactive move/resize
             * mode. */
            /* TODO: should reset to the pointer focus's current
             * input_handle_request_cursor */
            if (!lock_active && input_cursor_mode != CurNormal &&
                input_cursor_mode != CurPressed) {
                wlr_cursor_set_xcursor(cursor, cursor_manager, "default");
                input_cursor_mode = CurNormal;
                /* Drop the window off on its new monitor */
                selected_monitor = monitor_at(cursor->x, cursor->y);
                client_set_monitor(input_grab_client, selected_monitor, 0);
                input_grab_client = nullptr;
                return;
            }
            input_cursor_mode = CurNormal;
            input_grab_client = nullptr;
            break;
    }
    /* If the event wasn't handled by the compositor, notify the client with
     * pointer focus that a button press has occurred */
    wlr_seat_pointer_notify_button(
        seat, event->time_msec, event->button, event->state);
}

void input_handle_new_pointer_constraint(struct wl_listener*, void* data)
{
    PointerConstraint* pointer_constraint =
        util_calloc(1, sizeof(*pointer_constraint));
    pointer_constraint->constraint = data;
    LISTEN(&pointer_constraint->constraint->events.destroy,
           &pointer_constraint->destroy,
           input_handle_pointer_constraint_destroy);
}

static void input_cursor_constrain(struct wlr_pointer_constraint_v1* constraint)
{
    if (input_active_constraint == constraint)
        return;

    if (input_active_constraint)
        wlr_pointer_constraint_v1_send_deactivated(input_active_constraint);

    input_active_constraint = constraint;
    wlr_pointer_constraint_v1_send_activated(constraint);
}

void input_handle_frame(struct wl_listener*, void*)
{
    /* This event is forwarded by the cursor when a pointer emits a frame
     * event. Frame events are sent after regular pointer events to group
     * multiple events together. For instance, two axis events may happen at the
     * same time, in which case a frame event won't be sent in between. */
    /* Notify the client with pointer focus of the frame event. */
    wlr_seat_pointer_notify_frame(seat);
}

static void input_cursor_warp_to_hint(void)
{
    Client* c = nullptr;
    double sx = input_active_constraint->current.cursor_hint.x;
    double sy = input_active_constraint->current.cursor_hint.y;

    client_from_surface(input_active_constraint->surface, &c, nullptr);
    if (c && input_active_constraint->current.cursor_hint.enabled) {
        wlr_cursor_warp(
            cursor, nullptr, sx + c->geom.x + c->bw, sy + c->geom.y + c->bw);
        wlr_seat_pointer_warp(input_active_constraint->seat, sx, sy);
    }
}

static void input_handle_pointer_constraint_destroy(
    struct wl_listener* listener,
    void*)
{
    PointerConstraint* pointer_constraint =
        wl_container_of(listener, pointer_constraint, destroy);

    if (input_active_constraint == pointer_constraint->constraint) {
        input_cursor_warp_to_hint();
        input_active_constraint = nullptr;
    }

    wl_list_remove(&pointer_constraint->destroy.link);
    free(pointer_constraint);
}

static void input_cursor_activity(void)
{
    if (!input_cursor_hidden)
        return;

    input_cursor_hidden = false;

    if (input_last_cursor.shape) {
        wlr_cursor_set_xcursor(
            cursor,
            cursor_manager,
            wlr_cursor_shape_v1_name(input_last_cursor.shape));
    } else if (input_last_cursor.has_client_cursor) {
        /* surface may be NULL here: the client explicitly hid its cursor
         * (e.g. a game with pointer lock), which must be honored rather
         * than falling back to the default arrow below. */
        wlr_cursor_set_surface(cursor,
                               input_last_cursor.surface,
                               input_last_cursor.hotspot_x,
                               input_last_cursor.hotspot_y);
    } else {
        wlr_cursor_set_xcursor(cursor, cursor_manager, "default");
    }
}

void input_handle_motion_absolute(struct wl_listener*, void* data)
{
    /* This event is forwarded by the cursor when a pointer emits an _absolute_
     * motion event, from 0..1 on each axis. This happens, for example, when
     * wlroots is running under a Wayland window rather than KMS+DRM, and you
     * move the mouse over the window. You could enter the window from any edge,
     * so we have to warp the mouse there. Also, some hardware emits these
     * events. */
    struct wlr_pointer_motion_absolute_event* event = data;
    double lx, ly, dx, dy;

    if (!event->time_msec) /* this is 0 with virtual pointers */
        wlr_cursor_warp_absolute(
            cursor, &event->pointer->base, event->x, event->y);

    wlr_cursor_absolute_to_layout_coords(
        cursor, &event->pointer->base, event->x, event->y, &lx, &ly);
    dx = lx - cursor->x;
    dy = ly - cursor->y;
    input_motion(event->time_msec, &event->pointer->base, dx, dy, dx, dy);
}

void input_cursor_moved(void)
{
    Monitor* m = monitor_at(cursor->x, cursor->y);

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    input_cursor_activity();

    /* the part of a zoomed monitor on show follows the pointer */
    if (m && m->zoom > 1)
        wlr_output_schedule_frame(m->wlr_output);

    /* Update selected_monitor (even while dragging a window) */
    if (sloppy_focus) {
        Monitor* old = selected_monitor;
        selected_monitor = monitor_at(cursor->x, cursor->y);
        /* the single bar follows the focus, and crossing into empty
         * space changes it without going through client_focus() */
        if ((bar_single_monitor || bar_status == BarStatusFocus) &&
            selected_monitor != old)
            bar_draw_all();
    }
}

void input_motion(uint32_t time,
                  struct wlr_input_device* device,
                  double dx,
                  double dy,
                  double dx_unaccel,
                  double dy_unaccel)
{
    double sx = 0, sy = 0, sx_confined, sy_confined;
    Client *c = nullptr, *w = nullptr;
    LayerSurface* l = nullptr;
    struct wlr_surface* surface = nullptr;
    struct wlr_pointer_constraint_v1* constraint;
    struct wlr_box box;

    /* Find the client under the pointer and send the event along. */
    input_node_at(cursor->x, cursor->y, &surface, &c, nullptr, &sx, &sy);
    bar_title_hover(c, cursor->x, cursor->y);

    if (input_cursor_mode == CurPressed && !seat->drag &&
        surface != seat->pointer_state.focused_surface &&
        client_from_surface(seat->pointer_state.focused_surface, &w, &l) >= 0) {
        c = w;
        surface = seat->pointer_state.focused_surface;
        sx = cursor->x - (l ? l->scene->node.x : w->geom.x);
        sy = cursor->y - (l ? l->scene->node.y : w->geom.y);
    }

    /* time is 0 in internal calls meant to restore pointer focus. */
    if (time) {
        wlr_relative_pointer_manager_v1_send_relative_motion(
            relative_pointer_manager,
            seat,
            (uint64_t)time * 1000,
            dx,
            dy,
            dx_unaccel,
            dy_unaccel);

        wl_list_for_each(constraint, &pointer_constraints->constraints, link)
            input_cursor_constrain(constraint);

        if (input_active_constraint && input_cursor_mode != CurResize &&
            input_cursor_mode != CurMove) {
            /* own variable: c is the client under the cursor, and the
             * constraint may sit on an unmapped one (gamescope) */
            Client* cc = nullptr;
            client_from_surface(input_active_constraint->surface, &cc, nullptr);
            if (cc && input_active_constraint->surface ==
                          seat->pointer_state.focused_surface) {
                sx = cursor->x - cc->geom.x - cc->bw;
                sy = cursor->y - cc->geom.y - cc->bw;
                if (wlr_region_confine(&input_active_constraint->region,
                                       sx,
                                       sy,
                                       sx + dx,
                                       sy + dy,
                                       &sx_confined,
                                       &sy_confined)) {
                    dx = sx_confined - sx;
                    dy = sy_confined - sy;
                }

                if (input_active_constraint->type ==
                    WLR_POINTER_CONSTRAINT_V1_LOCKED)
                    return;
            }
        }

        wlr_cursor_move(cursor, device, dx, dy);
        input_cursor_moved();
    }

    /* Update drag icon's position */
    wlr_scene_node_set_position(
        &drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));

    /* hovering the open menu, so nothing below it takes focus */
    if (!lock_active && traypopup_active()) {
        traypopup_motion(cursor->x, cursor->y);
        if (!seat->drag && !input_cursor_hidden)
            wlr_cursor_set_xcursor(cursor, cursor_manager, "default");
        return;
    }

    if (input_cursor_mode == CurPressed && input_grab_client && time &&
        !lock_active &&
        (cursor->x - input_press_x) * (cursor->x - input_press_x) +
                (cursor->y - input_press_y) * (cursor->y - input_press_y) >=
            move_threshold * move_threshold)
        input_grab(input_grab_client, CurMove);

    /* If we are currently grabbing the mouse, handle and return */
    if (input_cursor_mode == CurMove) {
        /* A tiled client keeps its slot in the layout: dragging it just swaps
         * it with the tile under the cursor, i3/sway style. */
        if (!input_grab_client->isfloating) {
            if (time && c && c != input_grab_client && !c->isfloating &&
                c->mon == input_grab_client->mon &&
                VISIBLEON(input_grab_client, input_grab_client->mon)) {
                layout_swap_clients(input_grab_client, c);
                layout_arrange(input_grab_client->mon);
            }
            return;
        }
        /* Move the grabbed client to the new position. */
        box = (struct wlr_box){ .x = (int)round(cursor->x) - input_grab_x,
                                .y = (int)round(cursor->y) - input_grab_y,
                                .width = input_grab_client->geom.width,
                                .height = input_grab_client->geom.height };
        input_magnet(&box);
        client_resize(input_grab_client, box, 1);
        return;
    } else if (input_cursor_mode == CurResize) {
        client_resize(
            input_grab_client,
            (struct wlr_box){
                .x = input_grab_client->geom.x,
                .y = input_grab_client->geom.y,
                .width = (int)round(cursor->x) - input_grab_client->geom.x,
                .height = (int)round(cursor->y) - input_grab_client->geom.y },
            1);
        return;
    }

    /* If there's no client surface under the cursor, set the cursor image to a
     * default. This is what makes the cursor image appear when you move it
     * off of a client or over its border. */
    if (!surface && !seat->drag && !input_cursor_hidden)
        wlr_cursor_set_xcursor(cursor, cursor_manager, "default");

    input_pointer_focus(c, surface, sx, sy, time);
}

void input_handle_motion(struct wl_listener*, void* data)
{
    /* This event is forwarded by the cursor when a pointer emits a _relative_
     * pointer motion event (i.e. a delta) */
    struct wlr_pointer_motion_event* event = data;
    /* The cursor doesn't move unless we tell it to. The cursor automatically
     * handles constraining the motion to the output layout, as well as any
     * special configuration applied for the specific input device which
     * generated the event. You can pass nullptr for the device if you want to
     * move the cursor around without any input. */
    input_motion(event->time_msec,
                 &event->pointer->base,
                 event->delta_x,
                 event->delta_y,
                 event->unaccel_dx,
                 event->unaccel_dy);
}

void input_move_resize(const Arg* arg)
{
    Client* c;

    if (input_cursor_mode != CurNormal && input_cursor_mode != CurPressed)
        return;
    input_node_at(cursor->x, cursor->y, nullptr, &c, nullptr, nullptr, nullptr);
    input_press_x = cursor->x;
    input_press_y = cursor->y;
    input_grab(c, arg->ui);
}

/* Moving c from the title bar waits for move_threshold of motion, so a plain
 * click stays a click: until then input_grab_client is set but
 * input_cursor_mode is CurPressed.
 */
void input_title_grab(Client* c)
{
    input_grab_client = c;
    input_press_x = cursor->x;
    input_press_y = cursor->y;
}

static void input_grab(Client* c, unsigned int mode)
{
    input_grab_client = nullptr;
    if (!c || client_is_unmanaged(c) || c->isfullscreen)
        return;
    input_grab_client = c;

    /* Float the window and tell input_motion to grab it - except when moving a
     * tiled client, which stays tiled and is swapped with the tile it is
     * dragged onto instead. */
    if (mode != CurMove || c->isfloating || !c->mon)
        client_set_floating(c, 1);
    switch (input_cursor_mode = mode) {
        case CurMove:
            input_grab_x = (int)round(input_press_x) - c->geom.x;
            input_grab_y = (int)round(input_press_y) - c->geom.y;
            wlr_cursor_set_xcursor(cursor, cursor_manager, "grabbing");
            break;
        case CurResize:
            /* Doesn't work for X11 output - the next absolute motion event
             * returns the cursor to where it started */
            wlr_cursor_warp_closest(cursor,
                                    nullptr,
                                    c->geom.x + c->geom.width,
                                    c->geom.y + c->geom.height);
            wlr_cursor_set_xcursor(cursor, cursor_manager, "se-resize");
            break;
    }
}

static void input_pointer_focus(Client* c,
                                struct wlr_surface* surface,
                                double sx,
                                double sy,
                                uint32_t time)
{
    struct timespec now;

    /* Only a real client surface takes focus: hovering a title bar (a tab) or
     * a border has no surface, and must not select anything until clicked. */
    if (surface && surface != seat->pointer_state.focused_surface &&
        sloppy_focus && time && c && !client_is_unmanaged(c))
        client_focus(c, 0);

    /* If surface is NULL, clear pointer focus */
    if (!surface) {
        wlr_seat_pointer_notify_clear_focus(seat);
        return;
    }

    if (!time) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        time = now.tv_sec * 1000 + now.tv_nsec / 1'000'000;
    }

    /* Let the client know that the mouse cursor has entered one
     * of its surfaces, and make keyboard focus follow if desired.
     * wlroots makes this a no-op if surface is already focused */
    wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

static void input_handle_last_cursor_destroy(struct wl_listener*, void*)
{
    /* When the surface is destroyed, clear our reference to it */
    input_last_cursor.surface = nullptr;
    wl_list_remove(&input_last_cursor.destroy.link);
    wl_list_init(&input_last_cursor.destroy.link);
}

void input_handle_request_cursor(struct wl_listener*, void* data)
{
    /* This event is raised by the seat when a client provides a cursor image */
    struct wlr_seat_pointer_request_set_cursor_event* event = data;
    /* If we're "grabbing" the cursor, don't use the client's image, we will
     * restore it after "grabbing" sending a leave event, followed by a enter
     * event, which will result in the client requesting set the cursor surface
     */
    if (input_cursor_mode != CurNormal && input_cursor_mode != CurPressed)
        return;
    /* This can be sent by any client, so we check to make sure this one
     * actually has pointer focus first. If so, we can tell the cursor to
     * use the provided surface as the cursor image. It will set the
     * hardware cursor on the output that it's currently on and continue to
     * do so as the cursor moves between outputs. */
    if (event->seat_client == seat->pointer_state.focused_client)
        input_set_client_cursor(
            event->surface, event->hotspot_x, event->hotspot_y);
}

void input_set_client_cursor(struct wlr_surface* surface,
                             int hotspot_x,
                             int hotspot_y)
{
    input_last_cursor.shape = 0;
    input_last_cursor.surface = surface;
    input_last_cursor.has_client_cursor = true;
    input_last_cursor.hotspot_x = hotspot_x;
    input_last_cursor.hotspot_y = hotspot_y;

    wl_list_remove(&input_last_cursor.destroy.link);
    wl_list_init(&input_last_cursor.destroy.link);
    if (surface) {
        input_last_cursor.destroy.notify = input_handle_last_cursor_destroy;
        wl_signal_add(&surface->events.destroy, &input_last_cursor.destroy);
    }

    if (!input_cursor_hidden)
        wlr_cursor_set_surface(cursor, surface, hotspot_x, hotspot_y);
}

void input_handle_request_cursor_shape(struct wl_listener*, void* data)
{
    struct wlr_cursor_shape_manager_v1_request_set_shape_event* event = data;
    if (input_cursor_mode != CurNormal && input_cursor_mode != CurPressed)
        return;
    /* This can be sent by any client, so we check to make sure this one
     * actually has pointer (or pen) focus first. If so, we can tell the
     * cursor to use the provided cursor shape. */
    if (event->device_type ==
                WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_TABLET_TOOL
            ? tablet_tool_focused_by(event->tablet_tool, event->seat_client)
            : event->seat_client == seat->pointer_state.focused_client) {
        input_last_cursor.shape = event->shape;
        input_last_cursor.surface = nullptr;

        if (!input_cursor_hidden)
            wlr_cursor_set_xcursor(
                cursor, cursor_manager, wlr_cursor_shape_v1_name(event->shape));
    }
}

/* Sticks a moved floating window to the nearby edges of the window area and
 * of the floating windows beside it */
static void input_magnet(struct wlr_box* b)
{
    Monitor* m = monitor_at(cursor->x, cursor->y);
    Client* c;
    int dx = move_snap + 1, dy = move_snap + 1, x = b->x, y = b->y;

    if (!m || move_snap <= 0)
        return;
    input_magnet_edge(b->x, b->width, m->w.x, &dx, &x);
    input_magnet_edge(b->x, b->width, m->w.x + m->w.width, &dx, &x);
    input_magnet_edge(b->y, b->height, m->w.y, &dy, &y);
    input_magnet_edge(b->y, b->height, m->w.y + m->w.height, &dy, &y);
    wl_list_for_each(c, &clients, link)
    {
        if (c == input_grab_client || !c->isfloating || c->isfullscreen ||
            !VISIBLEON(c, m))
            continue;
        if (b->y < c->geom.y + c->geom.height && c->geom.y < b->y + b->height) {
            input_magnet_edge(b->x, b->width, c->geom.x, &dx, &x);
            input_magnet_edge(
                b->x, b->width, c->geom.x + c->geom.width, &dx, &x);
        }
        if (b->x < c->geom.x + c->geom.width && c->geom.x < b->x + b->width) {
            input_magnet_edge(b->y, b->height, c->geom.y, &dy, &y);
            input_magnet_edge(
                b->y, b->height, c->geom.y + c->geom.height, &dy, &y);
        }
    }
    b->x = x;
    b->y = y;
}

/* Either side of [pos, pos + size) onto edge, if closer than *dist */
static void input_magnet_edge(int pos, int size, int edge, int* dist, int* to)
{
    if (abs(edge - pos) < *dist) {
        *dist = abs(edge - pos);
        *to = edge;
    }
    if (abs(edge - pos - size) < *dist) {
        *dist = abs(edge - pos - size);
        *to = edge - size;
    }
}

void input_warp_cursor(const Client* c)
{
    if (input_cursor_mode != CurNormal)
        return;

    if (!c && selected_monitor) {
        wlr_cursor_warp_closest(
            cursor,
            nullptr,
            selected_monitor->w.x + selected_monitor->w.width / 2.0,
            selected_monitor->w.y + selected_monitor->w.height / 2.0);
    } else if (c && (cursor->x < c->geom.x ||
                     cursor->x > c->geom.x + c->geom.width ||
                     cursor->y < c->geom.y ||
                     cursor->y > c->geom.y + c->geom.height)) {
        wlr_cursor_warp_closest(cursor,
                                nullptr,
                                c->geom.x + c->geom.width / 2.0,
                                c->geom.y + c->geom.height / 2.0);
    }
}

/* Runs the button bound to the wheel, if any. A notch is 120 in value120,
 * smooth scrolling without notches counts one every 15 units, the angle
 * libinput gives a notch, so a touchpad steps at about a wheel's pace. */
static int input_wheel_binding(const struct wlr_pointer_axis_event* event)
{
    static double notches;
    double step = event->delta_discrete ? event->delta_discrete / 120.0
                                        : event->delta / 15.0;
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
    unsigned int wheel = event->delta < 0 ? WheelUp : WheelDown;
    unsigned int click, mode;
    int title;
    Client* c;
    Arg arg;

    input_node_at(cursor->x, cursor->y, nullptr, &c, nullptr, nullptr, nullptr);
    click =
        c ? ClkClient : input_bar_click(monitor_at(cursor->x, cursor->y), &arg);
    title = c && bar_on_title_bar(c, cursor->x, cursor->y);
    for (const Button* b = buttons; b < buttons + button_count; b++) {
        if (CLEANMASK(mods) != CLEANMASK(b->mod) || b->button != wheel ||
            (b->click != click && !(b->click == ClkWinTitle && title)) ||
            !b->func)
            continue;
        if ((notches < 0) != (step < 0))
            notches = 0;
        /* pressed, as for a click, so focusing does not warp the pointer
         * away from where it scrolls */
        mode = input_cursor_mode;
        input_cursor_mode = CurPressed;
        for (notches += step; fabs(notches) >= 1;
             notches -= notches < 0 ? -1 : 1)
            b->func(&b->arg);
        input_cursor_mode = mode;
        return 1;
    }
    return 0;
}

void input_node_at(double x,
                   double y,
                   struct wlr_surface** psurface,
                   Client** pc,
                   LayerSurface** pl,
                   double* nx,
                   double* ny)
{
    struct wlr_scene_node *node, *pnode;
    struct wlr_surface* surface = nullptr;
    struct wlr_scene_surface* scene_surface = nullptr;
    Client* c = nullptr;
    LayerSurface* l = nullptr;

    for (int layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
        if (!(node = wlr_scene_node_at(&layers[layer]->node, x, y, nx, ny)))
            continue;

        if (node->type == WLR_SCENE_NODE_BUFFER) {
            scene_surface = wlr_scene_surface_try_from_buffer(
                wlr_scene_buffer_from_node(node));
            if (!scene_surface) {
                /* A title bar carries no surface but knows its client */
                for (pnode = node; pnode && !c; pnode = &pnode->parent->node)
                    c = pnode->data;
                if (c && c->type == LayerShell)
                    c = nullptr;
                if (c)
                    break;
                continue;
            }
            surface = scene_surface->surface;
        }
        /* Walk the tree to find a node that knows the client */
        for (pnode = node; pnode && !c; pnode = &pnode->parent->node)
            c = pnode->data;
        if (c && c->type == LayerShell) {
            c = nullptr;
            l = pnode->data;
        }
    }

    if (psurface)
        *psurface = surface;
    if (pc)
        *pc = c;
    if (pl)
        *pl = l;
}
