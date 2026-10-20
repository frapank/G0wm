/*
 * See LICENSE file for copyright and license details.
 *
 * ~/.config/g0wm/config: written from config.h, read back over it
 */
#include <errno.h>
#include <limits.h>
#include <scfg.h>
#include <stdarg.h>

#include "g0wm.h"
#include "util.h"

/* the only file that includes it: these are the definitions */
#include "config.h"

typedef struct {
    int val;
    const char* name;
} Enum;

/* tables end on { 0, NULL }; a real zero value is listed before it */
/* in combo order: logo+shift+q */
static const Enum modnames[] = { { WLR_MODIFIER_LOGO, "logo" },
                                 { WLR_MODIFIER_CTRL, "ctrl" },
                                 { WLR_MODIFIER_ALT, "alt" },
                                 { WLR_MODIFIER_SHIFT, "shift" },
                                 { WLR_MODIFIER_CAPS, "caps" },
                                 { WLR_MODIFIER_MOD2, "mod2" },
                                 { WLR_MODIFIER_MOD3, "mod3" },
                                 { WLR_MODIFIER_MOD5, "mod5" },
                                 { 0, NULL } };

static const Enum dragmodes[] = { { CurMove, "move" },
                                  { CurResize, "resize" },
                                  { 0, NULL } };

static const Enum clickareas[] = {
    { ClkTagBar, "tagbar" },
    { ClkLtSymbol, "ltsymbol" },
    { ClkStatus, "status" },
    { ClkTitle, "title" },
    { ClkClient, "client" },
    { ClkRoot, "root" },
    { ClkTray, "tray" },
    { ClkWinTitle, "wintitle" },
    { 0, NULL },
};

static const Enum mousebuttons[] = {
    { BTN_LEFT, "left" },        { BTN_MIDDLE, "middle" },
    { BTN_RIGHT, "right" },      { WheelUp, "wheel-up" },
    { WheelDown, "wheel-down" }, { 0, NULL }
};

static const Enum gesturemotions[] = { { SwipeLeft, "swipe-left" },
                                       { SwipeRight, "swipe-right" },
                                       { SwipeUp, "swipe-up" },
                                       { SwipeDown, "swipe-down" },
                                       { PinchIn, "pinch-in" },
                                       { PinchOut, "pinch-out" },
                                       { 0, NULL } };

static const Enum cornerstyles[] = { { CornerNormal, "normal" },
                                     { CornerSquircle, "squircle" },
                                     { 0, NULL } };

#ifdef INTEGRATED_BACKGROUND
static const Enum opacitykinds[] = { { OpacityNormal, "normal" },
                                     { OpacityBlur, "blur" },
                                     { 0, NULL } };
#endif /* INTEGRATED_BACKGROUND */

static const Enum transforms[] = {
    { WL_OUTPUT_TRANSFORM_NORMAL, "normal" },
    { WL_OUTPUT_TRANSFORM_90, "90" },
    { WL_OUTPUT_TRANSFORM_180, "180" },
    { WL_OUTPUT_TRANSFORM_270, "270" },
    { WL_OUTPUT_TRANSFORM_FLIPPED, "flipped" },
    { WL_OUTPUT_TRANSFORM_FLIPPED_90, "flipped-90" },
    { WL_OUTPUT_TRANSFORM_FLIPPED_180, "flipped-180" },
    { WL_OUTPUT_TRANSFORM_FLIPPED_270, "flipped-270" },
    { 0, NULL }
};

static const Enum scrollmethods[] = {
    { LIBINPUT_CONFIG_SCROLL_NO_SCROLL, "none" },
    { LIBINPUT_CONFIG_SCROLL_2FG, "2fg" },
    { LIBINPUT_CONFIG_SCROLL_EDGE, "edge" },
    { LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN, "on-button-down" },
    { 0, NULL }
};

static const Enum clickmethods[] = {
    { LIBINPUT_CONFIG_CLICK_METHOD_NONE, "none" },
    { LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS, "button-areas" },
    { LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER, "clickfinger" },
    { 0, NULL }
};

static const Enum sendevents[] = {
    { LIBINPUT_CONFIG_SEND_EVENTS_ENABLED, "enabled" },
    { LIBINPUT_CONFIG_SEND_EVENTS_DISABLED, "disabled" },
    { LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE,
      "disabled-on-external-mouse" },
    { 0, NULL }
};

static const Enum accelprofiles[] = {
    { LIBINPUT_CONFIG_ACCEL_PROFILE_NONE, "none" },
    { LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT, "flat" },
    { LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE, "adaptive" },
    { 0, NULL }
};

static const Enum buttonmaps[] = { { LIBINPUT_CONFIG_TAP_MAP_LRM, "lrm" },
                                   { LIBINPUT_CONFIG_TAP_MAP_LMR, "lmr" },
                                   { 0, NULL } };

static const Enum loglevels[] = { { WLR_SILENT, "silent" },
                                  { WLR_ERROR, "error" },
                                  { WLR_INFO, "info" },
                                  { WLR_DEBUG, "debug" },
                                  { 0, NULL } };

static const struct {
    const char* name;
    void (*arrange)(Monitor*);
} arranges[] = { { "tile", tile }, { "float", NULL }, { "tabbed", tabbed } };

/* which member of the Arg a binding uses: 'i' int, 'u' unsigned, 'f' float,
 * 'v' an execvp() list, 'l' a layout symbol, 'c' a drag mode, 0 none */

typedef struct {
    void (*func)(const Arg*);
    const char* name;
    char arg;
} Action;

static const Action actions[] = {
    { chvt, "chvt", 'u' },
    { focusmon, "focusmon", 'i' },
    { focusstack, "focusstack", 'i' },
    { incnmaster, "incnmaster", 'i' },
    { killclient, "killclient", 0 },
    { magnify, "magnify", 'f' },
    { moveresize, "moveresize", 'c' },
    { movestack, "movestack", 'i' },
    { notifyactions, "notifyactions", 0 },
    { notifydismiss, "notifydismiss", 0 },
    { notifydismissall, "notifydismissall", 0 },
    { notifynext, "notifynext", 0 },
    { notifyopen, "notifyopen", 0 },
    { notifyprev, "notifyprev", 0 },
    { notifyscroll, "notifyscroll", 0 },
    { quit, "quit", 0 },
    { reloadsettings, "reloadsettings", 0 },
    { resizeheight, "resizeheight", 'i' },
    { resizewidth, "resizewidth", 'i' },
    { runnertoggle, "runnertoggle", 0 },
    { setlayout, "setlayout", 'l' },
    { setmfact, "setmfact", 'f' },
    { setopacityfocus, "setopacityfocus", 'f' },
    { setopacityunfocus, "setopacityunfocus", 'f' },
    { shifttag, "shifttag", 'i' },
    { shiftview, "shiftview", 'i' },
    { spawn, "spawn", 'v' },
    { tag, "tag", 'u' },
    { tagmon, "tagmon", 'i' },
    { togglebar, "togglebar", 0 },
    { togglefloating, "togglefloating", 0 },
    { togglefullscreen, "togglefullscreen", 0 },
    { togglegaps, "togglegaps", 0 },
    { toggleinhibit, "toggleinhibit", 0 },
    { toggleopacity, "toggleopacity", 0 },
    { toggletabbed, "toggletabbed", 'l' },
    { toggletag, "toggletag", 'u' },
    { toggletitlebar, "toggletitlebar", 0 },
    { toggleview, "toggleview", 'u' },
    { trayactivate, "trayactivate", 0 },
    { traymenu, "traymenu", 0 },
    { view, "view", 'u' },
    { zoom, "zoom", 0 },
};

static int line; /* of the directive being read */

static void warn(const char* fmt, ...)
{
    va_list ap;

    fprintf(stderr, "g0wm: config:%d: ", line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static const char* enumname(const Enum* e, int val)
{
    for (; e->name; e++)
        if (e->val == val)
            return e->name;
    return "unknown";
}

static int enumval(const Enum* e,
                   const char* name,
                   int fallback,
                   const char* what)
{
    const Enum* p;

    for (p = e; p->name; p++)
        if (!strcmp(p->name, name))
            return p->val;
    warn("%s is not a %s", name, what);
    return fallback;
}

static const Action* actionbyfunc(void (*func)(const Arg*))
{
    size_t i;

    for (i = 0; i < LENGTH(actions); i++)
        if (actions[i].func == func)
            return &actions[i];
    return NULL;
}

static const Action* actionbyname(const char* name)
{
    size_t i;

    if (!name)
        return NULL;
    for (i = 0; i < LENGTH(actions); i++)
        if (!strcmp(actions[i].name, name))
            return &actions[i];
    warn("%s is not an action", name);
    return NULL;
}

static const char* arrangename(void (*fn)(Monitor*))
{
    size_t i;

    for (i = 0; i < LENGTH(arranges); i++)
        if (arranges[i].arrange == fn)
            return arranges[i].name;
    return "float";
}

static uint32_t packrgba(const float c[static 4])
{
    return ((uint32_t)(c[0] * 255 + 0.5f) << 24) |
           ((uint32_t)(c[1] * 255 + 0.5f) << 16) |
           ((uint32_t)(c[2] * 255 + 0.5f) << 8) | (uint32_t)(c[3] * 255 + 0.5f);
}

/* the holes keep every name on its Scheme* index */
static const char* const schemenames[] = {
    [SchemeNorm] = "norm",
    [SchemeSel] = "sel",
    [SchemeUrg] = "urg",
    [SchemeTitle] = "title",
    [SchemeTitleSel] = "titlesel",
    [SchemeStatus] = "status",
    [SchemeNotify] = "notify",
    [SchemeNotifyLow] = "notifylow",
    [SchemeNotifyCrit] = "notifycrit",
    [SchemeRunner] = "runner",
    [SchemeRunnerSuggest] = "runnersuggest",
};

/* repeatable, so never missing */
static const char* const records[] = { "layout",    "rule",     "monitor",
                                       "autostart", "key",      "button",
                                       "gesture",   "padbutton" };

static FILE* fp;
static int depth;
static const char* prev; /* last top level directive */

static void indent(void)
{
    int i;

    for (i = 0; i < depth; i++)
        fputc('\t', fp);
}

/* a blank line between top level groups */
static void start(const char* k)
{
    if (!depth) {
        if (prev && strcmp(prev, k))
            fputc('\n', fp);
        prev = k;
    }
    indent();
    fputs(k, fp);
}

/* quoted only when it has to be */
static void word(const char* s)
{
    fputc(' ', fp);
    if (*s && !strpbrk(s, " \t\"'\\{}")) {
        fputs(s, fp);
        return;
    }
    fputc('"', fp);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            fputc('\\', fp);
        fputc(*s, fp);
    }
    fputc('"', fp);
}

/* floats, so a double's digits would only be conversion noise */
static void num(double v)
{
    fprintf(fp, " %g", round(v * 1e6) / 1e6);
}

/* a comment over k */
static void note(const char* k, const char* s)
{
    if (!depth) {
        if (prev)
            fputc('\n', fp);
        prev = k;
    }
    indent();
    fprintf(fp, "# %s\n", s);
}

static void wopen(const char* k)
{
    start(k);
    fputs(" {\n", fp);
    depth++;
}

static void wclose(void)
{
    depth--;
    indent();
    fputs("}\n", fp);
}

/* NULL is off, not empty */
static void wstr(const char* k, const char* v)
{
    start(k);
    if (v)
        word(v);
    fputc('\n', fp);
}

static void wint(const char* k, long v)
{
    start(k);
    fprintf(fp, " %ld\n", v);
}

static void wnum(const char* k, double v)
{
    start(k);
    num(v);
    fputc('\n', fp);
}

static void wbool(const char* k, int v)
{
    start(k);
    fputs(v ? " true\n" : " false\n", fp);
}

static void whex(const char* k, uint32_t c)
{
    start(k);
    fprintf(fp, " #%08x\n", c);
}

static void wlist(const char* k, const char* const* v, size_t n)
{
    size_t i;

    start(k);
    for (i = 0; i < n; i++)
        word(v[i]);
    fputc('\n', fp);
}

static void wcombo(uint32_t mod, const char* last)
{
    const Enum* e;

    fputc(' ', fp);
    for (e = modnames; e->name; e++)
        if (mod & (uint32_t)e->val)
            fprintf(fp, "%s+", e->name);
    fputs(last, fp);
}

/* logo+alt, or none */
static void wmods(const char* k, uint32_t mod)
{
    const Enum* e;
    const char* sep = " ";

    start(k);
    if (!mod)
        fputs(" none", fp);
    for (e = modnames; e->name; e++) {
        if (mod & (uint32_t)e->val) {
            fprintf(fp, "%s%s", sep, e->name);
            sep = "+";
        }
    }
    fputc('\n', fp);
}

static void warg(char kind, const Arg* arg)
{
    const char* const* v;

    switch (kind) {
        case 'i':
            fprintf(fp, " %d", arg->i);
            break;
        case 'u':
            fprintf(fp, " %u", arg->ui);
            break;
        case 'f':
            num(arg->f);
            break;
        case 'v':
            for (v = arg->v; v && *v; v++)
                word(*v);
            break;
        case 'l':
            if (arg->v)
                word(((const Layout*)arg->v)->symbol);
            break;
        case 'c':
            word(enumname(dragmodes, (int)arg->ui));
            break;
    }
}

static void wlook(void)
{
    size_t i;

    wopen("look");
    wlist("fonts", fonts, nfonts);
    wopen("colors");
    note("norm", "fg bg border");
    for (i = 0; i < LENGTH(schemenames); i++) {
        if (!schemenames[i])
            continue;
        start(schemenames[i]);
        fprintf(fp,
                " #%08x #%08x #%08x\n",
                colors[i][ColFg],
                colors[i][ColBg],
                colors[i][ColBorder]);
    }
    wclose();
    whex("rootcolor", packrgba(rootcolor));
    whex("fullscreen_bg", packrgba(fullscreen_bg));
#ifdef INTEGRATED_BACKGROUND
    note("wallpaper", "empty for none");
    wstr("wallpaper", wallpaper);
#endif
    note("tags", "up to 31");
    wlist("tags", (const char* const*)tags, ntags);
    wclose();
}

static void wwindows(void)
{
    wopen("windows");
    wint("borderpx", borderpx);
    note("cornerstyle", "normal or squircle");
    wstr("cornerstyle", enumname(cornerstyles, cornerstyle));
    wint("cornerpx", cornerpx);
    wint("gaps", gaps);
    wint("gappx", gappx);
    wbool("smartgaps", smartgaps);
    wbool("titlebar", titlebar);
    wint("titlepadding", titlepadding);
    wbool("titlecenter", titlecenter);
    note("titleclose", "shown with the pointer over it");
    wbool("titleclose", titleclose);
    whex("titleclosecolor", titleclosecolor);
    note("movethreshold", "px dragged on a title before the window moves");
    wint("movethreshold", movethreshold);
    note("movesnap", "px at which a moved floating window sticks, 0 off");
    wint("movesnap", movesnap);
    wclose();
}

static void wbar(void)
{
    wopen("bar");
    wbool("showbar", showbar);
    wbool("topbar", topbar);
    wbool("barwintitle", barwintitle);
    note("bartagdots", "dots instead of the tag names");
    wbool("bartagdots", bartagdots);
    wbool("barltsymbol", barltsymbol);
    wint("barboxradius", barboxradius);
    wint("barpadding", barpadding);
    note("barheight", "times the font height, 0 for automatic");
    wnum("barheight", barheight);
    note("barsinglemon", "one bar only, on the first monitor");
    wbool("barsinglemon", barsinglemon);
    wbool("showsystray", showsystray);
    wint("systrayspacing", systrayspacing);
    wint("systraypadding", systraypadding);
    note("systrayiconsize", "0 fills the bar");
    wint("systrayiconsize", systrayiconsize);
    wbool("shownotifications", shownotifications);
    note("notification_timeout", "seconds");
    wint("notification_timeout", notification_timeout);
    wint("notification_linewidth", notification_linewidth);
    wint("notification_lineradius", notification_lineradius);
    wstr("notification_actionsign", notification_actionsign);
    note("runner_placeholder", "shown while nothing is typed");
    wstr("runner_placeholder", runner_placeholder);
    wclose();
}

static void wopacity(void)
{
    wopen("opacity");
    wbool("opacity_enabled", opacity_enabled);
    wnum("opacity_focus", opacity_focus);
    wnum("opacity_unfocus", opacity_unfocus);
    note("opacity_deco", "bar, title bars and borders");
    wnum("opacity_deco", opacity_deco);
#ifdef INTEGRATED_BACKGROUND
    note("opacity_type", "normal or blur");
    wstr("opacity_type", enumname(opacitykinds, opacity_type));
    wint("blur_radius", blur_radius);
    wint("blur_passes", blur_passes);
    wnum("blur_saturation", blur_saturation);
    wnum("blur_brightness", blur_brightness);
#endif
    wclose();
}

static void winput(void)
{
    wopen("input");
    wbool("sloppyfocus", sloppyfocus);
    wopen("xkb_rules");
    wstr("rules", xkb_rules.rules);
    wstr("model", xkb_rules.model);
    wstr("layout", xkb_rules.layout);
    wstr("variant", xkb_rules.variant);
    wstr("options", xkb_rules.options);
    wclose();
    wint("repeat_rate", repeat_rate);
    wint("repeat_delay", repeat_delay);
    wstr("cursor_theme", cursor_theme);
    wint("cursor_size", cursor_size);
    wbool("hide_cursor_when_typing", hide_cursor_when_typing);
    wbool("tap_to_click", tap_to_click);
    wbool("tap_and_drag", tap_and_drag);
    wbool("drag_lock", drag_lock);
    wbool("natural_scrolling", natural_scrolling);
    wbool("disable_while_typing", disable_while_typing);
    wbool("left_handed", left_handed);
    wbool("middle_button_emulation", middle_button_emulation);
    note("scroll_method", "none, 2fg, edge or on-button-down");
    wstr("scroll_method", enumname(scrollmethods, scroll_method));
    note("click_method", "none, button-areas or clickfinger");
    wstr("click_method", enumname(clickmethods, click_method));
    note("send_events_mode", "enabled, disabled or disabled-on-external-mouse");
    wstr("send_events_mode", enumname(sendevents, (int)send_events_mode));
    note("accel_profile", "none, flat or adaptive, speed from -1 to 1");
    wstr("accel_profile", enumname(accelprofiles, accel_profile));
    wnum("accel_speed", accel_speed);
    note("touchpad_natural_scrolling", "the same three, for touchpads");
    wbool("touchpad_natural_scrolling", touchpad_natural_scrolling);
    wstr("touchpad_accel_profile",
         enumname(accelprofiles, touchpad_accel_profile));
    wnum("touchpad_accel_speed", touchpad_accel_speed);
    note("pinchzoom_mod", "held while pinching to zoom, none for off");
    wmods("pinchzoom_mod", pinchzoom_mod);
    note("button_map", "lrm or lmr");
    wstr("button_map", enumname(buttonmaps, button_map));
    note("tablet_output", "output names as wlr-randr shows them, none for all");
    wstr("tablet_output", tablet_output);
    note("tablet_left_handed", "buttons on the right");
    wbool("tablet_left_handed", tablet_left_handed);
    note("touch_output", "none: the one udev reports");
    wstr("touch_output", touch_output);
    wclose();
}

static void wmisc(void)
{
    wopen("misc");
    note("log_level", "silent, error, info or debug");
    wstr("log_level", enumname(loglevels, log_level));
    note("bypass_surface_visibility",
         "a hidden fullscreen window keeps the screen awake");
    wbool("bypass_surface_visibility", bypass_surface_visibility);
    note("allow_tearing", "tearing for fullscreen windows that ask for it");
    wbool("allow_tearing", allow_tearing);
    note("auto_vrr", "VRR for fullscreen games and videos");
    wbool("auto_vrr", auto_vrr);
    wclose();
}

static void wlayouts(void)
{
    size_t i;

    note("layout", "layout: symbol, then tile, float or tabbed");
    for (i = 0; i < nlayouts; i++) {
        start("layout");
        word(layouts[i].symbol);
        word(arrangename(layouts[i].arrange));
        fputc('\n', fp);
    }
}

static void wrules(void)
{
    size_t i;

    note("rule",
         "rule: opacity 0 keeps the default, monitor -1 is the focused one");
    for (i = 0; i < nrules; i++) {
        wopen("rule");
        wstr("id", rules[i].id);
        wstr("title", rules[i].title);
        wint("tags", rules[i].tags);
        wbool("isfloating", rules[i].isfloating);
        wnum("opacity_focus", rules[i].opacity_focus);
        wnum("opacity_unfocus", rules[i].opacity_unfocus);
        wint("monitor", rules[i].monitor);
        wclose();
    }
}

static void wmonitors(void)
{
    size_t i;

    note("monitor", "monitor: the first matching name wins");
    for (i = 0; i < nmonrules; i++) {
        wopen("monitor");
        wstr("name", monrules[i].name);
        wnum("mfact", monrules[i].mfact);
        wint("nmaster", monrules[i].nmaster);
        wnum("scale", monrules[i].scale);
        wstr("layout", monrules[i].lt ? monrules[i].lt->symbol : NULL);
        wstr("transform", enumname(transforms, (int)monrules[i].rr));
        wint("x", monrules[i].x);
        wint("y", monrules[i].y);
        wint("width", monrules[i].width);
        wint("height", monrules[i].height);
        wint("refresh", monrules[i].refresh);
        wclose();
    }
}

/* one NULL terminated run of arguments per command */
static void wautostart(void)
{
    const char* const* p;

    note("autostart", "autostart: a command, run without a shell");
    for (p = autostart; *p; p++) {
        start("autostart");
        for (; *p; p++)
            word(*p);
        fputc('\n', fp);
    }
}

static void wkeys(void)
{
    size_t i;

    note("key", "key: modifiers+key, action, argument");
    for (i = 0; i < nkeys; i++) {
        const Action* act = actionbyfunc(keys[i].func);
        char name[64];

        if (!act) {
            fprintf(
                stderr, "g0wm: binding %zu calls an action with no name\n", i);
            continue;
        }
        if (xkb_keysym_get_name(keys[i].keysym, name, sizeof name) < 0)
            continue;
        start("key");
        wcombo(keys[i].mod, name);
        word(act->name);
        warg(act->arg, &keys[i].arg);
        fputc('\n', fp);
    }
}

static void wbuttons(void)
{
    size_t i;

    note("button", "button: bar area, modifiers+button, action, argument");
    for (i = 0; i < nbuttons; i++) {
        const Action* act = actionbyfunc(buttons[i].func);

        if (!act) {
            fprintf(
                stderr, "g0wm: click %zu calls an action with no name\n", i);
            continue;
        }
        start("button");
        word(enumname(clickareas, (int)buttons[i].click));
        wcombo(buttons[i].mod, enumname(mousebuttons, (int)buttons[i].button));
        word(act->name);
        warg(act->arg, &buttons[i].arg);
        fputc('\n', fp);
    }
}

static void wgestures(void)
{
    size_t i;

    note("gesture", "gesture: motion, fingers, action, argument");
    for (i = 0; i < ngestures; i++) {
        const Action* act = actionbyfunc(gestures[i].func);

        if (!act) {
            fprintf(
                stderr, "g0wm: gesture %zu calls an action with no name\n", i);
            continue;
        }
        start("gesture");
        word(enumname(gesturemotions, (int)gestures[i].motion));
        fprintf(fp, " %u", gestures[i].fingers);
        word(act->name);
        warg(act->arg, &gestures[i].arg);
        fputc('\n', fp);
    }
}

static void wpadbuttons(void)
{
    size_t i;

    note("padbutton", "padbutton: button from 0, action, argument");
    for (i = 0; i < npadbuttons; i++) {
        const Action* act = actionbyfunc(padbuttons[i].func);

        if (!act) {
            fprintf(stderr,
                    "g0wm: pad button %zu calls an action with no name\n",
                    i);
            continue;
        }
        start("padbutton");
        fprintf(fp, " %u", padbuttons[i].button);
        word(act->name);
        warg(act->arg, &padbuttons[i].arg);
        fputc('\n', fp);
    }
}

static void defaults(void)
{
    note("", "a setting with no value is off");
    wlook();
    wwindows();
    wbar();
    wopacity();
    winput();
    wmisc();
    wlayouts();
    wrules();
    wmonitors();
    wautostart();
    wkeys();
    wbuttons();
    wgestures();
    wpadbuttons();
}

/* the defaults, rendered; NULL and a message when they cannot be */
static char* rendered(void)
{
    char* text = NULL;
    size_t len;

    if ((fp = open_memstream(&text, &len))) {
        depth = 0;
        prev = NULL;
        defaults();
        if (!fclose(fp))
            return text;
    }
    free(text);
    fprintf(stderr, "g0wm: cannot render the settings\n");
    return NULL;
}

static const struct scfg_directive* find(const struct scfg_block* b,
                                         const char* k)
{
    size_t i;

    if (!b)
        return NULL;
    for (i = 0; i < b->directives_len; i++) {
        if (!strcmp(b->directives[i].name, k)) {
            line = b->directives[i].lineno;
            return &b->directives[i];
        }
    }
    return NULL;
}

static const struct scfg_block* section(const struct scfg_block* b,
                                        const char* k)
{
    const struct scfg_directive* d = find(b, k);

    return d ? &d->children : NULL;
}

static size_t count(const struct scfg_block* b, const char* k)
{
    size_t i, n = 0;

    for (i = 0; i < b->directives_len; i++)
        n += !strcmp(b->directives[i].name, k);
    return n;
}

static const char* param(const struct scfg_directive* d, size_t i)
{
    return d && i < d->params_len ? d->params[i] : NULL;
}

static int isbool(const char* s)
{
    return s && (!strcmp(s, "true") || !strcmp(s, "false"));
}

static int isnum(const char* s, double* v)
{
    char* end;
    double x;

    if (!s || !*s)
        return 0;
    x = strtod(s, &end);
    if (*end)
        return 0;
    if (v)
        *v = x;
    return 1;
}

static double numval(const char* s, double cur)
{
    double v;

    return isnum(s, &v) ? v : cur;
}

/* a negative one would not convert */
static unsigned int uintval(const char* s, unsigned int cur)
{
    double v;

    if (!isnum(s, &v))
        return cur;
    if (v < 0 || v > UINT_MAX) {
        warn("%s is out of range", s);
        return cur;
    }
    return (unsigned int)v;
}

static uint32_t hexval(const char* s, uint32_t cur)
{
    unsigned long v;
    char* end;

    if (!s)
        return cur;
    if (*s == '#' && strlen(s) == 9) {
        v = strtoul(s + 1, &end, 16);
        if (!*end)
            return (uint32_t)v;
    }
    warn("%s is not an #rrggbbaa color", s);
    return cur;
}

/* the getters keep the value config.h gave when the file has nothing usable */
static int getbool(const struct scfg_block* b, const char* k, int cur)
{
    const char* s = param(find(b, k), 0);

    return isbool(s) ? *s == 't' : cur;
}

static double getnum(const struct scfg_block* b, const char* k, double cur)
{
    return numval(param(find(b, k), 0), cur);
}

static unsigned int getuint(const struct scfg_block* b,
                            const char* k,
                            unsigned int cur)
{
    return uintval(param(find(b, k), 0), cur);
}

static const char* getstr(const struct scfg_block* b,
                          const char* k,
                          const char* cur)
{
    const struct scfg_directive* d = find(b, k);

    return d ? param(d, 0) : cur;
}

static int getenum(const struct scfg_block* b,
                   const char* k,
                   const Enum* e,
                   int cur,
                   const char* what)
{
    const char* name = getstr(b, k, NULL);

    return name ? enumval(e, name, cur, what) : cur;
}

static uint32_t gethex(const struct scfg_block* b, const char* k, uint32_t cur)
{
    return hexval(getstr(b, k, NULL), cur);
}

static void getrgba(float c[static 4],
                    const struct scfg_block* b,
                    const char* k)
{
    uint32_t v = gethex(b, k, packrgba(c));

    c[0] = ((v >> 24) & 0xFF) / 255.0f;
    c[1] = ((v >> 16) & 0xFF) / 255.0f;
    c[2] = ((v >> 8) & 0xFF) / 255.0f;
    c[3] = (v & 0xFF) / 255.0f;
}

/* the parsed file outlives the settings, so its strings are used in place.
 * One slot to spare: the result is NULL terminated as well as counted. */
static const char** strlist(const struct scfg_directive* d,
                            size_t from,
                            size_t* n)
{
    const char** v;
    size_t i;

    if (!d || d->params_len < from)
        return NULL;
    v = ecalloc(d->params_len - from + 1, sizeof *v);
    for (i = from; i < d->params_len; i++)
        v[i - from] = d->params[i];
    if (n)
        *n = d->params_len - from;
    return v;
}

/* logo+shift+q: the mods into *mod, returns q */
static const char* splitmods(const char* s, uint32_t* mod)
{
    const char* plus;
    char name[16];

    *mod = 0;
    while ((plus = strchr(s, '+'))) {
        snprintf(name, sizeof name, "%.*s", (int)(plus - s), s);
        *mod |= (uint32_t)enumval(modnames, name, 0, "modifier");
        s = plus + 1;
    }
    return s;
}

static uint32_t getmods(const struct scfg_block* b, const char* k, uint32_t cur)
{
    const char* s = getstr(b, k, NULL);
    uint32_t mod;

    if (!s)
        return cur;
    if (!strcmp(s, "none"))
        return 0;
    s = splitmods(s, &mod);
    return mod | (uint32_t)enumval(modnames, s, 0, "modifier");
}

static const Layout* layoutbysymbol(const char* symbol)
{
    size_t i;

    if (!symbol)
        return NULL;
    for (i = 0; i < nlayouts; i++)
        if (!strcmp(layouts[i].symbol, symbol))
            return &layouts[i];
    warn("no layout draws %s", symbol);
    return NULL;
}

static Arg getarg(char kind, const struct scfg_directive* d, size_t i)
{
    const char* s = param(d, i);
    Arg a = { 0 };

    switch (kind) {
        case 'i':
            a.i = (int)numval(s, 0);
            break;
        case 'u':
            a.ui = uintval(s, 0);
            break;
        case 'f':
            a.f = (float)numval(s, 0);
            break;
        case 'v':
            a.v = strlist(d, i, NULL);
            break;
        case 'l':
            a.v = layoutbysymbol(s);
            break;
        case 'c':
            a.ui = s ? (uint32_t)enumval(dragmodes, s, CurMove, "drag mode")
                     : CurMove;
            break;
    }
    return a;
}

static void applycolors(const struct scfg_block* b)
{
    const struct scfg_directive* d;
    size_t i, j;

    for (i = 0; i < LENGTH(schemenames); i++) {
        if (!schemenames[i] || !(d = find(b, schemenames[i])))
            continue;
        for (j = 0; j < 3; j++)
            colors[i][j] = hexval(param(d, j), colors[i][j]);
    }
}

static void applylook(const struct scfg_block* b)
{
    const char** list;

    if (!b)
        return;
    if ((list = strlist(find(b, "fonts"), 0, &nfonts)))
        fonts = list;
    applycolors(section(b, "colors"));
    getrgba(rootcolor, b, "rootcolor");
    getrgba(fullscreen_bg, b, "fullscreen_bg");
#ifdef INTEGRATED_BACKGROUND
    wallpaper = getstr(b, "wallpaper", wallpaper);
#endif
    if ((list = strlist(find(b, "tags"), 0, &ntags))) {
        tags = (char**)list;
        if (ntags > 31) { /* TAGMASK has to fit in a uint32_t */
            warn("only the first 31 tags are used");
            ntags = 31;
        }
    }
}

static void applywindows(const struct scfg_block* b)
{
    if (!b)
        return;
    borderpx = getuint(b, "borderpx", borderpx);
    cornerstyle =
        getenum(b, "cornerstyle", cornerstyles, cornerstyle, "corner style");
    cornerpx = getuint(b, "cornerpx", cornerpx);
    gaps = (int)getnum(b, "gaps", gaps);
    gappx = getuint(b, "gappx", gappx);
    smartgaps = getbool(b, "smartgaps", smartgaps);
    titlebar = getbool(b, "titlebar", titlebar);
    titlepadding = getuint(b, "titlepadding", titlepadding);
    titlecenter = getbool(b, "titlecenter", titlecenter);
    titleclose = getbool(b, "titleclose", titleclose);
    titleclosecolor = gethex(b, "titleclosecolor", titleclosecolor);
    movethreshold = (int)getnum(b, "movethreshold", movethreshold);
    movesnap = (int)getnum(b, "movesnap", movesnap);
}

static void applybar(const struct scfg_block* b)
{
    if (!b)
        return;
    showbar = getbool(b, "showbar", showbar);
    topbar = getbool(b, "topbar", topbar);
    barwintitle = getbool(b, "barwintitle", barwintitle);
    bartagdots = getbool(b, "bartagdots", bartagdots);
    barltsymbol = getbool(b, "barltsymbol", barltsymbol);
    barboxradius = getuint(b, "barboxradius", barboxradius);
    barpadding = getuint(b, "barpadding", barpadding);
    barheight = (float)getnum(b, "barheight", barheight);
    barsinglemon = getbool(b, "barsinglemon", barsinglemon);
    showsystray = getbool(b, "showsystray", showsystray);
    systrayspacing = getuint(b, "systrayspacing", systrayspacing);
    systraypadding = getuint(b, "systraypadding", systraypadding);
    systrayiconsize = getuint(b, "systrayiconsize", systrayiconsize);
    shownotifications = getbool(b, "shownotifications", shownotifications);
    notification_timeout =
        getuint(b, "notification_timeout", notification_timeout);
    notification_linewidth =
        getuint(b, "notification_linewidth", notification_linewidth);
    notification_lineradius =
        getuint(b, "notification_lineradius", notification_lineradius);
    notification_actionsign =
        getstr(b, "notification_actionsign", notification_actionsign);
    runner_placeholder = getstr(b, "runner_placeholder", runner_placeholder);
}

static void applyopacity(const struct scfg_block* b)
{
    if (!b)
        return;
    opacity_enabled = getbool(b, "opacity_enabled", opacity_enabled);
    opacity_focus = (float)getnum(b, "opacity_focus", opacity_focus);
    opacity_unfocus = (float)getnum(b, "opacity_unfocus", opacity_unfocus);
    opacity_deco = (float)getnum(b, "opacity_deco", opacity_deco);
#ifdef INTEGRATED_BACKGROUND
    opacity_type = getenum(
        b, "opacity_type", opacitykinds, opacity_type, "kind of opacity");
    blur_radius = getuint(b, "blur_radius", blur_radius);
    blur_passes = getuint(b, "blur_passes", blur_passes);
    blur_saturation = (float)getnum(b, "blur_saturation", blur_saturation);
    blur_brightness = (float)getnum(b, "blur_brightness", blur_brightness);
#endif
}

static void applyinput(const struct scfg_block* b)
{
    const struct scfg_block* x;

    if (!b)
        return;
    sloppyfocus = getbool(b, "sloppyfocus", sloppyfocus);
    if ((x = section(b, "xkb_rules"))) {
        xkb_rules.rules = getstr(x, "rules", xkb_rules.rules);
        xkb_rules.model = getstr(x, "model", xkb_rules.model);
        xkb_rules.layout = getstr(x, "layout", xkb_rules.layout);
        xkb_rules.variant = getstr(x, "variant", xkb_rules.variant);
        xkb_rules.options = getstr(x, "options", xkb_rules.options);
    }
    repeat_rate = (int)getnum(b, "repeat_rate", repeat_rate);
    repeat_delay = (int)getnum(b, "repeat_delay", repeat_delay);
    cursor_theme = getstr(b, "cursor_theme", cursor_theme);
    cursor_size = (int)getnum(b, "cursor_size", cursor_size);
    hide_cursor_when_typing =
        getbool(b, "hide_cursor_when_typing", hide_cursor_when_typing);
    tap_to_click = getbool(b, "tap_to_click", tap_to_click);
    tap_and_drag = getbool(b, "tap_and_drag", tap_and_drag);
    drag_lock = getbool(b, "drag_lock", drag_lock);
    natural_scrolling = getbool(b, "natural_scrolling", natural_scrolling);
    disable_while_typing =
        getbool(b, "disable_while_typing", disable_while_typing);
    left_handed = getbool(b, "left_handed", left_handed);
    middle_button_emulation =
        getbool(b, "middle_button_emulation", middle_button_emulation);
    scroll_method = (enum libinput_config_scroll_method)getenum(
        b, "scroll_method", scrollmethods, scroll_method, "scroll method");
    click_method = (enum libinput_config_click_method)getenum(
        b, "click_method", clickmethods, click_method, "click method");
    send_events_mode = (uint32_t)getenum(b,
                                         "send_events_mode",
                                         sendevents,
                                         (int)send_events_mode,
                                         "send events mode");
    accel_profile =
        (enum libinput_config_accel_profile)getenum(b,
                                                    "accel_profile",
                                                    accelprofiles,
                                                    accel_profile,
                                                    "acceleration profile");
    accel_speed = getnum(b, "accel_speed", accel_speed);
    touchpad_natural_scrolling =
        getbool(b, "touchpad_natural_scrolling", touchpad_natural_scrolling);
    touchpad_accel_profile =
        (enum libinput_config_accel_profile)getenum(b,
                                                    "touchpad_accel_profile",
                                                    accelprofiles,
                                                    touchpad_accel_profile,
                                                    "acceleration profile");
    touchpad_accel_speed =
        getnum(b, "touchpad_accel_speed", touchpad_accel_speed);
    pinchzoom_mod = getmods(b, "pinchzoom_mod", pinchzoom_mod);
    button_map = (enum libinput_config_tap_button_map)getenum(
        b, "button_map", buttonmaps, button_map, "button map");
    tablet_output = getstr(b, "tablet_output", tablet_output);
    tablet_left_handed = getbool(b, "tablet_left_handed", tablet_left_handed);
    touch_output = getstr(b, "touch_output", touch_output);
}

static void applymisc(const struct scfg_block* b)
{
    if (!b)
        return;
    log_level = getenum(b, "log_level", loglevels, log_level, "log level");
    bypass_surface_visibility =
        getbool(b, "bypass_surface_visibility", bypass_surface_visibility);
    allow_tearing = getbool(b, "allow_tearing", allow_tearing);
    auto_vrr = getbool(b, "auto_vrr", auto_vrr);
}

/* none keeps the defaults: a monitor needs a layout */
static void applylayouts(const struct scfg_block* b)
{
    const struct scfg_directive* d;
    Layout* out;
    size_t i, j, n = 0;

    if (!count(b, "layout"))
        return;
    out = ecalloc(count(b, "layout"), sizeof *out);
    for (i = 0; i < b->directives_len; i++) {
        const char* name;

        d = &b->directives[i];
        line = d->lineno;
        if (strcmp(d->name, "layout"))
            continue;
        out[n].symbol = param(d, 0) ? param(d, 0) : "[ ]";
        name = param(d, 1) ? param(d, 1) : "float";
        for (j = 0; j < LENGTH(arranges); j++)
            if (!strcmp(arranges[j].name, name))
                break;
        if (j < LENGTH(arranges))
            out[n].arrange = arranges[j].arrange;
        else
            warn("%s is not a layout, using float", name);
        n++;
    }
    layouts = out;
    nlayouts = n;
}

static void applyrules(const struct scfg_block* b)
{
    const struct scfg_block* r;
    Rule* out;
    size_t i, n = 0;

    out = ecalloc(count(b, "rule") + 1, sizeof *out);
    for (i = 0; i < b->directives_len; i++) {
        if (strcmp(b->directives[i].name, "rule"))
            continue;
        r = &b->directives[i].children;
        out[n].id = getstr(r, "id", NULL);
        out[n].title = getstr(r, "title", NULL);
        out[n].tags = getuint(r, "tags", 0);
        out[n].isfloating = getbool(r, "isfloating", 0);
        out[n].opacity_focus = (float)getnum(r, "opacity_focus", 0);
        out[n].opacity_unfocus = (float)getnum(r, "opacity_unfocus", 0);
        out[n].monitor = (int)getnum(r, "monitor", -1);
        n++;
    }
    rules = out;
    nrules = n;
}

static void applymonitors(const struct scfg_block* b)
{
    const struct scfg_block* m;
    MonitorRule* out;
    size_t i, n = 0;

    if (!count(b, "monitor"))
        return;
    out = ecalloc(count(b, "monitor"), sizeof *out);
    for (i = 0; i < b->directives_len; i++) {
        if (strcmp(b->directives[i].name, "monitor"))
            continue;
        m = &b->directives[i].children;
        out[n].name = getstr(m, "name", NULL);
        out[n].mfact = (float)getnum(m, "mfact", 0.55);
        out[n].nmaster = (int)getnum(m, "nmaster", 1);
        out[n].scale = (float)getnum(m, "scale", 1);
        out[n].lt = layoutbysymbol(getstr(m, "layout", NULL));
        if (!out[n].lt)
            out[n].lt = layouts;
        out[n].rr =
            (enum wl_output_transform)getenum(m,
                                              "transform",
                                              transforms,
                                              WL_OUTPUT_TRANSFORM_NORMAL,
                                              "transform");
        out[n].x = (int)getnum(m, "x", -1);
        out[n].y = (int)getnum(m, "y", -1);
        out[n].width = (int)getnum(m, "width", 0);
        out[n].height = (int)getnum(m, "height", 0);
        out[n].refresh = (int)getnum(m, "refresh", 0);
        n++;
    }
    monrules = out;
    nmonrules = n;
}

/* back to one flat run of arguments per command, each NULL terminated */
static void applyautostart(const struct scfg_block* b)
{
    const struct scfg_directive* d;
    const char** out;
    size_t i, j, n = 0;

    for (i = 0; i < b->directives_len; i++)
        if (!strcmp(b->directives[i].name, "autostart"))
            n += b->directives[i].params_len + 1;
    out = ecalloc(n + 1, sizeof *out);

    n = 0;
    for (i = 0; i < b->directives_len; i++) {
        d = &b->directives[i];
        line = d->lineno;
        if (strcmp(d->name, "autostart") || !d->params_len)
            continue;
        for (j = 0; j < d->params_len; j++)
            out[n++] = d->params[j];
        out[n++] = NULL;
    }
    autostart = out;
}

static void applykeys(const struct scfg_block* b)
{
    const struct scfg_directive* d;
    const Action* act;
    Key* out;
    size_t i, n = 0;

    out = ecalloc(count(b, "key") + 1, sizeof *out);
    for (i = 0; i < b->directives_len; i++) {
        const char* name;
        xkb_keysym_t sym;

        d = &b->directives[i];
        line = d->lineno;
        if (strcmp(d->name, "key") || !(act = actionbyname(param(d, 1))))
            continue;
        name = splitmods(param(d, 0), &out[n].mod);
        if ((sym = xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS)) ==
            XKB_KEY_NoSymbol) {
            warn("%s is not a key", name);
            continue;
        }
        out[n].keysym = sym;
        out[n].func = act->func;
        out[n].arg = getarg(act->arg, d, 2);
        n++;
    }
    keys = out;
    nkeys = n;
}

static void applybuttons(const struct scfg_block* b)
{
    const struct scfg_directive* d;
    const Action* act;
    Button* out;
    size_t i, n = 0;

    out = ecalloc(count(b, "button") + 1, sizeof *out);
    for (i = 0; i < b->directives_len; i++) {
        d = &b->directives[i];
        line = d->lineno;
        if (strcmp(d->name, "button") || !(act = actionbyname(param(d, 2))))
            continue;
        out[n].click = (unsigned int)enumval(
            clickareas, param(d, 0), ClkClient, "click area");
        out[n].button =
            (unsigned int)enumval(mousebuttons,
                                  splitmods(param(d, 1), &out[n].mod),
                                  BTN_LEFT,
                                  "button");
        out[n].func = act->func;
        out[n].arg = getarg(act->arg, d, 3);
        n++;
    }
    buttons = out;
    nbuttons = n;
}

static void applygestures(const struct scfg_block* b)
{
    const struct scfg_directive* d;
    const Action* act;
    Gesture* out;
    size_t i, n = 0;

    out = ecalloc(count(b, "gesture") + 1, sizeof *out);
    for (i = 0; i < b->directives_len; i++) {
        d = &b->directives[i];
        line = d->lineno;
        if (strcmp(d->name, "gesture") || !(act = actionbyname(param(d, 2))))
            continue;
        out[n].motion = (unsigned int)enumval(
            gesturemotions, param(d, 0), SwipeLeft, "gesture motion");
        out[n].fingers = uintval(param(d, 1), 3);
        out[n].func = act->func;
        out[n].arg = getarg(act->arg, d, 3);
        n++;
    }
    gestures = out;
    ngestures = n;
}

static void applypadbuttons(const struct scfg_block* b)
{
    const struct scfg_directive* d;
    const Action* act;
    PadButton* out;
    size_t i, n = 0;

    out = ecalloc(count(b, "padbutton") + 1, sizeof *out);
    for (i = 0; i < b->directives_len; i++) {
        d = &b->directives[i];
        line = d->lineno;
        if (strcmp(d->name, "padbutton") || !(act = actionbyname(param(d, 1))))
            continue;
        out[n].button = uintval(param(d, 0), 0);
        out[n].func = act->func;
        out[n].arg = getarg(act->arg, d, 2);
        n++;
    }
    padbuttons = out;
    npadbuttons = n;
}

/* the layouts first: the rest names them by symbol */
static void apply(const struct scfg_block* b)
{
    applylayouts(b);
    applylook(section(b, "look"));
    applywindows(section(b, "windows"));
    applybar(section(b, "bar"));
    applyopacity(section(b, "opacity"));
    applyinput(section(b, "input"));
    applymisc(section(b, "misc"));
    applyrules(b);
    applymonitors(b);
    applyautostart(b);
    applykeys(b);
    applybuttons(b);
    applygestures(b);
    applypadbuttons(b);
}

static const char* settingspath(void)
{
    static char path[PATH_MAX];
    const char* dir = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");

    if (dir && *dir)
        snprintf(path, sizeof path, "%s/g0wm/config", dir);
    else if (home && *home)
        snprintf(path, sizeof path, "%s/.config/g0wm/config", home);
    else
        return NULL;
    return path;
}

/* mkdir -p, on a copy the caller owns */
static int mkdirp(char* path)
{
    char* p;

    for (p = path + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(path, 0755) < 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }
    return mkdir(path, 0755) < 0 && errno != EEXIST ? -1 : 0;
}

static int isrecord(const char* k)
{
    size_t i;

    for (i = 0; i < LENGTH(records); i++)
        if (!strcmp(records[i], k))
            return 1;
    return 0;
}

static const char* kindof(const struct scfg_directive* d)
{
    if (d->params_len != 1)
        return d->params_len ? "a list" : "off";
    if (isbool(d->params[0]))
        return "true or false";
    if (isnum(d->params[0], NULL))
        return "a number";
    return "a string";
}

/* off takes anything, strings and lists any words */
static int sameshape(const struct scfg_directive* want,
                     const struct scfg_directive* got)
{
    const char* k = kindof(want);

    if (!want->params_len)
        return 1;
    if (!strcmp(k, "a list") || !strcmp(k, "a string"))
        return got->params_len > 0;
    return !strcmp(k, kindof(got));
}

static void check(const struct scfg_block* want,
                  const struct scfg_block* got,
                  const char* path,
                  int* bad)
{
    const struct scfg_directive* w;
    const struct scfg_directive* g;
    char sub[256];
    size_t i;

    for (i = 0; i < want->directives_len; i++) {
        w = &want->directives[i];
        if (!*path && isrecord(w->name))
            continue;
        snprintf(sub, sizeof sub, "%s%s%s", path, *path ? "." : "", w->name);
        if (!(g = find(got, w->name))) {
            fprintf(stderr, "g0wm: config: %s is missing\n", sub);
            (*bad)++;
        } else if (w->children.directives_len) {
            check(&w->children, &g->children, sub, bad);
        } else if (!sameshape(w, g)) {
            line = g->lineno;
            warn("%s should be %s", sub, kindof(w));
            (*bad)++;
        }
    }
    /* a typo, or a setting this build was configured without */
    for (i = 0; i < got->directives_len; i++) {
        g = &got->directives[i];
        if (find(want, g->name) || (!*path && isrecord(g->name)))
            continue;
        snprintf(sub, sizeof sub, "%s%s%s", path, *path ? "." : "", g->name);
        line = g->lineno;
        warn("%s is not a setting this build has", sub);
        (*bad)++;
    }
}

/* -c: write the defaults when there are none, read back whatever is there,
 * then print what g0wm ends up with. Notes go to stderr, the settings to
 * stdout, so the two can be told apart. */
int settingswrite(void)
{
    const char* path = settingspath();
    char dir[PATH_MAX];
    char* slash;
    char* text;
    FILE* f;

    if (!path) {
        fprintf(stderr, "g0wm: neither XDG_CONFIG_HOME nor HOME is set\n");
        return 1;
    }

    /* never over the user's own file: a reinstall must not undo it */
    if (!access(path, F_OK)) {
        fprintf(stderr, "g0wm: reading the config in %s\n", path);
    } else {
        snprintf(dir, sizeof dir, "%s", path);
        if ((slash = strrchr(dir, '/')))
            *slash = '\0';
        if (mkdirp(dir) < 0) {
            fprintf(
                stderr, "g0wm: cannot create %s: %s\n", dir, strerror(errno));
            return 1;
        }
        if (!(text = rendered()))
            return 1;
        if (!(f = fopen(path, "w"))) {
            fprintf(
                stderr, "g0wm: cannot write %s: %s\n", path, strerror(errno));
            free(text);
            return 1;
        }
        fputs(text, f);
        free(text);
        if (fclose(f) != 0) {
            fprintf(
                stderr, "g0wm: cannot write %s: %s\n", path, strerror(errno));
            return 1;
        }
        fprintf(stderr, "g0wm: wrote %s\n", path);
    }

    settingsload();
    if (!(text = rendered()))
        return 1;
    fputs(text, stdout);
    free(text);
    return 0;
}

void settingsload(void)
{
    const char* path = settingspath();
    struct scfg_block* tree;
    struct scfg_block def = { 0 };
    char* text;
    FILE* f;
    int bad = 0;

    if (!path) {
        fprintf(stderr, "g0wm: neither XDG_CONFIG_HOME nor HOME is set\n");
        return;
    }
    if (access(path, F_OK) < 0) {
        fprintf(stderr,
                "g0wm: no settings in %s, run 'g0wm -c' to write them\n",
                path);
        return;
    }
    tree = ecalloc(1, sizeof *tree);
    if (scfg_load_file(tree, path) < 0) {
        fprintf(stderr, "g0wm: cannot read %s\n", path);
        scfg_block_finish(tree);
        free(tree);
        return;
    }

    /* checked against the defaults, parsed back */
    if ((text = rendered())) {
        if ((f = fmemopen(text, strlen(text), "r"))) {
            if (!scfg_parse_file(&def, f))
                check(&def, tree, "", &bad);
            fclose(f);
        }
        scfg_block_finish(&def);
        free(text);
    }
    /* never freed: the settings point into its strings */
    apply(tree);
    if (bad)
        fprintf(stderr,
                "g0wm: %s has %d problem%s; delete it and run 'g0wm -c' for a "
                "fresh one\n",
                path,
                bad,
                bad == 1 ? "" : "s");
}

/* the keymap and the repeat rate are otherwise only set when a keyboard
 * shows up; the trackpad settings are per device and still need a restart */
static void reloadkeyboard(void)
{
    struct xkb_context* context;
    struct xkb_keymap* keymap;

    if (!kb_group)
        return;
    if ((context = xkb_context_new(XKB_CONTEXT_NO_FLAGS))) {
        if ((keymap = xkb_keymap_new_from_names(
                 context, &xkb_rules, XKB_KEYMAP_COMPILE_NO_FLAGS))) {
            wlr_keyboard_set_keymap(&kb_group->wlr_group->keyboard, keymap);
            xkb_keymap_unref(keymap);
        }
        xkb_context_unref(context);
    }
    wlr_keyboard_set_repeat_info(
        &kb_group->wlr_group->keyboard, repeat_rate, repeat_delay);
}

void reloadsettings(const Arg* arg)
{
    Monitor* m;
    Client* c;

    settingsload();

    /* fewer tags than before would leave windows on bits nothing shows */
    wl_list_for_each(m, &mons, link)
    {
        if (!(m->tagset[m->seltags] & TAGMASK))
            m->tagset[m->seltags] = 1;
        wlr_scene_rect_set_color(m->fullscreen_bg, fullscreen_bg);
    }
    /* what a client copied out of the settings when it was mapped: its
     * border and its own opacity */
    wl_list_for_each(c, &clients, link)
    {
        if (!(c->tags & TAGMASK))
            c->tags = 1;
        c->opacity = c->opacity_unfocus = opacity_unfocus;
        c->opacity_focus = opacity_focus;
        if (!c->isfullscreen)
            c->bw = client_is_unmanaged(c) ? 0 : borderwidth();
    }
    wlr_scene_rect_set_color(root_bg, rootcolor);

    reloadkeyboard();
    tabletremap();
    reloadmons();
    updatemons(NULL, NULL);

    /* a floating client keeps its own geometry, so a new border or title bar
     * only reaches it through resize() */
    wl_list_for_each(c, &clients, link)
    {
        if (c->mon && c->isfloating && !c->isfullscreen)
            resize(c, c->geom, 1);
    }

    reloadopacity();
    notify_settimeout(notification_timeout);
    drawbars();
}
