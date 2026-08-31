/*
 * See LICENSE file for copyright and license details.
 *
 * the bar, the status text, the window title bars and the tray
 */
#include "g0wn.h"

/* function declarations */
static Monitor* barmonitor(void);
static int barvisible(Monitor* m);
static int statusescape(const char* p, uint32_t* scm);
#ifdef SYSTRAY
static void traynotify(void* data);
#endif /* SYSTRAY */
#ifdef TITLEBAR
static void drawtitle(Client* c);
#endif /* TITLEBAR */
#ifdef NOTIFICATIONS
static void notifysync(void);
#endif /* NOTIFICATIONS */
static void stopstatus(void);

/* variables */
#ifdef NOTIFICATIONS
static unsigned int notifyshownid;
static size_t notifyoff;
#endif /* NOTIFICATIONS */

/* function implementations */
bool baracceptsinput(struct wlr_scene_buffer* buffer, double* sx, double* sy)
{
    return true;
}

/* The monitor the bar is drawn on: the first output matched by monrules[] with
 * barsinglemon, the focused one without it. NULL when no output is enabled. */
static Monitor* barmonitor(void)
{
    const MonitorRule* r;
    Monitor* m;

    if (!barsinglemon)
        return selmon;

    for (r = monrules; r < END(monrules); r++) {
        /* mons grows at its head, so backwards is the order the outputs
         * showed up in: under the catch-all row the oldest one wins */
        wl_list_for_each_reverse(m, &mons, link)
        {
            if (m->wlr_output->enabled &&
                (!r->name || strstr(m->wlr_output->name, r->name)))
                return m;
        }
    }
    return NULL;
}

/* Whether m carries a bar. barsinglemon leaves every other monitor without
 * one, and arrangelayers() reads that off the scene node to free the height. */
static int barvisible(Monitor* m)
{
    return m->wlr_output->enabled && showbar &&
           (!barsinglemon || m == barmonitor());
}

void drawbar(Monitor* m)
{
    int x, w, tw = 0, traywidth = 0;
    int boxs = m->drw->font->height / 9;
    int boxw = m->drw->font->height / 6 + 2;
    /* the little squares follow the text, which drwl centers */
    int boxy = (m->b.height - m->drw->font->height) / 2 + boxs;
    uint32_t i, occ = 0, urg = 0;
    Client* c;
    Buffer* buf;
    /* what the bar reports on: its own monitor, or the focused one when
     * barsinglemon leaves a single bar standing in for the lot */
    Monitor* s = barsinglemon && selmon ? selmon : m;
    int sel = s == selmon;

#ifdef TITLEBAR
    /* Title bars are refreshed on the same events as the bar */
    wl_list_for_each(c, &clients, link) if (c->mon == m) drawtitle(c);
#endif

    if (!m->scene_buffer->node.enabled) {
#ifdef INTEGRATED_BACKGROUND
        /* a hidden bar has no backdrop either, and togglebar() comes through
         * here rather than through the tail of this function */
        blurbar(m);
#endif
        return;
    }
    if (!(buf = bufget(m->pool, LENGTH(m->pool), m->b.width, m->b.height)))
        return;
    drwl_setimage(m->drw, buf->image);
#ifdef SYSTRAY
    traywidth = tray_get_width(m->tray);
#endif

    /* draw status first so it can be overdrawn by tags later */
    if (sel) { /* status is only drawn on selected monitor */
        tw = STATUSW(m);
        drawstatus(m, stext, m->b.width - (tw + traywidth), tw, 1);
    }

    wl_list_for_each(c, &clients, link)
    {
        if (c->mon != s)
            continue;
        occ |= c->tags;
        if (c->isurgent)
            urg |= c->tags;
    }
    x = 0;
    c = focustop(s);
    for (i = 0; i < LENGTH(tags); i++) {
        w = TEXTW(m, tags[i]);
        drwl_setscheme(
            m->drw,
            colors[s->tagset[s->seltags] & 1 << i ? SchemeSel : SchemeNorm]);
        drwl_text(
            m->drw, x, 0, w, m->b.height, m->lrpad / 2, tags[i], urg & 1 << i);
        if (occ & 1 << i)
            drwl_rect(m->drw,
                      x + w - boxs - boxw,
                      boxy,
                      boxw,
                      boxw,
                      sel && c && c->tags & 1 << i,
                      urg & 1 << i);
        x += w;
    }
    w = TEXTW(m, s->ltsymbol);
    drwl_setscheme(m->drw, colors[SchemeNorm]);
    x = drwl_text(m->drw, x, 0, w, m->b.height, m->lrpad / 2, s->ltsymbol, 0);

    /* Remember the free box for notifyclick(): the title and the notification
     * share it, so its geometry must be measured in exactly one place. */
    w = m->b.width - (tw + x + traywidth);
    m->b.titlew = w > m->b.height ? w : 0;

    if (m->b.titlew) {
#ifdef RUNNER
        /* The prompt takes the box over the same way a notification does,
         * and outranks one if both would want it at once. */
        if (runner_active && sel) {
            const char* sug = runnersuggest();
            /* the caret scales with the font, which is loaded at the output's
             * dpi, so it keeps its proportions on every monitor */
            int tx, cx, cw = m->drw->font->height / 10 + 1;
            char save;

            drwl_setscheme(m->drw, colors[SchemeRunner]);
            drwl_text(
                m->drw, x, 0, w, m->b.height, m->lrpad / 2, runner_buf, 0);
            tx = x + m->lrpad / 2 + drwl_font_getwidth(m->drw, runner_buf);

            /* the caret tracks the cursor, which need not be at the end of
             * the text; measure just the part before it */
            save = runner_buf[runner_cur];
            runner_buf[runner_cur] = '\0';
            cx = x + m->lrpad / 2 + drwl_font_getwidth(m->drw, runner_buf);
            runner_buf[runner_cur] = save;

            /* Drawn before the suggestion so it keeps the prompt's own color,
             * and unconditionally: with nothing typed yet it is the only thing
             * telling the box apart from an empty title area. */
            if (cx + cw <= x + w)
                drwl_rect(m->drw,
                          cx,
                          boxy,
                          cw,
                          m->drw->font->height - 2 * boxs,
                          1,
                          0);
            tx += cw;

            if (sug && (size_t)runner_len < strlen(sug) && tx < x + w) {
                drwl_setscheme(m->drw, colors[SchemeRunnerSuggest]);
                drwl_text(m->drw,
                          tx,
                          0,
                          x + w - tx,
                          m->b.height,
                          0,
                          sug + runner_len,
                          0);
            } else if (!sug && tx < x + w) {
                /* Not a completion of what's typed, so it can't reuse the
                 * "sug + runner_len" tail above: it's an unrelated answer,
                 * appended rather than spliced in. */
                double calcval;
                char calcbuf[48];
                if (runnercalc(&calcval)) {
                    snprintf(calcbuf, sizeof calcbuf, "= %.10g", calcval);
                    drwl_setscheme(m->drw, colors[SchemeRunnerSuggest]);
                    drwl_text(
                        m->drw, tx, 0, x + w - tx, m->b.height, 0, calcbuf, 0);
                }
            }
        } else
#endif
        {
#ifdef NOTIFICATIONS
            /* A notification takes the box over for as long as it lasts, so the
             * window title (if barwintitle is on) steps aside and comes back
             * once the notification expires or is dismissed. */
            const char* text =
                shownotifications && sel ? notify_gettext() : NULL;
            if (text) {
                notifysync();
                drwl_setscheme(m->drw, colors[SchemeNotify]);
                drwl_text(m->drw,
                          x,
                          0,
                          w,
                          m->b.height,
                          m->lrpad / 2,
                          text + (notifyoff < strlen(text) ? notifyoff : 0),
                          0);
            } else
#endif
                if (barwintitle && c) {
                drwl_setscheme(m->drw, colors[sel ? SchemeSel : SchemeNorm]);
                drwl_text(m->drw,
                          x,
                          0,
                          w,
                          m->b.height,
                          m->lrpad / 2,
                          client_get_title(c),
                          0);
                if (c && c->isfloating)
                    drwl_rect(m->drw, x + boxs, boxy, boxw, boxw, 0, 0);
            } else {
                drwl_setscheme(m->drw, colors[SchemeNorm]);
                drwl_rect(m->drw, x, 0, w, m->b.height, 1, 1);
            }
        }
    }

#ifdef SYSTRAY
    if (traywidth > 0)
        pixman_image_composite32(PIXMAN_OP_SRC,
                                 m->tray->image,
                                 NULL,
                                 m->drw->image,
                                 0,
                                 0,
                                 0,
                                 0,
                                 m->b.width - traywidth,
                                 0,
                                 traywidth,
                                 m->b.height);
#endif

    wlr_scene_buffer_set_opacity(m->scene_buffer, decoopacity());
    wlr_scene_buffer_set_dest_size(
        m->scene_buffer, m->b.real_width, m->b.real_height);
    wlr_scene_node_set_position(
        &m->scene_buffer->node,
        m->m.x,
        m->m.y + (topbar ? 0 : m->m.height - m->b.real_height));
    wlr_scene_buffer_set_buffer(m->scene_buffer, &buf->base);
    wlr_buffer_unlock(&buf->base);
#ifdef INTEGRATED_BACKGROUND
    blurbar(m);
#endif
}

void drawbars(void)
{
    Monitor* m = NULL;

    wl_list_for_each(m, &mons, link) drawbar(m);
}

/* Redraws the bar that shows what selmon is up to, which barsinglemon can
 * keep on another monitor entirely. */
void drawselbar(void)
{
    Monitor* m = barmonitor();

    if (m)
        drawbar(m);
}

/* One colour escape at p: ^c#RRGGBB^ and ^b#RRGGBB^ set the foreground and
 * the background of what follows (an eight digit form carries alpha too), ^d^
 * goes back to SchemeNorm. Returns the bytes it takes, and applies it to scm
 * when that is given; zero means p does not start one. */
static int statusescape(const char* p, uint32_t* scm)
{
    static const char hex[] = "0123456789abcdef";
    const char* d;
    uint32_t clr = 0;
    int i;

    if (p[0] != '^')
        return 0;
    if (p[1] == 'd' && p[2] == '^') {
        if (scm) {
            scm[ColFg] = colors[SchemeNorm][ColFg];
            scm[ColBg] = colors[SchemeNorm][ColBg];
        }
        return 3;
    }
    if ((p[1] != 'c' && p[1] != 'b') || p[2] != '#')
        return 0;

    /* | 0x20 lowercases a letter and leaves a digit alone */
    for (i = 0; i < 8 && p[3 + i] && (d = strchr(hex, p[3 + i] | 0x20)); i++)
        clr = clr << 4 | (uint32_t)(d - hex);
    if ((i != 6 && i != 8) || p[3 + i] != '^')
        return 0;
    if (i == 6)
        clr = clr << 8 | 0xff; /* the short form is opaque */

    if (scm)
        scm[p[1] == 'c' ? ColFg : ColBg] = clr;
    return i + 4;
}

/* Draws the status text one colour run at a time, and returns its width
 * without the escapes. With render off it only measures, which is what
 * places the box before there is anything to draw in it. */
int drawstatus(Monitor* m, const char* text, int x, int w, int render)
{
    uint32_t scm[3];
    char seg[sizeof(stext)];
    const char* p = text;
    int total = 0, sw, len, xend = x + w;
    size_t n;

    if (!m || !text)
        return 0;
    memcpy(scm, colors[SchemeNorm], sizeof(scm));

    /* the runs cover the glyphs only, so the padding around them would keep
     * whatever the buffer held */
    if (render) {
        drwl_setscheme(m->drw, colors[SchemeNorm]);
        drwl_rect(m->drw, x, 0, w, m->b.height, 1, 1);
    }

    while (*p) {
        for (n = 0; *p && n + 1 < sizeof(seg);) {
            if (*p == '^') {
                if (p[1] == '^') {
                    seg[n++] = *p;
                    p += 2;
                    continue;
                }
                if (statusescape(p, NULL))
                    break;
            }
            seg[n++] = *p++;
        }
        seg[n] = '\0';

        sw = (int)drwl_font_getwidth(m->drw, seg);
        total += sw;
        if (render && sw > 0 && x < xend) {
            if (x + sw > xend) /* drwl_text() ellipsizes what it cannot fit */
                sw = xend - x;
            drwl_setscheme(m->drw, scm);
            drwl_text(m->drw, x, 0, sw, m->b.height, 0, seg, 0);
            x += sw;
        }

        if ((len = statusescape(p, scm)))
            p += len;
    }

    return total;
}

#ifdef SYSTRAY
static void traynotify(void* data)
{
    drawbar((Monitor*)data);
}

void trayactivate(const Arg* arg)
{
    Monitor* m = barmonitor(); /* the tray belongs to the bar clicked */

    if (!m)
        return;
    tray_leftclicked(m->tray, arg->ui);
}

void traymenu(const Arg* arg)
{
    Monitor* m = barmonitor();

    if (!m)
        return;
    tray_rightclicked(m->tray, arg->ui, traymenucmd);
}

#endif /* SYSTRAY */
#ifdef TITLEBAR
/* Renders the client's own title bar. In the tabbed layout every client of the
 * group shares one row, so these end up drawn side by side as tabs. */
static void drawtitle(Client* c)
{
    Monitor* m = c->mon;
    Buffer* buf;
    int w;

    if (!c->title)
        return;
    settitle(c);
    if (!m || !titlebar || c->isfullscreen || c->titlew <= 0 ||
        !c->scene->node.enabled) {
        wlr_scene_node_set_enabled(&c->title->node, 0);
        return;
    }

    w = (int)((float)c->titlew * m->wlr_output->scale);
    if (w != c->titlebufw) {
        bufpooldrop(c->titlepool, LENGTH(c->titlepool));
        c->titlebufw = w;
    }
    if (!(buf = bufget(c->titlepool, LENGTH(c->titlepool), w, m->t.height)))
        return;

    drwl_setimage(m->drw, buf->image);
    drwl_setscheme(
        m->drw,
        colors[c == (m->lt[m->sellt]->arrange == tabbed && !c->isfloating
                         ? tabtop(m)
                         : focustop(m))
                   ? SchemeTitleSel
                   : SchemeTitle]);
    drwl_text(m->drw,
              0,
              0,
              (unsigned int)w,
              m->t.height,
              m->lrpad / 2,
              client_get_title(c),
              0);

    wlr_scene_node_set_enabled(&c->title->node, 1);
    wlr_scene_buffer_set_opacity(c->title, decoopacity());
    wlr_scene_buffer_set_buffer(c->title, &buf->base);
    wlr_buffer_unlock(&buf->base);
}

#endif /* TITLEBAR */
#ifdef NOTIFICATIONS
/* Scrolls the notification on by one screenful, wrapping to the start once
 * the tail has been shown. Bound to a click on the box the notification and
 * the window title share (ClkTitle). */
void notifyclick(const Arg* arg)
{
    /* the box is measured on the monitor the bar is drawn on */
    Monitor* m = barmonitor();
    const char* text;
    char buf[NOTIFY_TEXTMAX];
    int boxw;
    size_t len, off, cut, good, n;

    if (!shownotifications || !m || !m->b.titlew || !(text = notify_gettext()))
        return;
    notifysync();

    /* drwl_text() spends lrpad/2 of the box on the left padding: measuring
     * against the full width would scroll text past unread. */
    boxw = m->b.titlew - m->lrpad / 2;
    len = strlen(text);
    off = notifyoff < len ? notifyoff : 0;

    if (boxw <= 0 || (int)drwl_font_getwidth(m->drw, text + off) <= boxw) {
        notifyoff = 0;
        drawbars();
        return;
    }

    good = off;
    for (cut = off + 1; cut <= len; cut++) {
        if (cut < len && (text[cut] & 0xC0) == 0x80)
            continue; /* not a UTF-8 codepoint boundary yet */
        n = cut - off;
        if (n >= sizeof(buf))
            break;
        memcpy(buf, text + off, n);
        buf[n] = '\0';
        if ((int)drwl_font_getwidth(m->drw, buf) > boxw)
            break;
        good = cut;
    }
    if (good == off) {
        /* not even one codepoint fits: force progress anyway */
        good = off + 1;
        while (good < len && (text[good] & 0xC0) == 0x80)
            good++;
    }
    notifyoff = good < len ? good : 0;
    drawbars();
}

/* Puts the notification away early, giving the box back to the window title. */
void notifydismiss(const Arg* arg)
{
    if (shownotifications)
        notify_dismiss();
}

/* A notification that just arrived (or replaced another one) is shown from
 * its start, whatever the previous one had been scrolled to. */
static void notifysync(void)
{
    if (notifyshownid != notify_getid()) {
        notifyshownid = notify_getid();
        notifyoff = 0;
    }
}

#endif /* NOTIFICATIONS */
#ifdef TITLEBAR
/* Sizes and places the client's title bar. It spans the whole window, except
 * in the tabbed layout where each client of the group only gets its own slice
 * of the shared row - which is what turns the title bars into tabs. */
void settitle(Client* c)
{
    Monitor* m = c->mon;
    Client* w;
    int i = 0, n = 0, x = 0, tw;

    if (!c->title || !m)
        return;

    tw = c->geom.width - 2 * (int)c->bw;
    if (m->lt[m->sellt]->arrange == tabbed && !c->isfloating &&
        !c->isfullscreen) {
        wl_list_for_each(w, &clients, link)
        {
            if (!VISIBLEON(w, m) || w->isfloating || w->isfullscreen)
                continue;
            if (w == c)
                i = n;
            n++;
        }
        if (n) {
            x = tw * i / n;
            tw = tw * (i + 1) / n - x;
        }
    }

    c->titlex = x;
    c->titlew = tw;
    wlr_scene_node_set_position(&c->title->node, (int)c->bw + x, (int)c->bw);
    wlr_scene_buffer_set_dest_size(c->title, MAX(tw, 1), titleheight(c));
}

int titleheight(Client* c)
{
    return titlebar && c->mon && !c->isfullscreen ? c->mon->t.real_height : 0;
}

#endif /* TITLEBAR */
int statusin(int fd, unsigned int mask, void* data)
{
    char status[256];
    ssize_t n;

    if (mask & WL_EVENT_ERROR)
        die("status in event error");
    if (mask & WL_EVENT_HANGUP) {
        stopstatus();
        return 0;
    }

    n = read(fd, status, sizeof(status) - 1);
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
            return 0;
        die("read:");
    }
    /* EOF: the status process is gone. The fd stays readable forever, so
     * keeping the source alive would spin the event loop at full speed and
     * starve rendering and input. */
    if (n == 0) {
        stopstatus();
        return 0;
    }

    status[n] = '\0';
    status[strcspn(status, "\n")] = '\0';

    strncpy(stext, status, sizeof(stext));
    drawbars();

    return 0;
}

/* Detach the bar status text from stdin, for good. */
static void stopstatus(void)
{
    if (!status_event_source)
        return;

    wl_event_source_remove(status_event_source);
    status_event_source = NULL;
}

void togglebar(const Arg* arg)
{
    /* under barsinglemon this hides the one bar from any monitor */
    Monitor* m = barmonitor();

    if (!m)
        return;
    wlr_scene_node_set_enabled(&m->scene_buffer->node,
                               !m->scene_buffer->node.enabled);
    arrangelayers(m);
    drawbars();
}

#ifdef TITLEBAR
/* arrange() leaves floating clients alone, and no client redraws a title
 * bar, so both are done by hand here */
void toggletitlebar(const Arg* arg)
{
    Monitor* m;
    Client* c;

    titlebar = !titlebar;

    wl_list_for_each(m, &mons, link) arrange(m);
    wl_list_for_each(c, &clients, link)
    {
        if (c->mon && c->isfloating && !c->isfullscreen)
            resize(c, c->geom, 1);
    }
    drawbars();

    wl_list_for_each(m, &mons, link)
    {
        if (m->wlr_output->enabled)
            wlr_output_schedule_frame(m->wlr_output);
    }
}

#endif /* TITLEBAR */
void updatebar(Monitor* m)
{
    int rw, rh;
    char fontattrs[12];
#ifdef SYSTRAY
    int iconsize;
#endif

    wlr_output_transformed_resolution(m->wlr_output, &rw, &rh);
    m->b.width = rw;
    m->b.real_width = (int)((float)m->b.width / m->wlr_output->scale);

    wlr_scene_node_set_enabled(&m->scene_buffer->node, barvisible(m));

    bufpooldrop(m->pool, LENGTH(m->pool));

    if (m->b.scale == m->wlr_output->scale && m->drw)
        return;

    drwl_font_destroy(m->drw->font);
    snprintf(
        fontattrs, sizeof(fontattrs), "dpi=%.2f", 96. * m->wlr_output->scale);
    if (!(drwl_font_create(m->drw, LENGTH(fonts), fonts, fontattrs)))
        die("Could not load font");

    m->b.scale = m->wlr_output->scale;
    m->lrpad = m->drw->font->height;
    /* the automatic height follows the font, which is loaded at the output's
     * dpi, so barheight scales it the same on every monitor */
    m->b.height = m->drw->font->height + 2;
    if (barheight > 0)
        m->b.height = MAX((int)((float)m->b.height * barheight + 0.5f),
                          m->drw->font->height);
    m->b.real_height = (int)((float)m->b.height / m->wlr_output->scale);
#ifdef TITLEBAR
    m->t.height = m->drw->font->height + (int)titlepadding;
    m->t.real_height = (int)((float)m->t.height / m->wlr_output->scale);
#endif

#ifdef SYSTRAY
    if (showbar && showsystray && watcher.running) {
        if (m->tray)
            destroytray(m->tray);
        /* systrayiconsize is unscaled, like cursor_size; 0 fills the bar */
        iconsize = systrayiconsize
                       ? (int)((float)systrayiconsize * m->wlr_output->scale)
                       : m->b.height;
        m->tray = createtray(m,
                             m->b.height,
                             MIN(iconsize, m->b.height),
                             (int)systrayspacing,
                             colors[SchemeNorm],
                             fonts,
                             fontattrs,
                             &traynotify,
                             &watcher);
        if (!m->tray)
            die("Couldn't create tray for monitor");
        wl_list_insert(&watcher.trays, &m->tray->link);
    }
#endif /* SYSTRAY */
}
