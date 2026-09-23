/*
 * See LICENSE file for copyright and license details.
 *
 * ~/.config/g0wm/settings.json: written from config.h, read back over it
 */
#include <errno.h>
#include <limits.h>

#include "g0wm.h"
#include "util.h"

/* the only file that includes it: these are the definitions */
#include "config.h"

cJSON* settings;

typedef struct {
    int val;
    const char* name;
} Enum;

/* tables end on { 0, NULL }; a real zero value is listed before it */
static const Enum modnames[] = { { WLR_MODIFIER_SHIFT, "shift" },
                                 { WLR_MODIFIER_CAPS, "caps" },
                                 { WLR_MODIFIER_CTRL, "ctrl" },
                                 { WLR_MODIFIER_ALT, "alt" },
                                 { WLR_MODIFIER_MOD2, "mod2" },
                                 { WLR_MODIFIER_MOD3, "mod3" },
                                 { WLR_MODIFIER_LOGO, "logo" },
                                 { WLR_MODIFIER_MOD5, "mod5" },
                                 { 0, NULL } };

static const Enum dragmodes[] = { { CurMove, "move" },
                                  { CurResize, "resize" },
                                  { 0, NULL } };

static const Enum clickareas[] = {
    { ClkTagBar, "tagbar" }, { ClkLtSymbol, "ltsymbol" },
    { ClkStatus, "status" }, { ClkTitle, "title" },
    { ClkClient, "client" }, { ClkRoot, "root" },
    { ClkTray, "tray" },     { 0, NULL }
};

static const Enum mousebuttons[] = { { BTN_LEFT, "left" },
                                     { BTN_MIDDLE, "middle" },
                                     { BTN_RIGHT, "right" },
                                     { 0, NULL } };

static const Enum cornerstyles[] = { { CornerNormal, "normal" },
                                     { CornerSquircle, "squircle" },
                                     { 0, NULL } };

static const Enum exclusiontypes[] = { { 0, "only-listed" },
                                       { 1, "all-but-listed" },
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
} arranges[] = { { "tile", tile },
                 { "float", NULL },
                 { "monocle", monocle },
                 { "tabbed", tabbed } };

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
    { moveresize, "moveresize", 'c' },
    { movestack, "movestack", 'i' },
#ifdef NOTIFICATIONS
    { notifydismiss, "notifydismiss", 0 },
    { notifyopen, "notifyopen", 0 },
    { notifyscroll, "notifyscroll", 0 },
#endif
    { quit, "quit", 0 },
    { reloadsettings, "reloadsettings", 0 },
    { resizeheight, "resizeheight", 'i' },
    { resizewidth, "resizewidth", 'i' },
#ifdef RUNNER
    { runnertoggle, "runnertoggle", 0 },
#endif
    { setlayout, "setlayout", 'l' },
    { setmfact, "setmfact", 'f' },
    { setopacityfocus, "setopacityfocus", 'f' },
    { setopacityunfocus, "setopacityunfocus", 'f' },
    { spawn, "spawn", 'v' },
    { tag, "tag", 'u' },
    { tagmon, "tagmon", 'i' },
    { togglebar, "togglebar", 0 },
    { togglefloating, "togglefloating", 0 },
    { togglefullscreen, "togglefullscreen", 0 },
    { togglegaps, "togglegaps", 0 },
    { toggleopacity, "toggleopacity", 0 },
    { toggletabbed, "toggletabbed", 'l' },
    { toggletag, "toggletag", 'u' },
#ifdef TITLEBAR
    { toggletitlebar, "toggletitlebar", 0 },
#endif
    { toggleview, "toggleview", 'u' },
#ifdef SYSTRAY
    { trayactivate, "trayactivate", 0 },
    { traymenu, "traymenu", 0 },
#endif
    { view, "view", 'u' },
    { zoom, "zoom", 0 },
};

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
    fprintf(stderr, "g0wm: settings.json: %s is not a %s\n", name, what);
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
    fprintf(stderr, "g0wm: settings.json: %s is not an action\n", name);
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

static cJSON* jhex(uint32_t c)
{
    char buf[10];

    snprintf(buf, sizeof buf, "#%08x", c);
    return cJSON_CreateString(buf);
}

/* floats, so a double's digits would only be conversion noise */
static cJSON* jnum(double v)
{
    return cJSON_CreateNumber(round(v * 1e6) / 1e6);
}

/* NULL is off, not empty */
static cJSON* jstr(const char* s)
{
    return s ? cJSON_CreateString(s) : cJSON_CreateNull();
}

/* one execvp() argument list */
static cJSON* jargv(const char* const* v)
{
    cJSON* a = cJSON_CreateArray();

    for (; v && *v; v++)
        cJSON_AddItemToArray(a, cJSON_CreateString(*v));
    return a;
}

static cJSON* jmods(uint32_t mod)
{
    const Enum* e;
    cJSON* a = cJSON_CreateArray();

    for (e = modnames; e->name; e++)
        if (mod & (uint32_t)e->val)
            cJSON_AddItemToArray(a, cJSON_CreateString(e->name));
    return a;
}

static cJSON* jarg(char kind, const Arg* arg)
{
    switch (kind) {
        case 'i':
            return cJSON_CreateNumber(arg->i);
        case 'u':
            return cJSON_CreateNumber(arg->ui);
        case 'f':
            return jnum(arg->f);
        case 'v':
            return jargv(arg->v);
        case 'l':
            return arg->v ? cJSON_CreateString(((const Layout*)arg->v)->symbol)
                          : cJSON_CreateNull();
        case 'c':
            return cJSON_CreateString(enumname(dragmodes, (int)arg->ui));
    }
    return cJSON_CreateNull();
}

static cJSON* jcolors(void)
{
    /* the holes keep every name on its Scheme* index */
    static const char* const names[] = {
        [SchemeNorm] = "norm",     [SchemeSel] = "sel",
        [SchemeUrg] = "urg",
#ifdef TITLEBAR
        [SchemeTitle] = "title",   [SchemeTitleSel] = "titlesel",
#endif
        [SchemeStatus] = "status",
#ifdef NOTIFICATIONS
        [SchemeNotify] = "notify",
#endif
#ifdef RUNNER
        [SchemeRunner] = "runner", [SchemeRunnerSuggest] = "runnersuggest",
#endif
    };
    cJSON* o = cJSON_CreateObject();
    size_t i;

    for (i = 0; i < LENGTH(names); i++) {
        cJSON* s;
        if (!names[i])
            continue;
        s = cJSON_AddObjectToObject(o, names[i]);
        cJSON_AddItemToObject(s, "fg", jhex(colors[i][ColFg]));
        cJSON_AddItemToObject(s, "bg", jhex(colors[i][ColBg]));
        cJSON_AddItemToObject(s, "border", jhex(colors[i][ColBorder]));
    }
    return o;
}

static cJSON* jlook(void)
{
    cJSON* o = cJSON_CreateObject();
    cJSON* a;
    size_t i;

    a = cJSON_AddArrayToObject(o, "fonts");
    for (i = 0; i < nfonts; i++)
        cJSON_AddItemToArray(a, cJSON_CreateString(fonts[i]));

    cJSON_AddItemToObject(o, "colors", jcolors());
    cJSON_AddItemToObject(o, "rootcolor", jhex(packrgba(rootcolor)));
    cJSON_AddItemToObject(o, "fullscreen_bg", jhex(packrgba(fullscreen_bg)));
#ifdef INTEGRATED_BACKGROUND
    cJSON_AddStringToObject(o, "wallpaper", wallpaper);
#endif

    a = cJSON_AddArrayToObject(o, "tags");
    for (i = 0; i < ntags; i++)
        cJSON_AddItemToArray(a, cJSON_CreateString(tags[i]));
    return o;
}

static cJSON* jwindows(void)
{
    cJSON* o = cJSON_CreateObject();
    cJSON* a;
    size_t i;

    cJSON_AddNumberToObject(o, "borderpx", borderpx);
    cJSON_AddStringToObject(
        o, "cornerstyle", enumname(cornerstyles, cornerstyle));
    cJSON_AddNumberToObject(o, "cornerpx", cornerpx);
    cJSON_AddNumberToObject(o, "gaps", gaps);
    cJSON_AddNumberToObject(o, "gappx", gappx);
    cJSON_AddBoolToObject(o, "smartgaps", smartgaps);
#ifdef TITLEBAR
    cJSON_AddBoolToObject(o, "titlebar", titlebar);
    cJSON_AddNumberToObject(o, "titlepadding", titlepadding);
#endif

    a = cJSON_AddArrayToObject(o, "layouts");
    for (i = 0; i < nlayouts; i++) {
        cJSON* l = cJSON_CreateObject();
        cJSON_AddStringToObject(l, "symbol", layouts[i].symbol);
        cJSON_AddStringToObject(l, "arrange", arrangename(layouts[i].arrange));
        cJSON_AddItemToArray(a, l);
    }

    a = cJSON_AddArrayToObject(o, "rules");
    for (i = 0; i < nrules; i++) {
        cJSON* r = cJSON_CreateObject();
        cJSON_AddItemToObject(r, "id", jstr(rules[i].id));
        cJSON_AddItemToObject(r, "title", jstr(rules[i].title));
        cJSON_AddNumberToObject(r, "tags", rules[i].tags);
        cJSON_AddBoolToObject(r, "isfloating", rules[i].isfloating);
        cJSON_AddItemToObject(r, "opacity_focus", jnum(rules[i].opacity_focus));
        cJSON_AddItemToObject(
            r, "opacity_unfocus", jnum(rules[i].opacity_unfocus));
        cJSON_AddNumberToObject(r, "monitor", rules[i].monitor);
        cJSON_AddItemToArray(a, r);
    }
    return o;
}

static cJSON* jbar(void)
{
    cJSON* o = cJSON_CreateObject();

    cJSON_AddBoolToObject(o, "showbar", showbar);
    cJSON_AddBoolToObject(o, "topbar", topbar);
    cJSON_AddBoolToObject(o, "barwintitle", barwintitle);
    cJSON_AddNumberToObject(o, "barpadding", barpadding);
    cJSON_AddItemToObject(o, "barheight", jnum(barheight));
    cJSON_AddBoolToObject(o, "barsinglemon", barsinglemon);
#ifdef SYSTRAY
    cJSON_AddBoolToObject(o, "showsystray", showsystray);
    cJSON_AddNumberToObject(o, "systrayspacing", systrayspacing);
    cJSON_AddNumberToObject(o, "systraypadding", systraypadding);
    cJSON_AddNumberToObject(o, "systrayiconsize", systrayiconsize);
#endif
#ifdef NOTIFICATIONS
    cJSON_AddBoolToObject(o, "shownotifications", shownotifications);
    cJSON_AddNumberToObject(o, "notification_timeout", notification_timeout);
#endif
    return o;
}

static cJSON* jopacity(void)
{
    cJSON* o = cJSON_CreateObject();

    cJSON_AddBoolToObject(o, "opacity_enabled", opacity_enabled);
    cJSON_AddItemToObject(o, "opacity_focus", jnum(opacity_focus));
    cJSON_AddItemToObject(o, "opacity_unfocus", jnum(opacity_unfocus));
    cJSON_AddItemToObject(o, "opacity_deco", jnum(opacity_deco));
    cJSON_AddStringToObject(o,
                            "opacity_exclusion_type",
                            enumname(exclusiontypes, opacity_exclusion_type));
    cJSON_AddItemToObject(o, "opacity_apps", jargv(opacity_apps));
#ifdef INTEGRATED_BACKGROUND
    cJSON_AddStringToObject(
        o, "opacity_type", enumname(opacitykinds, opacity_type));
    cJSON_AddNumberToObject(o, "blur_radius", blur_radius);
    cJSON_AddNumberToObject(o, "blur_passes", blur_passes);
    cJSON_AddItemToObject(o, "blur_saturation", jnum(blur_saturation));
    cJSON_AddItemToObject(o, "blur_brightness", jnum(blur_brightness));
#endif
    return o;
}

static cJSON* jmonitors(void)
{
    cJSON* a = cJSON_CreateArray();
    size_t i;

    for (i = 0; i < nmonrules; i++) {
        cJSON* m = cJSON_CreateObject();
        cJSON_AddItemToObject(m, "name", jstr(monrules[i].name));
        cJSON_AddItemToObject(m, "mfact", jnum(monrules[i].mfact));
        cJSON_AddNumberToObject(m, "nmaster", monrules[i].nmaster);
        cJSON_AddItemToObject(m, "scale", jnum(monrules[i].scale));
        cJSON_AddItemToObject(
            m, "layout", jstr(monrules[i].lt ? monrules[i].lt->symbol : NULL));
        cJSON_AddStringToObject(
            m, "transform", enumname(transforms, (int)monrules[i].rr));
        cJSON_AddNumberToObject(m, "x", monrules[i].x);
        cJSON_AddNumberToObject(m, "y", monrules[i].y);
        cJSON_AddNumberToObject(m, "width", monrules[i].width);
        cJSON_AddNumberToObject(m, "height", monrules[i].height);
        cJSON_AddNumberToObject(m, "refresh", monrules[i].refresh);
        cJSON_AddItemToArray(a, m);
    }
    return a;
}

static cJSON* jinput(void)
{
    cJSON* o = cJSON_CreateObject();
    cJSON* x;

    cJSON_AddBoolToObject(o, "sloppyfocus", sloppyfocus);

    x = cJSON_AddObjectToObject(o, "xkb_rules");
    cJSON_AddItemToObject(x, "rules", jstr(xkb_rules.rules));
    cJSON_AddItemToObject(x, "model", jstr(xkb_rules.model));
    cJSON_AddItemToObject(x, "layout", jstr(xkb_rules.layout));
    cJSON_AddItemToObject(x, "variant", jstr(xkb_rules.variant));
    cJSON_AddItemToObject(x, "options", jstr(xkb_rules.options));

    cJSON_AddNumberToObject(o, "repeat_rate", repeat_rate);
    cJSON_AddNumberToObject(o, "repeat_delay", repeat_delay);
    cJSON_AddItemToObject(o, "cursor_theme", jstr(cursor_theme));
    cJSON_AddNumberToObject(o, "cursor_size", cursor_size);
    cJSON_AddBoolToObject(
        o, "hide_cursor_when_typing", hide_cursor_when_typing);
    cJSON_AddBoolToObject(o, "tap_to_click", tap_to_click);
    cJSON_AddBoolToObject(o, "tap_and_drag", tap_and_drag);
    cJSON_AddBoolToObject(o, "drag_lock", drag_lock);
    cJSON_AddBoolToObject(o, "natural_scrolling", natural_scrolling);
    cJSON_AddBoolToObject(o, "disable_while_typing", disable_while_typing);
    cJSON_AddBoolToObject(o, "left_handed", left_handed);
    cJSON_AddBoolToObject(
        o, "middle_button_emulation", middle_button_emulation);
    cJSON_AddStringToObject(
        o, "scroll_method", enumname(scrollmethods, scroll_method));
    cJSON_AddStringToObject(
        o, "click_method", enumname(clickmethods, click_method));
    cJSON_AddStringToObject(
        o, "send_events_mode", enumname(sendevents, (int)send_events_mode));
    cJSON_AddStringToObject(
        o, "accel_profile", enumname(accelprofiles, accel_profile));
    cJSON_AddItemToObject(o, "accel_speed", jnum(accel_speed));
    cJSON_AddStringToObject(o, "button_map", enumname(buttonmaps, button_map));
    return o;
}

/* one NULL terminated run of arguments per command */
static cJSON* jautostart(void)
{
    cJSON* a = cJSON_CreateArray();
    const char* const* p;

    for (p = autostart; *p; p++) {
        cJSON_AddItemToArray(a, jargv(p));
        while (*++p)
            ;
    }
    return a;
}

static cJSON* jkeys(void)
{
    cJSON* a = cJSON_CreateArray();
    size_t i;

    for (i = 0; i < nkeys; i++) {
        const Action* act = actionbyfunc(keys[i].func);
        char name[64];
        cJSON* k;

        if (!act) {
            fprintf(
                stderr, "g0wm: binding %zu calls an action with no name\n", i);
            continue;
        }
        if (xkb_keysym_get_name(keys[i].keysym, name, sizeof name) < 0)
            continue;

        k = cJSON_CreateObject();
        cJSON_AddItemToObject(k, "mod", jmods(keys[i].mod));
        cJSON_AddStringToObject(k, "key", name);
        cJSON_AddStringToObject(k, "action", act->name);
        cJSON_AddItemToObject(k, "arg", jarg(act->arg, &keys[i].arg));
        cJSON_AddItemToArray(a, k);
    }
    return a;
}

static cJSON* jbuttons(void)
{
    cJSON* a = cJSON_CreateArray();
    size_t i;

    for (i = 0; i < nbuttons; i++) {
        const Action* act = actionbyfunc(buttons[i].func);
        cJSON* b;

        if (!act) {
            fprintf(
                stderr, "g0wm: click %zu calls an action with no name\n", i);
            continue;
        }
        b = cJSON_CreateObject();
        cJSON_AddStringToObject(
            b, "click", enumname(clickareas, (int)buttons[i].click));
        cJSON_AddItemToObject(b, "mod", jmods(buttons[i].mod));
        cJSON_AddStringToObject(
            b, "button", enumname(mousebuttons, (int)buttons[i].button));
        cJSON_AddStringToObject(b, "action", act->name);
        cJSON_AddItemToObject(b, "arg", jarg(act->arg, &buttons[i].arg));
        cJSON_AddItemToArray(a, b);
    }
    return a;
}

static cJSON* jmisc(void)
{
    cJSON* o = cJSON_CreateObject();

    cJSON_AddStringToObject(o, "log_level", enumname(loglevels, log_level));
    cJSON_AddBoolToObject(
        o, "bypass_surface_visibility", bypass_surface_visibility);
    return o;
}

static cJSON* defaults(void)
{
    cJSON* o = cJSON_CreateObject();

    cJSON_AddItemToObject(o, "look", jlook());
    cJSON_AddItemToObject(o, "windows", jwindows());
    cJSON_AddItemToObject(o, "bar", jbar());
    cJSON_AddItemToObject(o, "opacity", jopacity());
    cJSON_AddItemToObject(o, "monitors", jmonitors());
    cJSON_AddItemToObject(o, "input", jinput());
    cJSON_AddItemToObject(o, "autostart", jautostart());
    cJSON_AddItemToObject(o, "keys", jkeys());
    cJSON_AddItemToObject(o, "buttons", jbuttons());
    cJSON_AddItemToObject(o, "misc", jmisc());
    return o;
}

static const cJSON* item(const cJSON* o, const char* k)
{
    return cJSON_GetObjectItemCaseSensitive(o, k);
}

/* the getters keep the value config.h gave when the file has nothing usable */
static int getbool(const cJSON* o, const char* k, int cur)
{
    const cJSON* v = item(o, k);

    return cJSON_IsBool(v) ? cJSON_IsTrue(v) : cur;
}

static double getnum(const cJSON* o, const char* k, double cur)
{
    const cJSON* v = item(o, k);

    return cJSON_IsNumber(v) ? v->valuedouble : cur;
}

static const char* getstr(const cJSON* o, const char* k, const char* cur)
{
    const cJSON* v = item(o, k);

    if (cJSON_IsString(v))
        return v->valuestring;
    return cJSON_IsNull(v) ? NULL : cur;
}

static int getenum(const cJSON* o,
                   const char* k,
                   const Enum* e,
                   int cur,
                   const char* what)
{
    const char* name = getstr(o, k, NULL);

    return name ? enumval(e, name, cur, what) : cur;
}

static uint32_t gethex(const cJSON* o, const char* k, uint32_t cur)
{
    const char* s = getstr(o, k, NULL);
    unsigned long v;
    char* end;

    if (!s)
        return cur;
    if (*s == '#' && strlen(s) == 9) {
        v = strtoul(s + 1, &end, 16);
        if (!*end)
            return (uint32_t)v;
    }
    fprintf(stderr, "g0wm: settings.json: %s is not an #rrggbbaa color\n", s);
    return cur;
}

static void getrgba(float c[static 4], const cJSON* o, const char* k)
{
    uint32_t v = gethex(o, k, packrgba(c));

    c[0] = ((v >> 24) & 0xFF) / 255.0f;
    c[1] = ((v >> 16) & 0xFF) / 255.0f;
    c[2] = ((v >> 8) & 0xFF) / 255.0f;
    c[3] = (v & 0xFF) / 255.0f;
}

/* the parsed file outlives the settings, so its strings are used in place.
 * One slot to spare: the result is NULL terminated as well as counted. */
static const char** strlist(const cJSON* a, size_t* n)
{
    const char** v;
    const cJSON* e;
    size_t i = 0;

    if (!cJSON_IsArray(a))
        return NULL;
    v = ecalloc((size_t)cJSON_GetArraySize(a) + 1, sizeof *v);
    cJSON_ArrayForEach(e, a) if (cJSON_IsString(e)) v[i++] = e->valuestring;
    if (n)
        *n = i;
    return v;
}

static uint32_t getmods(const cJSON* o, const char* k)
{
    const cJSON* e;
    uint32_t m = 0;

    cJSON_ArrayForEach(e, item(o, k)) if (cJSON_IsString(e)) m |=
        (uint32_t)enumval(modnames, e->valuestring, 0, "modifier");
    return m;
}

static const Layout* layoutbysymbol(const char* symbol)
{
    size_t i;

    if (!symbol)
        return NULL;
    for (i = 0; i < nlayouts; i++)
        if (!strcmp(layouts[i].symbol, symbol))
            return &layouts[i];
    fprintf(stderr, "g0wm: settings.json: no layout draws %s\n", symbol);
    return NULL;
}

static Arg getarg(char kind, const cJSON* o)
{
    const cJSON* v = item(o, "arg");
    Arg a = { 0 };

    switch (kind) {
        case 'i':
            a.i = (int)getnum(o, "arg", 0);
            break;
        case 'u':
            a.ui = (uint32_t)getnum(o, "arg", 0);
            break;
        case 'f':
            a.f = (float)getnum(o, "arg", 0);
            break;
        case 'v':
            a.v = strlist(v, NULL);
            break;
        case 'l':
            a.v = layoutbysymbol(getstr(o, "arg", NULL));
            break;
        case 'c':
            a.ui = (uint32_t)getenum(o, "arg", dragmodes, CurMove, "drag mode");
            break;
    }
    return a;
}

static void applycolors(const cJSON* o)
{
    const cJSON* s;
    size_t i;
    static const char* const names[] = {
        [SchemeNorm] = "norm",     [SchemeSel] = "sel",
        [SchemeUrg] = "urg",
#ifdef TITLEBAR
        [SchemeTitle] = "title",   [SchemeTitleSel] = "titlesel",
#endif
        [SchemeStatus] = "status",
#ifdef NOTIFICATIONS
        [SchemeNotify] = "notify",
#endif
#ifdef RUNNER
        [SchemeRunner] = "runner", [SchemeRunnerSuggest] = "runnersuggest",
#endif
    };

    for (i = 0; i < LENGTH(names); i++) {
        if (!names[i] || !(s = item(o, names[i])))
            continue;
        colors[i][ColFg] = gethex(s, "fg", colors[i][ColFg]);
        colors[i][ColBg] = gethex(s, "bg", colors[i][ColBg]);
        colors[i][ColBorder] = gethex(s, "border", colors[i][ColBorder]);
    }
}

static void applylook(const cJSON* o)
{
    const char** list;

    if (!o)
        return;
    if ((list = strlist(item(o, "fonts"), &nfonts)))
        fonts = list;
    applycolors(item(o, "colors"));
    getrgba(rootcolor, o, "rootcolor");
    getrgba(fullscreen_bg, o, "fullscreen_bg");
#ifdef INTEGRATED_BACKGROUND
    wallpaper = getstr(o, "wallpaper", wallpaper);
#endif
    if ((list = strlist(item(o, "tags"), &ntags))) {
        tags = (char**)list;
        if (ntags > 31) { /* TAGMASK has to fit in a uint32_t */
            fprintf(stderr,
                    "g0wm: settings.json: only the first 31 tags are"
                    " used\n");
            ntags = 31;
        }
    }
}

static void applylayouts(const cJSON* a)
{
    const cJSON* e;
    Layout* out;
    size_t i, n = 0;

    if (!cJSON_IsArray(a) || !cJSON_GetArraySize(a))
        return;
    out = ecalloc((size_t)cJSON_GetArraySize(a), sizeof *out);
    cJSON_ArrayForEach(e, a)
    {
        const char* name = getstr(e, "arrange", "float");
        out[n].symbol = getstr(e, "symbol", "[ ]");
        for (i = 0; i < LENGTH(arranges); i++)
            if (!strcmp(arranges[i].name, name))
                out[n].arrange = arranges[i].arrange;
        n++;
    }
    layouts = out;
    nlayouts = n;
}

static void applyrules(const cJSON* a)
{
    const cJSON* e;
    Rule* out;
    size_t n = 0;

    if (!cJSON_IsArray(a))
        return;
    out = ecalloc((size_t)cJSON_GetArraySize(a) + 1, sizeof *out);
    cJSON_ArrayForEach(e, a)
    {
        out[n].id = getstr(e, "id", NULL);
        out[n].title = getstr(e, "title", NULL);
        out[n].tags = (uint32_t)getnum(e, "tags", 0);
        out[n].isfloating = getbool(e, "isfloating", 0);
        out[n].opacity_focus = (float)getnum(e, "opacity_focus", 0);
        out[n].opacity_unfocus = (float)getnum(e, "opacity_unfocus", 0);
        out[n].monitor = (int)getnum(e, "monitor", -1);
        n++;
    }
    rules = out;
    nrules = n;
}

static void applywindows(const cJSON* o)
{
    if (!o)
        return;
    borderpx = (unsigned int)getnum(o, "borderpx", borderpx);
    cornerstyle =
        getenum(o, "cornerstyle", cornerstyles, cornerstyle, "corner style");
    cornerpx = (unsigned int)getnum(o, "cornerpx", cornerpx);
    gaps = (int)getnum(o, "gaps", gaps);
    gappx = (unsigned int)getnum(o, "gappx", gappx);
    smartgaps = getbool(o, "smartgaps", smartgaps);
#ifdef TITLEBAR
    titlebar = getbool(o, "titlebar", titlebar);
    titlepadding = (unsigned int)getnum(o, "titlepadding", titlepadding);
#endif
    applylayouts(item(o, "layouts"));
    applyrules(item(o, "rules"));
}

static void applybar(const cJSON* o)
{
    if (!o)
        return;
    showbar = getbool(o, "showbar", showbar);
    topbar = getbool(o, "topbar", topbar);
    barwintitle = getbool(o, "barwintitle", barwintitle);
    barpadding = (unsigned int)getnum(o, "barpadding", barpadding);
    barheight = (float)getnum(o, "barheight", barheight);
    barsinglemon = getbool(o, "barsinglemon", barsinglemon);
#ifdef SYSTRAY
    showsystray = getbool(o, "showsystray", showsystray);
    systrayspacing = (unsigned int)getnum(o, "systrayspacing", systrayspacing);
    systraypadding = (unsigned int)getnum(o, "systraypadding", systraypadding);
    systrayiconsize =
        (unsigned int)getnum(o, "systrayiconsize", systrayiconsize);
#endif
#ifdef NOTIFICATIONS
    shownotifications = getbool(o, "shownotifications", shownotifications);
    notification_timeout =
        (unsigned int)getnum(o, "notification_timeout", notification_timeout);
#endif
}

static void applyopacity(const cJSON* o)
{
    const char** list;

    if (!o)
        return;
    opacity_enabled = getbool(o, "opacity_enabled", opacity_enabled);
    opacity_focus = (float)getnum(o, "opacity_focus", opacity_focus);
    opacity_unfocus = (float)getnum(o, "opacity_unfocus", opacity_unfocus);
    opacity_deco = (float)getnum(o, "opacity_deco", opacity_deco);
    opacity_exclusion_type = getenum(o,
                                     "opacity_exclusion_type",
                                     exclusiontypes,
                                     opacity_exclusion_type,
                                     "kind of exclusion");
    if ((list = strlist(item(o, "opacity_apps"), NULL)))
        opacity_apps = list;
#ifdef INTEGRATED_BACKGROUND
    opacity_type = getenum(
        o, "opacity_type", opacitykinds, opacity_type, "kind of opacity");
    blur_radius = (unsigned int)getnum(o, "blur_radius", blur_radius);
    blur_passes = (unsigned int)getnum(o, "blur_passes", blur_passes);
    blur_saturation = (float)getnum(o, "blur_saturation", blur_saturation);
    blur_brightness = (float)getnum(o, "blur_brightness", blur_brightness);
#endif
}

static void applymonitors(const cJSON* a)
{
    const cJSON* e;
    MonitorRule* out;
    size_t n = 0;

    if (!cJSON_IsArray(a) || !cJSON_GetArraySize(a))
        return;
    out = ecalloc((size_t)cJSON_GetArraySize(a), sizeof *out);
    cJSON_ArrayForEach(e, a)
    {
        out[n].name = getstr(e, "name", NULL);
        out[n].mfact = (float)getnum(e, "mfact", 0.55);
        out[n].nmaster = (int)getnum(e, "nmaster", 1);
        out[n].scale = (float)getnum(e, "scale", 1);
        out[n].lt = layoutbysymbol(getstr(e, "layout", NULL));
        if (!out[n].lt)
            out[n].lt = layouts;
        out[n].rr =
            (enum wl_output_transform)getenum(e,
                                              "transform",
                                              transforms,
                                              WL_OUTPUT_TRANSFORM_NORMAL,
                                              "transform");
        out[n].x = (int)getnum(e, "x", -1);
        out[n].y = (int)getnum(e, "y", -1);
        out[n].width = (int)getnum(e, "width", 0);
        out[n].height = (int)getnum(e, "height", 0);
        out[n].refresh = (int)getnum(e, "refresh", 0);
        n++;
    }
    monrules = out;
    nmonrules = n;
}

static void applyinput(const cJSON* o)
{
    const cJSON* x;

    if (!o)
        return;
    sloppyfocus = getbool(o, "sloppyfocus", sloppyfocus);
    if ((x = item(o, "xkb_rules"))) {
        xkb_rules.rules = getstr(x, "rules", xkb_rules.rules);
        xkb_rules.model = getstr(x, "model", xkb_rules.model);
        xkb_rules.layout = getstr(x, "layout", xkb_rules.layout);
        xkb_rules.variant = getstr(x, "variant", xkb_rules.variant);
        xkb_rules.options = getstr(x, "options", xkb_rules.options);
    }
    repeat_rate = (int)getnum(o, "repeat_rate", repeat_rate);
    repeat_delay = (int)getnum(o, "repeat_delay", repeat_delay);
    cursor_theme = getstr(o, "cursor_theme", cursor_theme);
    cursor_size = (int)getnum(o, "cursor_size", cursor_size);
    hide_cursor_when_typing =
        getbool(o, "hide_cursor_when_typing", hide_cursor_when_typing);
    tap_to_click = getbool(o, "tap_to_click", tap_to_click);
    tap_and_drag = getbool(o, "tap_and_drag", tap_and_drag);
    drag_lock = getbool(o, "drag_lock", drag_lock);
    natural_scrolling = getbool(o, "natural_scrolling", natural_scrolling);
    disable_while_typing =
        getbool(o, "disable_while_typing", disable_while_typing);
    left_handed = getbool(o, "left_handed", left_handed);
    middle_button_emulation =
        getbool(o, "middle_button_emulation", middle_button_emulation);
    scroll_method = (enum libinput_config_scroll_method)getenum(
        o, "scroll_method", scrollmethods, scroll_method, "scroll method");
    click_method = (enum libinput_config_click_method)getenum(
        o, "click_method", clickmethods, click_method, "click method");
    send_events_mode = (uint32_t)getenum(o,
                                         "send_events_mode",
                                         sendevents,
                                         (int)send_events_mode,
                                         "send events mode");
    accel_profile =
        (enum libinput_config_accel_profile)getenum(o,
                                                    "accel_profile",
                                                    accelprofiles,
                                                    accel_profile,
                                                    "acceleration profile");
    accel_speed = getnum(o, "accel_speed", accel_speed);
    button_map = (enum libinput_config_tap_button_map)getenum(
        o, "button_map", buttonmaps, button_map, "button map");
}

/* back to one flat run of arguments per command, each NULL terminated */
static void applyautostart(const cJSON* a)
{
    const cJSON* cmd;
    const cJSON* arg;
    const char** out;
    size_t n = 0;

    if (!cJSON_IsArray(a))
        return;
    cJSON_ArrayForEach(cmd, a) n += (size_t)cJSON_GetArraySize(cmd) + 1;
    out = ecalloc(n + 1, sizeof *out);

    n = 0;
    cJSON_ArrayForEach(cmd, a)
    {
        size_t start = n;
        cJSON_ArrayForEach(arg, cmd) if (cJSON_IsString(arg)) out[n++] =
            arg->valuestring;
        if (n > start)
            out[n++] = NULL;
    }
    autostart = out;
}

static void applykeys(const cJSON* a)
{
    const cJSON* e;
    Key* out;
    size_t n = 0;

    if (!cJSON_IsArray(a))
        return;
    out = ecalloc((size_t)cJSON_GetArraySize(a) + 1, sizeof *out);
    cJSON_ArrayForEach(e, a)
    {
        const Action* act = actionbyname(getstr(e, "action", NULL));
        const char* name = getstr(e, "key", NULL);
        xkb_keysym_t sym;

        if (!act || !name)
            continue;
        if ((sym = xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS)) ==
            XKB_KEY_NoSymbol) {
            fprintf(stderr, "g0wm: settings.json: %s is not a key\n", name);
            continue;
        }
        out[n].mod = getmods(e, "mod");
        out[n].keysym = sym;
        out[n].func = act->func;
        out[n].arg = getarg(act->arg, e);
        n++;
    }
    keys = out;
    nkeys = n;
}

static void applybuttons(const cJSON* a)
{
    const cJSON* e;
    Button* out;
    size_t n = 0;

    if (!cJSON_IsArray(a))
        return;
    out = ecalloc((size_t)cJSON_GetArraySize(a) + 1, sizeof *out);
    cJSON_ArrayForEach(e, a)
    {
        const Action* act = actionbyname(getstr(e, "action", NULL));

        if (!act)
            continue;
        out[n].click = (unsigned int)getenum(
            e, "click", clickareas, ClkClient, "click area");
        out[n].mod = getmods(e, "mod");
        out[n].button = (unsigned int)getenum(
            e, "button", mousebuttons, BTN_LEFT, "button");
        out[n].func = act->func;
        out[n].arg = getarg(act->arg, e);
        n++;
    }
    buttons = out;
    nbuttons = n;
}

static void applymisc(const cJSON* o)
{
    if (!o)
        return;
    log_level = getenum(o, "log_level", loglevels, log_level, "log level");
    bypass_surface_visibility =
        getbool(o, "bypass_surface_visibility", bypass_surface_visibility);
}

/* windows first: the layouts it builds are what the rest names by symbol */
static void apply(const cJSON* o)
{
    applylook(item(o, "look"));
    applywindows(item(o, "windows"));
    applybar(item(o, "bar"));
    applyopacity(item(o, "opacity"));
    applymonitors(item(o, "monitors"));
    applyinput(item(o, "input"));
    applyautostart(item(o, "autostart"));
    applykeys(item(o, "keys"));
    applybuttons(item(o, "buttons"));
    applymisc(item(o, "misc"));
}

static const char* settingspath(void)
{
    static char path[PATH_MAX];
    const char* dir = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");

    if (dir && *dir)
        snprintf(path, sizeof path, "%s/g0wm/settings.json", dir);
    else if (home && *home)
        snprintf(path, sizeof path, "%s/.config/g0wm/settings.json", home);
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

/* whole file into one buffer */
static char* slurp(const char* path)
{
    FILE* f = fopen(path, "r");
    char* text;
    long len;
    size_t got;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) < 0 || (len = ftell(f)) < 0) {
        fclose(f);
        fprintf(stderr, "g0wm: %s is not a regular file\n", path);
        return NULL;
    }
    rewind(f);
    text = ecalloc(1, (size_t)len + 1);
    got = fread(text, 1, (size_t)len, f);
    fclose(f);
    text[got] = '\0';
    return text;
}

static const char* jtypename(const cJSON* v)
{
    if (cJSON_IsBool(v))
        return "true or false";
    if (cJSON_IsNumber(v))
        return "a number";
    if (cJSON_IsString(v))
        return "a string";
    if (cJSON_IsArray(v))
        return "a list";
    if (cJSON_IsObject(v))
        return "a section";
    return "something else";
}

/* a null default is off by default, so the file may hold anything */
static int sametype(const cJSON* want, const cJSON* got)
{
    if (cJSON_IsNull(want))
        return 1;
    if (cJSON_IsBool(want))
        return cJSON_IsBool(got);
    return (want->type & 0xFF) == (got->type & 0xFF);
}

static void checkobj(const cJSON* want,
                     const cJSON* got,
                     const char* path,
                     int* bad);

static void checkitem(const cJSON* want,
                      const cJSON* got,
                      const char* path,
                      int* bad)
{
    if (!sametype(want, got)) {
        fprintf(stderr,
                "g0wm: settings.json: %s should be %s\n",
                path,
                jtypename(want));
        (*bad)++;
        return;
    }
    /* how long a list is, is the user's business */
    if (cJSON_IsObject(want))
        checkobj(want, got, path, bad);
}

static void checkobj(const cJSON* want,
                     const cJSON* got,
                     const char* path,
                     int* bad)
{
    const cJSON* w;
    const cJSON* g;
    char sub[256];

    cJSON_ArrayForEach(w, want)
    {
        snprintf(sub, sizeof sub, "%s%s%s", path, *path ? "." : "", w->string);
        g = cJSON_GetObjectItemCaseSensitive(got, w->string);
        if (!g) {
            fprintf(stderr, "g0wm: settings.json: %s is missing\n", sub);
            (*bad)++;
            continue;
        }
        checkitem(w, g, sub, bad);
    }
    /* a typo, or a setting this build was configured without */
    cJSON_ArrayForEach(g, got)
    {
        if (cJSON_GetObjectItemCaseSensitive(want, g->string))
            continue;
        snprintf(sub, sizeof sub, "%s%s%s", path, *path ? "." : "", g->string);
        fprintf(stderr,
                "g0wm: settings.json: %s is not a setting this build has\n",
                sub);
        (*bad)++;
    }
}

/* the defaults, rendered; NULL and a message when they cannot be */
static char* rendered(void)
{
    cJSON* def = defaults();
    char* text = cJSON_Print(def);

    cJSON_Delete(def);
    if (!text)
        fprintf(stderr, "g0wm: cannot render the settings\n");
    return text;
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
        fprintf(stderr, "g0wm: reading the settings.json in %s\n", path);
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
        fprintf(f, "%s\n", text);
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
    puts(text);
    free(text);
    return 0;
}

void settingsload(void)
{
    const char* path = settingspath();
    cJSON* tree;
    cJSON* def;
    char* text;
    int bad = 0;

    if (!path) {
        fprintf(stderr, "g0wm: neither XDG_CONFIG_HOME nor HOME is set\n");
        return;
    }
    if (!(text = slurp(path))) {
        fprintf(stderr,
                "g0wm: no settings in %s, run 'g0wm -c' to write them\n",
                path);
        return;
    }

    tree = cJSON_Parse(text);
    if (!tree) {
        /* the error points into text, so print before freeing it */
        const char* at = cJSON_GetErrorPtr();
        fprintf(stderr,
                "g0wm: %s is not valid JSON, near: %.40s\n",
                path,
                at ? at : "");
        free(text);
        return;
    }
    free(text);

    def = defaults();
    checkobj(def, tree, "", &bad);
    cJSON_Delete(def);
    apply(tree);
    /* the tree a reload replaces is left behind: the settings point into
     * its strings */
    settings = tree;
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
    /* what a client copied out of the settings when it was mapped: the
     * filter it was measured against, its border and its own opacity */
    wl_list_for_each(c, &clients, link)
    {
        if (!(c->tags & TAGMASK))
            c->tags = 1;
        c->hasopacity = opacityallowed(client_get_appid(c));
        c->opacity = c->opacity_unfocus = opacity_unfocus;
        c->opacity_focus = opacity_focus;
        if (!c->isfullscreen)
            c->bw = client_is_unmanaged(c) ? 0 : borderwidth();
    }
    wlr_scene_rect_set_color(root_bg, rootcolor);

    reloadkeyboard();
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
    drawbars();
}
