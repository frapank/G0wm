/*
 * See LICENSE file for copyright and license details.
 *
 * keyboard, pointer, tablet and drag-and-drop handling
 */
#include "g0wm.h"

/* function declarations */
static void createkeyboard(struct wlr_keyboard* keyboard);
static void createpointer(struct wlr_pointer* pointer);
static void cursorconstrain(struct wlr_pointer_constraint_v1* constraint);
static void cursorwarptohint(void);
static void destroydragicon(struct wl_listener* listener, void* data);
static void destroypointerconstraint(struct wl_listener* listener, void* data);
static void handlecursoractivity(void);
static int hidecursor(void* data);
static int keybinding(uint32_t mods, xkb_keysym_t sym);
static void keypress(struct wl_listener* listener, void* data);
static void keypressmod(struct wl_listener* listener, void* data);
static int keyrepeat(void* data);
static void pointerfocus(Client* c,
                         struct wlr_surface* surface,
                         double sx,
                         double sy,
                         uint32_t time);
static void unlastcursor(struct wl_listener* listener, void* data);
static void xytonode(double x,
                     double y,
                     struct wlr_surface** psurface,
                     Client** pc,
                     LayerSurface** pl,
                     double* nx,
                     double* ny);

/* variables */
static struct wlr_pointer_constraint_v1* active_constraint;
static bool cursor_hidden = false;
static int grabcx, grabcy; /* client-relative */
#ifdef RUNNER
static uint32_t runner_repeatcp; /* codepoint the armed key repeat types */
#endif                           /* RUNNER */

/* function implementations */
void axisnotify(struct wl_listener* listener, void* data)
{
    /* This event is forwarded by the cursor when a pointer emits an axis event,
     * for example when you move the scroll wheel. */
    struct wlr_pointer_axis_event* event = data;
    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    handlecursoractivity();
    /* TODO: allow usage of scroll wheel for mousebindings, it can be
     * implemented by checking the event's orientation and the delta of the
     * event */
    /* Notify the client with pointer focus of the axis event. */
    wlr_seat_pointer_notify_axis(seat,
                                 event->time_msec,
                                 event->orientation,
                                 event->delta,
                                 event->delta_discrete,
                                 event->source,
                                 event->relative_direction);
}

void buttonpress(struct wl_listener* listener, void* data)
{
    unsigned int i = 0, x = 0;
    double cx;
    int traywidth = 0, statusw;
#ifdef SYSTRAY
    unsigned int ti = 0, trayitems;
    double tx;
#endif
    unsigned int click;
    struct wlr_pointer_button_event* event = data;
    struct wlr_keyboard* keyboard;
    struct wlr_scene_node* node;
    struct wlr_scene_buffer* buffer;
    Monitor* pm; /* the monitor the pointer is on, bar included */
    uint32_t mods;
    Arg arg = {
        .v = NULL
    }; /* .v is the widest member: zeroes the whole union */
    Client* c;
    const Button* b;

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
    handlecursoractivity();

    click = ClkRoot;
    xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
    if (c)
        click = ClkClient;

    switch (event->state) {
        case WL_POINTER_BUTTON_STATE_PRESSED:
            cursor_mode = CurPressed;
            pm = xytomon(cursor->x, cursor->y);
            if (locked) {
                selmon = pm;
                break;
            }

            if (!c && !exclusive_focus && pm &&
                (node = wlr_scene_node_at(&layers[LyrBottom]->node,
                                          cursor->x,
                                          cursor->y,
                                          NULL,
                                          NULL)) &&
                (buffer = wlr_scene_buffer_from_node(node)) &&
                buffer == pm->scene_buffer) {
                /* The bar belongs to pm, but under barsinglemon it stands in
                 * for the focused monitor: leaving selmon alone is what lands
                 * the click on the monitor whose tags are on show. */
                if (!barsinglemon || !selmon)
                    selmon = pm;
                cx = (cursor->x - pm->m.x) * pm->wlr_output->scale;
#ifdef SYSTRAY
                traywidth = tray_get_width(pm->tray);
#endif
                statusw = STATUSW(pm);
                do
                    x += TEXTW(pm, tags[i]);
                while (cx >= x && ++i < LENGTH(tags));
                if (i < LENGTH(tags)) {
                    click = ClkTagBar;
                    arg.ui = 1 << i;
                } else if (cx < x + TEXTW(pm, selmon->ltsymbol))
                    click = ClkLtSymbol;
#ifdef SYSTRAY
                else if (traywidth && cx > pm->b.width - traywidth) {
                    /* the tray slots are evenly sized, so which one was hit
                     * follows from the cursor offset into the tray */
                    trayitems = watcher_get_n_items(&watcher);
                    tx = pm->b.width - traywidth;
                    while (trayitems && ++ti < trayitems &&
                           cx >= (tx += (double)traywidth / trayitems))
                        ;
                    click = ClkTray;
                    arg.ui = ti - 1;
                }
#endif
                else if (cx > pm->b.width - (statusw + traywidth)) {
                    click = ClkStatus;
                } else
                    click = ClkTitle;
            } else {
                selmon = pm;
            }

            /* Change focus if the button was _pressed_ over a client */
            xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
            if (click == ClkClient &&
                (!client_is_unmanaged(c) || client_wants_focus(c)))
                focusclient(c, 1);

            keyboard = wlr_seat_get_keyboard(seat);
            mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
            for (b = buttons; b < END(buttons); b++) {
                if (CLEANMASK(mods) == CLEANMASK(b->mod) &&
                    event->button == b->button && click == b->click &&
                    b->func) {
                    b->func((click == ClkTagBar || click == ClkTray) &&
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
            /* TODO: should reset to the pointer focus's current setcursor */
            if (!locked && cursor_mode != CurNormal &&
                cursor_mode != CurPressed) {
                wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
                cursor_mode = CurNormal;
                /* Drop the window off on its new monitor */
                selmon = xytomon(cursor->x, cursor->y);
                setmon(grabc, selmon, 0);
                grabc = NULL;
                return;
            }
            cursor_mode = CurNormal;
            break;
    }
    /* If the event wasn't handled by the compositor, notify the client with
     * pointer focus that a button press has occurred */
    wlr_seat_pointer_notify_button(
        seat, event->time_msec, event->button, event->state);
}

static void createkeyboard(struct wlr_keyboard* keyboard)
{
    /* Set the keymap to match the group keymap */
    wlr_keyboard_set_keymap(keyboard, kb_group->wlr_group->keyboard.keymap);

    /* Add the new keyboard to the group */
    wlr_keyboard_group_add_keyboard(kb_group->wlr_group, keyboard);
}

KeyboardGroup* createkeyboardgroup(void)
{
    KeyboardGroup* group = ecalloc(1, sizeof(*group));
    struct xkb_context* context;
    struct xkb_keymap* keymap;

    group->wlr_group = wlr_keyboard_group_create();
    group->wlr_group->data = group;

    /* Prepare an XKB keymap and assign it to the keyboard group. */
    context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!(keymap = xkb_keymap_new_from_names(
              context, &xkb_rules, XKB_KEYMAP_COMPILE_NO_FLAGS)))
        die("failed to compile keymap");

    wlr_keyboard_set_keymap(&group->wlr_group->keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);

    wlr_keyboard_set_repeat_info(
        &group->wlr_group->keyboard, repeat_rate, repeat_delay);

    /* Set up listeners for keyboard events */
    LISTEN(&group->wlr_group->keyboard.events.key, &group->key, keypress);
    LISTEN(&group->wlr_group->keyboard.events.modifiers,
           &group->modifiers,
           keypressmod);

    group->key_repeat_source =
        wl_event_loop_add_timer(event_loop, keyrepeat, group);

    /* A seat can only have one keyboard, but this is a limitation of the
     * Wayland protocol - not wlroots. We assign all connected keyboards to the
     * same wlr_keyboard_group, which provides a single wlr_keyboard interface
     * for all of them. Set this combined wlr_keyboard as the seat keyboard.
     */
    wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
    return group;
}

static void createpointer(struct wlr_pointer* pointer)
{
    struct libinput_device* device;
    if (wlr_input_device_is_libinput(&pointer->base) &&
        (device = wlr_libinput_get_device_handle(&pointer->base))) {

        if (libinput_device_config_tap_get_finger_count(device)) {
            libinput_device_config_tap_set_enabled(device, tap_to_click);
            libinput_device_config_tap_set_drag_enabled(device, tap_and_drag);
            libinput_device_config_tap_set_drag_lock_enabled(device, drag_lock);
            libinput_device_config_tap_set_button_map(device, button_map);
        }

        if (libinput_device_config_scroll_has_natural_scroll(device))
            libinput_device_config_scroll_set_natural_scroll_enabled(
                device, natural_scrolling);

        if (libinput_device_config_dwt_is_available(device))
            libinput_device_config_dwt_set_enabled(device,
                                                   disable_while_typing);

        if (libinput_device_config_left_handed_is_available(device))
            libinput_device_config_left_handed_set(device, left_handed);

        if (libinput_device_config_middle_emulation_is_available(device))
            libinput_device_config_middle_emulation_set_enabled(
                device, middle_button_emulation);

        if (libinput_device_config_scroll_get_methods(device) !=
            LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
            libinput_device_config_scroll_set_method(device, scroll_method);

        if (libinput_device_config_click_get_methods(device) !=
            LIBINPUT_CONFIG_CLICK_METHOD_NONE)
            libinput_device_config_click_set_method(device, click_method);

        if (libinput_device_config_send_events_get_modes(device))
            libinput_device_config_send_events_set_mode(device,
                                                        send_events_mode);

        if (libinput_device_config_accel_is_available(device)) {
            libinput_device_config_accel_set_profile(device, accel_profile);
            libinput_device_config_accel_set_speed(device, accel_speed);
        }
    }

    wlr_cursor_attach_input_device(cursor, &pointer->base);
}

void createpointerconstraint(struct wl_listener* listener, void* data)
{
    PointerConstraint* pointer_constraint =
        ecalloc(1, sizeof(*pointer_constraint));
    pointer_constraint->constraint = data;
    LISTEN(&pointer_constraint->constraint->events.destroy,
           &pointer_constraint->destroy,
           destroypointerconstraint);
}

static void cursorconstrain(struct wlr_pointer_constraint_v1* constraint)
{
    if (active_constraint == constraint)
        return;

    if (active_constraint)
        wlr_pointer_constraint_v1_send_deactivated(active_constraint);

    active_constraint = constraint;
    wlr_pointer_constraint_v1_send_activated(constraint);
}

void cursorframe(struct wl_listener* listener, void* data)
{
    /* This event is forwarded by the cursor when a pointer emits a frame
     * event. Frame events are sent after regular pointer events to group
     * multiple events together. For instance, two axis events may happen at the
     * same time, in which case a frame event won't be sent in between. */
    /* Notify the client with pointer focus of the frame event. */
    wlr_seat_pointer_notify_frame(seat);
}

static void cursorwarptohint(void)
{
    Client* c = NULL;
    double sx = active_constraint->current.cursor_hint.x;
    double sy = active_constraint->current.cursor_hint.y;

    toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
    if (c && active_constraint->current.cursor_hint.enabled) {
        wlr_cursor_warp(
            cursor, NULL, sx + c->geom.x + c->bw, sy + c->geom.y + c->bw);
        wlr_seat_pointer_warp(active_constraint->seat, sx, sy);
    }
}

static void destroydragicon(struct wl_listener* listener, void* data)
{
    /* Focus enter isn't sent during drag, so refocus the focused node. */
    focusclient(focustop(selmon), 1);
    motionnotify(0, NULL, 0, 0, 0, 0);
    wl_list_remove(&listener->link);
    free(listener);
}

static void destroypointerconstraint(struct wl_listener* listener, void* data)
{
    PointerConstraint* pointer_constraint =
        wl_container_of(listener, pointer_constraint, destroy);

    if (active_constraint == pointer_constraint->constraint) {
        cursorwarptohint();
        active_constraint = NULL;
    }

    wl_list_remove(&pointer_constraint->destroy.link);
    free(pointer_constraint);
}

void destroykeyboardgroup(struct wl_listener* listener, void* data)
{
    KeyboardGroup* group = wl_container_of(listener, group, destroy);
    wl_event_source_remove(group->key_repeat_source);
    wl_list_remove(&group->key.link);
    wl_list_remove(&group->modifiers.link);
    wl_list_remove(&group->destroy.link);
    wlr_keyboard_group_destroy(group->wlr_group);
    free(group);
}

static void handlecursoractivity(void)
{
    if (!cursor_hidden)
        return;

    cursor_hidden = false;

    if (last_cursor.shape) {
        wlr_cursor_set_xcursor(
            cursor, cursor_mgr, wlr_cursor_shape_v1_name(last_cursor.shape));
    } else if (last_cursor.has_client_cursor) {
        /* surface may be NULL here: the client explicitly hid its cursor
         * (e.g. a game with pointer lock), which must be honored rather
         * than falling back to the default arrow below. */
        wlr_cursor_set_surface(cursor,
                               last_cursor.surface,
                               last_cursor.hotspot_x,
                               last_cursor.hotspot_y);
    } else {
        wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
    }
}

static int hidecursor(void* data)
{
    wlr_cursor_unset_image(cursor);
    cursor_hidden = true;
    return 1;
}

void inputdevice(struct wl_listener* listener, void* data)
{
    /* This event is raised by the backend when a new input device becomes
     * available. */
    struct wlr_input_device* device = data;
    uint32_t caps;

    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            createkeyboard(wlr_keyboard_from_input_device(device));
            break;
        case WLR_INPUT_DEVICE_POINTER:
            createpointer(wlr_pointer_from_input_device(device));
            break;
        case WLR_INPUT_DEVICE_TABLET:
            wlr_cursor_attach_input_device(cursor, device);
            break;
        default:
            /* TODO handle other input device types */
            break;
    }

    /* We need to let the wlr_seat know what our capabilities are, which is
     * communiciated to the client. In g0wm we always have a cursor, even if
     * there are no pointer devices, so we always include that capability. */
    /* TODO do we actually require a cursor? */
    caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&kb_group->wlr_group->devices))
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(seat, caps);
}

static int keybinding(uint32_t mods, xkb_keysym_t sym)
{
    /*
     * Here we handle compositor keybindings. This is when the compositor is
     * processing keys, rather than passing them on to the client for its own
     * processing.
     */
    const Key* k;
    for (k = keys; k < END(keys); k++) {
        if (CLEANMASK(mods) == CLEANMASK(k->mod) &&
            xkb_keysym_to_lower(sym) == xkb_keysym_to_lower(k->keysym) &&
            k->func) {
            k->func(&k->arg);
            return 1;
        }
    }
    return 0;
}

static void keypress(struct wl_listener* listener, void* data)
{
    int i;
    /* This event is raised when a key is pressed or released. */
    KeyboardGroup* group = wl_container_of(listener, group, key);
    struct wlr_keyboard_key_event* event = data;

    /* Translate libinput keycode -> xkbcommon */
    uint32_t keycode = event->keycode + 8;
    /* Get a list of keysyms based on the keymap for this keyboard */
    const xkb_keysym_t* syms;
    int nsyms = xkb_state_key_get_syms(
        group->wlr_group->keyboard.xkb_state, keycode, &syms);

    int handled = 0;
    uint32_t mods = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);

    wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

    /* hide cursor when typing starts */
    if (hide_cursor_when_typing && !cursor_hidden &&
        event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
        hidecursor(NULL);

#ifdef RUNNER
    /* While the prompt is open, every key belongs to it: swallow press and
     * release instead of matching keybindings or forwarding to the client.
     * Returning early skips the repeat bookkeeping below, so disarm here what
     * that would have disarmed: the keystroke that opened the prompt counted
     * as handled and left a repeat armed, which would replay the binding and
     * toggle the prompt straight back off. */
    if (runner_active) {
        if (!locked && nsyms > 0 &&
            event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            /* only the primary keysym: the codepoint belongs to the key, not
             * to each of the syms it can produce, so looping would type it
             * once per sym */
            uint32_t codepoint = xkb_state_key_get_utf32(
                group->wlr_group->keyboard.xkb_state, keycode);
            runnerkey(syms[0], mods, codepoint);
            /* Arm the repeat for the prompt the way a handled binding does,
             * so a held key (backspace above all) repeats into it. Return and
             * Escape close the prompt from inside runnerkey(), and there is
             * nothing left to repeat into once they have. */
            if (runner_active &&
                group->wlr_group->keyboard.repeat_info.delay > 0) {
                group->mods = mods;
                group->keysyms = syms;
                group->nsyms = nsyms;
                runner_repeatcp = codepoint;
                runner_repeating = 1;
                wl_event_source_timer_update(
                    group->key_repeat_source,
                    group->wlr_group->keyboard.repeat_info.delay);
                return;
            }
        }
        group->nsyms = 0;
        runner_repeating = 0;
        wl_event_source_timer_update(group->key_repeat_source, 0);
        return;
    }
#endif

    /* On _press_ if there is no active screen locker,
     * attempt to process a compositor keybinding. */
    if (!locked && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (i = 0; i < nsyms; i++)
            handled = keybinding(mods, syms[i]) || handled;
    }

    if (handled && group->wlr_group->keyboard.repeat_info.delay > 0) {
        group->mods = mods;
        group->keysyms = syms;
        group->nsyms = nsyms;
        wl_event_source_timer_update(
            group->key_repeat_source,
            group->wlr_group->keyboard.repeat_info.delay);
    } else {
        group->nsyms = 0;
        wl_event_source_timer_update(group->key_repeat_source, 0);
    }

    if (handled)
        return;

    wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
    /* Pass unhandled keycodes along to the client. */
    wlr_seat_keyboard_notify_key(
        seat, event->time_msec, event->keycode, event->state);
}

static void keypressmod(struct wl_listener* listener, void* data)
{
    /* This event is raised when a modifier key, such as shift or alt, is
     * pressed. We simply communicate this to the client. */
    KeyboardGroup* group = wl_container_of(listener, group, modifiers);

    wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
    /* Send modifiers to the client. */
    wlr_seat_keyboard_notify_modifiers(seat,
                                       &group->wlr_group->keyboard.modifiers);
}

static int keyrepeat(void* data)
{
    KeyboardGroup* group = data;
    int i;
    if (!group->nsyms || group->wlr_group->keyboard.repeat_info.rate <= 0)
        return 0;
#ifdef RUNNER
    if (runner_active) {
        /* Holding the binding down past the repeat delay would replay it
         * while the prompt it just opened is up, closing it again: only a
         * repeat the prompt armed itself may run. Rearming after the fact
         * keeps a key that closed the prompt from repeating into nothing. */
        if (!runner_repeating)
            return 0;
        runnerkey(group->keysyms[0], group->mods, runner_repeatcp);
        if (runner_active)
            wl_event_source_timer_update(
                group->key_repeat_source,
                1000 / group->wlr_group->keyboard.repeat_info.rate);
        else
            group->nsyms = 0;
        return 0;
    }
#endif

    wl_event_source_timer_update(
        group->key_repeat_source,
        1000 / group->wlr_group->keyboard.repeat_info.rate);

    for (i = 0; i < group->nsyms; i++)
        keybinding(group->mods, group->keysyms[i]);

    return 0;
}

void motionabsolute(struct wl_listener* listener, void* data)
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
    motionnotify(event->time_msec, &event->pointer->base, dx, dy, dx, dy);
}

void tabletaxis(struct wl_listener* listener, void* data)
{
    struct wlr_tablet_tool_axis_event* event = data;
    double lx, ly, dx, dy, x, y;

    /* An axis event only carries the axes it actually updated; NAN tells
     * wlroots to keep the current coordinate for the others. */
    x = event->updated_axes & WLR_TABLET_TOOL_AXIS_X ? event->x : NAN;
    y = event->updated_axes & WLR_TABLET_TOOL_AXIS_Y ? event->y : NAN;
    if (isnan(x) && isnan(y))
        return;

    wlr_cursor_absolute_to_layout_coords(
        cursor, &event->tablet->base, x, y, &lx, &ly);
    dx = lx - cursor->x;
    dy = ly - cursor->y;
    motionnotify(event->time_msec, &event->tablet->base, dx, dy, dx, dy);
    /* Tablets emit no frame event of their own, and clients hold pointer
     * events back until one arrives. */
    wlr_seat_pointer_notify_frame(seat);
}

void tablettip(struct wl_listener* listener, void* data)
{
    struct wlr_tablet_tool_tip_event* event = data;
    struct wlr_pointer_button_event synth = {
        .time_msec = event->time_msec,
        .button = BTN_LEFT,
        .state = event->state == WLR_TABLET_TOOL_TIP_DOWN
                     ? WL_POINTER_BUTTON_STATE_PRESSED
                     : WL_POINTER_BUTTON_STATE_RELEASED,
    };
    buttonpress(NULL, &synth);
    wlr_seat_pointer_notify_frame(seat);
}

void motionnotify(uint32_t time,
                  struct wlr_input_device* device,
                  double dx,
                  double dy,
                  double dx_unaccel,
                  double dy_unaccel)
{
    double sx = 0, sy = 0, sx_confined, sy_confined;
    Client *c = NULL, *w = NULL;
    LayerSurface* l = NULL;
    struct wlr_surface* surface = NULL;
    struct wlr_pointer_constraint_v1* constraint;

    /* Find the client under the pointer and send the event along. */
    xytonode(cursor->x, cursor->y, &surface, &c, NULL, &sx, &sy);

    if (cursor_mode == CurPressed && !seat->drag &&
        surface != seat->pointer_state.focused_surface &&
        toplevel_from_wlr_surface(
            seat->pointer_state.focused_surface, &w, &l) >= 0) {
        c = w;
        surface = seat->pointer_state.focused_surface;
        sx = cursor->x - (l ? l->scene->node.x : w->geom.x);
        sy = cursor->y - (l ? l->scene->node.y : w->geom.y);
    }

    /* time is 0 in internal calls meant to restore pointer focus. */
    if (time) {
        wlr_relative_pointer_manager_v1_send_relative_motion(
            relative_pointer_mgr,
            seat,
            (uint64_t)time * 1000,
            dx,
            dy,
            dx_unaccel,
            dy_unaccel);

        wl_list_for_each(constraint, &pointer_constraints->constraints, link)
            cursorconstrain(constraint);

        if (active_constraint && cursor_mode != CurResize &&
            cursor_mode != CurMove) {
            toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
            if (c && active_constraint->surface ==
                         seat->pointer_state.focused_surface) {
                sx = cursor->x - c->geom.x - c->bw;
                sy = cursor->y - c->geom.y - c->bw;
                if (wlr_region_confine(&active_constraint->region,
                                       sx,
                                       sy,
                                       sx + dx,
                                       sy + dy,
                                       &sx_confined,
                                       &sy_confined)) {
                    dx = sx_confined - sx;
                    dy = sy_confined - sy;
                }

                if (active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
                    return;
            }
        }

        wlr_cursor_move(cursor, device, dx, dy);
        wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
        handlecursoractivity();

        /* Update selmon (even while dragging a window) */
        if (sloppyfocus) {
            Monitor* old = selmon;
            selmon = xytomon(cursor->x, cursor->y);
            /* the single bar follows the focus, and crossing into empty
             * space changes it without going through focusclient() */
            if (barsinglemon && selmon != old)
                drawbars();
        }
    }

    /* Update drag icon's position */
    wlr_scene_node_set_position(
        &drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));

    /* If we are currently grabbing the mouse, handle and return */
    if (cursor_mode == CurMove) {
        /* A tiled client keeps its slot in the layout: dragging it just swaps
         * it with the tile under the cursor, i3/sway style. */
        if (!grabc->isfloating) {
            if (c && c != grabc && !c->isfloating && c->mon == grabc->mon) {
                swapclients(grabc, c);
                arrange(grabc->mon);
            }
            return;
        }
        /* Move the grabbed client to the new position. */
        resize(grabc,
               (struct wlr_box){ .x = (int)round(cursor->x) - grabcx,
                                 .y = (int)round(cursor->y) - grabcy,
                                 .width = grabc->geom.width,
                                 .height = grabc->geom.height },
               1);
        return;
    } else if (cursor_mode == CurResize) {
        resize(
            grabc,
            (struct wlr_box){ .x = grabc->geom.x,
                              .y = grabc->geom.y,
                              .width = (int)round(cursor->x) - grabc->geom.x,
                              .height = (int)round(cursor->y) - grabc->geom.y },
            1);
        return;
    }

    /* If there's no client surface under the cursor, set the cursor image to a
     * default. This is what makes the cursor image appear when you move it
     * off of a client or over its border. */
    if (!surface && !seat->drag && !cursor_hidden)
        wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

    pointerfocus(c, surface, sx, sy, time);
}

void motionrelative(struct wl_listener* listener, void* data)
{
    /* This event is forwarded by the cursor when a pointer emits a _relative_
     * pointer motion event (i.e. a delta) */
    struct wlr_pointer_motion_event* event = data;
    /* The cursor doesn't move unless we tell it to. The cursor automatically
     * handles constraining the motion to the output layout, as well as any
     * special configuration applied for the specific input device which
     * generated the event. You can pass NULL for the device if you want to move
     * the cursor around without any input. */
    motionnotify(event->time_msec,
                 &event->pointer->base,
                 event->delta_x,
                 event->delta_y,
                 event->unaccel_dx,
                 event->unaccel_dy);
}

void moveresize(const Arg* arg)
{
    if (cursor_mode != CurNormal && cursor_mode != CurPressed)
        return;
    xytonode(cursor->x, cursor->y, NULL, &grabc, NULL, NULL, NULL);
    if (!grabc || client_is_unmanaged(grabc) || grabc->isfullscreen)
        return;

    /* Float the window and tell motionnotify to grab it - except when moving a
     * tiled client under a real layout, which stays tiled and is swapped with
     * the tile it is dragged onto instead. */
    if (arg->ui != CurMove || grabc->isfloating || !grabc->mon ||
        !grabc->mon->lt[grabc->mon->sellt]->arrange)
        setfloating(grabc, 1);
    switch (cursor_mode = arg->ui) {
        case CurMove:
            grabcx = (int)round(cursor->x) - grabc->geom.x;
            grabcy = (int)round(cursor->y) - grabc->geom.y;
            wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
            break;
        case CurResize:
            /* Doesn't work for X11 output - the next absolute motion event
             * returns the cursor to where it started */
            wlr_cursor_warp_closest(cursor,
                                    NULL,
                                    grabc->geom.x + grabc->geom.width,
                                    grabc->geom.y + grabc->geom.height);
            wlr_cursor_set_xcursor(cursor, cursor_mgr, "se-resize");
            break;
    }
}

static void pointerfocus(Client* c,
                         struct wlr_surface* surface,
                         double sx,
                         double sy,
                         uint32_t time)
{
    struct timespec now;

    /* Only a real client surface takes focus: hovering a title bar (a tab) or
     * a border has no surface, and must not select anything until clicked. */
    if (surface && surface != seat->pointer_state.focused_surface &&
        sloppyfocus && time && c && !client_is_unmanaged(c))
        focusclient(c, 0);

    /* If surface is NULL, clear pointer focus */
    if (!surface) {
        wlr_seat_pointer_notify_clear_focus(seat);
        return;
    }

    if (!time) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
    }

    /* Let the client know that the mouse cursor has entered one
     * of its surfaces, and make keyboard focus follow if desired.
     * wlroots makes this a no-op if surface is already focused */
    wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

void requeststartdrag(struct wl_listener* listener, void* data)
{
    struct wlr_seat_request_start_drag_event* event = data;

    if (wlr_seat_validate_pointer_grab_serial(
            seat, event->origin, event->serial))
        wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
    else
        wlr_data_source_destroy(event->drag->source);
}

static void unlastcursor(struct wl_listener* listener, void* data)
{
    /* When the surface is destroyed, clear our reference to it */
    last_cursor.surface = NULL;
    wl_list_remove(&last_cursor.destroy.link);
    wl_list_init(&last_cursor.destroy.link);
}

void setcursor(struct wl_listener* listener, void* data)
{
    /* This event is raised by the seat when a client provides a cursor image */
    struct wlr_seat_pointer_request_set_cursor_event* event = data;
    /* If we're "grabbing" the cursor, don't use the client's image, we will
     * restore it after "grabbing" sending a leave event, followed by a enter
     * event, which will result in the client requesting set the cursor surface
     */
    if (cursor_mode != CurNormal && cursor_mode != CurPressed)
        return;
    /* This can be sent by any client, so we check to make sure this one
     * actually has pointer focus first. If so, we can tell the cursor to
     * use the provided surface as the cursor image. It will set the
     * hardware cursor on the output that it's currently on and continue to
     * do so as the cursor moves between outputs. */
    if (event->seat_client == seat->pointer_state.focused_client) {
        last_cursor.shape = 0;
        last_cursor.surface = event->surface;
        last_cursor.has_client_cursor = true;
        last_cursor.hotspot_x = event->hotspot_x;
        last_cursor.hotspot_y = event->hotspot_y;

        wl_list_remove(&last_cursor.destroy.link);
        wl_list_init(&last_cursor.destroy.link);
        if (event->surface) {
            last_cursor.destroy.notify = unlastcursor;
            wl_signal_add(&event->surface->events.destroy,
                          &last_cursor.destroy);
        }

        if (!cursor_hidden)
            wlr_cursor_set_surface(
                cursor, event->surface, event->hotspot_x, event->hotspot_y);
    }
}

void setcursorshape(struct wl_listener* listener, void* data)
{
    struct wlr_cursor_shape_manager_v1_request_set_shape_event* event = data;
    if (cursor_mode != CurNormal && cursor_mode != CurPressed)
        return;
    /* This can be sent by any client, so we check to make sure this one
     * actually has pointer focus first. If so, we can tell the cursor to
     * use the provided cursor shape. */
    if (event->seat_client == seat->pointer_state.focused_client) {
        last_cursor.shape = event->shape;
        last_cursor.surface = NULL;

        if (!cursor_hidden)
            wlr_cursor_set_xcursor(
                cursor, cursor_mgr, wlr_cursor_shape_v1_name(event->shape));
    }
}

void setpsel(struct wl_listener* listener, void* data)
{
    /* This event is raised by the seat when a client wants to set the
     * selection, usually when the user copies something. wlroots allows
     * compositors to ignore such requests if they so choose, but in g0wm we
     * always honor them
     */
    struct wlr_seat_request_set_primary_selection_event* event = data;
    wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

void setsel(struct wl_listener* listener, void* data)
{
    /* This event is raised by the seat when a client wants to set the
     * selection, usually when the user copies something. wlroots allows
     * compositors to ignore such requests if they so choose, but in g0wm we
     * always honor them
     */
    struct wlr_seat_request_set_selection_event* event = data;
    wlr_seat_set_selection(seat, event->source, event->serial);
}

void startdrag(struct wl_listener* listener, void* data)
{
    struct wlr_drag* drag = data;
    if (!drag->icon)
        return;

    drag->icon->data = &wlr_scene_drag_icon_create(drag_icon, drag->icon)->node;
    LISTEN_STATIC(&drag->icon->events.destroy, destroydragicon);
}

void virtualkeyboard(struct wl_listener* listener, void* data)
{
    struct wlr_virtual_keyboard_v1* kb = data;
    /* virtual keyboards shouldn't share keyboard group */
    KeyboardGroup* group = createkeyboardgroup();
    /* Set the keymap to match the group keymap */
    wlr_keyboard_set_keymap(&kb->keyboard, group->wlr_group->keyboard.keymap);
    LISTEN(&kb->keyboard.base.events.destroy,
           &group->destroy,
           destroykeyboardgroup);

    /* Add the new keyboard to the group */
    wlr_keyboard_group_add_keyboard(group->wlr_group, &kb->keyboard);
}

void virtualpointer(struct wl_listener* listener, void* data)
{
    struct wlr_virtual_pointer_v1_new_pointer_event* event = data;
    struct wlr_input_device* device = &event->new_pointer->pointer.base;

    wlr_cursor_attach_input_device(cursor, device);
    if (event->suggested_output)
        wlr_cursor_map_input_to_output(cursor, device, event->suggested_output);
}

void warpcursor(const Client* c)
{
    if (cursor_mode != CurNormal)
        return;

    if (!c && selmon) {
        wlr_cursor_warp_closest(cursor,
                                NULL,
                                selmon->w.x + selmon->w.width / 2.0,
                                selmon->w.y + selmon->w.height / 2.0);
    } else if (c && (cursor->x < c->geom.x ||
                     cursor->x > c->geom.x + c->geom.width ||
                     cursor->y < c->geom.y ||
                     cursor->y > c->geom.y + c->geom.height)) {
        wlr_cursor_warp_closest(cursor,
                                NULL,
                                c->geom.x + c->geom.width / 2.0,
                                c->geom.y + c->geom.height / 2.0);
    }
}

static void xytonode(double x,
                     double y,
                     struct wlr_surface** psurface,
                     Client** pc,
                     LayerSurface** pl,
                     double* nx,
                     double* ny)
{
    struct wlr_scene_node *node, *pnode;
    struct wlr_surface* surface = NULL;
    struct wlr_scene_surface* scene_surface = NULL;
    Client* c = NULL;
    LayerSurface* l = NULL;
    int layer;

    for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
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
                    c = NULL;
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
            c = NULL;
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
