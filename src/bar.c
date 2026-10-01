/*
 * See LICENSE file for copyright and license details.
 *
 * the bar, the status text, the window title bars and the tray
 */
#include "g0wm.h"

#include <strings.h>

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
typedef struct {
    int textx, textw;   /* the › included */
    int countx, countw; /* queue or history position */
    char count[32];
} NotifyBox;

static void drawnotify(Monitor* m,
                       int x,
                       int w,
                       const Notification* n,
                       long hist);
static void drawnotifytext(Monitor* m,
                           const NotifyBox* b,
                           int x,
                           int w,
                           const char* text,
                           uint32_t* scm);
static size_t fitprefix(Monitor* m, const char* s, int w);
static void notifybox(Monitor* m,
                      const Notification* n,
                      long hist,
                      int x,
                      int w,
                      NotifyBox* b);
static void notifybrowse(dbus_uint32_t id);
static const Notification* notifyshown(long* hist);
static void notifysync(const Notification* n, long hist);
static int notifytick(void* data);
#endif /* NOTIFICATIONS */
static void stopstatus(void);

/* variables */
#ifdef NOTIFICATIONS
static const char notifymore[] = "\xe2\x80\xba"; /* › */
static dbus_uint32_t notifyshownid;              /* what notifyoff refers to */
static int notifyshownhist;
static size_t notifyoff;
/* history entry being viewed, 0 for live; by id since indices shift */
static dbus_uint32_t notifyviewid;
static uint64_t notifyview_ms; /* last input, the view times out */
static dbus_uint32_t notifyliveid;
/* times out the history view */
static struct wl_event_source* notifytimer;
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

    for (r = monrules; r < monrules + nmonrules; r++) {
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
    int r = (int)roundf((float)barcorner(m) * m->wlr_output->scale);

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
    cornerclear(buf->data, m->b.width, m->b.height, r);
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
    for (i = 0; i < ntags; i++) {
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

    /* Remember the free box for notifyscroll(): the title and the notification
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
            long hist = -1;
            const Notification* n =
                shownotifications && sel ? notifyshown(&hist) : NULL;
            if (n) {
                drawnotify(m, x, w, n, hist);
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

    cornercut(buf->data, m->b.width, m->b.height, r);

    wlr_scene_buffer_set_opacity(m->scene_buffer, decoopacity());
    wlr_scene_buffer_set_dest_size(
        m->scene_buffer, m->b.real_width, m->b.real_height);
    wlr_scene_node_set_position(
        &m->scene_buffer->node,
        m->m.x + (int)barpadding,
        m->m.y + (topbar ? (int)barpadding
                         : m->m.height - m->b.real_height - (int)barpadding));
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
            scm[ColFg] = colors[SchemeStatus][ColFg];
            scm[ColBg] = colors[SchemeStatus][ColBg];
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
    memcpy(scm, colors[SchemeStatus], sizeof(scm));

    /* the runs cover the glyphs only, so the padding around them would keep
     * whatever the buffer held */
    if (render) {
        drwl_setscheme(m->drw, colors[SchemeStatus]);
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
    tray_rightclicked(m->tray, arg->ui, traypopup_present);
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
/* Background and text. */
static void drawnotifytext(Monitor* m,
                           const NotifyBox* b,
                           int x,
                           int w,
                           const char* text,
                           uint32_t* scm)
{
    char buf[NOTIFY_TEXTMAX];
    size_t k;
    int aw;

    drwl_setscheme(m->drw, scm);
    drwl_rect(m->drw, x, 0, (unsigned int)w, m->b.height, 1, 1);
    if (b->textw > 0 && (int)drwl_font_getwidth(m->drw, text) <= b->textw) {
        drwl_text(m->drw, b->textx, 0, b->textw, m->b.height, 0, text, 0);
    } else if (b->textw > 0) {
        /* our own › instead of drwl's …, which covers the last codepoint */
        aw = (int)drwl_font_getwidth(m->drw, notifymore);
        k = fitprefix(m, text, b->textw - aw);
        memcpy(buf, text, k);
        buf[k] = '\0';
        if (k)
            drwl_text(
                m->drw, b->textx, 0, b->textw - aw, m->b.height, 0, buf, 0);
        if (aw <= b->textw)
            drwl_text(m->drw,
                      b->textx + b->textw - aw,
                      0,
                      aw,
                      m->b.height,
                      0,
                      notifymore,
                      0);
    }
    if (b->countw)
        drwl_text(m->drw, b->countx, 0, b->countw, m->b.height, 0, b->count, 0);
}

/* hist is its history index, -1 for the live one */
static void drawnotify(Monitor* m,
                       int x,
                       int w,
                       const Notification* n,
                       long hist)
{
    uint64_t now = notify_now(), end, left;
    NotifyBox b;

    notifysync(n, hist);
    notifybox(m, n, hist, x, w, &b);
    drawnotifytext(m,
                   &b,
                   x,
                   w,
                   n->text + (notifyoff < strlen(n->text) ? notifyoff : 0),
                   colors[SchemeNotify]);

    if (!notifyviewid)
        return;
    end = notifyview_ms + notification_timeout * 1000;
    left = end > now ? end - now : 1;
    if (!notifytimer)
        notifytimer = wl_event_loop_add_timer(event_loop, notifytick, NULL);
    if (notifytimer)
        wl_event_source_timer_update(notifytimer, (int)left);
}

/* bytes of s, cut on a codepoint, that fit in w */
static size_t fitprefix(Monitor* m, const char* s, int w)
{
    size_t cut[NOTIFY_TEXTMAX], ncut = 0, len = strlen(s), i, lo, hi, mid;
    char buf[NOTIFY_TEXTMAX];

    if (len >= sizeof(buf) || w <= 0)
        return 0;
    for (i = 1; i <= len; i++)
        if (i == len || (s[i] & 0xC0) != 0x80)
            cut[ncut++] = i;
    /* width grows with length: binary search */
    for (lo = 0, hi = ncut; lo < hi;) {
        mid = (lo + hi + 1) / 2;
        memcpy(buf, s, cut[mid - 1]);
        buf[cut[mid - 1]] = '\0';
        if ((int)drwl_font_getwidth(m->drw, buf) <= w)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo ? cut[lo - 1] : 0;
}

/* Shared by drawing, scrolling and hit testing so they always agree. */
static void notifybox(Monitor* m,
                      const Notification* n,
                      long hist,
                      int x,
                      int w,
                      NotifyBox* b)
{
    int pad = m->lrpad / 2, cx, end;

    memset(b, 0, sizeof(*b));
    cx = x + pad;

    if (hist >= 0)
        snprintf(
            b->count, sizeof(b->count), "%ld/%zu", hist + 1, notify_histlen());
    else if (notify_queued())
        snprintf(b->count, sizeof(b->count), "+%zu", notify_queued());
    end = x + w - pad;
    if (*b->count) {
        b->countw = (int)drwl_font_getwidth(m->drw, b->count);
        b->countx = end - b->countw;
        end = b->countx - pad;
    }
    b->textx = cx;
    b->textw = end > cx ? end - cx : 0;
}

/* id 0 goes back to the live notification */
static void notifybrowse(dbus_uint32_t id)
{
    notifyviewid = id;
    notifyview_ms = notify_now();
    drawbars();
}

/* The history entry being viewed (its index in *hist) or the live one
 * (*hist -1). Also ends the view on timeout or when a new one comes in. */
static const Notification* notifyshown(long* hist)
{
    const Notification *cur = notify_current(), *n;
    size_t i;

    if (!cur) {
        notifyliveid = 0;
    } else if (cur->id != notifyliveid) {
        notifyliveid = cur->id;
        notifyviewid = 0;
    }
    if (notifyviewid &&
        notify_now() >= notifyview_ms + notification_timeout * 1000)
        notifyviewid = 0;

    for (i = 0; notifyviewid && (n = notify_history(i)); i++)
        if (n->id == notifyviewid) {
            *hist = (long)i;
            return n;
        }
    notifyviewid = 0;
    *hist = -1;
    return cur;
}

/* Reset the scroll offset for another notification, or when this one got
 * shorter than where we'd scrolled to. */
static void notifysync(const Notification* n, long hist)
{
    if (!n || notifyshownid != n->id || notifyshownhist != (hist >= 0) ||
        notifyoff >= strlen(n->text)) {
        notifyshownid = n ? n->id : 0;
        notifyshownhist = hist >= 0;
        notifyoff = 0;
    }
}

static int notifytick(void* data)
{
    drawselbar();
    return 0;
}

void notifyfini(void)
{
    if (notifytimer)
        wl_event_source_remove(notifytimer);
    notifytimer = NULL;
}

/* Scrolls the notification on by one screenful, wrapping to the start once
 * the tail has been shown. */
void notifyscroll(const Arg* arg)
{
    /* the box is measured on the monitor the bar is drawn on */
    Monitor* m = barmonitor();
    const Notification* n;
    const char* text;
    NotifyBox b;
    long hist;
    size_t len, off, good;

    if (!shownotifications || !m || !m->b.titlew || !(n = notifyshown(&hist)))
        return;
    notifysync(n, hist);
    if (hist >= 0)
        notifyview_ms = notify_now();
    notifybox(m, n, hist, 0, m->b.titlew, &b);

    text = n->text;
    len = strlen(text);
    off = notifyoff < len ? notifyoff : 0;
    if (b.textw <= 0 ||
        (int)drwl_font_getwidth(m->drw, text + off) <= b.textw) {
        notifyoff = 0;
        drawbars();
        return;
    }

    /* on from what was shown, which left room for the › */
    good =
        off + fitprefix(m,
                        text + off,
                        b.textw - (int)drwl_font_getwidth(m->drw, notifymore));
    if (good == off) {
        /* not even one codepoint fits: force progress anyway */
        good = off + 1;
        while (good < len && (text[good] & 0xC0) == 0x80)
            good++;
    }
    notifyoff = good < len ? good : 0;
    drawbars();
}

void notifyprev(const Arg* arg)
{
    const Notification* h;
    long hist;

    if (!shownotifications)
        return;
    notifyshown(&hist);
    if ((h = notify_history((size_t)(hist + 1))))
        notifybrowse(h->id);
    else if (hist >= 0) /* the oldest: just refresh the timeout */
        notifybrowse(notifyviewid);
}

/* One step forward; on the live one, dismisses it for the next queued. */
void notifynext(const Arg* arg)
{
    long hist;

    if (!shownotifications)
        return;
    notifyshown(&hist);
    if (hist > 0)
        notifybrowse(notify_history((size_t)hist - 1)->id);
    else if (hist == 0)
        notifybrowse(0);
    else
        notify_dismiss();
}

/* Puts the notification away early, giving the box back to the window title.
 * On a history entry, forgets it. */
void notifydismiss(const Arg* arg)
{
    const Notification* h;
    long hist;

    if (!shownotifications)
        return;
    notifyshown(&hist);
    if (hist < 0) {
        notify_dismiss();
        return;
    }
    notify_histremove((size_t)hist);
    if ((h = notify_history((size_t)hist)) ||
        (h = hist ? notify_history((size_t)hist - 1) : NULL))
        notifybrowse(h->id);
    else
        notifybrowse(0);
}

/* The live and queued ones go to the history. */
void notifydismissall(const Arg* arg)
{
    int browsing = notifyviewid != 0;

    if (!shownotifications)
        return;
    notifyviewid = 0;
    notify_dismissall();
    if (browsing)
        drawbars();
}

/* Opens the notification: sends its default action and focuses the
 * sender's window, since it can't raise itself. */
void notifyopen(const Arg* arg)
{
    const Notification* n;
    const char* id;
    Client* c;
    long hist;

    if (!shownotifications || !(n = notifyshown(&hist)))
        return;
    wl_list_for_each(c, &clients, link)
    {
        id = client_get_appid(c);
        if (!c->mon || client_is_unmanaged(c) ||
            (strcasecmp(id, n->app) &&
             (!*n->desktop || strcasecmp(id, n->desktop))))
            continue;
        selmon = c->mon;
        if (!VISIBLEON(c, c->mon))
            view(&(Arg){ .ui = c->tags });
        focusclient(c, 1);
        break;
    }
    if (hist < 0) {
        notify_invoke();
    } else {
        notifyviewid = 0;
        drawbars();
    }
}

/* Vertical walks the history, horizontal scrolls the text.
 * Returns whether the wheel was consumed. */
int notifywheel(Monitor* pm, double delta, int horizontal)
{
    /* a mouse notch is 15; touchpads send it in slices */
    static const double notch = 15, step = 5;
    static double acc, hacc;
    Monitor* m = barmonitor();
    const Notification* n;
    const char* text;
    NotifyBox b;
    long hist;

    if (!shownotifications || !m || m != pm || !m->b.titlew)
        return 0;
    n = notifyshown(&hist);

    if (!horizontal) {
        if (!n && !notify_histlen())
            return 0;
        for (acc += delta; acc >= notch; acc -= notch)
            notifynext(NULL);
        for (; acc <= -notch; acc += notch)
            notifyprev(NULL);
        return 1;
    }

    if (!n)
        return 0;
    notifysync(n, hist);
    if (hist >= 0)
        notifyview_ms = notify_now();
    notifybox(m, n, hist, 0, m->b.titlew, &b);
    text = n->text;
    for (hacc += delta; hacc >= step; hacc -= step)
        if ((int)drwl_font_getwidth(m->drw, text + notifyoff) > b.textw)
            do
                notifyoff++;
            while ((text[notifyoff] & 0xC0) == 0x80);
    for (; hacc <= -step; hacc += step)
        while (notifyoff && (text[--notifyoff] & 0xC0) == 0x80)
            ;
    drawbars();
    return 1;
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
    /* the bar is narrower than the output by its padding on both sides */
    m->b.width =
        MAX(1, rw - 2 * (int)roundf((float)barpadding * m->wlr_output->scale));
    m->b.real_width = (int)((float)m->b.width / m->wlr_output->scale);

    wlr_scene_node_set_enabled(&m->scene_buffer->node, barvisible(m));

    bufpooldrop(m->pool, LENGTH(m->pool));

    if (m->b.scale == m->wlr_output->scale && m->drw)
        return;

    drwl_font_destroy(m->drw->font);
    snprintf(
        fontattrs, sizeof(fontattrs), "dpi=%.2f", 96. * m->wlr_output->scale);
    if (!(drwl_font_create(m->drw, nfonts, fonts, fontattrs)))
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
        m->tray =
            createtray(m,
                       m->b.height,
                       MIN(iconsize, m->b.height),
                       (int)((float)systrayspacing * m->wlr_output->scale),
                       (int)((float)systraypadding * m->wlr_output->scale),
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
