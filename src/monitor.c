/*
 * See LICENSE file for copyright and license details.
 *
 * outputs, their layout and the layer-shell surfaces on them
 */
#include "g0wn.h"

/* function declarations */
static void arrangelayer(Monitor* m,
                         struct wl_list* list,
                         struct wlr_box* usable_area,
                         int exclusive);
static void cleanupmon(struct wl_listener* listener, void* data);
static void commitlayersurfacenotify(struct wl_listener* listener, void* data);
static void destroylayersurfacenotify(struct wl_listener* listener, void* data);
static Monitor* dirtomon(enum wlr_direction dir);
static void outputmgrapplyortest(struct wlr_output_configuration_v1* config,
                                 int test);
static void rendermon(struct wl_listener* listener, void* data);
static void requestmonstate(struct wl_listener* listener, void* data);
static void unmaplayersurfacenotify(struct wl_listener* listener, void* data);

/* variables */
/* Map from ZWLR_LAYER_SHELL_* constants to Lyr* enum */
static const int layermap[] = { LyrBg, LyrBottom, LyrTop, LyrOverlay };

/* function implementations */
static void arrangelayer(Monitor* m,
                         struct wl_list* list,
                         struct wlr_box* usable_area,
                         int exclusive)
{
    LayerSurface* l;
    struct wlr_box full_area = m->m;

    wl_list_for_each(l, list, link)
    {
        struct wlr_layer_surface_v1* layer_surface = l->layer_surface;

        if (!layer_surface->initialized)
            continue;

        if (exclusive != (layer_surface->current.exclusive_zone > 0))
            continue;

        wlr_scene_layer_surface_v1_configure(
            l->scene_layer, &full_area, usable_area);
        wlr_scene_node_set_position(
            &l->popups->node, l->scene->node.x, l->scene->node.y);
    }
}

void arrangelayers(Monitor* m)
{
    int i;
    struct wlr_box usable_area = m->m;
    LayerSurface* l;
    uint32_t layers_above_shell[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
    };
    if (!m->wlr_output->enabled)
        return;

    if (m->scene_buffer->node.enabled) {
        usable_area.height -= m->b.real_height;
        usable_area.y += topbar ? m->b.real_height : 0;
    }

    /* Arrange exclusive surfaces from top->bottom */
    for (i = 3; i >= 0; i--)
        arrangelayer(m, &m->layers[i], &usable_area, 1);

    if (!wlr_box_equal(&usable_area, &m->w)) {
        m->w = usable_area;
        arrange(m);
    }

    /* Arrange non-exclusive surfaces from top->bottom */
    for (i = 3; i >= 0; i--)
        arrangelayer(m, &m->layers[i], &usable_area, 0);

    /* Find topmost keyboard interactive layer, if such a layer exists */
    for (i = 0; i < (int)LENGTH(layers_above_shell); i++) {
        wl_list_for_each_reverse(l, &m->layers[layers_above_shell[i]], link)
        {
            if (locked || !l->layer_surface->current.keyboard_interactive ||
                !l->mapped)
                continue;
            /* Deactivate the focused client. */
            focusclient(NULL, 0);
            exclusive_focus = l;
            client_notify_enter(l->layer_surface->surface,
                                wlr_seat_get_keyboard(seat));
            return;
        }
    }
}

static void cleanupmon(struct wl_listener* listener, void* data)
{
    Monitor* m = wl_container_of(listener, m, destroy);
    LayerSurface *l, *tmp;
    size_t i;

    /* m->layers[i] are intentionally not unlinked */
    for (i = 0; i < LENGTH(m->layers); i++) {
        wl_list_for_each_safe(l, tmp, &m->layers[i], link)
            wlr_layer_surface_v1_destroy(l->layer_surface);
    }

    for (i = 0; i < LENGTH(m->pool); i++)
        wlr_buffer_drop(&m->pool[i]->base);
#ifdef INTEGRATED_BACKGROUND
    bufpooldrop(m->wallpaperpool, LENGTH(m->wallpaperpool));
    bufpooldrop(m->blurpool, LENGTH(m->blurpool));
#endif

#ifdef SYSTRAY
    if (m->tray)
        destroytray(m->tray);
#endif

    drwl_setimage(m->drw, NULL);
    drwl_destroy(m->drw);

    wl_list_remove(&m->destroy.link);
    wl_list_remove(&m->frame.link);
    wl_list_remove(&m->link);
    wl_list_remove(&m->request_state.link);
    if (m->lock_surface)
        destroylocksurface(&m->destroy_lock_surface, NULL);
    m->wlr_output->data = NULL;
    wlr_output_layout_remove(output_layout, m->wlr_output);
    wlr_scene_output_destroy(m->scene_output);

    closemon(m);
    wlr_scene_node_destroy(&m->fullscreen_bg->node);
    wlr_scene_node_destroy(&m->scene_buffer->node);
#ifdef INTEGRATED_BACKGROUND
    wlr_scene_node_destroy(&m->wallpaper->node);
    wlr_scene_node_destroy(&m->barblur->node);
#endif
    free(m);
}

void closemon(Monitor* m)
{
    /* update selmon if needed and
     * move closed monitor's clients to the focused one */
    Client* c;
    int i = 0, nmons = wl_list_length(&mons);
    if (!nmons) {
        selmon = NULL;
    } else if (m == selmon) {
        do /* don't switch to disabled mons */
            selmon = wl_container_of(mons.next, selmon, link);
        while (!selmon->wlr_output->enabled && i++ < nmons);

        if (!selmon->wlr_output->enabled)
            selmon = NULL;
    }

    wl_list_for_each(c, &clients, link)
    {
        if (c->isfloating && c->geom.x > m->m.width)
            resize(c,
                   (struct wlr_box){ .x = c->geom.x - m->w.width,
                                     .y = c->geom.y,
                                     .width = c->geom.width,
                                     .height = c->geom.height },
                   0);
        if (c->mon == m)
            setmon(c, selmon, c->tags);
    }
    focusclient(focustop(selmon), 1);
    drawbars();
}

static void commitlayersurfacenotify(struct wl_listener* listener, void* data)
{
    LayerSurface* l = wl_container_of(listener, l, surface_commit);
    struct wlr_layer_surface_v1* layer_surface = l->layer_surface;
    struct wlr_scene_tree* scene_layer =
        layers[layermap[layer_surface->current.layer]];
    struct wlr_layer_surface_v1_state old_state;

    if (l->layer_surface->initial_commit) {
        client_set_scale(layer_surface->surface, l->mon->wlr_output->scale);

        /* Temporarily set the layer's current state to pending
         * so that we can easily arrange it */
        old_state = l->layer_surface->current;
        l->layer_surface->current = l->layer_surface->pending;
        arrangelayers(l->mon);
        l->layer_surface->current = old_state;
        return;
    }

    if (layer_surface->current.committed == 0 &&
        l->mapped == layer_surface->surface->mapped)
        return;
    l->mapped = layer_surface->surface->mapped;

    if (scene_layer != l->scene->node.parent) {
        wlr_scene_node_reparent(&l->scene->node, scene_layer);
        wl_list_remove(&l->link);
        wl_list_insert(&l->mon->layers[layer_surface->current.layer], &l->link);
        wlr_scene_node_reparent(
            &l->popups->node,
            (layer_surface->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP
                 ? layers[LyrTop]
                 : scene_layer));
    }

    arrangelayers(l->mon);
}

void createlayersurface(struct wl_listener* listener, void* data)
{
    struct wlr_layer_surface_v1* layer_surface = data;
    LayerSurface* l;
    struct wlr_surface* surface = layer_surface->surface;
    struct wlr_scene_tree* scene_layer =
        layers[layermap[layer_surface->pending.layer]];

    if (!layer_surface->output &&
        !(layer_surface->output = selmon ? selmon->wlr_output : NULL)) {
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }

    l = layer_surface->data = ecalloc(1, sizeof(*l));
    l->type = LayerShell;
    LISTEN(
        &surface->events.commit, &l->surface_commit, commitlayersurfacenotify);
    LISTEN(&surface->events.unmap, &l->unmap, unmaplayersurfacenotify);
    LISTEN(
        &layer_surface->events.destroy, &l->destroy, destroylayersurfacenotify);

    l->layer_surface = layer_surface;
    l->mon = layer_surface->output->data;
    l->scene_layer =
        wlr_scene_layer_surface_v1_create(scene_layer, layer_surface);
    l->scene = l->scene_layer->tree;
    l->popups = surface->data = wlr_scene_tree_create(
        layer_surface->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP
            ? layers[LyrTop]
            : scene_layer);
    l->scene->node.data = l->popups->node.data = l;

    wl_list_insert(&l->mon->layers[layer_surface->pending.layer], &l->link);
    wlr_surface_send_enter(surface, layer_surface->output);
}

void createmon(struct wl_listener* listener, void* data)
{
    /* This event is raised by the backend when a new output (aka a display or
     * monitor) becomes available. */
    struct wlr_output* wlr_output = data;
    const MonitorRule* r;
    size_t i;
    struct wlr_output_state state;
    struct wlr_output_mode* mode = NULL;
    Monitor* m;

    if (!wlr_output_init_render(wlr_output, alloc, drw))
        return;

    m = wlr_output->data = ecalloc(1, sizeof(*m));
    m->wlr_output = wlr_output;

    for (i = 0; i < LENGTH(m->layers); i++)
        wl_list_init(&m->layers[i]);

    wlr_output_state_init(&state);
    /* Initialize monitor state using configured rules */
    m->gaps = gaps;

    m->tagset[0] = m->tagset[1] = 1;
    for (r = monrules; r < END(monrules); r++) {
        if (!r->name || strstr(wlr_output->name, r->name)) {
            m->m.x = r->x;
            m->m.y = r->y;
            m->mfact = r->mfact;
            m->nmaster = r->nmaster;
            m->lt[0] = r->lt;
            m->lt[1] = &layouts[LENGTH(layouts) > 1 && r->lt != &layouts[1]];
            snprintf(m->ltsymbol,
                     LENGTH(m->ltsymbol),
                     "%s",
                     m->lt[m->sellt]->symbol);
            for (i = 0; i < LENGTH(m->taglt); i++) {
                m->taglt[i][0] = m->lt[0];
                m->taglt[i][1] = m->lt[1];
            }
            wlr_output_state_set_scale(&state, r->scale);
            wlr_output_state_set_transform(&state, r->rr);
            /* Load (or reuse, if already cached) the xcursor theme at this
             * monitor's scale so the cursor renders crisp and sized
             * relative to cursor_size * scale, not just cursor_size. */
            wlr_xcursor_manager_load(cursor_mgr, r->scale);
            if (r->width && r->height) {
                struct wlr_output_mode* m2;
                int32_t want = r->refresh * 1000, best_diff = INT32_MAX;
                wl_list_for_each(m2, &wlr_output->modes, link)
                {
                    int32_t diff = abs(m2->refresh - want);
                    if (m2->width == r->width && m2->height == r->height &&
                        diff < best_diff) {
                        mode = m2;
                        best_diff = diff;
                    }
                }
                if (!mode)
                    wlr_log(WLR_ERROR,
                            "no %dx%d mode found on %s, using preferred mode",
                            r->width,
                            r->height,
                            wlr_output->name);
            }
            break;
        }
    }

    /* The mode is a tuple of (width, height, refresh rate), and each
     * monitor supports only a specific set of modes. Use the mode matched
     * against the rule above, falling back to the preferred mode when the
     * rule didn't request one (or it wasn't found). */
    wlr_output_state_set_mode(
        &state, mode ? mode : wlr_output_preferred_mode(wlr_output));

    /* Set up event listeners */
    LISTEN(&wlr_output->events.frame, &m->frame, rendermon);
    LISTEN(&wlr_output->events.destroy, &m->destroy, cleanupmon);
    LISTEN(
        &wlr_output->events.request_state, &m->request_state, requestmonstate);

    wlr_output_state_set_enabled(&state, 1);
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    if (!(m->drw = drwl_create()))
        die("failed to create drwl context");

#ifdef INTEGRATED_BACKGROUND
    /* created before the bar so that it stays underneath it */
    m->barblur = wlr_scene_buffer_create(layers[LyrBottom], NULL);
    wlr_scene_node_set_enabled(&m->barblur->node, 0);
#endif
    m->scene_buffer = wlr_scene_buffer_create(layers[LyrBottom], NULL);
    m->scene_buffer->point_accepts_input = baracceptsinput;
#ifdef INTEGRATED_BACKGROUND
    m->wallpaper = wlr_scene_buffer_create(layers[LyrBg], NULL);
#endif
    updatebar(m);

    wl_list_insert(&mons, &m->link);
    drawbars();

    /* The xdg-protocol specifies:
     *
     * If the fullscreened surface is not opaque, the compositor must make
     * sure that other screen content not part of the same surface tree (made
     * up of subsurfaces, popups or similarly coupled surfaces) are not
     * visible below the fullscreened surface.
     *
     */
    /* updatemons() will resize and set correct position */
    m->fullscreen_bg =
        wlr_scene_rect_create(layers[LyrFS], 0, 0, fullscreen_bg);
    wlr_scene_node_set_enabled(&m->fullscreen_bg->node, 0);

    /* Adds this to the output layout in the order it was configured.
     *
     * The output layout utility automatically adds a wl_output global to the
     * display, which Wayland clients can see to find out information about the
     * output (such as DPI, scale factor, manufacturer, etc).
     */
    m->scene_output = wlr_scene_output_create(scene, wlr_output);
    if (m->m.x == -1 && m->m.y == -1)
        wlr_output_layout_add_auto(output_layout, wlr_output);
    else
        wlr_output_layout_add(output_layout, wlr_output, m->m.x, m->m.y);
}

static void destroylayersurfacenotify(struct wl_listener* listener, void* data)
{
    LayerSurface* l = wl_container_of(listener, l, destroy);

    wl_list_remove(&l->link);
    wl_list_remove(&l->destroy.link);
    wl_list_remove(&l->unmap.link);
    wl_list_remove(&l->surface_commit.link);
    /* Same as in unmapnotify(): the wl_surface may outlive the layer surface,
     * don't leave it pointing at the freed popup tree. */
    if (l->layer_surface->surface)
        l->layer_surface->surface->data = NULL;
    wlr_scene_node_destroy(&l->scene->node);
    wlr_scene_node_destroy(&l->popups->node);
    free(l);
}

static Monitor* dirtomon(enum wlr_direction dir)
{
    struct wlr_output* next;
    if (!wlr_output_layout_get(output_layout, selmon->wlr_output))
        return selmon;
    if ((next = wlr_output_layout_adjacent_output(
             output_layout, dir, selmon->wlr_output, selmon->m.x, selmon->m.y)))
        return next->data;
    if ((next = wlr_output_layout_farthest_output(
             output_layout,
             dir ^ (WLR_DIRECTION_LEFT | WLR_DIRECTION_RIGHT),
             selmon->wlr_output,
             selmon->m.x,
             selmon->m.y)))
        return next->data;
    return selmon;
}

void focusmon(const Arg* arg)
{
    int i = 0, nmons = wl_list_length(&mons);
    if (nmons) {
        do /* don't switch to disabled mons */
            selmon = dirtomon(arg->i);
        while (!selmon->wlr_output->enabled && i++ < nmons);
    }
    focusclient(focustop(selmon), 1);
}

void outputmgrapply(struct wl_listener* listener, void* data)
{
    struct wlr_output_configuration_v1* config = data;
    outputmgrapplyortest(config, 0);
}

static void outputmgrapplyortest(struct wlr_output_configuration_v1* config,
                                 int test)
{
    /*
     * Called when a client such as wlr-randr requests a change in output
     * configuration. This is only one way that the layout can be changed,
     * so any Monitor information should be updated by updatemons() after an
     * output_layout.change event, not here.
     */
    struct wlr_output_configuration_head_v1* config_head;
    int ok = 1;

    wl_list_for_each(config_head, &config->heads, link)
    {
        struct wlr_output* wlr_output = config_head->state.output;
        Monitor* m = wlr_output->data;
        struct wlr_output_state state;

        /* Ensure displays previously disabled by wlr-output-power-management-v1
         * are properly handled*/
        m->asleep = 0;

        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, config_head->state.enabled);
        if (!config_head->state.enabled)
            goto apply_or_test;

        if (config_head->state.mode)
            wlr_output_state_set_mode(&state, config_head->state.mode);
        else
            wlr_output_state_set_custom_mode(
                &state,
                config_head->state.custom_mode.width,
                config_head->state.custom_mode.height,
                config_head->state.custom_mode.refresh);

        wlr_output_state_set_transform(&state, config_head->state.transform);
        wlr_output_state_set_scale(&state, config_head->state.scale);
        wlr_output_state_set_adaptive_sync_enabled(
            &state, config_head->state.adaptive_sync_enabled);

    apply_or_test:
        ok &= test ? wlr_output_test_state(wlr_output, &state)
                   : wlr_output_commit_state(wlr_output, &state);

        /* Don't move monitors if position wouldn't change. This avoids
         * wlroots marking the output as manually configured.
         * wlr_output_layout_add does not like disabled outputs */
        if (!test && wlr_output->enabled &&
            (m->m.x != config_head->state.x || m->m.y != config_head->state.y))
            wlr_output_layout_add(output_layout,
                                  wlr_output,
                                  config_head->state.x,
                                  config_head->state.y);

        wlr_output_state_finish(&state);
    }

    if (ok)
        wlr_output_configuration_v1_send_succeeded(config);
    else
        wlr_output_configuration_v1_send_failed(config);
    wlr_output_configuration_v1_destroy(config);

    /* https://codeberg.org/dwl/dwl/issues/577 */
    updatemons(NULL, NULL);
}

void outputmgrtest(struct wl_listener* listener, void* data)
{
    struct wlr_output_configuration_v1* config = data;
    outputmgrapplyortest(config, 1);
}

void powermgrsetmode(struct wl_listener* listener, void* data)
{
    struct wlr_output_power_v1_set_mode_event* event = data;
    struct wlr_output_state state = { 0 };
    Monitor* m = event->output->data;

    if (!m)
        return;

    m->gamma_lut_changed =
        1; /* Reapply gamma LUT when re-enabling the output */
    wlr_output_state_set_enabled(&state, event->mode);
    if (!wlr_output_commit_state(m->wlr_output, &state))
        fprintf(stderr,
                "g0wn: failed to %s output %s\n",
                event->mode ? "enable" : "disable",
                m->wlr_output->name);

    /* Track what the output actually is rather than what we asked for: a
     * refused commit would otherwise leave us believing a live output is
     * asleep, and nothing would ever schedule a frame for it again. */
    m->asleep = !m->wlr_output->enabled;
    if (m->wlr_output->enabled)
        wlr_output_schedule_frame(m->wlr_output);
    updatemons(NULL, NULL);
}

static void rendermon(struct wl_listener* listener, void* data)
{
    /* This function is called every time an output is ready to display a frame,
     * generally at the output's refresh rate (e.g. 60Hz). */
    Monitor* m = wl_container_of(listener, m, frame);
    Client* c;
    struct wlr_output_state pending = { 0 };
    struct timespec now;

    /* Render if no XDG clients have an outstanding resize and are visible on
     * this monitor. */
    wl_list_for_each(c, &clients, link)
    {
        if (!client_is_rendered_on_mon(c, m))
            continue;
        /* done here rather than on focus changes so that buffers a client
         * adds later (subsurfaces, videos) are covered too */
        wlr_scene_node_for_each_buffer(
            &c->scene_surface->node, scenebuffersetopacity, c);
#ifdef INTEGRATED_BACKGROUND
        blurclient(c);
#endif
        /* a tab below the top one is not drawn and gets no frame callbacks,
         * so it never acks */
        if (c->resize && !c->isfloating && c->scene_surface->node.enabled &&
            !client_is_stopped(c))
            goto skip;
    }

    wlr_scene_output_commit(m->scene_output, NULL);

skip:
    /* Let clients know a frame has been rendered */
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(m->scene_output, &now);
    wlr_output_state_finish(&pending);
}

static void requestmonstate(struct wl_listener* listener, void* data)
{
    struct wlr_output_event_request_state* event = data;
    wlr_output_commit_state(event->output, event->state);
    updatemons(NULL, NULL);
}

void tagmon(const Arg* arg)
{
    Client* sel = focustop(selmon);
    if (sel)
        setmon(sel, dirtomon(arg->i), 0);
}

static void unmaplayersurfacenotify(struct wl_listener* listener, void* data)
{
    LayerSurface* l = wl_container_of(listener, l, unmap);

    l->mapped = 0;
    wlr_scene_node_set_enabled(&l->scene->node, 0);
    if (l == exclusive_focus)
        exclusive_focus = NULL;
    if (l->layer_surface->output && (l->mon = l->layer_surface->output->data))
        arrangelayers(l->mon);
    if (l->layer_surface->surface == seat->keyboard_state.focused_surface)
        focusclient(focustop(selmon), 1);
    motionnotify(0, NULL, 0, 0, 0, 0);
}

void updatemons(struct wl_listener* listener, void* data)
{
    /*
     * Called whenever the output layout changes: adding or removing a
     * monitor, changing an output's mode or position, etc. This is where
     * the change officially happens and we update geometry, window
     * positions, focus, and the stored configuration in wlroots'
     * output-manager implementation.
     */
    struct wlr_output_configuration_v1* config =
        wlr_output_configuration_v1_create();
    Client* c;
    struct wlr_output_configuration_head_v1* config_head;
    Monitor* m;

    /* First remove from the layout the disabled monitors */
    wl_list_for_each(m, &mons, link)
    {
        if (m->wlr_output->enabled || m->asleep)
            continue;
        config_head =
            wlr_output_configuration_head_v1_create(config, m->wlr_output);
        config_head->state.enabled = 0;
        /* Remove this output from the layout to avoid cursor enter inside it */
        wlr_output_layout_remove(output_layout, m->wlr_output);
        closemon(m);
        m->m = m->w = (struct wlr_box){ 0 };
    }
    /* Insert outputs that need to */
    wl_list_for_each(m, &mons, link)
    {
        if (m->wlr_output->enabled &&
            !wlr_output_layout_get(output_layout, m->wlr_output))
            wlr_output_layout_add_auto(output_layout, m->wlr_output);
    }

    /* Now that we update the output layout we can get its box */
    wlr_output_layout_get_box(output_layout, NULL, &sgeom);

    wlr_scene_node_set_position(&root_bg->node, sgeom.x, sgeom.y);
    wlr_scene_rect_set_size(root_bg, sgeom.width, sgeom.height);

    /* Make sure the clients are hidden when g0wn is locked */
    wlr_scene_node_set_position(&locked_bg->node, sgeom.x, sgeom.y);
    wlr_scene_rect_set_size(locked_bg, sgeom.width, sgeom.height);

    wl_list_for_each(m, &mons, link)
    {
        if (!m->wlr_output->enabled)
            continue;
        config_head =
            wlr_output_configuration_head_v1_create(config, m->wlr_output);

        /* Get the effective monitor geometry to use for surfaces */
        wlr_output_layout_get_box(output_layout, m->wlr_output, &m->m);
        m->w = m->m;
        wlr_scene_output_set_position(m->scene_output, m->m.x, m->m.y);
#ifdef INTEGRATED_BACKGROUND
        setwallpaper(m);
#endif

        wlr_scene_node_set_position(&m->fullscreen_bg->node, m->m.x, m->m.y);
        wlr_scene_rect_set_size(m->fullscreen_bg, m->m.width, m->m.height);

        if (m->lock_surface) {
            struct wlr_scene_tree* scene_tree = m->lock_surface->surface->data;
            wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
            wlr_session_lock_surface_v1_configure(
                m->lock_surface, m->m.width, m->m.height);
        }

        /* Calculate the effective monitor geometry to use for clients */
        arrangelayers(m);
        /* Don't move clients to the left output when plugging monitors */
        arrange(m);
        /* make sure fullscreen clients have the right size */
        if ((c = focustop(m)) && c->isfullscreen)
            resize(c, m->m, 0);

        /* Try to re-set the gamma LUT when updating monitors,
         * it's only really needed when enabling a disabled output, but meh. */
        m->gamma_lut_changed = 1;

        config_head->state.x = m->m.x;
        config_head->state.y = m->m.y;

        if (!selmon) {
            selmon = m;
        }
    }

    if (selmon && selmon->wlr_output->enabled) {
        wl_list_for_each(c, &clients, link)
        {
            if (!c->mon && client_surface(c)->mapped)
                setmon(c, selmon, c->tags);
        }
        focusclient(focustop(selmon), 1);
        if (selmon->lock_surface) {
            client_notify_enter(selmon->lock_surface->surface,
                                wlr_seat_get_keyboard(seat));
            client_activate_surface(selmon->lock_surface->surface, 1);
        }
    }

    if (stext[0] == '\0')
        strncpy(stext, "g0wn-" VERSION, sizeof(stext));
    wl_list_for_each(m, &mons, link)
    {
        updatebar(m);
        /* this very change can hand a monitor the single bar, and with it
         * the strip of height its clients are kept out of */
        arrangelayers(m);
        drawbar(m);
    }

    /* FIXME: figure out why the cursor image is at 0,0 after turning all
     * the monitors on.
     * Move the cursor image where it used to be. It does not generate a
     * wl_pointer.motion event for the clients, it's only the image what it's
     * at the wrong position after all. */
    wlr_cursor_move(cursor, NULL, 0, 0);

    wlr_output_manager_v1_set_configuration(output_mgr, config);
}

Monitor* xytomon(double x, double y)
{
    struct wlr_output* o = wlr_output_layout_output_at(output_layout, x, y);
    return o ? o->data : NULL;
}
