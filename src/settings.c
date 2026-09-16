/*
 * See LICENSE file for copyright and license details.
 *
 * ~/.config/g0wm/settings.json, written from config.h and checked at startup
 */
#include <errno.h>
#include <limits.h>

#include "g0wm.h"
#include "util.h"

cJSON* settings;

typedef struct {
    int val;
    const char* name;
} Enum;

/* tables end on { 0, NULL }; a real zero value is listed before it */
static const char* enumname(const Enum* e, int val)
{
    for (; e->name; e++)
        if (e->val == val)
            return e->name;
    return "unknown";
}

static cJSON* jhex(uint32_t c)
{
    char buf[10];

    snprintf(buf, sizeof buf, "#%08x", c);
    return cJSON_CreateString(buf);
}

/* the float[4] wlroots wants, back to 0xRRGGBBAA */
static cJSON* jhexf(const float c[static 4])
{
    return jhex(((uint32_t)(c[0] * 255 + 0.5f) << 24) |
                ((uint32_t)(c[1] * 255 + 0.5f) << 16) |
                ((uint32_t)(c[2] * 255 + 0.5f) << 8) |
                (uint32_t)(c[3] * 255 + 0.5f));
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
    static const Enum mods[] = { { WLR_MODIFIER_SHIFT, "shift" },
                                 { WLR_MODIFIER_CAPS, "caps" },
                                 { WLR_MODIFIER_CTRL, "ctrl" },
                                 { WLR_MODIFIER_ALT, "alt" },
                                 { WLR_MODIFIER_MOD2, "mod2" },
                                 { WLR_MODIFIER_MOD3, "mod3" },
                                 { WLR_MODIFIER_LOGO, "logo" },
                                 { WLR_MODIFIER_MOD5, "mod5" },
                                 { 0, NULL } };
    const Enum* e;
    cJSON* a = cJSON_CreateArray();

    for (e = mods; e->name; e++)
        if (mod & (uint32_t)e->val)
            cJSON_AddItemToArray(a, cJSON_CreateString(e->name));
    return a;
}

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
    { notifyclick, "notifyclick", 0 },
    { notifydismiss, "notifydismiss", 0 },
#endif
    { quit, "quit", 0 },
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

static const Action* action(void (*func)(const Arg*))
{
    size_t i;

    for (i = 0; i < LENGTH(actions); i++)
        if (actions[i].func == func)
            return &actions[i];
    return NULL;
}

static cJSON* jarg(char kind, const Arg* arg)
{
    static const Enum drag[] = { { CurMove, "move" },
                                 { CurResize, "resize" },
                                 { 0, NULL } };

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
            return cJSON_CreateString(enumname(drag, (int)arg->ui));
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
    for (i = 0; i < LENGTH(fonts); i++)
        cJSON_AddItemToArray(a, cJSON_CreateString(fonts[i]));

    cJSON_AddItemToObject(o, "colors", jcolors());
    cJSON_AddItemToObject(o, "rootcolor", jhexf(rootcolor));
    cJSON_AddItemToObject(o, "fullscreen_bg", jhexf(fullscreen_bg));
#ifdef INTEGRATED_BACKGROUND
    cJSON_AddStringToObject(o, "wallpaper", wallpaper);
#endif

    a = cJSON_AddArrayToObject(o, "tags");
    for (i = 0; i < LENGTH(tags); i++)
        cJSON_AddItemToArray(a, cJSON_CreateString(tags[i]));
    return o;
}

static cJSON* jwindows(void)
{
    static const Enum corners[] = { { CornerNormal, "normal" },
                                    { CornerSquircle, "squircle" },
                                    { 0, NULL } };
    cJSON* o = cJSON_CreateObject();
    cJSON* a;
    size_t i;

    cJSON_AddNumberToObject(o, "borderpx", borderpx);
    cJSON_AddStringToObject(o, "cornerstyle", enumname(corners, cornerstyle));
    cJSON_AddNumberToObject(o, "cornerpx", cornerpx);
    cJSON_AddNumberToObject(o, "gaps", gaps);
    cJSON_AddNumberToObject(o, "gappx", gappx);
    cJSON_AddBoolToObject(o, "smartgaps", smartgaps);
#ifdef TITLEBAR
    cJSON_AddBoolToObject(o, "titlebar", titlebar);
    cJSON_AddNumberToObject(o, "titlepadding", titlepadding);
#endif

    a = cJSON_AddArrayToObject(o, "layouts");
    for (i = 0; i < LENGTH(layouts); i++) {
        cJSON* l = cJSON_CreateObject();
        static const char* const arrange[] = {
            "tile", "float", "monocle", "tabbed"
        };
        cJSON_AddStringToObject(l, "symbol", layouts[i].symbol);
        cJSON_AddStringToObject(
            l, "arrange", i < LENGTH(arrange) ? arrange[i] : "float");
        cJSON_AddItemToArray(a, l);
    }

    a = cJSON_AddArrayToObject(o, "rules");
    for (i = 0; i < LENGTH(rules); i++) {
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
    static const Enum exclusion[] = { { 0, "only-listed" },
                                      { 1, "all-but-listed" },
                                      { 0, NULL } };
#ifdef INTEGRATED_BACKGROUND
    static const Enum kinds[] = { { OpacityNormal, "normal" },
                                  { OpacityBlur, "blur" },
                                  { 0, NULL } };
#endif
    cJSON* o = cJSON_CreateObject();
    cJSON* a;
    const char* const* app;

    cJSON_AddBoolToObject(o, "opacity_enabled", opacity_enabled);
    cJSON_AddItemToObject(o, "opacity_focus", jnum(opacity_focus));
    cJSON_AddItemToObject(o, "opacity_unfocus", jnum(opacity_unfocus));
    cJSON_AddItemToObject(o, "opacity_deco", jnum(opacity_deco));
    cJSON_AddStringToObject(o,
                            "opacity_exclusion_type",
                            enumname(exclusion, opacity_exclusion_type));

    a = cJSON_AddArrayToObject(o, "opacity_apps");
    for (app = opacity_apps; *app; app++)
        cJSON_AddItemToArray(a, cJSON_CreateString(*app));

#ifdef INTEGRATED_BACKGROUND
    cJSON_AddStringToObject(o, "opacity_type", enumname(kinds, opacity_type));
    cJSON_AddNumberToObject(o, "blur_radius", blur_radius);
    cJSON_AddNumberToObject(o, "blur_passes", blur_passes);
    cJSON_AddItemToObject(o, "blur_saturation", jnum(blur_saturation));
    cJSON_AddItemToObject(o, "blur_brightness", jnum(blur_brightness));
#endif
    return o;
}

static cJSON* jmonitors(void)
{
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
    cJSON* a = cJSON_CreateArray();
    size_t i;

    for (i = 0; i < LENGTH(monrules); i++) {
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
    static const Enum scrolls[] = {
        { LIBINPUT_CONFIG_SCROLL_NO_SCROLL, "none" },
        { LIBINPUT_CONFIG_SCROLL_2FG, "2fg" },
        { LIBINPUT_CONFIG_SCROLL_EDGE, "edge" },
        { LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN, "on-button-down" },
        { 0, NULL }
    };
    static const Enum clicks[] = {
        { LIBINPUT_CONFIG_CLICK_METHOD_NONE, "none" },
        { LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS, "button-areas" },
        { LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER, "clickfinger" },
        { 0, NULL }
    };
    static const Enum sends[] = {
        { LIBINPUT_CONFIG_SEND_EVENTS_ENABLED, "enabled" },
        { LIBINPUT_CONFIG_SEND_EVENTS_DISABLED, "disabled" },
        { LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE,
          "disabled-on-external-mouse" },
        { 0, NULL }
    };
    static const Enum accels[] = {
        { LIBINPUT_CONFIG_ACCEL_PROFILE_NONE, "none" },
        { LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT, "flat" },
        { LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE, "adaptive" },
        { 0, NULL }
    };
    static const Enum maps[] = { { LIBINPUT_CONFIG_TAP_MAP_LRM, "lrm" },
                                 { LIBINPUT_CONFIG_TAP_MAP_LMR, "lmr" },
                                 { 0, NULL } };
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
        o, "scroll_method", enumname(scrolls, scroll_method));
    cJSON_AddStringToObject(o, "click_method", enumname(clicks, click_method));
    cJSON_AddStringToObject(
        o, "send_events_mode", enumname(sends, (int)send_events_mode));
    cJSON_AddStringToObject(
        o, "accel_profile", enumname(accels, accel_profile));
    cJSON_AddItemToObject(o, "accel_speed", jnum(accel_speed));
    cJSON_AddStringToObject(o, "button_map", enumname(maps, button_map));
    return o;
}

static cJSON* jprograms(void)
{
    cJSON* o = cJSON_CreateObject();

    cJSON_AddItemToObject(o, "termcmd", jargv(termcmd));
    cJSON_AddItemToObject(o, "filemanagercmd", jargv(filemanagercmd));
    cJSON_AddItemToObject(o, "browsercmd", jargv(browsercmd));
#ifndef RUNNER
    cJSON_AddItemToObject(o, "menucmd", jargv(menucmd));
#endif
#ifdef SYSTRAY
    cJSON_AddItemToObject(o, "traymenucmd", jargv(traymenucmd));
#endif
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

    for (i = 0; i < LENGTH(keys); i++) {
        const Action* act = action(keys[i].func);
        char name[64];
        cJSON* k;

        if (!act) /* config.h added a binding actions[] does not list */
            continue;
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
    static const Enum where[] = {
        { ClkTagBar, "tagbar" }, { ClkLtSymbol, "ltsymbol" },
        { ClkStatus, "status" }, { ClkTitle, "title" },
        { ClkClient, "client" }, { ClkRoot, "root" },
        { ClkTray, "tray" },     { 0, NULL }
    };
    static const Enum btns[] = { { BTN_LEFT, "left" },
                                 { BTN_MIDDLE, "middle" },
                                 { BTN_RIGHT, "right" },
                                 { 0, NULL } };
    cJSON* a = cJSON_CreateArray();
    size_t i;

    for (i = 0; i < LENGTH(buttons); i++) {
        const Action* act = action(buttons[i].func);
        cJSON* b;

        if (!act)
            continue;
        b = cJSON_CreateObject();
        cJSON_AddStringToObject(
            b, "click", enumname(where, (int)buttons[i].click));
        cJSON_AddItemToObject(b, "mod", jmods(buttons[i].mod));
        cJSON_AddStringToObject(
            b, "button", enumname(btns, (int)buttons[i].button));
        cJSON_AddStringToObject(b, "action", act->name);
        cJSON_AddItemToObject(b, "arg", jarg(act->arg, &buttons[i].arg));
        cJSON_AddItemToArray(a, b);
    }
    return a;
}

static cJSON* jmisc(void)
{
    static const Enum levels[] = { { WLR_SILENT, "silent" },
                                   { WLR_ERROR, "error" },
                                   { WLR_INFO, "info" },
                                   { WLR_DEBUG, "debug" },
                                   { 0, NULL } };
    cJSON* o = cJSON_CreateObject();

    cJSON_AddStringToObject(o, "log_level", enumname(levels, log_level));
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
    cJSON_AddItemToObject(o, "programs", jprograms());
    cJSON_AddItemToObject(o, "autostart", jautostart());
    cJSON_AddItemToObject(o, "keys", jkeys());
    cJSON_AddItemToObject(o, "buttons", jbuttons());
    cJSON_AddItemToObject(o, "misc", jmisc());
    return o;
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

/* -c: write the defaults when there are none, else check what is there */
int settingswrite(void)
{
    const char* path = settingspath();
    char dir[PATH_MAX];
    char* slash;
    char* text;
    cJSON* def;
    FILE* f;

    if (!path) {
        fprintf(stderr, "g0wm: neither XDG_CONFIG_HOME nor HOME is set\n");
        return 1;
    }
    /* never over the user's own file: a reinstall must not undo it */
    if (!access(path, F_OK)) {
        printf("g0wm: checking the settings.json already in %s\n", path);
        settingsload();
        cJSON_Delete(settings);
        settings = NULL;
        return 0;
    }

    snprintf(dir, sizeof dir, "%s", path);
    if ((slash = strrchr(dir, '/')))
        *slash = '\0';
    if (mkdirp(dir) < 0) {
        fprintf(stderr, "g0wm: cannot create %s: %s\n", dir, strerror(errno));
        return 1;
    }

    def = defaults();
    text = cJSON_Print(def);
    cJSON_Delete(def);
    if (!text) {
        fprintf(stderr, "g0wm: cannot render the default settings\n");
        return 1;
    }

    if (!(f = fopen(path, "w"))) {
        fprintf(stderr, "g0wm: cannot write %s: %s\n", path, strerror(errno));
        free(text);
        return 1;
    }
    fprintf(f, "%s\n", text);
    free(text);
    if (fclose(f) != 0) {
        fprintf(stderr, "g0wm: cannot write %s: %s\n", path, strerror(errno));
        return 1;
    }
    printf("g0wm: wrote %s\n", path);
    return 0;
}

void settingsload(void)
{
    const char* path = settingspath();
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

    settings = cJSON_Parse(text);
    if (!settings) {
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
    checkobj(def, settings, "", &bad);
    cJSON_Delete(def);
    if (bad)
        fprintf(stderr,
                "g0wm: %s has %d problem%s; delete it and run 'g0wm -c' for a "
                "fresh one\n",
                path,
                bad,
                bad == 1 ? "" : "s");
}
