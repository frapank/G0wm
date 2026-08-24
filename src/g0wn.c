/*
 * See LICENSE file for copyright and license details.
 */
#include <fcntl.h>
#ifdef INTEGRATED_BACKGROUND
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif
#ifdef RUNNER
#include <dirent.h>
#include <sys/stat.h>
#endif
#include <getopt.h>
#include <libdrm/drm_fourcc.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <xkbcommon/xkbcommon.h>
#ifdef XWAYLAND
#include <wlr/xwayland.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#endif

#include "dbus.h"
#include "drwl.h"
#ifdef NOTIFICATIONS
#include "notify.h"
#endif
#ifdef SYSTRAY
#include "systray/tray.h"
#include "systray/watcher.h"
#endif
#include "util.h"
#include "xdg-shell-protocol.h"

/* macros */
#ifndef MAX
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#endif
#ifndef MIN
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#endif
#define CLEANMASK(mask) (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEON(C, M)                                                        \
    ((M) && (C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define LENGTH(X) (sizeof X / sizeof X[0])
#define OPACITY_MIN 0.1f /* floor for the opacity keybindings */
#define END(A) ((A) + LENGTH(A))
#define TAGMASK ((1u << LENGTH(tags)) - 1)
#define LISTEN(E, L, H) wl_signal_add((E), ((L)->notify = (H), (L)))
#define LISTEN_STATIC(E, H)                                                    \
    do {                                                                       \
        struct wl_listener* _l = ecalloc(1, sizeof(*_l));                      \
        _l->notify = (H);                                                      \
        wl_signal_add((E), _l);                                                \
    } while (0)
#define TEXTW(mon, text) (drwl_font_getwidth(mon->drw, text) + mon->lrpad)
/* the status box: drawbar() and buttonpress() agree on it to the pixel */
#define STATUSW(mon) (drawstatus((mon), stext, 0, 0, 0) + 2)

/* enums */
enum {
    SchemeNorm,
    SchemeSel,
    SchemeUrg,
    SchemeTitle,
    SchemeTitleSel,
    SchemeNotify,
    SchemeRunner,
    SchemeRunnerSuggest
}; /* color schemes */
enum { CurNormal, CurPressed, CurMove, CurResize }; /* cursor */
enum { OpacityNormal, OpacityBlur };                /* opacity_type */
enum { XDGShell, LayerShell, X11 };                 /* client types */
enum {
    LyrBg,
    LyrBottom,
    LyrTile,
    LyrFloat,
    LyrTop,
    LyrFS,
    LyrOverlay,
    LyrBlock,
    NUM_LAYERS
}; /* scene layers */
enum {
    ClkTagBar,
    ClkLtSymbol,
    ClkStatus,
    ClkTitle,
    ClkClient,
    ClkRoot,
    ClkTray
}; /* clicks */

typedef union {
    int i;
    uint32_t ui;
    float f;
    const void* v;
} Arg;

typedef struct {
    unsigned int click;
    unsigned int mod;
    unsigned int button;
    void (*func)(const Arg*);
    const Arg arg;
} Button;

typedef struct {
    struct wlr_buffer base;
    struct wl_listener release;
    bool busy;
    Img* image;
    uint32_t data[];
} Buffer;

typedef struct Monitor Monitor;
typedef struct {
    /* Must keep this field first */
    unsigned int type; /* XDGShell or X11* */

    Monitor* mon;
    struct wlr_scene_tree* scene;
    struct wlr_scene_rect* border[4]; /* top, bottom, left, right */
    struct wlr_scene_tree* scene_surface;
    struct wl_list link;
    struct wl_list flink;
    struct wlr_box geom;   /* layout-relative, includes border */
    struct wlr_box prev;   /* layout-relative, includes border */
    struct wlr_box bounds; /* only width and height are used */
    union {
        struct wlr_xdg_surface* xdg;
        struct wlr_xwayland_surface* xwayland;
    } surface;
    struct wlr_xdg_toplevel_decoration_v1* decoration;
    struct wl_listener commit;
    struct wl_listener map;
    struct wl_listener maximize;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener set_title;
    struct wl_listener fullscreen;
    struct wl_listener set_decoration_mode;
    struct wl_listener destroy_decoration;
#ifdef XWAYLAND
    struct wl_listener activate;
    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener configure;
    struct wl_listener set_hints;
#endif
    struct wlr_scene_buffer* title;
#ifdef INTEGRATED_BACKGROUND
    struct wlr_scene_buffer* blur; /* frosted wallpaper, below the whole box */
    struct wlr_buffer* blurbuf;    /* what blur's node was last handed */
    struct wlr_scene_buffer* titleblur; /* the same, for the title bar */
    struct wlr_buffer* titleblurbuf;
#endif
    Buffer* titlepool[2];
    int titlex, titlew; /* title bar placement, relative to the border box */
    int titlebufw;      /* pixel width titlepool was allocated at */
    unsigned int bw;
    uint32_t tags;
    int isfloating, isurgent, isfullscreen;
    float opacity;         /* the one in effect, focused or not */
    float opacity_focus;   /* used while the client holds focus */
    float opacity_unfocus; /* used while it does not */
    int hasopacity;        /* the app passed the opacity_apps filter */
    int borderscheme;      /* scheme its border is drawn in, to redo it */
    uint32_t resize;       /* configure serial of a pending resize */
} Client;

typedef struct {
    uint32_t mod;
    xkb_keysym_t keysym;
    void (*func)(const Arg*);
    const Arg arg;
} Key;

typedef struct {
    struct wlr_keyboard_group* wlr_group;

    int nsyms;
    const xkb_keysym_t* keysyms; /* invalid if nsyms == 0 */
    uint32_t mods;               /* invalid if nsyms == 0 */
    struct wl_event_source* key_repeat_source;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
} KeyboardGroup;

typedef struct {
    /* Must keep this field first */
    unsigned int type; /* LayerShell */

    Monitor* mon;
    struct wlr_scene_tree* scene;
    struct wlr_scene_tree* popups;
    struct wlr_scene_layer_surface_v1* scene_layer;
    struct wl_list link;
    int mapped;
    struct wlr_layer_surface_v1* layer_surface;

    struct wl_listener destroy;
    struct wl_listener unmap;
    struct wl_listener surface_commit;
} LayerSurface;

typedef struct {
    const char* symbol;
    void (*arrange)(Monitor*);
} Layout;

struct Monitor {
    struct wl_list link;
    struct wlr_output* wlr_output;
    struct wlr_scene_output* scene_output;
    struct wlr_scene_buffer* scene_buffer; /* bar buffer */
#ifdef INTEGRATED_BACKGROUND
    struct wlr_scene_buffer* wallpaper; /* wallpaper buffer */
    struct wlr_buffer* wallpaperbuf;    /* what wallpaper's node was last handed */
    struct wlr_scene_buffer* barblur;   /* the bar's own frosted backdrop */
#endif
    struct wlr_scene_rect* fullscreen_bg; /* See createmon() for info */
    struct wl_listener frame;
    struct wl_listener destroy;
    struct wl_listener request_state;
    struct wl_listener destroy_lock_surface;
    struct wlr_session_lock_surface_v1* lock_surface;
    struct wlr_box m; /* monitor area, layout-relative */
    struct {
        int width, height;
        int real_width, real_height; /* non-scaled */
        int titlew; /* free box shared by the window title and the
                     * notification, 0 when it doesn't fit */
        float scale;
    } b; /* bar area */
    struct {
        int height;
        int real_height; /* non-scaled */
    } t;                 /* per-client title bar */
#ifdef SYSTRAY
    Tray* tray;
#endif
    struct wlr_box w;         /* window area, layout-relative */
    struct wl_list layers[4]; /* LayerSurface.link */
    const Layout* lt[2];
    const Layout* taglt[32][2];
    unsigned int tagsellt[32];
    int gaps;
    unsigned int seltags;
    unsigned int sellt;
    uint32_t tagset[2];
    float mfact;
    int gamma_lut_changed;
    int nmaster;
    char ltsymbol[16];
    int asleep;
    Drwl* drw;
    Buffer* pool[2];
#ifdef INTEGRATED_BACKGROUND
    Buffer* wallpaperpool[1];
    Buffer* blurpool[1];        /* wallpaperpool, blurred */
    int blurw, blurh;           /* size blurpool was rendered at */
    int wallpaperw, wallpaperh; /* size wallpaperpool was rendered at */
#endif
    int lrpad;
};

typedef struct {
    const char* name;
    float mfact;
    int nmaster;
    float scale;
    const Layout* lt;
    enum wl_output_transform rr;
    int x, y;
    int width, height; /* 0,0 means use the preferred mode */
    int refresh;       /* Hz, ignored when width/height are 0 */
} MonitorRule;

typedef struct {
    struct wlr_pointer_constraint_v1* constraint;
    struct wl_listener destroy;
} PointerConstraint;

typedef struct {
    const char* id;
    const char* title;
    uint32_t tags;
    int isfloating;
    float opacity_focus;   /* 0 keeps the default from config.h */
    float opacity_unfocus; /* 0 keeps the default from config.h */
    int monitor;
} Rule;

typedef struct {
    struct wlr_scene_tree* scene;

    struct wlr_session_lock_v1* lock;
    struct wl_listener new_surface;
    struct wl_listener unlock;
    struct wl_listener destroy;
} SessionLock;

/* function declarations */
static void applybounds(Client* c, struct wlr_box* bbox);
static void applyrules(Client* c);
static void arrange(Monitor* m);
static void arrangelayer(Monitor* m,
                         struct wl_list* list,
                         struct wlr_box* usable_area,
                         int exclusive);
static void arrangelayers(Monitor* m);
static void autostartexec(void);
static void axisnotify(struct wl_listener* listener, void* data);
static bool baracceptsinput(struct wlr_scene_buffer* buffer,
                            double* sx,
                            double* sy);
static Monitor* barmonitor(void);
static int barvisible(Monitor* m);
#ifdef INTEGRATED_BACKGROUND
static void blurbar(Monitor* m);
static void blurbox(Client* c,
                    struct wlr_scene_buffer* node,
                    struct wlr_buffer** last,
                    int bx,
                    int by,
                    int bw,
                    int bh);
static void blurclient(Client* c);
static void blurrows(uint32_t* dst, const uint32_t* src, int w, int h, int r);
static void blurshrink(uint32_t* dst, const uint32_t* src, int w, int h, int d);
static void blursrcbox(Monitor* m,
                       struct wlr_fbox* src,
                       int x,
                       int y,
                       int w,
                       int h);
static void blurtint(uint32_t* px, size_t n);
static void blurtranspose(uint32_t* dst, const uint32_t* src, int w, int h);
static void blurwallpaper(Monitor* m);
#endif
static void bufdestroy(struct wlr_buffer* buffer);
static bool bufdatabegin(struct wlr_buffer* buffer,
                         uint32_t flags,
                         void** data,
                         uint32_t* format,
                         size_t* stride);
static void bufdataend(struct wlr_buffer* buffer);
static Buffer* bufget(Buffer** pool, size_t poollen, int width, int height);
static void bufpooldrop(Buffer** pool, size_t poollen);
static void bufrelease(struct wl_listener* listener, void* data);
static void buttonpress(struct wl_listener* listener, void* data);
static void chvt(const Arg* arg);
static void checkidleinhibitor(struct wlr_surface* exclude);
static void cleanup(void);
static void cleanupmon(struct wl_listener* listener, void* data);
static void cleanuplisteners(void);
static void closemon(Monitor* m);
static void commitlayersurfacenotify(struct wl_listener* listener, void* data);
static void commitnotify(struct wl_listener* listener, void* data);
static void commitpopup(struct wl_listener* listener, void* data);
static void createdecoration(struct wl_listener* listener, void* data);
static void createidleinhibitor(struct wl_listener* listener, void* data);
static void createkeyboard(struct wlr_keyboard* keyboard);
static KeyboardGroup* createkeyboardgroup(void);
static void createlayersurface(struct wl_listener* listener, void* data);
static void createlocksurface(struct wl_listener* listener, void* data);
static void createmon(struct wl_listener* listener, void* data);
static void createnotify(struct wl_listener* listener, void* data);
static void createpointer(struct wlr_pointer* pointer);
static void createpointerconstraint(struct wl_listener* listener, void* data);
static void createpopup(struct wl_listener* listener, void* data);
static void cursorconstrain(struct wlr_pointer_constraint_v1* constraint);
static void cursorframe(struct wl_listener* listener, void* data);
static void cursorwarptohint(void);
static float decoopacity(void);
static int decotranslucent(void);
static void destroydecoration(struct wl_listener* listener, void* data);
static void destroydragicon(struct wl_listener* listener, void* data);
static void destroyidleinhibitor(struct wl_listener* listener, void* data);
static void destroylayersurfacenotify(struct wl_listener* listener, void* data);
static void destroylock(SessionLock* lock, int unlocked);
static void destroylocksurface(struct wl_listener* listener, void* data);
static void destroynotify(struct wl_listener* listener, void* data);
static void destroypointerconstraint(struct wl_listener* listener, void* data);
static void destroysessionlock(struct wl_listener* listener, void* data);
static void destroykeyboardgroup(struct wl_listener* listener, void* data);
static Monitor* dirtomon(enum wlr_direction dir);
static void drawbar(Monitor* m);
static void drawbars(void);
static void drawselbar(void);
static int drawstatus(Monitor* m, const char* text, int x, int w, int render);
static void drawtitle(Client* c);
static void focusclient(Client* c, int lift);
static void focusmon(const Arg* arg);
static void focusstack(const Arg* arg);
static Client* focustop(Monitor* m);
static void fullscreennotify(struct wl_listener* listener, void* data);
static void gpureset(struct wl_listener* listener, void* data);
static void handlecursoractivity(void);
static int hidecursor(void* data);
static void handlesig(int signo);
static void incnmaster(const Arg* arg);
static void inputdevice(struct wl_listener* listener, void* data);
static int keybinding(uint32_t mods, xkb_keysym_t sym);
static void keypress(struct wl_listener* listener, void* data);
static void keypressmod(struct wl_listener* listener, void* data);
static int keyrepeat(void* data);
static void killclient(const Arg* arg);
static void locksession(struct wl_listener* listener, void* data);
static void mapnotify(struct wl_listener* listener, void* data);
static void maximizenotify(struct wl_listener* listener, void* data);
static void monocle(Monitor* m);
static void movestack(const Arg* arg);
static void motionabsolute(struct wl_listener* listener, void* data);
static void tabletaxis(struct wl_listener* listener, void* data);
static void tablettip(struct wl_listener* listener, void* data);
static void motionnotify(uint32_t time,
                         struct wlr_input_device* device,
                         double sx,
                         double sy,
                         double sx_unaccel,
                         double sy_unaccel);
static void motionrelative(struct wl_listener* listener, void* data);
static void moveresize(const Arg* arg);
#ifdef NOTIFICATIONS
static void notifyclick(const Arg* arg);
static void notifydismiss(const Arg* arg);
static void notifysync(void);
#endif
static int opacityallowed(const char* appid);
static void opacityrefresh(void);
static void outputmgrapply(struct wl_listener* listener, void* data);
static void outputmgrapplyortest(struct wlr_output_configuration_v1* config,
                                 int test);
static void outputmgrtest(struct wl_listener* listener, void* data);
static void pointerfocus(Client* c,
                         struct wlr_surface* surface,
                         double sx,
                         double sy,
                         uint32_t time);
static void powermgrsetmode(struct wl_listener* listener, void* data);
static void quit(const Arg* arg);
static void rendermon(struct wl_listener* listener, void* data);
static void requestdecorationmode(struct wl_listener* listener, void* data);
static void requeststartdrag(struct wl_listener* listener, void* data);
static void requestmonstate(struct wl_listener* listener, void* data);
static void resize(Client* c, struct wlr_box geo, int interact);
static void resizeheight(const Arg* arg);
static void resizewidth(const Arg* arg);
static void run(char* startup_cmd);
#ifdef RUNNER
static int runnercalc(double* out);
static void runnerbuildcache(void);
static void runnerfreecache(void);
static void runnerkey(xkb_keysym_t sym, uint32_t mods, uint32_t codepoint);
static int runnerpathstale(void);
static const char* runnersuggest(void);
static void runnertoggle(const Arg* arg);
#endif
static void scenebuffersetopacity(struct wlr_scene_buffer* buffer,
                                  int sx,
                                  int sy,
                                  void* data);
static void setbordercolor(Client* c, int scheme);
static void setcursor(struct wl_listener* listener, void* data);
static void setcursorshape(struct wl_listener* listener, void* data);
static void setfloating(Client* c, int floating);
static void setfullscreen(Client* c, int fullscreen);
static void setlayout(const Arg* arg);
static void setmfact(const Arg* arg);
static void settitle(Client* c);
static int titleheight(Client* c);
static void setmon(Client* c, Monitor* m, uint32_t newtags);
static void setopacityfocus(const Arg* arg);
static void setopacityunfocus(const Arg* arg);
static void setpsel(struct wl_listener* listener, void* data);
static void setsel(struct wl_listener* listener, void* data);
static void setup(void);
#ifdef INTEGRATED_BACKGROUND
static void setwallpaper(Monitor* m);
#endif
static void spawn(const Arg* arg);
static void startdrag(struct wl_listener* listener, void* data);
static int statusescape(const char* p, uint32_t* scm);
static int statusin(int fd, unsigned int mask, void* data);
static void stopstatus(void);
static void swapclients(Client* a, Client* b);
static void tag(const Arg* arg);
static void tabbed(Monitor* m);
static unsigned int tagindex(uint32_t tagset);
static Client* tabtop(Monitor* m);
static void tagmon(const Arg* arg);
static void tile(Monitor* m);
static void togglebar(const Arg* arg);
static void togglefloating(const Arg* arg);
static void togglefullscreen(const Arg* arg);
static void togglegaps(const Arg* arg);
static void toggleopacity(const Arg* arg);
static void toggletabbed(const Arg* arg);
static void toggletag(const Arg* arg);
static void toggleview(const Arg* arg);
#ifdef SYSTRAY
static void trayactivate(const Arg* arg);
static void traymenu(const Arg* arg);
static void traynotify(void* data);
#endif
static void unlastcursor(struct wl_listener* listener, void* data);
static void unlocksession(struct wl_listener* listener, void* data);
static void unmaplayersurfacenotify(struct wl_listener* listener, void* data);
static void unmapnotify(struct wl_listener* listener, void* data);
static void updatemons(struct wl_listener* listener, void* data);
static void updatebar(Monitor* m);
static void updatetitle(struct wl_listener* listener, void* data);
static void urgent(struct wl_listener* listener, void* data);
static void view(const Arg* arg);
static void virtualkeyboard(struct wl_listener* listener, void* data);
static void virtualpointer(struct wl_listener* listener, void* data);
static void warpcursor(const Client* c);
static Monitor* xytomon(double x, double y);
static void xytonode(double x,
                     double y,
                     struct wlr_surface** psurface,
                     Client** pc,
                     LayerSurface** pl,
                     double* nx,
                     double* ny);
static void zoom(const Arg* arg);

/* variables */
static pid_t child_pid = -1;
static int locked;
static void* exclusive_focus;
static struct wl_display* dpy;
static struct wl_event_loop* event_loop;
static struct wlr_backend* backend;
static struct wlr_scene* scene;
static struct wlr_scene_tree* layers[NUM_LAYERS];
static struct wlr_scene_tree* drag_icon;
/* Map from ZWLR_LAYER_SHELL_* constants to Lyr* enum */
static const int layermap[] = { LyrBg, LyrBottom, LyrTop, LyrOverlay };
static struct wlr_renderer* drw;
static struct wlr_allocator* alloc;
static struct wlr_compositor* compositor;
static struct wlr_session* session;

static struct wlr_xdg_shell* xdg_shell;
static struct wlr_xdg_activation_v1* activation;
static struct wlr_xdg_decoration_manager_v1* xdg_decoration_mgr;
static struct wl_list clients; /* tiling order */
static struct wl_list fstack;  /* focus order */
static struct wlr_idle_notifier_v1* idle_notifier;
static struct wlr_idle_inhibit_manager_v1* idle_inhibit_mgr;
static struct wlr_layer_shell_v1* layer_shell;
static struct wlr_output_manager_v1* output_mgr;
static struct wlr_virtual_keyboard_manager_v1* virtual_keyboard_mgr;
static struct wlr_virtual_pointer_manager_v1* virtual_pointer_mgr;
static struct wlr_cursor_shape_manager_v1* cursor_shape_mgr;
static struct wlr_output_power_manager_v1* power_mgr;

static struct wlr_pointer_constraints_v1* pointer_constraints;
static struct wlr_relative_pointer_manager_v1* relative_pointer_mgr;
static struct wlr_pointer_constraint_v1* active_constraint;

static struct wlr_cursor* cursor;
static struct wlr_xcursor_manager* cursor_mgr;
static bool cursor_hidden = false;
static struct {
    enum wp_cursor_shape_device_v1_shape shape;
    struct wlr_surface* surface;
    bool has_client_cursor;
    int hotspot_x;
    int hotspot_y;
    struct wl_listener destroy;
} last_cursor;

static struct wlr_scene_rect* root_bg;
#ifdef INTEGRATED_BACKGROUND
static GdkPixbuf* wallpaper_src; /* full-res decode, cached across resizes */
static int wallpaper_load_failed;
#endif
static struct wlr_session_lock_manager_v1* session_lock_mgr;
static struct wlr_scene_rect* locked_bg;
static struct wlr_session_lock_v1* cur_lock;

static struct wlr_seat* seat;
static KeyboardGroup* kb_group;
static unsigned int cursor_mode;
static Client* grabc;
static int grabcx, grabcy; /* client-relative */

static struct wlr_output_layout* output_layout;
static struct wlr_box sgeom;
static struct wl_list mons;
static Monitor* selmon;

static char stext[256];
static struct wl_event_source* status_event_source;

static DBusConnection* bus_conn;
static struct wl_event_source* bus_source;
#ifdef SYSTRAY
static Watcher watcher = { .running = 0 };
#endif
#ifdef NOTIFICATIONS
static unsigned int notifyshownid;
static size_t notifyoff;
#endif
#ifdef RUNNER
static int runner_active;
static char runner_buf[256];
static int runner_len;
static int runner_cur; /* cursor offset into runner_buf, 0..runner_len */
static char** runner_cmds;
static int runner_ncmds;
static struct timespec runner_stamp; /* newest PATH mtime the cache was built
                                        from */
static uint32_t runner_repeatcp; /* codepoint the armed key repeat types */
static int runner_repeating;     /* the armed repeat belongs to the prompt */
#endif

static const struct wlr_buffer_impl buffer_impl = {
    .destroy = bufdestroy,
    .begin_data_ptr_access = bufdatabegin,
    .end_data_ptr_access = bufdataend,
};

/* global event handlers */
static struct wl_listener cursor_axis = { .notify = axisnotify };
static struct wl_listener cursor_button = { .notify = buttonpress };
static struct wl_listener cursor_frame = { .notify = cursorframe };
static struct wl_listener cursor_motion = { .notify = motionrelative };
static struct wl_listener cursor_motion_absolute = { .notify = motionabsolute };
static struct wl_listener cursor_tablet_axis = { .notify = tabletaxis };
static struct wl_listener cursor_tablet_tip = { .notify = tablettip };
static struct wl_listener gpu_reset = { .notify = gpureset };
static struct wl_listener layout_change = { .notify = updatemons };
static struct wl_listener new_idle_inhibitor = { .notify =
                                                     createidleinhibitor };
static struct wl_listener new_input_device = { .notify = inputdevice };
static struct wl_listener new_virtual_keyboard = { .notify = virtualkeyboard };
static struct wl_listener new_virtual_pointer = { .notify = virtualpointer };
static struct wl_listener new_pointer_constraint = {
    .notify = createpointerconstraint
};
static struct wl_listener new_output = { .notify = createmon };
static struct wl_listener new_xdg_toplevel = { .notify = createnotify };
static struct wl_listener new_xdg_popup = { .notify = createpopup };
static struct wl_listener new_xdg_decoration = { .notify = createdecoration };
static struct wl_listener new_layer_surface = { .notify = createlayersurface };
static struct wl_listener output_mgr_apply = { .notify = outputmgrapply };
static struct wl_listener output_mgr_test = { .notify = outputmgrtest };
static struct wl_listener output_power_mgr_set_mode = { .notify =
                                                            powermgrsetmode };
static struct wl_listener request_activate = { .notify = urgent };
static struct wl_listener request_cursor = { .notify = setcursor };
static struct wl_listener request_set_psel = { .notify = setpsel };
static struct wl_listener request_set_sel = { .notify = setsel };
static struct wl_listener request_set_cursor_shape = { .notify =
                                                           setcursorshape };
static struct wl_listener request_start_drag = { .notify = requeststartdrag };
static struct wl_listener start_drag = { .notify = startdrag };
static struct wl_listener new_session_lock = { .notify = locksession };

#ifdef XWAYLAND
static void activatex11(struct wl_listener* listener, void* data);
static void associatex11(struct wl_listener* listener, void* data);
static void configurex11(struct wl_listener* listener, void* data);
static void createnotifyx11(struct wl_listener* listener, void* data);
static void dissociatex11(struct wl_listener* listener, void* data);
static void sethints(struct wl_listener* listener, void* data);
static void xwaylandready(struct wl_listener* listener, void* data);
static struct wl_listener new_xwayland_surface = { .notify = createnotifyx11 };
static struct wl_listener xwayland_ready = { .notify = xwaylandready };
static struct wlr_xwayland* xwayland;
#endif

static pid_t* autostart_pids;
static size_t autostart_len;

/* configuration, allows nested code to access above variables */
#include "config.h"

/* attempt to encapsulate suck into one file */
#include "client.h"

/* function implementations */
void applybounds(Client* c, struct wlr_box* bbox)
{
    /* set minimum possible */
    c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
    c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);

    if (c->geom.x >= bbox->x + bbox->width)
        c->geom.x = bbox->x + bbox->width - c->geom.width;
    if (c->geom.y >= bbox->y + bbox->height)
        c->geom.y = bbox->y + bbox->height - c->geom.height;
    if (c->geom.x + c->geom.width <= bbox->x)
        c->geom.x = bbox->x;
    if (c->geom.y + c->geom.height <= bbox->y)
        c->geom.y = bbox->y;
}

void applyrules(Client* c)
{
    /* rule matching */
    const char *appid, *title;
    uint32_t newtags = 0;
    int i;
    const Rule* r;
    Monitor *mon = selmon, *m;

    appid = client_get_appid(c);
    title = client_get_title(c);

    for (r = rules; r < END(rules); r++) {
        if ((!r->title || strstr(title, r->title)) &&
            (!r->id || strstr(appid, r->id))) {
            c->isfloating = r->isfloating;
            if (r->opacity_focus > 0)
                c->opacity_focus = r->opacity_focus;
            if (r->opacity_unfocus > 0)
                c->opacity = c->opacity_unfocus = r->opacity_unfocus;
            newtags |= r->tags;
            i = 0;
            wl_list_for_each(m, &mons, link)
            {
                if (r->monitor == i++)
                    mon = m;
            }
        }
    }

    c->isfloating |= client_is_float_type(c);
    setmon(c, mon, newtags);
}

void arrange(Monitor* m)
{
    Client* c;

    if (!m->wlr_output->enabled)
        return;

    wl_list_for_each(c, &clients, link)
    {
        if (c->mon == m) {
            wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m));
            /* re-enable content tabbed() may have hidden for a non-top tab */
            wlr_scene_node_set_enabled(&c->scene_surface->node, 1);
            client_set_suspended(c, !VISIBLEON(c, m));
        }
    }

    wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
                               (c = focustop(m)) && c->isfullscreen);

    snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "%s", m->lt[m->sellt]->symbol);

    /* We move all clients (except fullscreen and unmanaged) to LyrTile while
     * in floating layout to avoid "real" floating clients be always on top */
    wl_list_for_each(c, &clients, link)
    {
        if (c->mon != m || c->scene->node.parent == layers[LyrFS])
            continue;

        wlr_scene_node_reparent(&c->scene->node,
                                (!m->lt[m->sellt]->arrange && c->isfloating)
                                    ? layers[LyrTile]
                                : (m->lt[m->sellt]->arrange && c->isfloating)
                                    ? layers[LyrFloat]
                                    : c->scene->node.parent);
    }

    if (m->lt[m->sellt]->arrange)
        m->lt[m->sellt]->arrange(m);
    motionnotify(0, NULL, 0, 0, 0, 0);
    checkidleinhibitor(NULL);
    warpcursor(focustop(selmon));
}

void arrangelayer(Monitor* m,
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

void autostartexec(void)
{
    const char* const* p;
    size_t i = 0;

    /* count entries */
    for (p = autostart; *p; autostart_len++, p++)
        while (*++p)
            ;

    autostart_pids = calloc(autostart_len, sizeof(pid_t));
    for (p = autostart; *p; i++, p++) {
        if ((autostart_pids[i] = fork()) == 0) {
            setsid();
            execvp(*p, (char* const*)p);
            die("g0wn: execvp %s:", *p);
        }
        /* skip arguments */
        while (*++p)
            ;
    }
}

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

bool baracceptsinput(struct wlr_scene_buffer* buffer, double* sx, double* sy)
{
    return true;
}

/* The monitor the bar is drawn on: the first output matched by monrules[] with
 * barsinglemon, the focused one without it. NULL when no output is enabled. */
Monitor* barmonitor(void)
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
int barvisible(Monitor* m)
{
    return m->wlr_output->enabled && showbar &&
           (!barsinglemon || m == barmonitor());
}

#ifdef INTEGRATED_BACKGROUND
/* Repoints the bar's backdrop at the strip of frosted wallpaper it sits on.
 * The bar is drawn by g0wn, so decotranslucent() decides whether it shows. */
void blurbar(Monitor* m)
{
    struct wlr_fbox src;
    Buffer* buf;
    int y, w, h;

    if (!m->barblur)
        return;

    buf = m->blurpool[0];
    y = topbar ? 0 : m->m.height - m->b.real_height;
    w = MIN(m->b.real_width, m->wallpaperw);
    h = MIN(m->b.real_height, m->wallpaperh - y);
    if (!buf || !m->scene_buffer->node.enabled || !opacity_enabled ||
        !decotranslucent() || y < 0 || w <= 0 || h <= 0) {
        wlr_scene_node_set_enabled(&m->barblur->node, 0);
        return;
    }

    blursrcbox(m, &src, 0, y, w, h);
    wlr_scene_node_set_position(&m->barblur->node, m->m.x, m->m.y + y);
    wlr_scene_buffer_set_dest_size(m->barblur, w, h);
    wlr_scene_buffer_set_source_box(m->barblur, &src);
    wlr_scene_node_set_enabled(&m->barblur->node, 1);
}

/* Points one of a client's backdrop nodes at the frosted wallpaper under the
 * box it covers, in the coordinates of the client's own tree. last is what the
 * node already holds: set_buffer drops the texture and damages the node, so
 * handing it the same buffer every frame never stops asking for another. */
void blurbox(Client* c,
             struct wlr_scene_buffer* node,
             struct wlr_buffer** last,
             int bx,
             int by,
             int bw,
             int bh)
{
    struct wlr_fbox src;
    Monitor* m = c->mon;
    Buffer* buf = m ? m->blurpool[0] : NULL;
    int x, y, w, h;

    if (!node || !m)
        return;

    /* the wallpaper covers the monitor pixel for pixel, so the crop is just
     * the box in monitor-local coordinates, clipped to it */
    x = MAX(0, c->geom.x - m->m.x + bx);
    y = MAX(0, c->geom.y - m->m.y + by);
    w = MIN(c->geom.x - m->m.x + bx + bw, m->wallpaperw) - x;
    h = MIN(c->geom.y - m->m.y + by + bh, m->wallpaperh) - y;
    if (!buf || w <= 0 || h <= 0) {
        wlr_scene_node_set_enabled(&node->node, 0);
        return;
    }

    blursrcbox(m, &src, x, y, w, h);
    /* the node hangs off the client's tree, hence the offset back into it */
    wlr_scene_node_set_position(
        &node->node, x - (c->geom.x - m->m.x), y - (c->geom.y - m->m.y));
    wlr_scene_buffer_set_dest_size(node, w, h);
    wlr_scene_buffer_set_source_box(node, &src);
    if (*last != &buf->base) {
        wlr_scene_buffer_set_buffer(node, &buf->base);
        *last = &buf->base;
    }
    wlr_scene_node_set_enabled(&node->node, 1);
}

/* Points a client's backdrop at the frosted wallpaper it covers. Done while
 * rendering, so nothing that moves a window has to keep the two in step. */
void blurclient(Client* c)
{
    Monitor* m = c->mon;
    int th, bw, x, w;

    if (!c->blur)
        return;

    /* the box covers the decorations too, so either letting light through is
     * enough; a fullscreen client is drawn opaque and never does */
    if (!m || !m->blurpool[0] || c->isfullscreen || !opacity_enabled ||
        ((!c->hasopacity || c->opacity >= 1.0f) && !decotranslucent())) {
        wlr_scene_node_set_enabled(&c->blur->node, 0);
        if (c->titleblur)
            wlr_scene_node_set_enabled(&c->titleblur->node, 0);
        return;
    }

    th = titleheight(c);
    bw = (int)c->bw;

    if (!th || m->lt[m->sellt]->arrange != tabbed || c->isfloating) {
        blurbox(c, c->blur, &c->blurbuf, 0, 0, c->geom.width, c->geom.height);
        if (c->titleblur)
            wlr_scene_node_set_enabled(&c->titleblur->node, 0);
        return;
    }

    /* Tabs share one box and the ones under the top are drawn in lower trees,
     * so a backdrop over the whole box buries their title bars. Each tab backs
     * its own slice of the row, the top one backs the body below it. */
    x = bw + c->titlex;
    w = c->titlew;
    if (c->titlex == 0) { /* first tab, take the corner in */
        x = 0;
        w += bw;
    }
    if (c->titlex + c->titlew == c->geom.width - 2 * bw)
        w += bw; /* last tab, same */
    blurbox(c, c->titleblur, &c->titleblurbuf, x, 0, w, bw + th);

    if (c == tabtop(m))
        blurbox(c,
                c->blur,
                &c->blurbuf,
                0,
                bw + th,
                c->geom.width,
                c->geom.height - bw - th);
    else
        wlr_scene_node_set_enabled(&c->blur->node, 0);
}

/* One box blur along the rows: a 2r+1 window slides on a running sum, so a
 * pass costs the same whatever the radius is, and three land near a Gaussian. */
void blurrows(uint32_t* dst, const uint32_t* src, int w, int h, int r)
{
    int x, y, i;

    for (y = 0; y < h; y++) {
        const uint32_t* s = src + (size_t)y * (size_t)w;
        uint32_t* d = dst + (size_t)y * (size_t)w;
        uint32_t sr = 0, sg = 0, sb = 0, n = 0;

        for (i = 0; i <= r && i < w; i++) {
            sr += (s[i] >> 16) & 0xff;
            sg += (s[i] >> 8) & 0xff;
            sb += s[i] & 0xff;
            n++;
        }
        for (x = 0; x < w; x++) {
            d[x] = 0xff000000u | ((sr / n) << 16) | ((sg / n) << 8) | (sb / n);
            if (x - r >= 0) {
                sr -= (s[x - r] >> 16) & 0xff;
                sg -= (s[x - r] >> 8) & 0xff;
                sb -= s[x - r] & 0xff;
                n--;
            }
            if (x + r + 1 < w) {
                sr += (s[x + r + 1] >> 16) & 0xff;
                sg += (s[x + r + 1] >> 8) & 0xff;
                sb += s[x + r + 1] & 0xff;
                n++;
            }
        }
    }
}

/* Averages every d*d block down to one pixel; the blur then runs on the
 * small image, which is what keeps a radius this wide affordable. */
void blurshrink(uint32_t* dst, const uint32_t* src, int w, int h, int d)
{
    int sw = MAX(1, w / d), sh = MAX(1, h / d);
    int x, y, i, j;

    for (y = 0; y < sh; y++)
        for (x = 0; x < sw; x++) {
            uint32_t sr = 0, sg = 0, sb = 0, n = 0;

            for (j = y * d; j < MIN(h, (y + 1) * d); j++)
                for (i = x * d; i < MIN(w, (x + 1) * d); i++) {
                    uint32_t px = src[(size_t)j * (size_t)w + (size_t)i];

                    sr += (px >> 16) & 0xff;
                    sg += (px >> 8) & 0xff;
                    sb += px & 0xff;
                    n++;
                }
            dst[(size_t)y * (size_t)sw + (size_t)x] =
                n ? 0xff000000u | ((sr / n) << 16) | ((sg / n) << 8) | (sb / n)
                  : 0xff000000u;
        }
}

/* Maps a monitor-local rectangle onto the smaller frosted buffer. The crop
 * stays in floating point so it does not snap to that buffer's grid. */
void blursrcbox(Monitor* m, struct wlr_fbox* src, int x, int y, int w, int h)
{
    double sx = (double)m->blurw / (double)m->wallpaperw;
    double sy = (double)m->blurh / (double)m->wallpaperh;

    src->x = x * sx;
    src->y = y * sy;
    src->width = w * sx;
    src->height = h * sy;
}

/* A blur this wide leaves grey mush, so the saturation is pushed back up:
 * that is what makes it read as glass rather than as a smudge. */
void blurtint(uint32_t* px, size_t n)
{
    size_t i;

    if (blur_saturation == 1.0f && blur_brightness == 1.0f)
        return;

    for (i = 0; i < n; i++) {
        float r = (float)((px[i] >> 16) & 0xff);
        float g = (float)((px[i] >> 8) & 0xff);
        float b = (float)(px[i] & 0xff);
        float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;

        r = (l + (r - l) * blur_saturation) * blur_brightness;
        g = (l + (g - l) * blur_saturation) * blur_brightness;
        b = (l + (b - l) * blur_saturation) * blur_brightness;
        px[i] = 0xff000000u |
                ((uint32_t)(MAX(0.0f, MIN(255.0f, r)) + 0.5f) << 16) |
                ((uint32_t)(MAX(0.0f, MIN(255.0f, g)) + 0.5f) << 8) |
                (uint32_t)(MAX(0.0f, MIN(255.0f, b)) + 0.5f);
    }
}

/* Turns a w*h image on its side, so that blurring its rows blurs the columns
 * of the original and one row pass covers both directions. */
void blurtranspose(uint32_t* dst, const uint32_t* src, int w, int h)
{
    int x, y;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            dst[(size_t)x * (size_t)h + (size_t)y] =
                src[(size_t)y * (size_t)w + (size_t)x];
}

/* Builds the frosted copy OpacityBlur shows through everything transparent,
 * once per wallpaper rather than per frame. It stays at 1/d of the monitor
 * and the GPU scales it back up: a backdrop is never occluded away, so a
 * full-size copy would cross the bus on every frame anything moved. */
void blurwallpaper(Monitor* m)
{
    Buffer* buf;
    Client* c;
    uint32_t *a, *b, *t;
    unsigned int i;
    int w = m->wallpaperw, h = m->wallpaperh;
    int d, sw, sh, r;

    /* the bar's node is what owns the buffer, so it lets go of it first */
    if (m->barblur)
        wlr_scene_buffer_set_buffer(m->barblur, NULL);
    bufpooldrop(m->blurpool, LENGTH(m->blurpool));
    m->blurw = m->blurh = 0;

    /* the nodes pick the new buffer up next frame; the pointer they compare
     * against has to go now, or a reused address would read as unchanged */
    wl_list_for_each(c, &clients, link) if (c->mon == m) c->blurbuf =
        c->titleblurbuf = NULL;

    if (opacity_type != OpacityBlur || !m->wallpaperpool[0] || w <= 0 || h <= 0)
        return;

    /* the shrink blurs by itself, so the radius shrinks with the image; never
     * less than a quarter, as this size is what the effect costs per frame */
    d = MAX(4, MIN(8, (int)blur_radius / 4));
    sw = MAX(1, w / d);
    sh = MAX(1, h / d);
    r = MAX(1, (int)blur_radius / d);

    if (!(buf = bufget(m->blurpool, LENGTH(m->blurpool), sw, sh)))
        return;
    a = ecalloc((size_t)sw * (size_t)sh, sizeof(*a));
    b = ecalloc((size_t)sw * (size_t)sh, sizeof(*b));

    blurshrink(a, m->wallpaperpool[0]->data, w, h, d);
    for (i = 0; i < blur_passes; i++) {
        blurrows(b, a, sw, sh, r);
        t = a, a = b, b = t;
    }
    blurtranspose(b, a, sw, sh);
    t = a, a = b, b = t;
    for (i = 0; i < blur_passes; i++) {
        blurrows(b, a, sh, sw, r);
        t = a, a = b, b = t;
    }
    blurtranspose(b, a, sh, sw);
    t = a, a = b, b = t;

    blurtint(a, (size_t)sw * (size_t)sh);
    memcpy(buf->data, a, (size_t)sw * (size_t)sh * sizeof(*a));
    free(a);
    free(b);

    /* what blurbar() and blurclient() scale their crops by */
    m->blurw = sw;
    m->blurh = sh;

    /* the pool keeps no reference of its own: the bar's node is what holds
     * the buffer up, and the clients take their own lock as they pick it up */
    if (m->barblur)
        wlr_scene_buffer_set_buffer(m->barblur, &buf->base);
    wlr_buffer_unlock(&buf->base);
    blurbar(m);
}
#endif /* INTEGRATED_BACKGROUND */

void bufdestroy(struct wlr_buffer* wlr_buffer)
{
    Buffer* buf = wl_container_of(wlr_buffer, buf, base);
    if (buf->busy)
        wl_list_remove(&buf->release.link);
    drwl_image_destroy(buf->image);
    free(buf);
}

bool bufdatabegin(struct wlr_buffer* wlr_buffer,
                  uint32_t flags,
                  void** data,
                  uint32_t* format,
                  size_t* stride)
{
    Buffer* buf = wl_container_of(wlr_buffer, buf, base);

    if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
        return false;

    *data = buf->data;
    *stride = wlr_buffer->width * 4;
    *format = DRM_FORMAT_ARGB8888;

    return true;
}

void bufdataend(struct wlr_buffer* wlr_buffer) {}

/* Grabs a free width*height buffer from pool, allocating it on first use. The
 * bar and every client's title bar own a pool, as their sizes differ. */
Buffer* bufget(Buffer** pool, size_t poollen, int width, int height)
{
    size_t i;
    Buffer* buf = NULL;

    for (i = 0; i < poollen; i++) {
        if (pool[i]) {
            if (pool[i]->busy)
                continue;
            buf = pool[i];
            break;
        }

        buf = ecalloc(1, sizeof(Buffer) + ((size_t)width * 4 * (size_t)height));
        buf->image = drwl_image_create(NULL, width, height, buf->data);
        wlr_buffer_init(&buf->base, &buffer_impl, width, height);
        pool[i] = buf;
        break;
    }
    if (!buf)
        return NULL;

    buf->busy = true;
    LISTEN(&buf->base.events.release, &buf->release, bufrelease);
    wlr_buffer_lock(&buf->base);
    return buf;
}

void bufpooldrop(Buffer** pool, size_t poollen)
{
    size_t i;

    for (i = 0; i < poollen; i++)
        if (pool[i]) {
            wlr_buffer_drop(&pool[i]->base);
            pool[i] = NULL;
        }
}

void bufrelease(struct wl_listener* listener, void* data)
{
    Buffer* buf = wl_container_of(listener, buf, release);
    buf->busy = false;
    wl_list_remove(&buf->release.link);
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

void chvt(const Arg* arg)
{
    wlr_session_change_vt(session, arg->ui);
}

void checkidleinhibitor(struct wlr_surface* exclude)
{
    int inhibited = 0, unused_lx, unused_ly;
    struct wlr_idle_inhibitor_v1* inhibitor;
    wl_list_for_each(inhibitor, &idle_inhibit_mgr->inhibitors, link)
    {
        struct wlr_surface* surface =
            wlr_surface_get_root_surface(inhibitor->surface);
        struct wlr_scene_tree* tree = surface->data;
        /* An unmapped surface owns no scene tree (unmapnotify() clears
         * surface->data), so it cannot be visible and must not keep the
         * session awake. */
        if (exclude != surface &&
            (bypass_surface_visibility ||
             (surface->mapped &&
              (!tree ||
               wlr_scene_node_coords(&tree->node, &unused_lx, &unused_ly))))) {
            inhibited = 1;
            break;
        }
    }

    wlr_idle_notifier_v1_set_inhibited(idle_notifier, inhibited);
}

void cleanup(void)
{
    size_t i;

    cleanuplisteners();
#ifdef XWAYLAND
    wlr_xwayland_destroy(xwayland);
    xwayland = NULL;
#endif
    wl_display_destroy_clients(dpy);

    /* kill child processes */
    for (i = 0; i < autostart_len; i++) {
        if (0 < autostart_pids[i]) {
            kill(autostart_pids[i], SIGTERM);
            waitpid(autostart_pids[i], NULL, 0);
        }
    }

    if (child_pid > 0) {
        kill(-child_pid, SIGTERM);
        waitpid(child_pid, NULL, 0);
    }
    wlr_xcursor_manager_destroy(cursor_mgr);

    destroykeyboardgroup(&kb_group->destroy, NULL);

#ifdef SYSTRAY
    if (watcher.running)
        watcher_stop(&watcher);
#endif
#ifdef NOTIFICATIONS
    if (shownotifications)
        notify_stop();
#endif
    if (bus_conn) {
        stopbus(bus_conn, bus_source);
        dbus_connection_unref(bus_conn);
    }

    /* If it's not destroyed manually, it will cause a use-after-free of
     * wlr_seat. Destroy it until it's fixed on the wlroots side */
    wlr_backend_destroy(backend);

    wl_display_destroy(dpy);
    /* Destroy after the wayland display (when the monitors are already
       destroyed) to avoid destroying them with an invalid scene output. */
    wlr_scene_node_destroy(&scene->tree.node);

#ifdef INTEGRATED_BACKGROUND
    if (wallpaper_src)
        g_object_unref(wallpaper_src);
#endif

    drwl_fini();
}

void cleanupmon(struct wl_listener* listener, void* data)
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

void cleanuplisteners(void)
{
    wl_list_remove(&cursor_axis.link);
    wl_list_remove(&cursor_button.link);
    wl_list_remove(&cursor_frame.link);
    wl_list_remove(&cursor_motion.link);
    wl_list_remove(&cursor_motion_absolute.link);
    wl_list_remove(&cursor_tablet_axis.link);
    wl_list_remove(&cursor_tablet_tip.link);
    wl_list_remove(&gpu_reset.link);
    wl_list_remove(&new_idle_inhibitor.link);
    wl_list_remove(&layout_change.link);
    wl_list_remove(&new_input_device.link);
    wl_list_remove(&new_virtual_keyboard.link);
    wl_list_remove(&new_virtual_pointer.link);
    wl_list_remove(&new_pointer_constraint.link);
    wl_list_remove(&new_output.link);
    wl_list_remove(&new_xdg_toplevel.link);
    wl_list_remove(&new_xdg_decoration.link);
    wl_list_remove(&new_xdg_popup.link);
    wl_list_remove(&new_layer_surface.link);
    wl_list_remove(&output_mgr_apply.link);
    wl_list_remove(&output_mgr_test.link);
    wl_list_remove(&output_power_mgr_set_mode.link);
    wl_list_remove(&request_activate.link);
    wl_list_remove(&request_cursor.link);
    wl_list_remove(&request_set_psel.link);
    wl_list_remove(&request_set_sel.link);
    wl_list_remove(&request_set_cursor_shape.link);
    wl_list_remove(&request_start_drag.link);
    wl_list_remove(&start_drag.link);
    wl_list_remove(&new_session_lock.link);
#ifdef XWAYLAND
    wl_list_remove(&new_xwayland_surface.link);
    wl_list_remove(&xwayland_ready.link);
#endif
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

void commitlayersurfacenotify(struct wl_listener* listener, void* data)
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

void commitnotify(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, commit);

    if (c->surface.xdg->initial_commit) {
        /*
         * Get the monitor this client will be rendered on
         * Note that if the user set a rule in which the client is placed on
         * a different monitor based on its title, this will likely select
         * a wrong monitor.
         */
        applyrules(c);
        if (c->mon) {
            client_set_scale(client_surface(c), c->mon->wlr_output->scale);
        }
        setmon(c, NULL, 0); /* Make sure to reapply rules in mapnotify() */

        wlr_xdg_toplevel_set_wm_capabilities(
            c->surface.xdg->toplevel,
            WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
        if (c->decoration)
            requestdecorationmode(&c->set_decoration_mode, c->decoration);
        wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
        return;
    }

    resize(c, c->geom, (c->isfloating && !c->isfullscreen));

    /* mark a pending resize as completed */
    if (c->resize && c->resize <= c->surface.xdg->current.configure_serial)
        c->resize = 0;
}

void commitpopup(struct wl_listener* listener, void* data)
{
    struct wlr_surface* surface = data;
    struct wlr_xdg_popup* popup = wlr_xdg_popup_try_from_wlr_surface(surface);
    LayerSurface* l = NULL;
    Client* c = NULL;
    struct wlr_box box;
    int type = -1;

    if (!popup->base->initial_commit)
        return;

    type = toplevel_from_wlr_surface(popup->base->surface, &c, &l);
    if (!popup->parent || type < 0)
        return;
    /* The parent may have been unmapped in the meantime, dropping its scene
     * tree; there is nothing to parent the popup to. */
    if (!popup->parent->data) {
        wlr_xdg_popup_destroy(popup);
        return;
    }
    popup->base->surface->data =
        wlr_scene_xdg_surface_create(popup->parent->data, popup->base);
    if ((l && !l->mon) || (c && !c->mon)) {
        wlr_xdg_popup_destroy(popup);
        return;
    }
    box = type == LayerShell ? l->mon->m : c->mon->w;
    box.x -= (type == LayerShell ? l->scene->node.x : c->geom.x);
    box.y -= (type == LayerShell ? l->scene->node.y : c->geom.y);
    wlr_xdg_popup_unconstrain_from_box(popup, &box);
    wl_list_remove(&listener->link);
    free(listener);
}

void createdecoration(struct wl_listener* listener, void* data)
{
    struct wlr_xdg_toplevel_decoration_v1* deco = data;
    Client* c = deco->toplevel->base->data;
    c->decoration = deco;

    LISTEN(&deco->events.request_mode,
           &c->set_decoration_mode,
           requestdecorationmode);
    LISTEN(&deco->events.destroy, &c->destroy_decoration, destroydecoration);

    requestdecorationmode(&c->set_decoration_mode, deco);
}

void createidleinhibitor(struct wl_listener* listener, void* data)
{
    struct wlr_idle_inhibitor_v1* idle_inhibitor = data;
    LISTEN_STATIC(&idle_inhibitor->events.destroy, destroyidleinhibitor);

    checkidleinhibitor(NULL);
}

void createkeyboard(struct wlr_keyboard* keyboard)
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

void createlocksurface(struct wl_listener* listener, void* data)
{
    SessionLock* lock = wl_container_of(listener, lock, new_surface);
    struct wlr_session_lock_surface_v1* lock_surface = data;
    Monitor* m = lock_surface->output->data;
    struct wlr_scene_tree* scene_tree = lock_surface->surface->data =
        wlr_scene_subsurface_tree_create(lock->scene, lock_surface->surface);
    m->lock_surface = lock_surface;

    wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
    wlr_session_lock_surface_v1_configure(
        lock_surface, m->m.width, m->m.height);

    LISTEN(&lock_surface->events.destroy,
           &m->destroy_lock_surface,
           destroylocksurface);

    if (m == selmon)
        client_notify_enter(lock_surface->surface, wlr_seat_get_keyboard(seat));
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

void createnotify(struct wl_listener* listener, void* data)
{
    /* This event is raised when a client creates a new toplevel (application
     * window). */
    struct wlr_xdg_toplevel* toplevel = data;
    Client* c = NULL;

    /* Allocate a Client for this surface */
    c = toplevel->base->data = ecalloc(1, sizeof(*c));
    c->surface.xdg = toplevel->base;
    c->bw = borderpx;
    c->opacity = c->opacity_unfocus = opacity_unfocus;
    c->opacity_focus = opacity_focus;

    LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
    LISTEN(&toplevel->base->surface->events.map, &c->map, mapnotify);
    LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmapnotify);
    LISTEN(&toplevel->events.destroy, &c->destroy, destroynotify);
    LISTEN(
        &toplevel->events.request_fullscreen, &c->fullscreen, fullscreennotify);
    LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
    LISTEN(&toplevel->events.set_title, &c->set_title, updatetitle);
}

void createpointer(struct wlr_pointer* pointer)
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

void createpopup(struct wl_listener* listener, void* data)
{
    /* This event is raised when a client (either xdg-shell or layer-shell)
     * creates a new popup. */
    struct wlr_xdg_popup* popup = data;
    LISTEN_STATIC(&popup->base->surface->events.commit, commitpopup);
}

void cursorconstrain(struct wlr_pointer_constraint_v1* constraint)
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

void cursorwarptohint(void)
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

/* The opacity g0wn's own drawing runs at. It rides on opacity_enabled, so the
 * one key turns the windows and the decorations off and on together. */
float decoopacity(void)
{
    return opacity_enabled ? opacity_deco : 1.0f;
}

/* Whether light gets through what g0wn draws itself. opacity_deco fades the
 * lot, and a colour in colors[] can carry an alpha of its own, which is how a
 * bar stays see-through with opacity_deco left at 1. */
int decotranslucent(void)
{
    size_t i, j;

    if (decoopacity() < 1.0f)
        return 1;
    for (i = 0; i < LENGTH(colors); i++)
        for (j = 1; j < LENGTH(colors[i]); j++) /* 0 is the foreground */
            if ((colors[i][j] & 0xff) < 0xff)
                return 1;
    return 0;
}

void destroydecoration(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, destroy_decoration);

    wl_list_remove(&c->destroy_decoration.link);
    wl_list_remove(&c->set_decoration_mode.link);
}

void destroydragicon(struct wl_listener* listener, void* data)
{
    /* Focus enter isn't sent during drag, so refocus the focused node. */
    focusclient(focustop(selmon), 1);
    motionnotify(0, NULL, 0, 0, 0, 0);
    wl_list_remove(&listener->link);
    free(listener);
}

void destroyidleinhibitor(struct wl_listener* listener, void* data)
{
    /* `data` is the wlr_surface of the idle inhibitor being destroyed,
     * at this point the idle inhibitor is still in the list of the manager */
    checkidleinhibitor(wlr_surface_get_root_surface(data));
    wl_list_remove(&listener->link);
    free(listener);
}

void destroylayersurfacenotify(struct wl_listener* listener, void* data)
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

void destroylock(SessionLock* lock, int unlock)
{
    wlr_seat_keyboard_notify_clear_focus(seat);
    if ((locked = !unlock))
        goto destroy;

    wlr_scene_node_set_enabled(&locked_bg->node, 0);

    focusclient(focustop(selmon), 0);
    motionnotify(0, NULL, 0, 0, 0, 0);

destroy:
    wl_list_remove(&lock->new_surface.link);
    wl_list_remove(&lock->unlock.link);
    wl_list_remove(&lock->destroy.link);

    wlr_scene_node_destroy(&lock->scene->node);
    cur_lock = NULL;
    free(lock);
}

void destroylocksurface(struct wl_listener* listener, void* data)
{
    Monitor* m = wl_container_of(listener, m, destroy_lock_surface);
    struct wlr_session_lock_surface_v1 *surface,
        *lock_surface = m->lock_surface;

    m->lock_surface = NULL;
    wl_list_remove(&m->destroy_lock_surface.link);

    if (lock_surface->surface != seat->keyboard_state.focused_surface)
        return;

    if (locked && cur_lock && !wl_list_empty(&cur_lock->surfaces)) {
        surface = wl_container_of(cur_lock->surfaces.next, surface, link);
        client_notify_enter(surface->surface, wlr_seat_get_keyboard(seat));
    } else if (!locked) {
        focusclient(focustop(selmon), 1);
    } else {
        wlr_seat_keyboard_clear_focus(seat);
    }
}

void destroynotify(struct wl_listener* listener, void* data)
{
    /* Called when the xdg_toplevel is destroyed. */
    Client* c = wl_container_of(listener, c, destroy);
    wl_list_remove(&c->destroy.link);
    wl_list_remove(&c->set_title.link);
    wl_list_remove(&c->fullscreen.link);
#ifdef XWAYLAND
    if (c->type != XDGShell) {
        wl_list_remove(&c->activate.link);
        wl_list_remove(&c->associate.link);
        wl_list_remove(&c->configure.link);
        wl_list_remove(&c->dissociate.link);
        wl_list_remove(&c->set_hints.link);
    } else
#endif
    {
        wl_list_remove(&c->commit.link);
        wl_list_remove(&c->map.link);
        wl_list_remove(&c->unmap.link);
        wl_list_remove(&c->maximize.link);
    }
    free(c);
}

void destroypointerconstraint(struct wl_listener* listener, void* data)
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

void destroysessionlock(struct wl_listener* listener, void* data)
{
    SessionLock* lock = wl_container_of(listener, lock, destroy);
    destroylock(lock, 0);
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

Monitor* dirtomon(enum wlr_direction dir)
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

    /* Title bars are refreshed on the same events as the bar */
    wl_list_for_each(c, &clients, link) if (c->mon == m) drawtitle(c);

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
int statusescape(const char* p, uint32_t* scm)
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

#ifdef INTEGRATED_BACKGROUND
void setwallpaper(Monitor* m)
{
    GdkPixbuf* scaled;
    GError* error = NULL;
    Buffer* buf;
    int mw = m->m.width, mh = m->m.height;
    int pw, ph, sw, sh, ox, oy, x, y, rowstride, nch;
    double scalefactor;
    guchar* src;
    uint32_t* dst;

    if (!wallpaper || !*wallpaper || mw <= 0 || mh <= 0) {
        wlr_scene_buffer_set_buffer(m->wallpaper, NULL);
        m->wallpaperbuf = NULL;
        bufpooldrop(m->wallpaperpool, LENGTH(m->wallpaperpool));
        m->wallpaperw = m->wallpaperh = 0;
        blurwallpaper(m);
        return;
    }

    if (m->wallpaperpool[0] && mw == m->wallpaperw && mh == m->wallpaperh) {
        wlr_scene_buffer_set_dest_size(m->wallpaper, mw, mh);
        wlr_scene_node_set_position(&m->wallpaper->node, m->m.x, m->m.y);
        /* guarded like blurclient()'s: set_buffer throws the texture away,
         * and this path runs on every monitor layout change */
        if (m->wallpaperbuf != &m->wallpaperpool[0]->base) {
            wlr_scene_buffer_set_buffer(m->wallpaper, &m->wallpaperpool[0]->base);
            m->wallpaperbuf = &m->wallpaperpool[0]->base;
        }
        /* the frosted copy is the same size and survives with it, unless the
         * monitor went away and took it along */
        if (!m->blurpool[0])
            blurwallpaper(m);
        else
            blurbar(m);
        return;
    }

    if (!wallpaper_src && !wallpaper_load_failed &&
        !(wallpaper_src = gdk_pixbuf_new_from_file(wallpaper, &error))) {
        wlr_log(WLR_ERROR, "wallpaper: %s", error->message);
        g_error_free(error);
        wallpaper_load_failed = 1;
    }
    if (!wallpaper_src)
        return;

    pw = gdk_pixbuf_get_width(wallpaper_src);
    ph = gdk_pixbuf_get_height(wallpaper_src);
    scalefactor = MAX((double)mw / pw, (double)mh / ph);
    sw = MAX(1, (int)lround(pw * scalefactor));
    sh = MAX(1, (int)lround(ph * scalefactor));
    scaled = gdk_pixbuf_scale_simple(wallpaper_src, sw, sh, GDK_INTERP_BILINEAR);
    if (!scaled) {
        wlr_log(WLR_ERROR, "wallpaper: failed to scale %s", wallpaper);
        return;
    }

    ox = MIN(sw - mw, (sw - mw) / 2);
    oy = MIN(sh - mh, (sh - mh) / 2);

    bufpooldrop(m->wallpaperpool, LENGTH(m->wallpaperpool));
    if (!(buf =
              bufget(m->wallpaperpool, LENGTH(m->wallpaperpool), mw, mh))) {
        g_object_unref(scaled);
        return;
    }

    src = gdk_pixbuf_get_pixels(scaled);
    rowstride = gdk_pixbuf_get_rowstride(scaled);
    nch = gdk_pixbuf_get_n_channels(scaled);
    dst = buf->data;
    for (y = 0; y < mh; y++) {
        guchar* srow = src + (size_t)(y + oy) * rowstride + (size_t)ox * nch;
        for (x = 0; x < mw; x++, srow += nch, dst++)
            *dst = (0xffu << 24) | ((uint32_t)srow[0] << 16) |
                   ((uint32_t)srow[1] << 8) | (uint32_t)srow[2];
    }
    g_object_unref(scaled);
    m->wallpaperw = mw;
    m->wallpaperh = mh;

    wlr_scene_buffer_set_dest_size(m->wallpaper, mw, mh);
    wlr_scene_node_set_position(&m->wallpaper->node, m->m.x, m->m.y);
    wlr_scene_buffer_set_buffer(m->wallpaper, &buf->base);
    m->wallpaperbuf = &buf->base;
    wlr_buffer_unlock(&buf->base);
    blurwallpaper(m);
}
#endif /* INTEGRATED_BACKGROUND */

#ifdef SYSTRAY
void traynotify(void* data)
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

/* Renders the client's own title bar. In the tabbed layout every client of the
 * group shares one row, so these end up drawn side by side as tabs. */
void drawtitle(Client* c)
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

void focusclient(Client* c, int lift)
{
    struct wlr_surface* old = seat->keyboard_state.focused_surface;
    int unused_lx, unused_ly, old_client_type;
    Client* old_c = NULL;
    LayerSurface* old_l = NULL;

    if (locked)
        return;

    /* Warp cursor to center of client if it is outside */
    if (lift)
        warpcursor(c);

    /* Raise client in stacking order if requested */
    if (c && lift)
        wlr_scene_node_raise_to_top(&c->scene->node);

    if (c && client_surface(c) == old)
        return;

    if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) ==
        XDGShell) {
        struct wlr_xdg_popup *popup, *tmp;
        wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
            wlr_xdg_popup_destroy(popup);
    }

    /* Put the new client atop the focus stack and select its monitor */
    if (c && !client_is_unmanaged(c)) {
        wl_list_remove(&c->flink);
        wl_list_insert(&fstack, &c->flink);
        selmon = c->mon;
        c->isurgent = 0;
        c->opacity = c->opacity_focus;

        /* Don't change border color if there is an exclusive focus or we are
         * handling a drag operation */
        if (!exclusive_focus && !seat->drag)
            setbordercolor(c, SchemeSel);

        /* tabbed() otherwise only reruns on arrange() */
        if (c->mon && c->mon->lt[c->mon->sellt]->arrange == tabbed &&
            !c->isfloating && !c->isfullscreen)
            tabbed(c->mon);
    }

    /* Deactivate old client if focus is changing */
    if (old && (!c || client_surface(c) != old)) {
        /* If an overlay is focused, don't focus or activate the client,
         * but only update its position in fstack to render its border with its
         * color and focus it after the overlay is closed. */
        if (old_client_type == LayerShell &&
            wlr_scene_node_coords(
                &old_l->scene->node, &unused_lx, &unused_ly) &&
            old_l->layer_surface->current.layer >=
                ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
            return;
        } else if (old_c && old_c == exclusive_focus &&
                   client_wants_focus(old_c)) {
            return;
            /* Don't deactivate old client if the new one wants focus, as this
             * causes issues with winecfg and probably other clients */
        } else if (old_c && !client_is_unmanaged(old_c) &&
                   (!c || !client_wants_focus(c))) {
            setbordercolor(old_c, SchemeNorm);
            client_activate_surface(old, 0);
            old_c->opacity = old_c->opacity_unfocus;
        }
    }
    drawbars();

    if (!c) {
        /* With no client, all we have left is to clear focus */
        wlr_seat_keyboard_notify_clear_focus(seat);
        return;
    }

    /* Change cursor surface */
    motionnotify(0, NULL, 0, 0, 0, 0);

    /* Have a client, so focus its top-level wlr_surface */
    client_notify_enter(client_surface(c), wlr_seat_get_keyboard(seat));

    /* Activate the new client */
    client_activate_surface(client_surface(c), 1);
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

void focusstack(const Arg* arg)
{
    /* Focus the next or previous client (in tiling order) on selmon */
    Client *c, *sel = focustop(selmon);
    if (!sel || (sel->isfullscreen && !client_has_children(sel)))
        return;
    if (arg->i > 0) {
        wl_list_for_each(c, &sel->link, link)
        {
            if (&c->link == &clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(c, selmon))
                break; /* found it */
        }
    } else {
        wl_list_for_each_reverse(c, &sel->link, link)
        {
            if (&c->link == &clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(c, selmon))
                break; /* found it */
        }
    }
    /* If only one client is visible on selmon, then c == sel */
    focusclient(c, 1);
}

/* We probably should change the name of this: it sounds like it
 * will focus the topmost client of this mon, when actually will
 * only return that client */
Client* focustop(Monitor* m)
{
    Client* c;
    wl_list_for_each(c, &fstack, flink)
    {
        if (VISIBLEON(c, m))
            return c;
    }
    return NULL;
}

void fullscreennotify(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, fullscreen);
    setfullscreen(c, client_wants_fullscreen(c));
}

void gpureset(struct wl_listener* listener, void* data)
{
    struct wlr_renderer* old_drw = drw;
    struct wlr_allocator* old_alloc = alloc;
    struct Monitor* m;
    if (!(drw = wlr_renderer_autocreate(backend)))
        die("couldn't recreate renderer");

    if (!(alloc = wlr_allocator_autocreate(backend, drw)))
        die("couldn't recreate allocator");

    wl_list_remove(&gpu_reset.link);
    wl_signal_add(&drw->events.lost, &gpu_reset);

    wlr_compositor_set_renderer(compositor, drw);

    wl_list_for_each(m, &mons, link)
    {
        wlr_output_init_render(m->wlr_output, alloc, drw);
    }

    wlr_allocator_destroy(old_alloc);
    wlr_renderer_destroy(old_drw);
}

void handlesig(int signo)
{
    if (signo == SIGCHLD) {
        pid_t pid, *p, *lim;
        while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
            if (pid == child_pid)
                child_pid = -1;
            if (!(p = autostart_pids))
                continue;
            lim = &p[autostart_len];

            for (; p < lim; p++) {
                if (*p == pid) {
                    *p = -1;
                    break;
                }
            }
        }
    } else if (signo == SIGINT || signo == SIGTERM) {
        quit(NULL);
    }
}

void handlecursoractivity(void)
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

int hidecursor(void* data)
{
    wlr_cursor_unset_image(cursor);
    cursor_hidden = true;
    return 1;
}

void incnmaster(const Arg* arg)
{
    if (!arg || !selmon)
        return;
    selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
    arrange(selmon);
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
     * communiciated to the client. In g0wn we always have a cursor, even if
     * there are no pointer devices, so we always include that capability. */
    /* TODO do we actually require a cursor? */
    caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&kb_group->wlr_group->devices))
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(seat, caps);
}

#ifdef RUNNER
static int runnercmp(const void* a, const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

static void runnerbuildcache(void)
{
    /* Same idea as dmenu_path: list every executable name under $PATH once,
     * so each keystroke only has to filter an already-built array. */
    const char* path = getenv("PATH");
    char *p, *dir, *saveptr;
    char full[1024];
    struct stat st;
    DIR* d;
    struct dirent* e;
    int i, w;
    size_t cap = 0;

    if (!path)
        return;
    if (!(p = strdup(path)))
        die("strdup:");
    for (dir = strtok_r(p, ":", &saveptr); dir;
         dir = strtok_r(NULL, ":", &saveptr)) {
        if (!(d = opendir(dir)))
            continue;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.')
                continue;
            if (snprintf(full, sizeof full, "%s/%s", dir, e->d_name) >=
                (int)sizeof full)
                continue;
            /* Regular files only: +x on a directory just means it can be
             * traversed, so access() alone would list the subdirectories a
             * PATH entry happens to carry as if they were commands. */
            if (stat(full, &st) != 0 || !S_ISREG(st.st_mode) ||
                access(full, X_OK) != 0)
                continue;
            if ((size_t)runner_ncmds == cap) {
                cap = cap ? cap * 2 : 256;
                if (!(runner_cmds = realloc(runner_cmds, cap * sizeof(char*))))
                    die("realloc:");
            }
            if (!(runner_cmds[runner_ncmds++] = strdup(e->d_name)))
                die("strdup:");
        }
        closedir(d);
    }
    free(p);

    qsort(runner_cmds, runner_ncmds, sizeof(char*), runnercmp);
    for (i = 0, w = 0; i < runner_ncmds; i++) {
        if (w && !strcmp(runner_cmds[w - 1], runner_cmds[i]))
            free(runner_cmds[i]);
        else
            runner_cmds[w++] = runner_cmds[i];
    }
    runner_ncmds = w;
}

static void runnerfreecache(void)
{
    while (runner_ncmds)
        free(runner_cmds[--runner_ncmds]);
    free(runner_cmds);
    runner_cmds = NULL;
}

static int runnerpathstale(void)
{
    /* A directory's mtime moves whenever an entry is added or removed, so
     * stat()ing the PATH entries - a syscall each, no readdir - answers
     * whether a rescan would turn anything up. That keeps the cost of
     * noticing a newly installed program off every prompt but the one that
     * follows the install. */
    const char* path = getenv("PATH");
    char *p, *dir, *saveptr;
    struct stat st;
    struct timespec newest = { 0, 0 };

    if (!path)
        return 0;
    if (!(p = strdup(path)))
        die("strdup:");
    for (dir = strtok_r(p, ":", &saveptr); dir;
         dir = strtok_r(NULL, ":", &saveptr))
        /* to the nanosecond: at second granularity an install and the removal
         * that follows it within the same second cancel out */
        if (stat(dir, &st) == 0 && (st.st_mtim.tv_sec > newest.tv_sec ||
                                    (st.st_mtim.tv_sec == newest.tv_sec &&
                                     st.st_mtim.tv_nsec > newest.tv_nsec)))
            newest = st.st_mtim;
    free(p);

    if (newest.tv_sec == runner_stamp.tv_sec &&
        newest.tv_nsec == runner_stamp.tv_nsec)
        return 0;
    runner_stamp = newest;
    return 1;
}

static const char* runnersuggest(void)
{
    int i;

    if (!runner_len)
        return NULL;
    for (i = 0; i < runner_ncmds; i++)
        if (!strncmp(runner_cmds[i], runner_buf, runner_len))
            return runner_cmds[i];
    return NULL;
}

/* +, -, *, /, %, parens, a decimal point and whitespace: nothing here can
 * reach a shell or a file, so a malformed expression can only fail to
 * parse, never do anything. */
struct RunnerCalcState {
    const char* s;
    int depth;
    int failed;
};

static double runnercalcexpr(struct RunnerCalcState* c);

/* Every recursive call goes through here first, so one depth check bounds
 * atom, term and expr together; runner_buf is 256 bytes, so even fully
 * parenthesized input like "(((((1)))))" cannot come close to this limit. */
static int runnercalcenter(struct RunnerCalcState* c)
{
    if (++c->depth > 64) {
        c->failed = 1;
        return 0;
    }
    return 1;
}

static double runnercalcnum(struct RunnerCalcState* c)
{
    double v = 0, frac = 0.1;
    int any = 0;

    while (*c->s >= '0' && *c->s <= '9') {
        v = v * 10 + (*c->s - '0');
        c->s++;
        any = 1;
    }
    if (*c->s == '.') {
        c->s++;
        while (*c->s >= '0' && *c->s <= '9') {
            v += (*c->s - '0') * frac;
            frac *= 0.1;
            c->s++;
            any = 1;
        }
    }
    if (!any)
        c->failed = 1;
    return v;
}

static double runnercalcatom(struct RunnerCalcState* c)
{
    double v;

    if (!runnercalcenter(c))
        return 0;
    while (*c->s == ' ' || *c->s == '\t')
        c->s++;
    if (*c->s == '-') {
        c->s++;
        v = -runnercalcatom(c);
    } else if (*c->s == '+') {
        c->s++;
        v = runnercalcatom(c);
    } else if (*c->s == '(') {
        c->s++;
        v = runnercalcexpr(c);
        while (*c->s == ' ' || *c->s == '\t')
            c->s++;
        if (*c->s != ')')
            c->failed = 1;
        else
            c->s++;
    } else {
        v = runnercalcnum(c);
    }
    c->depth--;
    return v;
}

static double runnercalcterm(struct RunnerCalcState* c)
{
    double v = runnercalcatom(c), rhs;

    for (;;) {
        while (*c->s == ' ' || *c->s == '\t')
            c->s++;
        if (*c->s == '*') {
            c->s++;
            v *= runnercalcatom(c);
        } else if (*c->s == '/') {
            c->s++;
            rhs = runnercalcatom(c);
            if (rhs == 0.0) {
                c->failed = 1;
                return 0;
            }
            v /= rhs;
        } else if (*c->s == '%') {
            c->s++;
            rhs = runnercalcatom(c);
            if (rhs == 0.0) {
                c->failed = 1;
                return 0;
            }
            v = fmod(v, rhs);
        } else {
            break;
        }
    }
    return v;
}

static double runnercalcexpr(struct RunnerCalcState* c)
{
    double v = runnercalcterm(c);

    for (;;) {
        while (*c->s == ' ' || *c->s == '\t')
            c->s++;
        if (*c->s == '+') {
            c->s++;
            v += runnercalcterm(c);
        } else if (*c->s == '-') {
            c->s++;
            v -= runnercalcterm(c);
        } else {
            break;
        }
    }
    return v;
}

/* Only a buffer made purely of digits/operators/parens/whitespace, with at
 * least one digit and one operator, is worth trying to parse: this keeps
 * plain PATH lookups (letters) and bare numbers untouched. */
static int runnerisexpr(void)
{
    int i, digit = 0, op = 0;
    char ch;

    for (i = 0; i < runner_len; i++) {
        ch = runner_buf[i];
        if (ch >= '0' && ch <= '9')
            digit = 1;
        else if (ch == '+' || ch == '-' || ch == '*' || ch == '/' ||
                 ch == '%')
            op = 1;
        else if (ch != '(' && ch != ')' && ch != '.' && ch != ' ' &&
                 ch != '\t')
            return 0;
    }
    return digit && op;
}

static int runnercalc(double* out)
{
    struct RunnerCalcState c = { runner_buf, 0, 0 };
    double v;

    if (!runnerisexpr())
        return 0;
    v = runnercalcexpr(&c);
    while (*c.s == ' ' || *c.s == '\t')
        c.s++;
    if (c.failed || *c.s != '\0' || !isfinite(v))
        return 0;
    *out = v;
    return 1;
}

static void runnertoggle(const Arg* arg)
{
    if (!selmon)
        return;
    if (!runner_active) {
        if (runnerpathstale() || !runner_cmds) {
            runnerfreecache();
            runnerbuildcache();
        }
        runner_len = 0;
        runner_cur = 0;
        runner_buf[0] = '\0';
        /* the repeat this very binding is arming must not reach the prompt */
        runner_repeating = 0;
    }
    runner_active = !runner_active;
    drawselbar();
}

static void runnerkey(xkb_keysym_t sym, uint32_t mods, uint32_t codepoint)
{
    const char* sug;
    const char* cmd;
    char* argv[4];
    Arg a;

    /* Ctrl+<letter> line-editing, readline-style. Ctrl doesn't change the
     * keysym a key produces, only mods, so these are matched on the plain
     * letter. Every other Ctrl combination falls through untouched: the
     * codepoint it produces is a control character, which the text case
     * below already rejects. */
    if (mods & WLR_MODIFIER_CTRL && xkb_keysym_to_lower(sym) == XKB_KEY_c) {
        runner_len = 0;
        runner_cur = 0;
        runner_buf[0] = '\0';
    } else if (mods & WLR_MODIFIER_CTRL && xkb_keysym_to_lower(sym) == XKB_KEY_a) {
        runner_cur = 0;
    } else if (mods & WLR_MODIFIER_CTRL && xkb_keysym_to_lower(sym) == XKB_KEY_e) {
        runner_cur = runner_len;
    } else if (mods & WLR_MODIFIER_CTRL && xkb_keysym_to_lower(sym) == XKB_KEY_f) {
        if (runner_cur < runner_len)
            runner_cur++;
    } else if (mods & WLR_MODIFIER_CTRL && xkb_keysym_to_lower(sym) == XKB_KEY_b) {
        if (runner_cur > 0)
            runner_cur--;
    } else if (mods & WLR_MODIFIER_CTRL && xkb_keysym_to_lower(sym) == XKB_KEY_w) {
        /* delete the word behind the cursor: skip trailing spaces, then the
         * run of non-spaces before them, same as a shell's line editor */
        int end = runner_cur;
        while (runner_cur > 0 && runner_buf[runner_cur - 1] == ' ')
            runner_cur--;
        while (runner_cur > 0 && runner_buf[runner_cur - 1] != ' ')
            runner_cur--;
        memmove(runner_buf + runner_cur,
                runner_buf + end,
                runner_len - end + 1);
        runner_len -= end - runner_cur;
    } else
        switch (sym) {
            case XKB_KEY_Escape:
                runner_active = 0;
                break;
            case XKB_KEY_Return:
            case XKB_KEY_KP_Enter: {
                double calcval;
                sug = runnersuggest();
                cmd = sug ? sug : runner_buf;
                /* A bare expression has no business reaching /bin/sh: it
                 * would just fail as an unknown command. Tab already turns
                 * it into its result for anyone who wants that value. */
                if (*cmd && !(!sug && runnercalc(&calcval))) {
                    argv[0] = "/bin/sh";
                    argv[1] = "-c";
                    argv[2] = (char*)cmd;
                    argv[3] = NULL;
                    a.v = argv;
                    spawn(&a);
                }
                runner_active = 0;
                break;
            }
            case XKB_KEY_BackSpace:
                if (runner_cur) {
                    memmove(runner_buf + runner_cur - 1,
                            runner_buf + runner_cur,
                            runner_len - runner_cur + 1);
                    runner_cur--;
                    runner_len--;
                }
                break;
            case XKB_KEY_Tab:
                sug = runnersuggest();
                if (sug && strlen(sug) < sizeof(runner_buf)) {
                    runner_len = (int)strlen(sug);
                    memcpy(runner_buf, sug, runner_len + 1);
                    runner_cur = runner_len;
                } else if (!sug) {
                    double calcval;
                    char calcbuf[48];
                    if (runnercalc(&calcval)) {
                        snprintf(calcbuf, sizeof calcbuf, "%.10g", calcval);
                        if (strlen(calcbuf) < sizeof(runner_buf)) {
                            runner_len = (int)strlen(calcbuf);
                            memcpy(runner_buf, calcbuf, runner_len + 1);
                            runner_cur = runner_len;
                        }
                    }
                }
                break;
            default:
                if (codepoint >= 0x20 && codepoint < 0x7f &&
                    runner_len < (int)sizeof(runner_buf) - 1) {
                    memmove(runner_buf + runner_cur + 1,
                            runner_buf + runner_cur,
                            runner_len - runner_cur + 1);
                    runner_buf[runner_cur] = (char)codepoint;
                    runner_cur++;
                    runner_len++;
                }
                break;
        }
    drawselbar();
}
#endif /* RUNNER */

int keybinding(uint32_t mods, xkb_keysym_t sym)
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

void keypress(struct wl_listener* listener, void* data)
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

void keypressmod(struct wl_listener* listener, void* data)
{
    /* This event is raised when a modifier key, such as shift or alt, is
     * pressed. We simply communicate this to the client. */
    KeyboardGroup* group = wl_container_of(listener, group, modifiers);

    wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
    /* Send modifiers to the client. */
    wlr_seat_keyboard_notify_modifiers(seat,
                                       &group->wlr_group->keyboard.modifiers);
}

int keyrepeat(void* data)
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

void killclient(const Arg* arg)
{
    Client* sel = focustop(selmon);
    if (sel)
        client_send_close(sel);
}

void locksession(struct wl_listener* listener, void* data)
{
    struct wlr_session_lock_v1* session_lock = data;
    SessionLock* lock;
    wlr_scene_node_set_enabled(&locked_bg->node, 1);
    if (cur_lock) {
        wlr_session_lock_v1_destroy(session_lock);
        return;
    }
    lock = session_lock->data = ecalloc(1, sizeof(*lock));
    focusclient(NULL, 0);

    lock->scene = wlr_scene_tree_create(layers[LyrBlock]);
    cur_lock = lock->lock = session_lock;
    locked = 1;

    LISTEN(&session_lock->events.new_surface,
           &lock->new_surface,
           createlocksurface);
    LISTEN(&session_lock->events.destroy, &lock->destroy, destroysessionlock);
    LISTEN(&session_lock->events.unlock, &lock->unlock, unlocksession);

    wlr_session_lock_v1_send_locked(session_lock);
}

void mapnotify(struct wl_listener* listener, void* data)
{
    /* Called when the surface is mapped, or ready to display on-screen. */
    Client* p = NULL;
    Client *w, *c = wl_container_of(listener, c, map);
    Monitor* m;
    int i;

    /* Create scene tree for this client and its border */
    c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrTile]);
    /* Enabled later by a call to arrange() */
    wlr_scene_node_set_enabled(&c->scene->node, client_is_unmanaged(c));
    c->scene_surface =
        c->type == XDGShell
            ? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
            : wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
    c->scene->node.data = c->scene_surface->node.data = c;

    client_get_geometry(c, &c->geom);

    /* Handle unmanaged clients first so we can return prior create borders */
    if (client_is_unmanaged(c)) {
        /* Unmanaged clients always are floating */
        wlr_scene_node_reparent(&c->scene->node, layers[LyrFloat]);
        wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
        client_set_size(c, c->geom.width, c->geom.height);
        if (client_wants_focus(c)) {
            focusclient(c, 1);
            exclusive_focus = c;
        }
        goto unset_fullscreen;
    }

    for (i = 0; i < 4; i++) {
        c->border[i] = wlr_scene_rect_create(
            c->scene, 0, 0, (float[]){ 0.0f, 0.0f, 0.0f, 0.0f });
        c->border[i]->node.data = c;
    }
    /* the colour comes from here, once all four exist */
    setbordercolor(c, c->isurgent ? SchemeUrg : SchemeNorm);

    c->title = wlr_scene_buffer_create(c->scene, NULL);
    c->title->node.data = c;
    wlr_scene_node_set_enabled(&c->title->node, 0);

#ifdef INTEGRATED_BACKGROUND
    /* the frosted backdrop goes under the lot: the surface, which is what it
     * shows through, and the decorations, which opacity_deco may fade too */
    c->blur = wlr_scene_buffer_create(c->scene, NULL);
    c->blur->node.data = c;
    wlr_scene_node_set_enabled(&c->blur->node, 0);
    wlr_scene_node_lower_to_bottom(&c->blur->node);

    /* backs the title bar alone, which is what tabs need */
    c->titleblur = wlr_scene_buffer_create(c->scene, NULL);
    c->titleblur->node.data = c;
    wlr_scene_node_set_enabled(&c->titleblur->node, 0);
    wlr_scene_node_lower_to_bottom(&c->titleblur->node);
#endif

    /* Initialize client geometry with room for border */
    client_set_tiled(
        c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
    c->geom.width += 2 * c->bw;
    c->geom.height += 2 * c->bw;

    /* Insert this client into client lists. */
    wl_list_insert(clients.prev,
                   &c->link); /* attach at the bottom of the stack */
    wl_list_insert(&fstack, &c->flink);

    /* done here rather than in applyrules(): clients with a parent skip it */
    c->hasopacity = opacityallowed(client_get_appid(c));

    /* Set initial monitor, tags, floating status, and focus:
     * we always consider floating, clients that have parent and thus
     * we set the same tags and monitor as its parent.
     * If there is no parent, apply rules */
    if ((p = client_get_parent(c))) {
        c->isfloating = 1;
        setmon(c, p->mon, p->tags);
    } else {
        applyrules(c);
    }
    drawbars();

unset_fullscreen:
    m = c->mon ? c->mon : xytomon(c->geom.x, c->geom.y);
    wl_list_for_each(w, &clients, link)
    {
        if (w != c && w != p && w->isfullscreen && m == w->mon &&
            (w->tags & c->tags))
            setfullscreen(w, 0);
    }
}

void maximizenotify(struct wl_listener* listener, void* data)
{
    /* This event is raised when a client would like to maximize itself,
     * typically because the user clicked on the maximize button on
     * client-side decorations. g0wn doesn't support maximization, but
     * to conform to xdg-shell protocol we still must send a configure.
     * Since xdg-shell protocol v5 we should ignore request of unsupported
     * capabilities, just schedule a empty configure when the client uses <5
     * protocol version
     * wlr_xdg_surface_schedule_configure() is used to send an empty reply. */
    Client* c = wl_container_of(listener, c, maximize);
    if (c->surface.xdg->initialized &&
        wl_resource_get_version(c->surface.xdg->toplevel->resource) <
            XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
        wlr_xdg_surface_schedule_configure(c->surface.xdg);
}

void monocle(Monitor* m)
{
    Client* c;
    int n = 0;

    wl_list_for_each(c, &clients, link)
    {
        if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
            continue;
        resize(c, m->w, 0);
        n++;
    }
    if (n)
        snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "[%d]", n);
    if ((c = focustop(m)))
        wlr_scene_node_raise_to_top(&c->scene->node);
}

void movestack(const Arg* arg)
{
    Client *c, *sel = focustop(selmon);

    if (!sel || wl_list_length(&clients) <= 1)
        return;

    if (arg->i > 0) {
        wl_list_for_each(c, &sel->link, link)
        {
            if (&c->link == &clients) {
                c = wl_container_of(&clients, c, link);
                break; /* wrap past the sentinel node */
            }
            if (VISIBLEON(c, selmon))
                break; /* found it */
        }
    } else {
        wl_list_for_each_reverse(c, &sel->link, link)
        {
            if (&c->link == &clients) {
                c = wl_container_of(&clients, c, link);
                break; /* wrap past the sentinel node */
            }
            if (VISIBLEON(c, selmon))
                break; /* found it */
        }
        /* backup one client */
        c = wl_container_of(c->link.prev, c, link);
    }

    wl_list_remove(&sel->link);
    wl_list_insert(&c->link, &sel->link);
    arrange(selmon);
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

    if (!shownotifications || !m || !m->b.titlew ||
        !(text = notify_gettext()))
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
void notifysync(void)
{
    if (notifyshownid != notify_getid()) {
        notifyshownid = notify_getid();
        notifyoff = 0;
    }
}
#endif /* NOTIFICATIONS */

/* opacity_apps lists either the apps that get opacity or the ones that do not,
 * depending on opacity_exclusion_type; an empty list covers every app. */
int opacityallowed(const char* appid)
{
    const char* const* a;

    for (a = opacity_apps; *a; a++)
        if (strstr(appid, *a))
            return !opacity_exclusion_type;
    return opacity_exclusion_type || !*opacity_apps;
}

/* opacity is (re)applied while rendering, and changing it damages nothing by
 * itself, so a frame has to be asked for everywhere */
void opacityrefresh(void)
{
    Monitor* m;
    Client* c;

    /* a decoration is drawn by g0wn, not by a client, so nothing would come
     * back to redraw it: the bar and the title bars follow drawbars() */
    wl_list_for_each(c, &clients, link) setbordercolor(c, c->borderscheme);
    drawbars();

    wl_list_for_each(m, &mons, link)
    {
        if (m->wlr_output->enabled)
            wlr_output_schedule_frame(m->wlr_output);
    }
}

void outputmgrapply(struct wl_listener* listener, void* data)
{
    struct wlr_output_configuration_v1* config = data;
    outputmgrapplyortest(config, 0);
}

void outputmgrapplyortest(struct wlr_output_configuration_v1* config, int test)
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

void pointerfocus(Client* c,
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

void quit(const Arg* arg)
{
    wl_display_terminate(dpy);
}

void rendermon(struct wl_listener* listener, void* data)
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
        if (c->resize && !c->isfloating && !client_is_stopped(c))
            goto skip;
    }

    wlr_scene_output_commit(m->scene_output, NULL);

skip:
    /* Let clients know a frame has been rendered */
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(m->scene_output, &now);
    wlr_output_state_finish(&pending);
}

void requestdecorationmode(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, set_decoration_mode);
    if (c->surface.xdg->initialized)
        wlr_xdg_toplevel_decoration_v1_set_mode(
            c->decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
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

void requestmonstate(struct wl_listener* listener, void* data)
{
    struct wlr_output_event_request_state* event = data;
    wlr_output_commit_state(event->output, event->state);
    updatemons(NULL, NULL);
}

void resize(Client* c, struct wlr_box geo, int interact)
{
    struct wlr_box* bbox;
    struct wlr_box clip;
    int th;

    if (!c->mon || !client_surface(c)->mapped)
        return;

    th = titleheight(c);

    bbox = interact ? &sgeom : &c->mon->w;

    client_set_bounds(c, geo.width, geo.height);
    c->geom = geo;
    applybounds(c, bbox);

    /* Update scene-graph, including borders */
    wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
    wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw + th);
    wlr_scene_rect_set_size(c->border[0], c->geom.width, c->bw);
    wlr_scene_rect_set_size(c->border[1], c->geom.width, c->bw);
    wlr_scene_rect_set_size(c->border[2], c->bw, c->geom.height - 2 * c->bw);
    wlr_scene_rect_set_size(c->border[3], c->bw, c->geom.height - 2 * c->bw);
    wlr_scene_node_set_position(&c->border[1]->node, 0, c->geom.height - c->bw);
    wlr_scene_node_set_position(&c->border[2]->node, 0, c->bw);
    wlr_scene_node_set_position(
        &c->border[3]->node, c->geom.width - c->bw, c->bw);

    /* this is a no-op if size hasn't changed */
    c->resize = client_set_size(
        c, c->geom.width - 2 * c->bw, MAX(1, c->geom.height - 2 * c->bw - th));
    client_get_clip(c, &clip);
    clip.height -= th;
    wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);

    settitle(c);
}

void resizeheight(const Arg* arg)
{
    Client* c = focustop(selmon);

    if (!c || !c->isfloating)
        return;
    resize(c,
           (struct wlr_box){ .x = c->geom.x,
                             .y = c->geom.y,
                             .width = c->geom.width,
                             .height = MAX(1, c->geom.height + arg->i) },
           1);
}

void resizewidth(const Arg* arg)
{
    Client* c = focustop(selmon);

    if (!c)
        return;
    if (!c->isfloating) {
        /* A tiled client has no free geometry, so widen/narrow the master
         * area instead - g0wn's equivalent of resizing a sway split. */
        setmfact(&(Arg){ .f = arg->i > 0 ? +0.05f : -0.05f });
        return;
    }
    resize(c,
           (struct wlr_box){ .x = c->geom.x,
                             .y = c->geom.y,
                             .width = MAX(1, c->geom.width + arg->i),
                             .height = c->geom.height },
           1);
}

void run(char* startup_cmd)
{
    /* Add a Unix socket to the Wayland display. */
    const char* socket = wl_display_add_socket_auto(dpy);
    if (!socket)
        die("startup: display_add_socket_auto");
    setenv("WAYLAND_DISPLAY", socket, 1);

    /* Start the backend. This will enumerate outputs and inputs, become the DRM
     * master, etc */
    if (!wlr_backend_start(backend))
        die("startup: backend_start");

    /* Now that the socket exists and the backend is started, run the startup
     * command */
    autostartexec();
    if (startup_cmd) {
        if ((child_pid = fork()) < 0)
            die("startup: fork:");
        if (child_pid == 0) {
            close(STDIN_FILENO);
            setsid();
            execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
            die("startup: execl:");
        }
    }

    /* Mark stdout as non-blocking to avoid the startup script
     * causing g0wn to freeze when a user neither closes stdin
     * nor consumes standard input in his startup script */

    if (fd_set_nonblock(STDOUT_FILENO) < 0)
        close(STDOUT_FILENO);

    drawbars();

    /* At this point the outputs are initialized, choose initial selmon based on
     * cursor position, and set default cursor image */
    selmon = xytomon(cursor->x, cursor->y);

    /* TODO hack to get cursor to display in its initial location (100, 100)
     * instead of (0, 0) and then jumping. Still may not be fully
     * initialized, as the image/coordinates are not transformed for the
     * monitor when displayed here */
    wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
    wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

    /* Run the Wayland event loop. This does not return until you exit the
     * compositor. Starting the backend rigged up all of the necessary event
     * loop configuration to listen to libinput events, DRM events, generate
     * frame events at the refresh rate, and so on. */
    wl_display_run(dpy);
}

void unlastcursor(struct wl_listener* listener, void* data)
{
    /* When the surface is destroyed, clear our reference to it */
    last_cursor.surface = NULL;
    wl_list_remove(&last_cursor.destroy.link);
    wl_list_init(&last_cursor.destroy.link);
}

void scenebuffersetopacity(struct wlr_scene_buffer* buffer,
                           int sx,
                           int sy,
                           void* data)
{
    Client* c = data;
    /* xdg-popups hang off Client.scene, not Client.scene_surface, so this
     * never touches them */
    wlr_scene_buffer_set_opacity(
        buffer,
        c->isfullscreen || !opacity_enabled || !c->hasopacity ? 1.0f
                                                              : c->opacity);
}

/* Colours a client's border, remembering the scheme so the opacity toggle can
 * put it back on. wlroots wants a premultiplied colour, so the alpha scales
 * the other three channels with it. */
void setbordercolor(Client* c, int scheme)
{
    float color[4] = COLOR(colors[scheme][ColBorder]);
    float a = color[3] * decoopacity();

    c->borderscheme = scheme;
    color[0] *= a;
    color[1] *= a;
    color[2] *= a;
    color[3] = a;
    client_set_border_color(c, color);
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

void setfloating(Client* c, int floating)
{
    Client* p = client_get_parent(c);
    c->isfloating = floating;
    /* If in floating layout do not change the client's layer */
    if (!c->mon || !client_surface(c)->mapped ||
        !c->mon->lt[c->mon->sellt]->arrange)
        return;
    wlr_scene_node_reparent(
        &c->scene->node,
        layers[c->isfullscreen || (p && p->isfullscreen) ? LyrFS
               : c->isfloating                           ? LyrFloat
                                                         : LyrTile]);
    arrange(c->mon);
    drawbars();
}

void setfullscreen(Client* c, int fullscreen)
{
    c->isfullscreen = fullscreen;
    if (!c->mon || !client_surface(c)->mapped)
        return;
    c->bw = fullscreen ? 0 : borderpx;
    client_set_fullscreen(c, fullscreen);
    wlr_scene_node_reparent(&c->scene->node,
                            layers[c->isfullscreen ? LyrFS
                                   : c->isfloating ? LyrFloat
                                                   : LyrTile]);

    if (fullscreen) {
        c->prev = c->geom;
        resize(c, c->mon->m, 0);
    } else {
        /* restore previous size instead of arrange for floating windows since
         * client positions are set by the user and cannot be recalculated */
        resize(c, c->prev, 0);
    }
    arrange(c->mon);
    drawbars();
}

void setlayout(const Arg* arg)
{
    if (!selmon)
        return;
    if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
        selmon->sellt ^= 1;
    if (arg && arg->v)
        selmon->lt[selmon->sellt] = (Layout*)arg->v;
    snprintf(selmon->ltsymbol,
             LENGTH(selmon->ltsymbol),
             "%s",
             selmon->lt[selmon->sellt]->symbol);
    arrange(selmon);
    drawselbar();
}

/* arg > 1.0 will set mfact absolutely */
void setmfact(const Arg* arg)
{
    float f;

    if (!arg || !selmon || !selmon->lt[selmon->sellt]->arrange)
        return;
    f = arg->f < 1.0f ? arg->f + selmon->mfact : arg->f - 1.0f;
    if (f < 0.1 || f > 0.9)
        return;
    selmon->mfact = f;
    arrange(selmon);
}

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

void setmon(Client* c, Monitor* m, uint32_t newtags)
{
    Monitor* oldmon = c->mon;

    if (oldmon == m)
        return;
    c->mon = m;
    c->prev = c->geom;

    /* Scene graph sends surface leave/enter events on move and resize */
    if (oldmon)
        arrange(oldmon);
    if (m) {
        /* Make sure window actually overlaps with the monitor */
        resize(c, c->geom, 0);
        c->tags =
            newtags ? newtags
                    : m->tagset[m->seltags]; /* assign tags of target monitor */
        c->prev.x = (m->w.width - c->prev.width) / 2 + m->m.x;
        c->prev.y = (m->w.height - c->prev.height) / 2 + m->m.y;
        setfullscreen(c, c->isfullscreen); /* This will call arrange(c->mon) */
        setfloating(c, c->isfloating);
    }
    focusclient(focustop(selmon), 1);
}

/* arg->f is added to the opacity the focused client uses while focused */
void setopacityfocus(const Arg* arg)
{
    Client* sel = focustop(selmon);

    if (!sel)
        return;
    sel->opacity_focus =
        MIN(MAX(sel->opacity_focus + arg->f, OPACITY_MIN), 1.0f);
    sel->opacity = sel->opacity_focus;
    opacityrefresh();
}

/* the same for the opacity it falls back to once it loses focus */
void setopacityunfocus(const Arg* arg)
{
    Client* sel = focustop(selmon);

    if (!sel)
        return;
    sel->opacity_unfocus =
        MIN(MAX(sel->opacity_unfocus + arg->f, OPACITY_MIN), 1.0f);
}

void setpsel(struct wl_listener* listener, void* data)
{
    /* This event is raised by the seat when a client wants to set the
     * selection, usually when the user copies something. wlroots allows
     * compositors to ignore such requests if they so choose, but in g0wn we
     * always honor them
     */
    struct wlr_seat_request_set_primary_selection_event* event = data;
    wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

void setsel(struct wl_listener* listener, void* data)
{
    /* This event is raised by the seat when a client wants to set the
     * selection, usually when the user copies something. wlroots allows
     * compositors to ignore such requests if they so choose, but in g0wn we
     * always honor them
     */
    struct wlr_seat_request_set_selection_event* event = data;
    wlr_seat_set_selection(seat, event->source, event->serial);
}

void setup(void)
{
    int drm_fd, i, sig[] = { SIGCHLD, SIGINT, SIGTERM, SIGPIPE };
    struct sigaction sa = { .sa_flags = SA_RESTART, .sa_handler = handlesig };
    char cursor_size_str[8];
    sigemptyset(&sa.sa_mask);

    for (i = 0; i < (int)LENGTH(sig); i++)
        sigaction(sig[i], &sa, NULL);

    wlr_log_init(log_level, NULL);

    /* The Wayland display is managed by libwayland. It handles accepting
     * clients from the Unix socket, managing Wayland globals, and so on. */
    dpy = wl_display_create();
    event_loop = wl_display_get_event_loop(dpy);

    /* The backend is a wlroots feature which abstracts the underlying input and
     * output hardware. The autocreate option will choose the most suitable
     * backend based on the current environment, such as opening an X11 window
     * if an X11 server is running. */
    if (!(backend = wlr_backend_autocreate(event_loop, &session)))
        die("couldn't create backend");

    /* Initialize the scene graph used to lay out windows */
    scene = wlr_scene_create();
    root_bg = wlr_scene_rect_create(&scene->tree, 0, 0, rootcolor);
    for (i = 0; i < NUM_LAYERS; i++)
        layers[i] = wlr_scene_tree_create(&scene->tree);
    drag_icon = wlr_scene_tree_create(&scene->tree);
    wlr_scene_node_place_below(&drag_icon->node, &layers[LyrBlock]->node);

    /* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
     * can also specify a renderer using the WLR_RENDERER env var.
     * The renderer is responsible for defining the various pixel formats it
     * supports for shared memory, this configures that for clients. */
    if (!(drw = wlr_renderer_autocreate(backend)))
        die("couldn't create renderer");
    wl_signal_add(&drw->events.lost, &gpu_reset);

    /* Create shm, drm and linux_dmabuf interfaces by ourselves.
     * The simplest way is to call:
     *      wlr_renderer_init_wl_display(drw);
     * but we need to create the linux_dmabuf interface manually to integrate it
     * with wlr_scene. */
    wlr_renderer_init_wl_shm(drw, dpy);

    if (wlr_renderer_get_texture_formats(drw, WLR_BUFFER_CAP_DMABUF)) {
        wlr_drm_create(dpy, drw);
        wlr_scene_set_linux_dmabuf_v1(
            scene, wlr_linux_dmabuf_v1_create_with_renderer(dpy, 5, drw));
    }

    if ((drm_fd = wlr_renderer_get_drm_fd(drw)) >= 0 &&
        drw->features.timeline && backend->features.timeline)
        wlr_linux_drm_syncobj_manager_v1_create(dpy, 1, drm_fd);

    /* Autocreates an allocator for us.
     * The allocator is the bridge between the renderer and the backend. It
     * handles the buffer creation, allowing wlroots to render onto the
     * screen */
    if (!(alloc = wlr_allocator_autocreate(backend, drw)))
        die("couldn't create allocator");

    /* This creates some hands-off wlroots interfaces. The compositor is
     * necessary for clients to allocate surfaces and the data device manager
     * handles the clipboard. Each of these wlroots interfaces has room for you
     * to dig your fingers in and play with their behavior if you want. Note
     * that the clients cannot set the selection directly without compositor
     * approval, see the setsel() function. */
    compositor = wlr_compositor_create(dpy, 6, drw);
    wlr_subcompositor_create(dpy);
    wlr_data_device_manager_create(dpy);
    wlr_export_dmabuf_manager_v1_create(dpy);
    wlr_screencopy_manager_v1_create(dpy);
    wlr_ext_image_copy_capture_manager_v1_create(dpy, 1);
    wlr_ext_output_image_capture_source_manager_v1_create(dpy, 1);
    wlr_data_control_manager_v1_create(dpy);
    wlr_ext_data_control_manager_v1_create(dpy, 1);
    wlr_primary_selection_v1_device_manager_create(dpy);
    wlr_viewporter_create(dpy);
    wlr_single_pixel_buffer_manager_v1_create(dpy);
    wlr_fractional_scale_manager_v1_create(dpy, 1);
    wlr_presentation_create(dpy, backend, 2);
    wlr_alpha_modifier_v1_create(dpy);

    /* Initializes the interface used to implement urgency hints */
    activation = wlr_xdg_activation_v1_create(dpy);
    wl_signal_add(&activation->events.request_activate, &request_activate);

    wlr_scene_set_gamma_control_manager_v1(
        scene, wlr_gamma_control_manager_v1_create(dpy));

    power_mgr = wlr_output_power_manager_v1_create(dpy);
    wl_signal_add(&power_mgr->events.set_mode, &output_power_mgr_set_mode);

    /* Creates an output layout, which is a wlroots utility for working with an
     * arrangement of screens in a physical layout. */
    output_layout = wlr_output_layout_create(dpy);
    wl_signal_add(&output_layout->events.change, &layout_change);

    wlr_xdg_output_manager_v1_create(dpy, output_layout);

    /* Configure a listener to be notified when new outputs are available on the
     * backend. */
    wl_list_init(&mons);
    wl_signal_add(&backend->events.new_output, &new_output);

    /* Set up our client lists, the xdg-shell and the layer-shell. The xdg-shell
     * is a Wayland protocol which is used for application windows. For more
     * detail on shells, refer to the article:
     *
     * https://drewdevault.com/2018/07/29/Wayland-shells.html
     */
    wl_list_init(&clients);
    wl_list_init(&fstack);

    xdg_shell = wlr_xdg_shell_create(dpy, 6);
    wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_toplevel);
    wl_signal_add(&xdg_shell->events.new_popup, &new_xdg_popup);

    layer_shell = wlr_layer_shell_v1_create(dpy, 3);
    wl_signal_add(&layer_shell->events.new_surface, &new_layer_surface);

    idle_notifier = wlr_idle_notifier_v1_create(dpy);

    idle_inhibit_mgr = wlr_idle_inhibit_v1_create(dpy);
    wl_signal_add(&idle_inhibit_mgr->events.new_inhibitor, &new_idle_inhibitor);

    session_lock_mgr = wlr_session_lock_manager_v1_create(dpy);
    wl_signal_add(&session_lock_mgr->events.new_lock, &new_session_lock);
    locked_bg = wlr_scene_rect_create(layers[LyrBlock],
                                      sgeom.width,
                                      sgeom.height,
                                      (float[4]){ 0.1f, 0.1f, 0.1f, 1.0f });
    wlr_scene_node_set_enabled(&locked_bg->node, 0);

    /* Use decoration protocols to negotiate server-side decorations */
    wlr_server_decoration_manager_set_default_mode(
        wlr_server_decoration_manager_create(dpy),
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
    xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
    wl_signal_add(&xdg_decoration_mgr->events.new_toplevel_decoration,
                  &new_xdg_decoration);

    pointer_constraints = wlr_pointer_constraints_v1_create(dpy);
    wl_signal_add(&pointer_constraints->events.new_constraint,
                  &new_pointer_constraint);

    relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(dpy);

    /*
     * Creates a cursor, which is a wlroots utility for tracking the cursor
     * image shown on screen.
     */
    cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor, output_layout);

    /* Initialize the last_cursor destroy listener link so it's safe to remove
     * later */
    wl_list_init(&last_cursor.destroy.link);

    /* Creates an xcursor manager, another wlroots utility which loads up
     * Xcursor themes to source cursor images from and makes sure that cursor
     * images are available at all scale factors on the screen (necessary for
     * HiDPI support). Scaled cursors will be loaded with each output. */
    /* Passing NULL as the theme name makes libxcursor look for a theme called
     * "default", which many systems don't ship; wlroots then falls back to a
     * fixed-size built-in cursor that ignores cursor_size entirely. Naming a
     * theme explicitly in config.h avoids that. */
    snprintf(cursor_size_str, sizeof(cursor_size_str), "%d", cursor_size);
    cursor_mgr = wlr_xcursor_manager_create(cursor_theme, cursor_size);
    setenv("XCURSOR_SIZE", cursor_size_str, 1);
    if (cursor_theme)
        setenv("XCURSOR_THEME", cursor_theme, 1);

    /*
     * wlr_cursor *only* displays an image on screen. It does not move around
     * when the pointer moves. However, we can attach input devices to it, and
     * it will generate aggregate events for all of them. In these events, we
     * can choose how we want to process them, forwarding them to clients and
     * moving the cursor around. More detail on this process is described in
     * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html
     *
     * And more comments are sprinkled throughout the notify functions above.
     */
    wl_signal_add(&cursor->events.motion, &cursor_motion);
    wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
    wl_signal_add(&cursor->events.tablet_tool_axis, &cursor_tablet_axis);
    wl_signal_add(&cursor->events.tablet_tool_tip, &cursor_tablet_tip);
    wl_signal_add(&cursor->events.button, &cursor_button);
    wl_signal_add(&cursor->events.axis, &cursor_axis);
    wl_signal_add(&cursor->events.frame, &cursor_frame);

    cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(dpy, 1);
    wl_signal_add(&cursor_shape_mgr->events.request_set_shape,
                  &request_set_cursor_shape);

    /*
     * Configures a seat, which is a single "seat" at which a user sits and
     * operates the computer. This conceptually includes up to one keyboard,
     * pointer, touch, and drawing tablet device. We also rig up a listener to
     * let us know when new input devices are available on the backend.
     */
    wl_signal_add(&backend->events.new_input, &new_input_device);
    virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(dpy);
    wl_signal_add(&virtual_keyboard_mgr->events.new_virtual_keyboard,
                  &new_virtual_keyboard);
    virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(dpy);
    wl_signal_add(&virtual_pointer_mgr->events.new_virtual_pointer,
                  &new_virtual_pointer);

    seat = wlr_seat_create(dpy, "seat0");
    wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
    wl_signal_add(&seat->events.request_set_selection, &request_set_sel);
    wl_signal_add(&seat->events.request_set_primary_selection,
                  &request_set_psel);
    wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
    wl_signal_add(&seat->events.start_drag, &start_drag);

    kb_group = createkeyboardgroup();
    wl_list_init(&kb_group->destroy.link);

    output_mgr = wlr_output_manager_v1_create(dpy);
    wl_signal_add(&output_mgr->events.apply, &output_mgr_apply);
    wl_signal_add(&output_mgr->events.test, &output_mgr_test);

    drwl_init();

    status_event_source = wl_event_loop_add_fd(wl_display_get_event_loop(dpy),
                                               STDIN_FILENO,
                                               WL_EVENT_READABLE,
                                               statusin,
                                               NULL);

    /* Missing the session bus is not fatal: g0wn comes up without a tray
     * and/or bar notifications. */
    if (showbar &&
        (0
#ifdef SYSTRAY
         || showsystray
#endif
#ifdef NOTIFICATIONS
         || shownotifications
#endif
         )) {
        if ((bus_conn = dbus_bus_get(DBUS_BUS_SESSION, NULL)) &&
            (bus_source = startbus(bus_conn, event_loop))) {
#ifdef SYSTRAY
            if (showsystray)
                watcher_start(&watcher, bus_conn, event_loop);
#endif
#ifdef NOTIFICATIONS
            if (shownotifications)
                notify_start(
                    bus_conn, event_loop, notification_timeout, drawbars);
#endif
        } else
            fprintf(stderr,
                    "Couldn't connect to the session bus, "
                    "systray/notifications not available\n");
    }

    /* Make sure XWayland clients don't connect to the parent X server,
     * e.g when running in the x11 backend or the wayland backend and the
     * compositor has Xwayland support */
    unsetenv("DISPLAY");
#ifdef XWAYLAND
    /*
     * Initialise the XWayland X server.
     * It will be started when the first X client is started.
     */
    if ((xwayland = wlr_xwayland_create(dpy, compositor, 1))) {
        wl_signal_add(&xwayland->events.ready, &xwayland_ready);
        wl_signal_add(&xwayland->events.new_surface, &new_xwayland_surface);

        setenv("DISPLAY", xwayland->display_name, 1);
    } else {
        fprintf(stderr,
                "failed to setup XWayland X server, continuing without it\n");
    }
#endif
}

void spawn(const Arg* arg)
{
    if (fork() == 0) {
        close(STDIN_FILENO);
        open("/dev/null", O_RDWR);
        dup2(STDERR_FILENO, STDOUT_FILENO);
        setsid();
        execvp(((char**)arg->v)[0], (char**)arg->v);
        die("g0wn: execvp %s failed:", ((char**)arg->v)[0]);
    }
}

void startdrag(struct wl_listener* listener, void* data)
{
    struct wlr_drag* drag = data;
    if (!drag->icon)
        return;

    drag->icon->data = &wlr_scene_drag_icon_create(drag_icon, drag->icon)->node;
    LISTEN_STATIC(&drag->icon->events.destroy, destroydragicon);
}

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
void stopstatus(void)
{
    if (!status_event_source)
        return;

    wl_event_source_remove(status_event_source);
    status_event_source = NULL;
}

/* Exchanges two clients in the tiling order, which is all a layout looks at. */
void swapclients(Client* a, Client* b)
{
    struct wl_list *aprev = a->link.prev, *bprev = b->link.prev;

    if (aprev == &b->link) {
        wl_list_remove(&a->link);
        wl_list_insert(bprev, &a->link);
    } else if (bprev == &a->link) {
        wl_list_remove(&b->link);
        wl_list_insert(aprev, &b->link);
    } else {
        wl_list_remove(&a->link);
        wl_list_remove(&b->link);
        wl_list_insert(bprev, &a->link);
        wl_list_insert(aprev, &b->link);
    }
}

void tag(const Arg* arg)
{
    Client* sel = focustop(selmon);
    if (!sel || (arg->ui & TAGMASK) == 0)
        return;

    sel->tags = arg->ui & TAGMASK;
    focusclient(focustop(selmon), 1);
    arrange(selmon);
    drawbars();
}

/* i3/sway-style tabbed layout: the group behaves as a single window, and the
 * clients' title bars are packed into its one title row as tabs. */
void tabbed(Monitor* m)
{
    unsigned int e = m->gaps;
    struct wlr_box b;
    Client *c, *top;
    int n = 0;

    wl_list_for_each(c, &clients, link) if (VISIBLEON(c, m) && !c->isfloating &&
                                            !c->isfullscreen) n++;
    if (n == 0)
        return;
    if (smartgaps == n)
        e = 0;

    b.x = m->w.x + (int)(gappx * e);
    b.y = m->w.y + (int)(gappx * e);
    b.width = m->w.width - 2 * (int)(gappx * e);
    b.height = m->w.height - 2 * (int)(gappx * e);

    top = tabtop(m);
    wl_list_for_each(c, &clients, link)
    {
        if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
            continue;
        resize(c, b, 0);
        /* only the top tab's contents are drawn; scene_surface rather than
         * scene, since the title bars (the tabs) hang off the latter */
        wlr_scene_node_set_enabled(&c->scene_surface->node, c == top);
    }
    snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "|%d|", n);
    if (top)
        wlr_scene_node_raise_to_top(&top->scene->node);
}

Client* tabtop(Monitor* m)
{
    Client* c;
    wl_list_for_each(c, &fstack, flink) if (VISIBLEON(c, m) && !c->isfloating &&
                                            !c->isfullscreen) return c;
    return NULL;
}

unsigned int tagindex(uint32_t tagset)
{
    return tagset ? (unsigned int)__builtin_ctz(tagset) : 0;
}

void tagmon(const Arg* arg)
{
    Client* sel = focustop(selmon);
    if (sel)
        setmon(sel, dirtomon(arg->i), 0);
}

void tile(Monitor* m)
{
    unsigned int r, e = m->gaps, mw;
    int h, my, ty, i, n = 0;
    Client* c;

    wl_list_for_each(c, &clients, link) if (VISIBLEON(c, m) && !c->isfloating &&
                                            !c->isfullscreen) n++;
    if (n == 0)
        return;
    if (smartgaps == n)
        e = 0;

    if (n > m->nmaster)
        mw = m->nmaster ? (int)roundf((m->w.width + gappx * e) * m->mfact) : 0;
    else
        mw = m->w.width;
    i = 0;
    my = ty = gappx * e;
    wl_list_for_each(c, &clients, link)
    {
        if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
            continue;
        if (i < m->nmaster) {
            r = MIN(n, m->nmaster) - i;
            h = MAX(1, ((int)m->w.height - my - (int)(gappx * e * r)) / (int)r);
            resize(c,
                   (struct wlr_box){ .x = m->w.x + gappx * e,
                                     .y = m->w.y + my,
                                     .width = mw - 2 * gappx * e,
                                     .height = h },
                   0);
            my += c->geom.height + gappx * e;
        } else {
            r = n - i;
            h = MAX(1, ((int)m->w.height - ty - (int)(gappx * e * r)) / (int)r);
            resize(c,
                   (struct wlr_box){ .x = m->w.x + mw,
                                     .y = m->w.y + ty,
                                     .width = m->w.width - mw - gappx * e,
                                     .height = h },
                   0);
            ty += c->geom.height + gappx * e;
        }
        i++;
    }
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

void togglefloating(const Arg* arg)
{
    Client* sel = focustop(selmon);
    /* return if fullscreen */
    if (sel && !sel->isfullscreen)
        setfloating(sel, !sel->isfloating);
}

void togglefullscreen(const Arg* arg)
{
    Client* sel = focustop(selmon);
    if (sel)
        setfullscreen(sel, !sel->isfullscreen);
}

void togglegaps(const Arg* arg)
{
    selmon->gaps = !selmon->gaps;
    arrange(selmon);
}

void toggleopacity(const Arg* arg)
{
    opacity_enabled = !opacity_enabled;
    opacityrefresh();
}

/* Switches to the tabbed layout passed in arg, or back to the layout that was
 * selected before it if we are already tabbed - setlayout(NULL) flips lt[] back
 * to the other slot, which still holds it.
 *
 * Passing NULL rather than &(Arg){0}: Arg is a union, so that initialiser only
 * covers its first member (int i) and leaves the rest of ->v as whatever was on
 * the stack, which setlayout() then stores as the layout pointer. */
void toggletabbed(const Arg* arg)
{
    if (!selmon || !arg || !arg->v)
        return;
    setlayout(selmon->lt[selmon->sellt]->arrange == tabbed ? NULL : arg);
}

void toggletag(const Arg* arg)
{
    uint32_t newtags;
    Client* sel = focustop(selmon);
    if (!sel || !(newtags = sel->tags ^ (arg->ui & TAGMASK)))
        return;

    sel->tags = newtags;
    focusclient(focustop(selmon), 1);
    arrange(selmon);
    drawbars();
}

void toggleview(const Arg* arg)
{
    uint32_t newtagset;
    if (!(newtagset =
              selmon ? selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK)
                     : 0))
        return;

    selmon->tagset[selmon->seltags] = newtagset;
    focusclient(focustop(selmon), 1);
    arrange(selmon);
    drawbars();
}

void unlocksession(struct wl_listener* listener, void* data)
{
    SessionLock* lock = wl_container_of(listener, lock, unlock);
    destroylock(lock, 1);
}

void unmaplayersurfacenotify(struct wl_listener* listener, void* data)
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

void unmapnotify(struct wl_listener* listener, void* data)
{
    /* Called when the surface is unmapped, and should no longer be shown. */
    Client* c = wl_container_of(listener, c, unmap);
    if (c == grabc) {
        cursor_mode = CurNormal;
        grabc = NULL;
    }

    if (client_is_unmanaged(c)) {
        if (c == exclusive_focus) {
            exclusive_focus = NULL;
            focusclient(focustop(selmon), 1);
        }
    } else {
        wl_list_remove(&c->link);
        setmon(c, NULL, 0);
        wl_list_remove(&c->flink);
    }

    /* The wl_surface can outlive the mapping (e.g. a client holding an idle
     * inhibitor on it), so drop the back-pointer to the scene tree we are
     * about to free instead of leaving it dangling. mapnotify() sets it again
     * on the next map. */
    if (client_surface(c))
        client_surface(c)->data = NULL;
    wlr_scene_node_destroy(&c->scene->node);
    c->title = NULL;
#ifdef INTEGRATED_BACKGROUND
    c->blur = NULL;
    c->blurbuf = NULL;
    c->titleblur = NULL;
    c->titleblurbuf = NULL;
#endif
    bufpooldrop(c->titlepool, LENGTH(c->titlepool));
    c->titlebufw = 0;
    drawbars();
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
    m->t.height = m->drw->font->height + (int)titlepadding;
    m->t.real_height = (int)((float)m->t.height / m->wlr_output->scale);

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

void updatetitle(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, set_title);
    if (c == focustop(c->mon))
        drawbars();
}

void urgent(struct wl_listener* listener, void* data)
{
    struct wlr_xdg_activation_v1_request_activate_event* event = data;
    Client* c = NULL;
    toplevel_from_wlr_surface(event->surface, &c, NULL);
    if (!c || c == focustop(selmon))
        return;

    c->isurgent = 1;
    drawbars();

    if (client_surface(c)->mapped)
        setbordercolor(c, SchemeUrg);
}

void view(const Arg* arg)
{
    unsigned int i;

    if (!selmon || (arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
        return;

    i = tagindex(selmon->tagset[selmon->seltags]);
    selmon->taglt[i][0] = selmon->lt[0];
    selmon->taglt[i][1] = selmon->lt[1];
    selmon->tagsellt[i] = selmon->sellt;

    selmon->seltags ^= 1; /* toggle sel tagset */
    if (arg->ui & TAGMASK)
        selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;

    i = tagindex(selmon->tagset[selmon->seltags]);
    selmon->lt[0] = selmon->taglt[i][0];
    selmon->lt[1] = selmon->taglt[i][1];
    selmon->sellt = selmon->tagsellt[i];

    focusclient(focustop(selmon), 1);
    arrange(selmon);
    drawbars();
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

Monitor* xytomon(double x, double y)
{
    struct wlr_output* o = wlr_output_layout_output_at(output_layout, x, y);
    return o ? o->data : NULL;
}

void xytonode(double x,
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

void zoom(const Arg* arg)
{
    Client *c, *sel = focustop(selmon);

    if (!sel || !selmon || !selmon->lt[selmon->sellt]->arrange ||
        sel->isfloating)
        return;

    /* Search for the first tiled window that is not sel, marking sel as
     * NULL if we pass it along the way */
    wl_list_for_each(c, &clients, link)
    {
        if (VISIBLEON(c, selmon) && !c->isfloating) {
            if (c != sel)
                break;
            sel = NULL;
        }
    }

    /* Return if no other tiled window was found */
    if (&c->link == &clients)
        return;

    /* If we passed sel, move c to the front; otherwise, move sel to the
     * front */
    if (!sel)
        sel = c;
    wl_list_remove(&sel->link);
    wl_list_insert(&clients, &sel->link);

    focusclient(sel, 1);
    arrange(selmon);
}

#ifdef XWAYLAND
void activatex11(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, activate);

    /* Only "managed" windows can be activated */
    if (!client_is_unmanaged(c))
        wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

void associatex11(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, associate);

    LISTEN(&client_surface(c)->events.map, &c->map, mapnotify);
    LISTEN(&client_surface(c)->events.unmap, &c->unmap, unmapnotify);
}

void configurex11(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, configure);
    struct wlr_xwayland_surface_configure_event* event = data;
    if (!client_surface(c) || !client_surface(c)->mapped) {
        wlr_xwayland_surface_configure(c->surface.xwayland,
                                       event->x,
                                       event->y,
                                       event->width,
                                       event->height);
        return;
    }
    /* Unmanaged clients never get a monitor, so this precedes the c->mon
     * check below. */
    if (client_is_unmanaged(c)) {
        wlr_scene_node_set_position(&c->scene->node, event->x, event->y);
        wlr_xwayland_surface_configure(c->surface.xwayland,
                                       event->x,
                                       event->y,
                                       event->width,
                                       event->height);
        return;
    }
    /* c->mon is dereferenced below: closemon() leaves mapped clients without a
     * monitor whenever the last output goes away. */
    if (!c->mon) {
        wlr_xwayland_surface_configure(c->surface.xwayland,
                                       event->x,
                                       event->y,
                                       event->width,
                                       event->height);
        return;
    }
    /* A fullscreen client does not get to pick its own geometry. Games under
     * XWayland keep re-asserting their internal resolution, and honouring that
     * would shrink the window to a box in the corner of the black fullscreen
     * backdrop while the game happily keeps rendering. Restate the size it
     * actually has instead. */
    if (c->isfullscreen) {
        resize(c, c->mon->m, 0);
        return;
    }

    if ((c->isfloating && c != grabc) || !c->mon->lt[c->mon->sellt]->arrange) {
        resize(c,
               (struct wlr_box){ .x = event->x - c->bw,
                                 .y = event->y - c->bw,
                                 .width = event->width + c->bw * 2,
                                 .height = event->height + c->bw * 2 },
               0);
    } else {
        arrange(c->mon);
    }
}

void createnotifyx11(struct wl_listener* listener, void* data)
{
    struct wlr_xwayland_surface* xsurface = data;
    Client* c;

    /* Allocate a Client for this surface */
    c = xsurface->data = ecalloc(1, sizeof(*c));
    c->surface.xwayland = xsurface;
    c->type = X11;
    c->bw = client_is_unmanaged(c) ? 0 : borderpx;
    c->opacity = c->opacity_unfocus = opacity_unfocus;
    c->opacity_focus = opacity_focus;

    /* Listen to the various events it can emit */
    LISTEN(&xsurface->events.associate, &c->associate, associatex11);
    LISTEN(&xsurface->events.destroy, &c->destroy, destroynotify);
    LISTEN(&xsurface->events.dissociate, &c->dissociate, dissociatex11);
    LISTEN(&xsurface->events.request_activate, &c->activate, activatex11);
    LISTEN(&xsurface->events.request_configure, &c->configure, configurex11);
    LISTEN(
        &xsurface->events.request_fullscreen, &c->fullscreen, fullscreennotify);
    LISTEN(&xsurface->events.set_hints, &c->set_hints, sethints);
    LISTEN(&xsurface->events.set_title, &c->set_title, updatetitle);
}

void dissociatex11(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, dissociate);
    wl_list_remove(&c->map.link);
    wl_list_remove(&c->unmap.link);
}

void sethints(struct wl_listener* listener, void* data)
{
    Client* c = wl_container_of(listener, c, set_hints);
    struct wlr_surface* surface = client_surface(c);
    if (c == focustop(selmon) || !c->surface.xwayland->hints)
        return;

    c->isurgent = xcb_icccm_wm_hints_get_urgency(c->surface.xwayland->hints);
    drawbars();

    if (c->isurgent && surface && surface->mapped)
        setbordercolor(c, SchemeUrg);
}

void xwaylandready(struct wl_listener* listener, void* data)
{
    struct wlr_xcursor* xcursor;

    /* assign the one and only seat */
    wlr_xwayland_set_seat(xwayland, seat);

    /* Set the default XWayland cursor to match the rest of g0wn. */
    if ((xcursor = wlr_xcursor_manager_get_xcursor(cursor_mgr, "default", 1)))
        wlr_xwayland_set_cursor(
            xwayland,
            wlr_xcursor_image_get_buffer(xcursor->images[0]),
            xcursor->images[0]->hotspot_x,
            xcursor->images[0]->hotspot_y);
}
#endif

int main(int argc, char* argv[])
{
    char* startup_cmd = NULL;
    int c;

    while ((c = getopt(argc, argv, "s:hdv")) != -1) {
        if (c == 's')
            startup_cmd = optarg;
        else if (c == 'd')
            log_level = WLR_DEBUG;
        else if (c == 'v')
            die("g0wn " VERSION);
        else
            goto usage;
    }
    if (optind < argc)
        goto usage;

    /* Wayland requires XDG_RUNTIME_DIR for creating its communications socket
     */
    if (!getenv("XDG_RUNTIME_DIR"))
        die("XDG_RUNTIME_DIR must be set");
    setup();
    run(startup_cmd);
    cleanup();
    return EXIT_SUCCESS;

usage:
    die("Usage: %s [-v] [-d] [-s startup command]", argv[0]);
}
