#ifndef SXBV_H
#define SXBV_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include "mupdf/fitz.h"
#include "config.h"

typedef enum { FIT_NONE, FIT_WIDTH, FIT_HEIGHT, FIT_PAGE } FitMode;

typedef enum {
    CMD_NONE,
    CMD_SCROLL_DOWN, CMD_SCROLL_UP,
    CMD_SCROLL_LEFT, CMD_SCROLL_RIGHT,
    CMD_SCREEN_DOWN, CMD_SCREEN_UP,
    CMD_NEXT_PAGE,   CMD_PREV_PAGE,
    CMD_FIRST_PAGE,  CMD_LAST_PAGE,
    CMD_ZOOM_IN,     CMD_ZOOM_OUT,    CMD_ZOOM_RESET,
    CMD_FIT_WIDTH,   CMD_FIT_HEIGHT,  CMD_FIT_PAGE,
    CMD_ROTATE_CW,   CMD_ROTATE_CCW,
    CMD_FULLSCREEN,
    CMD_SEARCH_START, CMD_SEARCH_NEXT, CMD_SEARCH_PREV,
    CMD_QUIT,
} Command;

typedef struct {
    KeySym       ks;
    unsigned int mod;
    Command      cmd;
} Keybind;

/* A resolved color: both an XftColor (for text) and an
 * unsigned long pixel value (for XSetForeground fills) */
typedef struct {
    XftColor     xft;
    unsigned long pixel;
} SpdfColor;

typedef struct {
    /* MuPDF */
    fz_context  *ctx;
    fz_document *doc;
    int          page_count;
    int          page;
    float        zoom;
    int          rotation;
    FitMode      fit;

    /* rendered page */
    fz_pixmap   *pix;
    int          pix_w, pix_h;
    int          scroll_x, scroll_y;

    /* X11 */
    Display     *dpy;
    int          screen;
    Visual      *visual;
    Colormap     cmap;
    Window       win;
    GC           gc;
    Atom         wm_delete;
    Atom         net_wm_state;
    Atom         net_wm_fullscreen;
    int          win_w, win_h;
    int          fullscreen;

    /* Xft */
    XftDraw     *xftdraw;
    XftFont     *font;
    int          bar_h;   /* height of status bar in pixels */

    /* resolved colors */
    SpdfColor    c_bg;     /* window/bar background */
    SpdfColor    c_fg;     /* bar text              */
    SpdfColor    c_mark;   /* search hit            */
    SpdfColor    c_sel;    /* active search hit     */
    SpdfColor    c_pagebg; /* page area background  */

    /* search */
    char         search_buf[MAX_SEARCH];
    int          search_mode;
    int          search_dir;
    fz_quad      hits[MAX_HITS];
    int          hit_count;
    int          hit;

    /* numeric prefix */
    int          num_buf;
    int          num_valid;

    const char  *filename;
} Viewer;

/* image.c */
fz_matrix      page_matrix(Viewer *v);
void           render_page(Viewer *v);
unsigned char *to_bgrx(Viewer *v);

/* window.c */
int  win_init(Viewer *v);
void win_draw(Viewer *v);
void win_update_title(Viewer *v);
void win_toggle_fullscreen(Viewer *v);

/* search.c */
void search_free(Viewer *v);
void search_do(Viewer *v, int dir);

/* main.c */
void clamp_scroll(Viewer *v);
void go_page(Viewer *v, int p);
void zoom_by(Viewer *v, float d);

#endif /* SXBV_H */
