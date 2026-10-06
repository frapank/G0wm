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
static void drawpill(Monitor* m,
                     int x,
                     int y,
                     int w,
                     int h,
                     double r,
                     int hollow,
                     uint32_t rgba);
#ifdef SYSTRAY
static void traynotify(void* data);
#endif /* SYSTRAY */
#ifdef TITLEBAR
static void drawtitle(Client* c);
#endif /* TITLEBAR */
#ifdef NOTIFICATIONS
typedef struct {
    int linex, linew;
    int iconx, iconw, iconh; /* iconw 0 without one */
    int textx, textw;        /* the › included */
    int countx, countw;      /* queue or history position */
    char count[32];
    int signx, signw; /* notification_actionsign */
    /* while picking, the action labels shown in place of the text */
    int chipx[NOTIFY_ACTMAX], chipw[NOTIFY_ACTMAX];
    size_t chip0, nchip;
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
                           const Notification* n,
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
static void notifypickclose(void);
static void notifypickrun(size_t i);
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
/* animates the line and times out the history view */
static struct wl_event_source* notifytimer;
/* picked action, -1 when closed; the picker belongs to notifypickid */
static long notifypicksel = -1;
static dbus_uint32_t notifypickid;
static int notifyboxx; /* last drawn x, for hit testing */
#endif                 /* NOTIFICATIONS */

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

/* whether (x, y) falls in a w by h box with corners of radius r */
static int inpill(double x, double y, int w, int h, double r)
{
    double dx, dy;

    if (x > w || y > h)
        return 0;
    r = fmax(r, 0);
    dx = fmax(fmax(r - x, x - (w - r)), 0);
    dy = fmax(fmax(r - y, y - (h - r)), 0);
    return dx * dx + dy * dy <= r * r;
}

/* Antialiased rounded rectangle, blended over the buffer. hollow keeps a
 * one pixel outline, as drwl_rect() does when not filled. */
static void drawpill(Monitor* m,
                     int x,
                     int y,
                     int w,
                     int h,
                     double r,
                     int hollow,
                     uint32_t rgba)
{
    pixman_image_t *mask, *src;
    pixman_color_t clr;
    uint8_t* data;
    int stride, px, py, sx, sy, cov;
    double fx, fy;

    if (w <= 0 || h <= 0 ||
        !(mask = pixman_image_create_bits(PIXMAN_a8, w, h, NULL, 0)))
        return;
    r = fmin(fmax(r, 0), fmin(w, h) / 2.0);
    data = (uint8_t*)pixman_image_get_data(mask);
    stride = pixman_image_get_stride(mask);

    /* 4x4 supersampling */
    for (py = 0; py < h; py++)
        for (px = 0; px < w; px++) {
            for (cov = 0, sy = 0; sy < 4; sy++)
                for (sx = 0; sx < 4; sx++) {
                    fx = px + (sx + 0.5) / 4;
                    fy = py + (sy + 0.5) / 4;
                    cov += inpill(fx, fy, w, h, r) &&
                           !(hollow && fx > 1 && fy > 1 &&
                             inpill(fx - 1, fy - 1, w - 2, h - 2, r - 1));
                }
            data[py * stride + px] = (uint8_t)(cov * 255 / 16);
        }

    clr = convert_color(rgba);
    if ((src = pixman_image_create_solid_fill(&clr))) {
        pixman_image_composite32(
            PIXMAN_OP_OVER, src, mask, m->drw->image, 0, 0, 0, 0, x, y, w, h);
        pixman_image_unref(src);
    }
    pixman_image_unref(mask);
}

/* the same colour at a third of its alpha */
static uint32_t fade(uint32_t rgba)
{
    return (rgba & ~0xffu) | (rgba & 0xffu) / 3;
}

/* the dot of tag i, a pill when selected; returns its cell's width */
static int tagdot(Monitor* m,
                  Monitor* s,
                  uint32_t i,
                  int* d,
                  int* lead,
                  int* sw)
{
    int gap;

    *d = MAX(m->drw->font->height * 2 / 5, 4);
    gap = *d * 3 / 2;
    *sw = s->tagset[s->seltags] & 1 << i ? *d * 3 : *d;
    *lead = i ? gap / 2 : m->lrpad / 2;
    return *lead + *sw + (i == ntags - 1 ? m->lrpad / 2 : gap - gap / 2);
}

/* shared with barclick() */
int tagwidth(Monitor* m, Monitor* s, uint32_t i)
{
    int d, lead, sw;

    if (!bartagdots)
        return (int)TEXTW(m, tags[i]);
    return tagdot(m, s, i, &d, &lead, &sw);
}

void drawbar(Monitor* m)
{
    int x, w, tw = 0, traywidth = 0;
    int boxs = m->drw->font->height / 9;
    int boxw = m->drw->font->height / 6 + 2;
    /* the little squares follow the text, which drwl centers */
    int boxy = (m->b.height - m->drw->font->height) / 2 + boxs;
    double boxr = barboxradius * m->wlr_output->scale;
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
    for (i = 0; i < ntags && bartagdots; i++) {
        int d, lead, sw;
        uint32_t clr;

        w = tagdot(m, s, i, &d, &lead, &sw);
        drwl_setscheme(m->drw, colors[SchemeNorm]);
        drwl_rect(m->drw, x, 0, w, m->b.height, 1, 1);
        if (urg & 1 << i)
            clr = colors[SchemeUrg][ColBorder];
        else if (s->tagset[s->seltags] & 1 << i)
            clr = colors[SchemeSel][ColFg];
        else if (occ & 1 << i)
            clr = colors[SchemeNorm][ColFg];
        else /* empty */
            clr = fade(colors[SchemeNorm][ColFg]);
        drawpill(m, x + lead, (m->b.height - d) / 2, sw, d, d / 2.0, 0, clr);
        x += w;
    }
    for (i = 0; i < ntags && !bartagdots; i++) {
        w = TEXTW(m, tags[i]);
        drwl_setscheme(
            m->drw,
            colors[s->tagset[s->seltags] & 1 << i ? SchemeSel : SchemeNorm]);
        drwl_text(
            m->drw, x, 0, w, m->b.height, m->lrpad / 2, tags[i], urg & 1 << i);
        if (occ & 1 << i)
            drawpill(m,
                     x + w - boxs - boxw,
                     boxy,
                     boxw,
                     boxw,
                     boxr,
                     !(sel && c && c->tags & 1 << i),
                     m->drw->scheme[urg & 1 << i ? ColBg : ColFg]);
        x += w;
    }
    if (barltsymbol) {
        w = TEXTW(m, s->ltsymbol);
        drwl_setscheme(m->drw, colors[SchemeNorm]);
        x = drwl_text(
            m->drw, x, 0, w, m->b.height, m->lrpad / 2, s->ltsymbol, 0);
    }

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
            int fh = m->drw->font->height, cw = MAX(fh / 12, 2);
            int tx, cx, end = x + w;
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

            if (cx + cw <= end)
                drawpill(m,
                         cx,
                         boxy,
                         cw,
                         fh - 2 * boxs,
                         cw / 2.0,
                         0,
                         colors[SchemeRunner][ColFg]);
            tx += cw;

            drwl_setscheme(m->drw, colors[SchemeRunnerSuggest]);
            if (!runner_len && *runner_placeholder && tx < end) {
                drwl_text(m->drw,
                          tx,
                          0,
                          end - tx,
                          m->b.height,
                          0,
                          runner_placeholder,
                          0);
            } else if (sug && (size_t)runner_len < strlen(sug) && tx < end) {
                drwl_text(m->drw,
                          tx,
                          0,
                          end - tx,
                          m->b.height,
                          0,
                          sug + runner_len,
                          0);
            } else if (!sug && tx < end) {
                /* Not a completion of what's typed, so it can't reuse the
                 * "sug + runner_len" tail above: it's an unrelated answer,
                 * appended rather than spliced in. */
                double calcval;
                char calcbuf[48];
                if (runnercalc(&calcval)) {
                    snprintf(calcbuf, sizeof calcbuf, "= %.10g", calcval);
                    drwl_text(
                        m->drw, tx, 0, end - tx, m->b.height, 0, calcbuf, 0);
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
                    drawpill(m,
                             x + boxs,
                             boxy,
                             boxw,
                             boxw,
                             boxr,
                             1,
                             m->drw->scheme[ColFg]);
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

/* One escape at p: ^c#RRGGBB^ and ^b#RRGGBB^ set the foreground and the
 * background of what follows (an eight digit form carries alpha too), ^d^
 * goes back to SchemeNorm, ^sN!cmd^ opens a clickable area for button N
 * (1 left, 2 right, 3 middle, 4/5 wheel up/down; ^s!^ is 1) and ^e^ closes
 * it. Returns the bytes it takes, and applies a colour to scm when that is
 * given; zero means p does not start one. */
static int statusescape(const char* p, uint32_t* scm)
{
    static const char hex[] = "0123456789abcdef";
    const char* d;
    uint32_t clr = 0;
    int i;

    if (p[0] != '^')
        return 0;
    if (p[1] == 's') {
        i = p[2] >= '1' && p[2] <= '0' + STATUS_BUTTONS ? 3 : 2;
        if (p[i++] != '!' || !p[i] || p[i] == '^')
            return 0;
        for (; p[i] && p[i] != '^'; i++)
            ;
        return p[i] == '^' ? i + 1 : 0;
    }
    if (p[1] == 'e' && p[2] == '^')
        return 3;
    if (p[1] == 'e' && p[2] == '!' && p[3] == '^')
        return 4;
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

/* Copies into cmd the command for button at x pixels into the status text.
 * Must walk the text the same way drawstatus() does. */
static int statusarea(Monitor* m,
                      const char* text,
                      int x,
                      int button,
                      char* cmd,
                      size_t sz)
{
    char seg[sizeof(stext)];
    const char* p = text;
    const char* area[STATUS_BUTTONS] = { 0 };
    int sx = 0, len;
    size_t n;

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

        sx += (int)drwl_font_getwidth(m->drw, seg);
        if (x < sx) {
            if (!area[button - 1])
                return 0;
            n = strcspn(area[button - 1], "^");
            if (n >= sz)
                n = sz - 1;
            memcpy(cmd, area[button - 1], n);
            cmd[n] = '\0';
            return 1;
        }

        if ((len = statusescape(p, NULL))) {
            if (p[1] == 's' && p[2] == '!')
                area[0] = p + 3;
            else if (p[1] == 's')
                area[p[2] - '1'] = p + 4;
            else if (p[1] == 'e')
                memset(area, 0, sizeof(area));
            p += len;
        }
    }
    return 0;
}

static int statuscmd(Monitor* pm, int x, int button, char* cmd, size_t sz)
{
    Monitor* m = barmonitor();

    return m && m == pm && button >= 1 && button <= STATUS_BUTTONS &&
           statusarea(m, stext, x, button, cmd, sz);
}

static void statusrun(char* cmd)
{
    char* argv[] = { "/bin/sh", "-c", cmd, NULL };

    spawn(&(Arg){ .v = argv });
}

int statusclick(Monitor* pm, int x, int button)
{
    char cmd[sizeof(stext)];

    if (!statuscmd(pm, x, button, cmd, sizeof(cmd)))
        return 0;
    statusrun(cmd);
    return 1;
}

int statusscroll(Monitor* pm, int x, double delta)
{
    /* a mouse notch is 15; touchpads send it in slices */
    static const double notch = 15;
    static double acc;
    char cmd[sizeof(stext)];

    if (delta == 0 || !statuscmd(pm, x, delta < 0 ? 4 : 5, cmd, sizeof(cmd)))
        return 0;
    if ((acc < 0) != (delta < 0))
        acc = 0;
    for (acc += delta; acc >= notch; acc -= notch)
        statusrun(cmd);
    for (; acc <= -notch; acc += notch)
        statusrun(cmd);
    return 1;
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
/* the top tab, or the focused client */
static int titlesel(Client* c)
{
    Monitor* m = c->mon;

    return c == (m->lt[m->sellt]->arrange == tabbed && !c->isfloating
                     ? tabtop(m)
                     : focustop(m));
}

/* Renders the client's own title bar. In the tabbed layout every client of the
 * group shares one row, so these end up drawn side by side as tabs. */
static void drawtitle(Client* c)
{
    Monitor* m = c->mon;
    Buffer* buf;
    const char* title;
    int w, h, sel, tab, lead, tw, fd;
    uint32_t fg;

    if (!c->title)
        return;
    settitle(c);
    if (!m || !titlebar || c->isfullscreen || c->titlew <= 0 ||
        !c->scene->node.enabled) {
        wlr_scene_node_set_enabled(&c->title->node, 0);
        return;
    }

    w = (int)((float)c->titlew * m->wlr_output->scale);
    h = m->t.height;
    if (w != c->titlebufw) {
        bufpooldrop(c->titlepool, LENGTH(c->titlepool));
        c->titlebufw = w;
    }
    if (!(buf = bufget(c->titlepool, LENGTH(c->titlepool), w, h)))
        return;

    sel = titlesel(c);
    tab = m->lt[m->sellt]->arrange == tabbed && !c->isfloating &&
          c->titlew < c->geom.width - 2 * (int)c->bw;
    fg = colors[sel ? SchemeTitleSel : SchemeTitle][ColFg];
    title = client_get_title(c);
    fd = MAX(m->drw->font->height / 3, 3);

    lead = m->lrpad / 2 + (c->isfloating ? fd + m->lrpad / 3 : 0);
    if (titlecenter) {
        tw = (int)drwl_font_getwidth(m->drw, title);
        lead = MAX(MIN((w - tw) / 2, w - m->lrpad / 2 - tw), lead);
    }

    drwl_setimage(m->drw, buf->image);
    drwl_setscheme(m->drw, colors[sel ? SchemeTitleSel : SchemeTitle]);
    drwl_rect(m->drw, 0, 0, w, h, 1, 1);
    if (w > lead)
        drwl_text(m->drw, 0, 0, w, h, lead, title, 0);

    if (c->isfloating)
        drawpill(
            m, lead - fd - m->lrpad / 3, (h - fd) / 2, fd, fd, fd / 2.0, 0, fg);
    if (tab && c->titlex)
        drawpill(m,
                 0,
                 h / 4,
                 (int)MAX(m->wlr_output->scale, 1),
                 h - h / 2,
                 0,
                 0,
                 fade(colors[SchemeTitle][ColFg]));

    wlr_scene_node_set_enabled(&c->title->node, 1);
    wlr_scene_buffer_set_opacity(c->title, decoopacity());
    wlr_scene_buffer_set_buffer(c->title, &buf->base);
    wlr_buffer_unlock(&buf->base);
}

#endif /* TITLEBAR */
#ifdef NOTIFICATIONS
/* Background and text. drwl_text() paints its own background, so a progress
 * fill is drawn by calling this twice under different clips. */
static void drawnotifytext(Monitor* m,
                           const NotifyBox* b,
                           int x,
                           int w,
                           const Notification* n,
                           const char* text,
                           uint32_t* scm)
{
    char buf[NOTIFY_TEXTMAX];
    size_t k, i;
    int aw;

    drwl_setscheme(m->drw, scm);
    drwl_rect(m->drw, x, 0, (unsigned int)w, m->b.height, 1, 1);
    if (b->nchip) {
        for (i = b->chip0; i < b->chip0 + b->nchip; i++)
            drwl_text(m->drw,
                      b->chipx[i],
                      0,
                      b->chipw[i],
                      m->b.height,
                      m->lrpad / 2,
                      n->act[i].label,
                      (long)i == notifypicksel);
        if (b->chip0 || b->chip0 + b->nchip < n->nact) {
            aw = (int)drwl_font_getwidth(m->drw, notifymore);
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
    } else if (b->textw > 0 &&
               (int)drwl_font_getwidth(m->drw, text) <= b->textw) {
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
    if (b->signw)
        drwl_text(m->drw,
                  b->signx,
                  0,
                  b->signw,
                  m->b.height,
                  0,
                  notification_actionsign,
                  0);
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
    int scheme = n->urgency == UrgencyLow        ? SchemeNotifyLow
                 : n->urgency == UrgencyCritical ? SchemeNotifyCrit
                                                 : SchemeNotify;
    int full = m->drw->font->height, len = full, top, fw, i;
    uint64_t now = notify_now(), wake = 0, end, left;
    uint32_t line = colors[scheme][ColBorder], filled[3], a, bg, tint;
    unsigned int held;
    const char* text;
    pixman_image_t* img;
    pixman_transform_t t;
    pixman_region32_t clip;
    NotifyBox b;

    notifysync(n, hist);
    notifypicking();
    notifyboxx = x;
    notifybox(m, n, hist, x, w, &b);
    text = n->text + (notifyoff < strlen(n->text) ? notifyoff : 0);

    if (n->value < 0) {
        drawnotifytext(m, &b, x, w, n, text, colors[scheme]);
    } else {
        /* the fill's background is premixed: a tint drawn over would dye the
         * text too */
        memcpy(filled, colors[scheme], sizeof(filled));
        bg = colors[scheme][ColBg];
        a = (line & 0xff) / 3;
        for (filled[ColBg] = bg & 0xff, i = 8; i < 32; i += 8) {
            tint = (bg >> i & 0xff) * (255 - a) + (line >> i & 0xff) * a;
            filled[ColBg] |= (tint / 255) << i;
        }
        fw = w * n->value / 100;
        pixman_region32_init_rect(&clip, x, 0, (unsigned int)fw, m->b.height);
        pixman_image_set_clip_region32(m->drw->image, &clip);
        pixman_region32_fini(&clip);
        drawnotifytext(m, &b, x, w, n, text, filled);
        pixman_region32_init_rect(
            &clip, x + fw, 0, (unsigned int)(w - fw), m->b.height);
        pixman_image_set_clip_region32(m->drw->image, &clip);
        pixman_region32_fini(&clip);
        drawnotifytext(m, &b, x, w, n, text, colors[scheme]);
        pixman_region32_init_rect(&clip, 0, 0, m->b.width, m->b.height);
        pixman_image_set_clip_region32(m->drw->image, &clip);
        pixman_region32_fini(&clip);
    }

    /* once, after both passes: they blend with OVER */
    if (b.linew) {
        /* full for sticky and history entries, the latter at half alpha */
        if (hist < 0 && n->timeout_ms && notify_held(&held)) {
            len = (int)((uint64_t)full * held / n->timeout_ms);
        } else if (hist < 0 && n->timeout_ms) {
            end = n->shown_ms + n->timeout_ms;
            left = end > now ? end - now : 0;
            len = (int)((uint64_t)full * left / n->timeout_ms);
            wake = n->timeout_ms / (unsigned int)(full > 0 ? full : 1);
            wake = wake < 16 ? 16 : wake;
        }
        if (hist >= 0)
            line = (line & ~0xffu) | (line & 0xffu) / 2;
        top = (m->b.height - full) / 2;
        drawpill(m,
                 b.linex,
                 top + full - len,
                 b.linew,
                 len,
                 notification_lineradius * m->wlr_output->scale,
                 0,
                 line);
    }

    if (b.iconw) {
        img = n->icon->img;
        pixman_transform_init_scale(
            &t,
            pixman_double_to_fixed((double)pixman_image_get_width(img) /
                                   b.iconw),
            pixman_double_to_fixed((double)pixman_image_get_height(img) /
                                   b.iconh));
        pixman_image_set_transform(img, &t);
        pixman_image_set_filter(img, PIXMAN_FILTER_BILINEAR, NULL, 0);
        pixman_image_composite32(PIXMAN_OP_OVER,
                                 img,
                                 NULL,
                                 m->drw->image,
                                 0,
                                 0,
                                 0,
                                 0,
                                 b.iconx,
                                 (m->b.height - b.iconh) / 2,
                                 b.iconw,
                                 b.iconh);
        pixman_image_set_transform(img, NULL);
    }

    if (notifyviewid) {
        end = notifyview_ms + notification_timeout * 1000;
        left = end > now ? end - now : 1;
        wake = !wake || left < wake ? left : wake;
    }
    if (wake && !notifytimer)
        notifytimer = wl_event_loop_add_timer(event_loop, notifytick, NULL);
    if (notifytimer)
        wl_event_source_timer_update(notifytimer, (int)wake);
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
    int pad = m->lrpad / 2, size = m->drw->font->height, cx, end, iw, ih;
    int picking;
    size_t i;

    memset(b, 0, sizeof(*b));
    cx = x + pad;
    if (notification_linewidth) {
        b->linex = cx;
        b->linew =
            (int)roundf((float)notification_linewidth * m->wlr_output->scale);
        b->linew = b->linew > 0 ? b->linew : 1;
        cx += b->linew + pad;
    }
    if (n->icon && size > 0) {
        iw = pixman_image_get_width(n->icon->img);
        ih = pixman_image_get_height(n->icon->img);
        if (iw > 0 && ih > 0) {
            b->iconw = iw >= ih ? size : (iw * size / ih ? iw * size / ih : 1);
            b->iconh = ih >= iw ? size : (ih * size / iw ? ih * size / iw : 1);
            b->iconx = cx;
            cx += b->iconw + pad;
        }
    }

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
    picking = hist < 0 && notifypicksel >= 0 && n->id == notifypickid;
    if (hist < 0 && n->nact && !picking && *notification_actionsign) {
        b->signw = (int)drwl_font_getwidth(m->drw, notification_actionsign);
        b->signx = end - b->signw;
        end = b->signx - pad;
    }
    b->textx = cx;
    b->textw = end > cx ? end - cx : 0;

    if (!picking)
        return;
    /* scroll the chips until the picked one fits */
    for (b->chip0 = 0;; b->chip0++) {
        for (i = b->chip0, cx = b->textx; i < n->nact; i++) {
            b->chipw[i] =
                (int)drwl_font_getwidth(m->drw, n->act[i].label) + m->lrpad;
            if (cx + b->chipw[i] > end)
                break;
            b->chipx[i] = cx;
            cx += b->chipw[i] + pad;
        }
        b->nchip = i - b->chip0;
        if (i > (size_t)notifypicksel || b->chip0 >= (size_t)notifypicksel)
            break;
    }
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
        (notifypicksel >= 0 ||
         notify_now() >= notifyview_ms + notification_timeout * 1000))
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

/* Also closes the picker once its notification is gone or has no actions. */
int notifypicking(void)
{
    const Notification* cur;

    if (notifypicksel < 0)
        return 0;
    cur = notify_current();
    if (!cur || cur->id != notifypickid || !cur->nact) {
        notifypickclose();
        return 0;
    }
    if ((size_t)notifypicksel >= cur->nact) /* updated with fewer */
        notifypicksel = (long)cur->nact - 1;
    return 1;
}

static void notifypickclose(void)
{
    notifypicksel = -1;
    notify_hold(0);
}

static void notifypickrun(size_t i)
{
    notifypicksel = -1;
    notify_action(i); /* releases the hold */
    drawbars();
}

/* Toggles the picker; while open it owns the keyboard (notifypickkey()). */
void notifyactions(const Arg* arg)
{
    const Notification* cur;

    if (!shownotifications)
        return;
    if (notifypicking()) {
        notifypickclose();
    } else if ((cur = notify_current()) && cur->nact) {
        notifyviewid = 0;
        notifypicksel = 0;
        notifypickid = cur->id;
        notify_hold(1);
    } else {
        return;
    }
    drawbars();
}

/* left/right, h/l or Tab move, Return or space runs, 1-9 run directly,
 * Escape closes */
void notifypickkey(xkb_keysym_t sym)
{
    const Notification* cur;
    long n;

    if (!notifypicking() || !(cur = notify_current()))
        return;
    n = (long)cur->nact;
    switch (sym) {
        case XKB_KEY_Left:
        case XKB_KEY_h:
        case XKB_KEY_ISO_Left_Tab:
            notifypicksel = (notifypicksel + n - 1) % n;
            break;
        case XKB_KEY_Right:
        case XKB_KEY_l:
        case XKB_KEY_Tab:
            notifypicksel = (notifypicksel + 1) % n;
            break;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
        case XKB_KEY_space:
            notifypickrun((size_t)notifypicksel);
            return;
        case XKB_KEY_Escape:
            notifypickclose();
            break;
        default:
            if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9 &&
                (long)(sym - XKB_KEY_1) < n)
                notifypickrun((size_t)(sym - XKB_KEY_1));
            return;
    }
    drawbars();
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
 * On a history entry, forgets it; with the picker open, closes the picker. */
void notifydismiss(const Arg* arg)
{
    const Notification* h;
    long hist;

    if (!shownotifications)
        return;
    if (notifypicking()) {
        notifypickclose();
        drawbars();
        return;
    }
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
 * sender's window, since it can't raise itself. From the pointer (arg->ui is
 * x + 1) a click on the action sign opens the picker, and with the picker
 * open a click runs the action under it or closes the picker. */
void notifyopen(const Arg* arg)
{
    const Notification* n;
    const char* id;
    Monitor* m = barmonitor();
    NotifyBox b;
    Client* c;
    long hist, px = arg && arg->ui ? (long)arg->ui - 1 : -1;
    size_t i;

    if (!shownotifications || !(n = notifyshown(&hist)))
        return;
    if (px >= 0 && m && hist < 0) {
        notifybox(m, n, hist, notifyboxx, m->b.titlew, &b);
        if (notifypicking()) {
            for (i = b.chip0; i < b.chip0 + b.nchip; i++)
                if (px >= b.chipx[i] && px < b.chipx[i] + b.chipw[i]) {
                    notifypickrun(i);
                    return;
                }
            notifypickclose();
            drawbars();
            return;
        }
        if (b.signw && px >= b.signx - m->lrpad / 2 &&
            px < b.signx + b.signw + m->lrpad / 2) {
            notifyactions(NULL);
            return;
        }
    }
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

/* Vertical walks the history (or the picker), horizontal scrolls the text.
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

    if (!horizontal && notifypicking()) {
        for (acc += delta; acc >= notch; acc -= notch)
            notifypickkey(XKB_KEY_Right);
        for (; acc <= -notch; acc += notch)
            notifypickkey(XKB_KEY_Left);
        return 1;
    }
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
    static char buf[STATUS_MAX];
    static size_t len;
    static int skip; /* in a line too long for buf, up to its newline */
    char *p, *nl, *line = NULL;
    ssize_t n;

    if (mask & WL_EVENT_ERROR)
        die("status in event error");
    if (mask & WL_EVENT_HANGUP) {
        stopstatus();
        return 0;
    }

    n = read(fd, buf + len, sizeof(buf) - 1 - len);
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

    len += (size_t)n;
    buf[len] = '\0';
    for (p = buf; (nl = strchr(p, '\n')); p = nl + 1) {
        *nl = '\0';
        if (skip)
            skip = 0;
        else
            line = p;
    }
    if (line) {
        snprintf(stext, sizeof(stext), "%s", line);
        drawbars();
    }

    /* keep the start of the next line */
    len = strlen(p);
    memmove(buf, p, len + 1);
    if (len == sizeof(buf) - 1) {
        len = 0;
        skip = 1;
    }

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
