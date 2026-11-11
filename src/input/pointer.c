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
static unsigned int input_bar_click(Monitor* pointer_monitor, ActionArg* argument);
static void input_cursor_constrain(struct wlr_pointer_constraint_v1* constraint);
static void input_cursor_warp_to_hint(void);
static void input_handle_pointer_constraint_destroy(struct wl_listener* listener, void*);
static void input_grab(WindowClient* client, unsigned int mode);
static void input_cursor_activity(void);
static void input_magnet(struct wlr_box* box);
static void input_magnet_edge(int pos, int size, int edge, int* dist, int* to);
static void input_pointer_focus(WindowClient* client,
                                struct wlr_surface* surface,
                                double surface_x,
                                double surface_y,
                                uint32_t time);
static void input_handle_last_cursor_destroy(struct wl_listener*, void*);
static int input_wheel_binding(const struct wlr_pointer_axis_event* event);

/* variables */
struct CursorState input_last_cursor;
unsigned int input_cursor_mode;
WindowClient* input_grab_client;
static struct wlr_pointer_constraint_v1* input_active_constraint;
bool input_cursor_hidden = false;
static int input_grab_x, input_grab_y; /* client-relative */
static double input_press_x, input_press_y;

/* function implementations */
/* The part of pm's bar the cursor is over, ClkRoot if it is not over it.
 * arg picks up the tag or tray item the part stands for. */
static unsigned int input_bar_click(Monitor* pointer_monitor, ActionArg* argument)
{
    /* under bar.single_monitor the bar shows the focused monitor's tags */
    Monitor* target =
        settings->bar.single_monitor && selected_monitor ? selected_monitor : pointer_monitor;

    if (exclusive_focus || !pointer_monitor)
        return CLICK_AREA_ROOT;
    struct wlr_scene_node* node = wlr_scene_node_at(&layers[SCENE_LAYER_BOTTOM]->node, cursor->x,
                                                    cursor->y, nullptr, nullptr);
    if (!node)
        return CLICK_AREA_ROOT;
    struct wlr_scene_buffer* buffer = wlr_scene_buffer_from_node(node);
    if (!buffer || buffer != pointer_monitor->bar_scene_buffer)
        return CLICK_AREA_ROOT;

    unsigned int tag_index = 0;
    unsigned int tags_width = 0;
    const double click_x = (cursor->x - pointer_monitor->geometry.x - settings->bar.padding) *
                           pointer_monitor->wlr_output->scale;
    const int tray_width = tray_get_width(pointer_monitor->tray);
    const int status_width = bar_status_shown(pointer_monitor) ? STATUSW(pointer_monitor) : 0;
    do
        tags_width += bar_tag_width(pointer_monitor, target, tag_index);
    while (click_x >= tags_width && ++tag_index < settings->look.tag_count);
    if (tag_index < settings->look.tag_count) {
        argument->unsigned_integer = 1 << tag_index;
        return CLICK_AREA_TAGS;
    } else if (settings->bar.layout_symbol &&
               click_x < tags_width + TEXTW(pointer_monitor, target->layout_symbol))
        return CLICK_AREA_LAYOUT_SYMBOL;
    else if (tray_width && click_x > pointer_monitor->bar_metrics.width_px - tray_width) {
        argument->unsigned_integer = tray_index_at(
            pointer_monitor->tray, click_x - (pointer_monitor->bar_metrics.width_px - tray_width));
        return CLICK_AREA_TRAY;
    } else if (click_x > pointer_monitor->bar_metrics.width_px - (status_width + tray_width)) {
        argument->integer =
            (int)(click_x - (pointer_monitor->bar_metrics.width_px - (status_width + tray_width)));
        return CLICK_AREA_STATUS;
    }
    /* x + 1, so 0 still means "from the keyboard" */
    argument->unsigned_integer = (unsigned int)click_x + 1;
    return CLICK_AREA_BAR_TITLE;
}

void input_handle_axis(struct wl_listener*, void* data)
{
    /* This event is forwarded by the cursor when a pointer emits an axis event,
     * for example when you move the scroll wheel. */
    struct wlr_pointer_axis_event* event = data;
    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    input_cursor_activity();
    {
        WindowClient* client;
        ActionArg argument;
        Monitor* pointer_monitor = monitor_at(cursor->x, cursor->y);
        struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
        int horizontal = event->orientation == WL_POINTER_AXIS_HORIZONTAL_SCROLL ||
                         (keyboard && wlr_keyboard_get_modifiers(keyboard) & WLR_MODIFIER_SHIFT);

        input_node_at(cursor->x, cursor->y, nullptr, &client, nullptr, nullptr, nullptr);
        if (!lock_active && !client &&
            input_bar_click(pointer_monitor, &argument) == CLICK_AREA_BAR_TITLE &&
            bar_notification_wheel(pointer_monitor, event->delta, horizontal))
            return;
    }
    if (!lock_active && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        WindowClient* client;
        ActionArg argument;
        Monitor* pointer_monitor = monitor_at(cursor->x, cursor->y);

        input_node_at(cursor->x, cursor->y, nullptr, &client, nullptr, nullptr, nullptr);
        if (!client && input_bar_click(pointer_monitor, &argument) == CLICK_AREA_STATUS &&
            bar_status_scroll(pointer_monitor, argument.integer, event->delta))
            return;
    }
    if (!lock_active && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL &&
        input_wheel_binding(event))
        return;
    /* Notify the client with pointer focus of the axis event. */
    wlr_seat_pointer_notify_axis(seat, event->time_msec, event->orientation, event->delta,
                                 event->delta_discrete, event->source, event->relative_direction);
}

void input_handle_button(struct wl_listener*, void* data)
{
    struct wlr_pointer_button_event* event = data;
    ActionArg argument = { .pointer =
                               nullptr }; /* .v is the widest member: zeroes the whole union */

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    input_cursor_activity();

    /* the open menu owns the pointer, nothing under it sees the click */
    if (!lock_active && traypopup_active()) {
        if (event->state == WL_POINTER_BUTTON_STATE_PRESSED)
            traypopup_click(cursor->x, cursor->y);
        return;
    }

    unsigned int click = CLICK_AREA_ROOT;
    WindowClient* client;
    input_node_at(cursor->x, cursor->y, nullptr, &client, nullptr, nullptr, nullptr);
    if (client)
        click = CLICK_AREA_CLIENT;

    switch (event->state) {
        case WL_POINTER_BUTTON_STATE_PRESSED:
            input_cursor_mode = CURSOR_MODE_PRESSED;
            /* the monitor the pointer is on, bar included */
            Monitor* pointer_monitor = monitor_at(cursor->x, cursor->y);
            if (lock_active) {
                selected_monitor = pointer_monitor;
                break;
            }

            if (!client &&
                (click = input_bar_click(pointer_monitor, &argument)) != CLICK_AREA_ROOT) {
                /* The bar belongs to pm, but under bar.single_monitor it stands
                 * in for the focused monitor: leaving selected_monitor alone is
                 * what lands the click on the monitor whose tags are on show.
                 */
                if (!settings->bar.single_monitor || !selected_monitor)
                    selected_monitor = pointer_monitor;
            } else {
                selected_monitor = pointer_monitor;
            }

            /* Change focus if the button was _pressed_ over a client */
            input_node_at(cursor->x, cursor->y, nullptr, &client, nullptr, nullptr, nullptr);
            if (click == CLICK_AREA_CLIENT &&
                (!client_is_unmanaged(client) || client_wants_focus(client)))
                client_focus(client, 1);

            if (click == CLICK_AREA_STATUS && bar_status_click(pointer_monitor, argument.integer,
                                                               event->button == BTN_LEFT     ? 1
                                                               : event->button == BTN_RIGHT  ? 2
                                                               : event->button == BTN_MIDDLE ? 3
                                                                                             : 0))
                return;

            struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
            const uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
            if (click == CLICK_AREA_CLIENT && !CLEANMASK(mods) && event->button == BTN_LEFT &&
                bar_title_click(client, cursor->x, cursor->y))
                return;
            for (const MouseBinding *binding = settings->bindings.mouse_bindings,
                                    *end = binding + settings->bindings.mouse_binding_count;
                 binding < end; binding++) {
                if (CLEANMASK(mods) == CLEANMASK(binding->modifiers) &&
                    event->button == binding->button && click == binding->click_area &&
                    binding->callback) {
                    binding->callback((click == CLICK_AREA_TAGS || click == CLICK_AREA_TRAY ||
                                       click == CLICK_AREA_BAR_TITLE) &&
                                              binding->argument.integer == 0
                                          ? &argument
                                          : &binding->argument);
                    return;
                }
            }
            break;
        case WL_POINTER_BUTTON_STATE_RELEASED:
            /* If you released any buttons, we exit interactive move/resize
             * mode. */
            /* TODO: should reset to the pointer focus's current
             * input_handle_request_cursor */
            if (!lock_active && input_cursor_mode != CURSOR_MODE_NORMAL &&
                input_cursor_mode != CURSOR_MODE_PRESSED) {
                wlr_cursor_set_xcursor(cursor, cursor_manager, "default");
                input_cursor_mode = CURSOR_MODE_NORMAL;
                /* Drop the window off on its new monitor */
                selected_monitor = monitor_at(cursor->x, cursor->y);
                client_set_monitor(input_grab_client, selected_monitor, 0);
                input_grab_client = nullptr;
                return;
            }
            input_cursor_mode = CURSOR_MODE_NORMAL;
            input_grab_client = nullptr;
            break;
    }
    /* If the event wasn't handled by the compositor, notify the client with
     * pointer focus that a button press has occurred */
    wlr_seat_pointer_notify_button(seat, event->time_msec, event->button, event->state);
}

void input_handle_new_pointer_constraint(struct wl_listener*, void* data)
{
    PointerConstraint* pointer_constraint = util_calloc(1, sizeof(*pointer_constraint));
    pointer_constraint->constraint = data;
    LISTEN(&pointer_constraint->constraint->events.destroy, &pointer_constraint->destroy,
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
    WindowClient* client = nullptr;
    double hint_x = input_active_constraint->current.cursor_hint.x;
    double hint_y = input_active_constraint->current.cursor_hint.y;

    client_from_surface(input_active_constraint->surface, &client, nullptr);
    if (client && input_active_constraint->current.cursor_hint.enabled) {
        wlr_cursor_warp(cursor, nullptr, hint_x + client->geometry.x + client->border_width,
                        hint_y + client->geometry.y + client->border_width);
        wlr_seat_pointer_warp(input_active_constraint->seat, hint_x, hint_y);
    }
}

static void input_handle_pointer_constraint_destroy(struct wl_listener* listener, void*)
{
    PointerConstraint* pointer_constraint = wl_container_of(listener, pointer_constraint, destroy);

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
        wlr_cursor_set_xcursor(cursor, cursor_manager,
                               wlr_cursor_shape_v1_name(input_last_cursor.shape));
    } else if (input_last_cursor.has_client_cursor) {
        /* surface may be NULL here: the client explicitly hid its cursor
         * (e.g. a game with pointer lock), which must be honored rather
         * than falling back to the default arrow below. */
        wlr_cursor_set_surface(cursor, input_last_cursor.surface, input_last_cursor.hotspot_x,
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

    if (!event->time_msec) /* this is 0 with virtual pointers */
        wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);

    double layout_x;
    double layout_y;
    wlr_cursor_absolute_to_layout_coords(cursor, &event->pointer->base, event->x, event->y,
                                         &layout_x, &layout_y);
    double delta_x = layout_x - cursor->x;
    double delta_y = layout_y - cursor->y;
    input_motion(event->time_msec, &event->pointer->base, delta_x, delta_y, delta_x, delta_y);
}

void input_cursor_moved(void)
{
    Monitor* monitor = monitor_at(cursor->x, cursor->y);

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    input_cursor_activity();

    /* the part of a zoomed monitor on show follows the pointer */
    if (monitor && monitor->zoom_factor > 1)
        wlr_output_schedule_frame(monitor->wlr_output);

    /* Update selected_monitor (even while dragging a window) */
    if (settings->input.sloppy_focus) {
        Monitor* previous_monitor = selected_monitor;
        selected_monitor = monitor_at(cursor->x, cursor->y);
        /* the single bar follows the focus, and crossing into empty
         * space changes it without going through client_focus() */
        if ((settings->bar.single_monitor || settings->bar.status == BAR_STATUS_FOCUSED) &&
            selected_monitor != previous_monitor)
            bar_draw_all();
    }
}

void input_motion(uint32_t time,
                  struct wlr_input_device* device,
                  double delta_x,
                  double delta_y,
                  double delta_x_unaccel,
                  double delta_y_unaccel)
{
    double surface_x = 0;
    double surface_y = 0;
    WindowClient* client = nullptr;
    WindowClient* focused_client = nullptr;
    LayerSurface* layer_surface = nullptr;
    struct wlr_surface* surface = nullptr;

    /* Find the client under the pointer and send the event along. */
    input_node_at(cursor->x, cursor->y, &surface, &client, nullptr, &surface_x, &surface_y);
    bar_title_hover(client, cursor->x, cursor->y);

    if (input_cursor_mode == CURSOR_MODE_PRESSED && !seat->drag &&
        surface != seat->pointer_state.focused_surface &&
        client_from_surface(seat->pointer_state.focused_surface, &focused_client, &layer_surface) >=
            0) {
        client = focused_client;
        surface = seat->pointer_state.focused_surface;
        surface_x =
            cursor->x - (layer_surface ? layer_surface->scene->node.x : focused_client->geometry.x);
        surface_y =
            cursor->y - (layer_surface ? layer_surface->scene->node.y : focused_client->geometry.y);
    }

    /* time is 0 in internal calls meant to restore pointer focus. */
    if (time) {
        wlr_relative_pointer_manager_v1_send_relative_motion(
            relative_pointer_manager, seat, (uint64_t)time * 1000, delta_x, delta_y,
            delta_x_unaccel, delta_y_unaccel);

        struct wlr_pointer_constraint_v1* constraint;
        wl_list_for_each(constraint, &pointer_constraints->constraints, link)
            input_cursor_constrain(constraint);

        if (input_active_constraint && input_cursor_mode != CURSOR_MODE_RESIZE &&
            input_cursor_mode != CURSOR_MODE_MOVE) {
            /* own variable: client is the client under the cursor, and the
             * constraint may sit on an unmapped one (gamescope) */
            WindowClient* constraint_client = nullptr;
            client_from_surface(input_active_constraint->surface, &constraint_client, nullptr);
            if (constraint_client &&
                input_active_constraint->surface == seat->pointer_state.focused_surface) {
                surface_x =
                    cursor->x - constraint_client->geometry.x - constraint_client->border_width;
                surface_y =
                    cursor->y - constraint_client->geometry.y - constraint_client->border_width;
                double surface_x_confined;
                double surface_y_confined;
                if (wlr_region_confine(&input_active_constraint->region, surface_x, surface_y,
                                       surface_x + delta_x, surface_y + delta_y,
                                       &surface_x_confined, &surface_y_confined)) {
                    delta_x = surface_x_confined - surface_x;
                    delta_y = surface_y_confined - surface_y;
                }

                if (input_active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
                    return;
            }
        }

        wlr_cursor_move(cursor, device, delta_x, delta_y);
        input_cursor_moved();
    }

    /* Update drag icon's position */
    wlr_scene_node_set_position(&drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));

    /* hovering the open menu, so nothing below it takes focus */
    if (!lock_active && traypopup_active()) {
        traypopup_motion(cursor->x, cursor->y);
        if (!seat->drag && !input_cursor_hidden)
            wlr_cursor_set_xcursor(cursor, cursor_manager, "default");
        return;
    }

    if (input_cursor_mode == CURSOR_MODE_PRESSED && input_grab_client && time && !lock_active &&
        (cursor->x - input_press_x) * (cursor->x - input_press_x) +
                (cursor->y - input_press_y) * (cursor->y - input_press_y) >=
            settings->input.move_threshold * settings->input.move_threshold)
        input_grab(input_grab_client, CURSOR_MODE_MOVE);

    /* If we are currently grabbing the mouse, handle and return */
    if (input_cursor_mode == CURSOR_MODE_MOVE) {
        /* A tiled client keeps its slot in the layout: dragging it just swaps
         * it with the tile under the cursor, i3/sway style. */
        if (!input_grab_client->is_floating) {
            if (time && client && client != input_grab_client && !client->is_floating &&
                client->monitor == input_grab_client->monitor &&
                VISIBLEON(input_grab_client, input_grab_client->monitor)) {
                layout_swap_clients(input_grab_client, client);
                layout_arrange(input_grab_client->monitor);
            }
            return;
        }
        /* Move the grabbed client to the new position. */
        struct wlr_box box = (struct wlr_box){ .x = (int)round(cursor->x) - input_grab_x,
                                               .y = (int)round(cursor->y) - input_grab_y,
                                               .width = input_grab_client->geometry.width,
                                               .height = input_grab_client->geometry.height };
        input_magnet(&box);
        client_resize(input_grab_client, box, 1);
        return;
    } else if (input_cursor_mode == CURSOR_MODE_RESIZE) {
        client_resize(
            input_grab_client,
            (struct wlr_box){ .x = input_grab_client->geometry.x,
                              .y = input_grab_client->geometry.y,
                              .width = (int)round(cursor->x) - input_grab_client->geometry.x,
                              .height = (int)round(cursor->y) - input_grab_client->geometry.y },
            1);
        return;
    }

    /* If there's no client surface under the cursor, set the cursor image to a
     * default. This is what makes the cursor image appear when you move it
     * off of a client or over its border. */
    if (!surface && !seat->drag && !input_cursor_hidden)
        wlr_cursor_set_xcursor(cursor, cursor_manager, "default");

    input_pointer_focus(client, surface, surface_x, surface_y, time);
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
    input_motion(event->time_msec, &event->pointer->base, event->delta_x, event->delta_y,
                 event->unaccel_dx, event->unaccel_dy);
}

void input_move_resize(const ActionArg* argument)
{

    if (input_cursor_mode != CURSOR_MODE_NORMAL && input_cursor_mode != CURSOR_MODE_PRESSED)
        return;
    WindowClient* client;
    input_node_at(cursor->x, cursor->y, nullptr, &client, nullptr, nullptr, nullptr);
    input_press_x = cursor->x;
    input_press_y = cursor->y;
    input_grab(client, argument->unsigned_integer);
}

/* Moving c from the title bar waits for move_threshold of motion, so a plain
 * click stays a click: until then input_grab_client is set but
 * input_cursor_mode is CurPressed.
 */
void input_title_grab(WindowClient* client)
{
    input_grab_client = client;
    input_press_x = cursor->x;
    input_press_y = cursor->y;
}

static void input_grab(WindowClient* client, unsigned int mode)
{
    input_grab_client = nullptr;
    if (!client || client_is_unmanaged(client) || client->is_fullscreen)
        return;
    input_grab_client = client;

    /* Float the window and tell input_motion to grab it - except when moving a
     * tiled client, which stays tiled and is swapped with the tile it is
     * dragged onto instead. */
    if (mode != CURSOR_MODE_MOVE || client->is_floating || !client->monitor)
        client_set_floating(client, 1);
    switch (input_cursor_mode = mode) {
        case CURSOR_MODE_MOVE:
            input_grab_x = (int)round(input_press_x) - client->geometry.x;
            input_grab_y = (int)round(input_press_y) - client->geometry.y;
            wlr_cursor_set_xcursor(cursor, cursor_manager, "grabbing");
            break;
        case CURSOR_MODE_RESIZE:
            /* Doesn't work for X11 output - the next absolute motion event
             * returns the cursor to where it started */
            wlr_cursor_warp_closest(cursor, nullptr, client->geometry.x + client->geometry.width,
                                    client->geometry.y + client->geometry.height);
            wlr_cursor_set_xcursor(cursor, cursor_manager, "se-resize");
            break;
    }
}

static void input_pointer_focus(WindowClient* client,
                                struct wlr_surface* surface,
                                double surface_x,
                                double surface_y,
                                uint32_t time)
{

    /* Only a real client surface takes focus: hovering a title bar (a tab) or
     * a border has no surface, and must not select anything until clicked. */
    if (surface && surface != seat->pointer_state.focused_surface && settings->input.sloppy_focus &&
        time && client && !client_is_unmanaged(client))
        client_focus(client, 0);

    /* If surface is NULL, clear pointer focus */
    if (!surface) {
        wlr_seat_pointer_notify_clear_focus(seat);
        return;
    }

    struct timespec now;
    if (!time) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        time = now.tv_sec * 1000 + now.tv_nsec / 1'000'000;
    }

    /* Let the client know that the mouse cursor has entered one
     * of its surfaces, and make keyboard focus follow if desired.
     * wlroots makes this a no-op if surface is already focused */
    wlr_seat_pointer_notify_enter(seat, surface, surface_x, surface_y);
    wlr_seat_pointer_notify_motion(seat, time, surface_x, surface_y);
}

static void input_handle_last_cursor_destroy(struct wl_listener*, void*)
{
    /* When the surface is destroyed, clear our reference to it */
    input_last_cursor.surface = nullptr;
    wl_list_remove(&input_last_cursor.surface_destroy.link);
    wl_list_init(&input_last_cursor.surface_destroy.link);
}

void input_handle_request_cursor(struct wl_listener*, void* data)
{
    /* This event is raised by the seat when a client provides a cursor image */
    struct wlr_seat_pointer_request_set_cursor_event* event = data;
    /* If we're "grabbing" the cursor, don't use the client's image, we will
     * restore it after "grabbing" sending a leave event, followed by a enter
     * event, which will result in the client requesting set the cursor surface
     */
    if (input_cursor_mode != CURSOR_MODE_NORMAL && input_cursor_mode != CURSOR_MODE_PRESSED)
        return;
    /* This can be sent by any client, so we check to make sure this one
     * actually has pointer focus first. If so, we can tell the cursor to
     * use the provided surface as the cursor image. It will set the
     * hardware cursor on the output that it's currently on and continue to
     * do so as the cursor moves between outputs. */
    if (event->seat_client == seat->pointer_state.focused_client)
        input_set_client_cursor(event->surface, event->hotspot_x, event->hotspot_y);
}

void input_set_client_cursor(struct wlr_surface* surface, int hotspot_x, int hotspot_y)
{
    input_last_cursor.shape = 0;
    input_last_cursor.surface = surface;
    input_last_cursor.has_client_cursor = true;
    input_last_cursor.hotspot_x = hotspot_x;
    input_last_cursor.hotspot_y = hotspot_y;

    wl_list_remove(&input_last_cursor.surface_destroy.link);
    wl_list_init(&input_last_cursor.surface_destroy.link);
    if (surface) {
        input_last_cursor.surface_destroy.notify = input_handle_last_cursor_destroy;
        wl_signal_add(&surface->events.destroy, &input_last_cursor.surface_destroy);
    }

    if (!input_cursor_hidden)
        wlr_cursor_set_surface(cursor, surface, hotspot_x, hotspot_y);
}

void input_handle_request_cursor_shape(struct wl_listener*, void* data)
{
    struct wlr_cursor_shape_manager_v1_request_set_shape_event* event = data;
    if (input_cursor_mode != CURSOR_MODE_NORMAL && input_cursor_mode != CURSOR_MODE_PRESSED)
        return;
    /* This can be sent by any client, so we check to make sure this one
     * actually has pointer (or pen) focus first. If so, we can tell the
     * cursor to use the provided cursor shape. */
    if (event->device_type == WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_TABLET_TOOL
            ? tablet_tool_focused_by(event->tablet_tool, event->seat_client)
            : event->seat_client == seat->pointer_state.focused_client) {
        input_last_cursor.shape = event->shape;
        input_last_cursor.surface = nullptr;

        if (!input_cursor_hidden)
            wlr_cursor_set_xcursor(cursor, cursor_manager, wlr_cursor_shape_v1_name(event->shape));
    }
}

/* Sticks a moved floating window to the nearby edges of the window area and
 * of the floating windows beside it */
static void input_magnet(struct wlr_box* box)
{
    Monitor* monitor = monitor_at(cursor->x, cursor->y);

    if (!monitor || settings->input.move_snap <= 0)
        return;
    int snap_x = settings->input.move_snap + 1;
    int snap_y = settings->input.move_snap + 1;
    int snapped_x = box->x;
    int snapped_y = box->y;
    input_magnet_edge(box->x, box->width, monitor->usable_area.x, &snap_x, &snapped_x);
    input_magnet_edge(box->x, box->width, monitor->usable_area.x + monitor->usable_area.width,
                      &snap_x, &snapped_x);
    input_magnet_edge(box->y, box->height, monitor->usable_area.y, &snap_y, &snapped_y);
    input_magnet_edge(box->y, box->height, monitor->usable_area.y + monitor->usable_area.height,
                      &snap_y, &snapped_y);
    WindowClient* client;
    wl_list_for_each(client, &clients, link) {
        if (client == input_grab_client || !client->is_floating || client->is_fullscreen ||
            !VISIBLEON(client, monitor))
            continue;
        if (box->y < client->geometry.y + client->geometry.height &&
            client->geometry.y < box->y + box->height) {
            input_magnet_edge(box->x, box->width, client->geometry.x, &snap_x, &snapped_x);
            input_magnet_edge(box->x, box->width, client->geometry.x + client->geometry.width,
                              &snap_x, &snapped_x);
        }
        if (box->x < client->geometry.x + client->geometry.width &&
            client->geometry.x < box->x + box->width) {
            input_magnet_edge(box->y, box->height, client->geometry.y, &snap_y, &snapped_y);
            input_magnet_edge(box->y, box->height, client->geometry.y + client->geometry.height,
                              &snap_y, &snapped_y);
        }
    }
    box->x = snapped_x;
    box->y = snapped_y;
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

void input_warp_cursor(const WindowClient* client)
{
    if (input_cursor_mode != CURSOR_MODE_NORMAL)
        return;

    if (!client && selected_monitor) {
        wlr_cursor_warp_closest(
            cursor, nullptr,
            selected_monitor->usable_area.x + selected_monitor->usable_area.width / 2.0,
            selected_monitor->usable_area.y + selected_monitor->usable_area.height / 2.0);
    } else if (client && (cursor->x < client->geometry.x ||
                          cursor->x > client->geometry.x + client->geometry.width ||
                          cursor->y < client->geometry.y ||
                          cursor->y > client->geometry.y + client->geometry.height)) {
        wlr_cursor_warp_closest(cursor, nullptr, client->geometry.x + client->geometry.width / 2.0,
                                client->geometry.y + client->geometry.height / 2.0);
    }
}

/* Runs the button bound to the wheel, if any. A notch is 120 in value120,
 * smooth scrolling without notches counts one every 15 units, the angle
 * libinput gives a notch, so a touchpad steps at about a wheel's pace. */
static int input_wheel_binding(const struct wlr_pointer_axis_event* event)
{
    static double notches;
    double step = event->delta_discrete ? event->delta_discrete / 120.0 : event->delta / 15.0;
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
    unsigned int wheel = event->delta < 0 ? MOUSE_WHEEL_UP : MOUSE_WHEEL_DOWN;
    WindowClient* client;

    input_node_at(cursor->x, cursor->y, nullptr, &client, nullptr, nullptr, nullptr);
    ActionArg argument;
    unsigned int click =
        client ? CLICK_AREA_CLIENT : input_bar_click(monitor_at(cursor->x, cursor->y), &argument);
    int title = client && bar_on_title_bar(client, cursor->x, cursor->y);
    unsigned int mode;
    for (const MouseBinding *binding = settings->bindings.mouse_bindings,
                            *end = binding + settings->bindings.mouse_binding_count;
         binding < end; binding++) {
        if (CLEANMASK(mods) != CLEANMASK(binding->modifiers) || binding->button != wheel ||
            (binding->click_area != click &&
             !(binding->click_area == CLICK_AREA_WINDOW_TITLE && title)) ||
            !binding->callback)
            continue;
        if ((notches < 0) != (step < 0))
            notches = 0;
        /* pressed, as for a click, so focusing does not warp the pointer
         * away from where it scrolls */
        mode = input_cursor_mode;
        input_cursor_mode = CURSOR_MODE_PRESSED;
        for (notches += step; fabs(notches) >= 1; notches -= notches < 0 ? -1 : 1)
            binding->callback(&binding->argument);
        input_cursor_mode = mode;
        return 1;
    }
    return 0;
}

void input_node_at(double x,
                   double y,
                   struct wlr_surface** out_surface,
                   WindowClient** out_client,
                   LayerSurface** out_layer_surface,
                   double* node_x,
                   double* node_y)
{
    struct wlr_surface* surface = nullptr;
    struct wlr_scene_surface* scene_surface = nullptr;
    WindowClient* client = nullptr;
    LayerSurface* layer_surface = nullptr;

    for (int layer = SCENE_LAYER_COUNT - 1; !surface && layer >= 0; layer--) {
        struct wlr_scene_node* node = wlr_scene_node_at(&layers[layer]->node, x, y, node_x, node_y);
        if (!node)
            continue;
        struct wlr_scene_node* parent_node;

        if (node->type == WLR_SCENE_NODE_BUFFER) {
            scene_surface = wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node));
            if (!scene_surface) {
                /* A title bar carries no surface but knows its client */
                for (parent_node = node; parent_node && !client;
                     parent_node = &parent_node->parent->node)
                    client = parent_node->data;
                if (client && client->type == SURFACE_TYPE_LAYER_SHELL)
                    client = nullptr;
                if (client)
                    break;
                continue;
            }
            surface = scene_surface->surface;
        }
        /* Walk the tree to find a node that knows the client */
        for (parent_node = node; parent_node && !client; parent_node = &parent_node->parent->node)
            client = parent_node->data;
        if (client && client->type == SURFACE_TYPE_LAYER_SHELL) {
            client = nullptr;
            layer_surface = parent_node->data;
        }
    }

    if (out_surface)
        *out_surface = surface;
    if (out_client)
        *out_client = client;
    if (out_layer_surface)
        *out_layer_surface = layer_surface;
}
