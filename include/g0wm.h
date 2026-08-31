/*
 * See LICENSE file for copyright and license details.
 */
#ifndef G0WM_H
#define G0WM_H

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
/* vendored, and every helper in it is static */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "drwl.h"
#pragma GCC diagnostic pop
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
#ifdef TITLEBAR
    struct wlr_scene_buffer* title;
#endif
#ifdef INTEGRATED_BACKGROUND
    struct wlr_scene_buffer* blur; /* frosted wallpaper, below the whole box */
    struct wlr_buffer* blurbuf;    /* what blur's node was last handed */
#ifdef TITLEBAR
    struct wlr_scene_buffer* titleblur; /* the same, for the title bar */
    struct wlr_buffer* titleblurbuf;
#endif
#endif
#ifdef TITLEBAR
    Buffer* titlepool[2];
    int titlex, titlew; /* title bar placement, relative to the border box */
    int titlebufw;      /* pixel width titlepool was allocated at */
#endif
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
    struct wlr_buffer* wallpaperbuf; /* what wallpaper's node was last handed */
    struct wlr_scene_buffer* barblur; /* the bar's own frosted backdrop */
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
#ifdef TITLEBAR
    struct {
        int height;
        int real_height; /* non-scaled */
    } t;                 /* per-client title bar */
#endif
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
void arrange(Monitor* m);
void arrangelayers(Monitor* m);
void axisnotify(struct wl_listener* listener, void* data);
bool baracceptsinput(struct wlr_scene_buffer* buffer, double* sx, double* sy);
#ifdef INTEGRATED_BACKGROUND
void blurbar(Monitor* m);
void blurclient(Client* c);
#endif /* INTEGRATED_BACKGROUND */
void bufdestroy(struct wlr_buffer* buffer);
bool bufdatabegin(struct wlr_buffer* buffer,
                  uint32_t flags,
                  void** data,
                  uint32_t* format,
                  size_t* stride);
void bufdataend(struct wlr_buffer* buffer);
Buffer* bufget(Buffer** pool, size_t poollen, int width, int height);
void bufpooldrop(Buffer** pool, size_t poollen);
void buttonpress(struct wl_listener* listener, void* data);
void chvt(const Arg* arg);
void checkidleinhibitor(struct wlr_surface* exclude);
void closemon(Monitor* m);
void createdecoration(struct wl_listener* listener, void* data);
KeyboardGroup* createkeyboardgroup(void);
void createlayersurface(struct wl_listener* listener, void* data);
void createmon(struct wl_listener* listener, void* data);
void createnotify(struct wl_listener* listener, void* data);
void createpointerconstraint(struct wl_listener* listener, void* data);
void createpopup(struct wl_listener* listener, void* data);
void cursorframe(struct wl_listener* listener, void* data);
float decoopacity(void);
void destroylocksurface(struct wl_listener* listener, void* data);
void destroynotify(struct wl_listener* listener, void* data);
void destroykeyboardgroup(struct wl_listener* listener, void* data);
void drawbar(Monitor* m);
void drawbars(void);
void drawselbar(void);
int drawstatus(Monitor* m, const char* text, int x, int w, int render);
void focusclient(Client* c, int lift);
void focusmon(const Arg* arg);
void focusstack(const Arg* arg);
Client* focustop(Monitor* m);
void fullscreennotify(struct wl_listener* listener, void* data);
void incnmaster(const Arg* arg);
void inputdevice(struct wl_listener* listener, void* data);
void killclient(const Arg* arg);
void locksession(struct wl_listener* listener, void* data);
void mapnotify(struct wl_listener* listener, void* data);
void monocle(Monitor* m);
void movestack(const Arg* arg);
void motionabsolute(struct wl_listener* listener, void* data);
void tabletaxis(struct wl_listener* listener, void* data);
void tablettip(struct wl_listener* listener, void* data);
void motionnotify(uint32_t time,
                  struct wlr_input_device* device,
                  double sx,
                  double sy,
                  double sx_unaccel,
                  double sy_unaccel);
void motionrelative(struct wl_listener* listener, void* data);
void moveresize(const Arg* arg);
#ifdef NOTIFICATIONS
void notifyclick(const Arg* arg);
void notifydismiss(const Arg* arg);
#endif /* NOTIFICATIONS */
int opacityallowed(const char* appid);
void outputmgrapply(struct wl_listener* listener, void* data);
void outputmgrtest(struct wl_listener* listener, void* data);
void powermgrsetmode(struct wl_listener* listener, void* data);
void quit(const Arg* arg);
void requeststartdrag(struct wl_listener* listener, void* data);
void resize(Client* c, struct wlr_box geo, int interact);
void resizeheight(const Arg* arg);
void resizewidth(const Arg* arg);
void run(char* startup_cmd);
#ifdef RUNNER
int runnercalc(double* out);
void runnerkey(xkb_keysym_t sym, uint32_t mods, uint32_t codepoint);
const char* runnersuggest(void);
void runnertoggle(const Arg* arg);
#endif /* RUNNER */
void scenebuffersetopacity(struct wlr_scene_buffer* buffer,
                           int sx,
                           int sy,
                           void* data);
void setbordercolor(Client* c, int scheme);
void setcursor(struct wl_listener* listener, void* data);
void setcursorshape(struct wl_listener* listener, void* data);
void setfloating(Client* c, int floating);
void setlayout(const Arg* arg);
void setmfact(const Arg* arg);
#ifdef TITLEBAR
void settitle(Client* c);
int titleheight(Client* c);
#endif /* TITLEBAR */
void setmon(Client* c, Monitor* m, uint32_t newtags);
void setopacityfocus(const Arg* arg);
void setopacityunfocus(const Arg* arg);
void setpsel(struct wl_listener* listener, void* data);
void setsel(struct wl_listener* listener, void* data);
#ifdef INTEGRATED_BACKGROUND
void setwallpaper(Monitor* m);
#endif /* INTEGRATED_BACKGROUND */
void spawn(const Arg* arg);
void startdrag(struct wl_listener* listener, void* data);
int statusin(int fd, unsigned int mask, void* data);
void swapclients(Client* a, Client* b);
void tag(const Arg* arg);
void tabbed(Monitor* m);
Client* tabtop(Monitor* m);
void tagmon(const Arg* arg);
void tile(Monitor* m);
void togglebar(const Arg* arg);
void togglefloating(const Arg* arg);
void togglefullscreen(const Arg* arg);
void togglegaps(const Arg* arg);
void toggleopacity(const Arg* arg);
void toggletabbed(const Arg* arg);
void toggletag(const Arg* arg);
#ifdef TITLEBAR
void toggletitlebar(const Arg* arg);
#endif /* TITLEBAR */
void toggleview(const Arg* arg);
#ifdef SYSTRAY
void trayactivate(const Arg* arg);
void traymenu(const Arg* arg);
#endif /* SYSTRAY */
void unmapnotify(struct wl_listener* listener, void* data);
void updatemons(struct wl_listener* listener, void* data);
void updatebar(Monitor* m);
void updatetitle(struct wl_listener* listener, void* data);
void urgent(struct wl_listener* listener, void* data);
void view(const Arg* arg);
void virtualkeyboard(struct wl_listener* listener, void* data);
void virtualpointer(struct wl_listener* listener, void* data);
void warpcursor(const Client* c);
Monitor* xytomon(double x, double y);
void zoom(const Arg* arg);
#ifdef XWAYLAND
void createnotifyx11(struct wl_listener* listener, void* data);
void xwaylandready(struct wl_listener* listener, void* data);
#endif /* XWAYLAND */

/* variables */
extern int locked;
extern void* exclusive_focus;
extern struct wl_event_loop* event_loop;
extern struct wlr_backend* backend;
extern struct wlr_scene* scene;
extern struct wlr_scene_tree* layers[NUM_LAYERS];
extern struct wlr_scene_tree* drag_icon;
extern struct wlr_renderer* drw;
extern struct wlr_allocator* alloc;
extern struct wlr_compositor* compositor;
extern struct wl_list clients; /* tiling order */
extern struct wl_list fstack;  /* focus order */
extern struct wlr_idle_notifier_v1* idle_notifier;
extern struct wlr_output_manager_v1* output_mgr;
extern struct wlr_pointer_constraints_v1* pointer_constraints;
extern struct wlr_relative_pointer_manager_v1* relative_pointer_mgr;
extern struct wlr_cursor* cursor;
extern struct wlr_xcursor_manager* cursor_mgr;
extern struct last_cursor_state {
    enum wp_cursor_shape_device_v1_shape shape;
    struct wlr_surface* surface;
    bool has_client_cursor;
    int hotspot_x;
    int hotspot_y;
    struct wl_listener destroy;
} last_cursor;
extern struct wlr_scene_rect* root_bg;
#ifdef INTEGRATED_BACKGROUND
extern GdkPixbuf* wallpaper_src; /* full-res decode, cached across resizes */
#endif                           /* INTEGRATED_BACKGROUND */
extern struct wlr_scene_rect* locked_bg;
extern struct wlr_seat* seat;
extern KeyboardGroup* kb_group;
extern unsigned int cursor_mode;
extern Client* grabc;
extern struct wlr_output_layout* output_layout;
extern struct wlr_box sgeom;
extern struct wl_list mons;
extern Monitor* selmon;
extern char stext[256];
extern struct wl_event_source* status_event_source;
#ifdef SYSTRAY
extern Watcher watcher;
#endif /* SYSTRAY */
#ifdef RUNNER
extern int runner_active;
extern char runner_buf[256];
extern int runner_len;
extern int runner_cur;       /* cursor offset into runner_buf, 0..runner_len */
extern int runner_repeating; /* the armed repeat belongs to the prompt */
#endif                       /* RUNNER */
extern struct wl_listener request_activate;
#ifdef XWAYLAND
extern struct wlr_xwayland* xwayland;
#endif /* XWAYLAND */

/* configuration, allows nested code to access above variables. Each
 * module gets its own copy of the settings: config.h declares them
 * static, and no single module reads all of them. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "config.h"
#pragma GCC diagnostic pop

/* attempt to encapsulate suck into one file */
#include "client.h"

#endif /* G0WM_H */
