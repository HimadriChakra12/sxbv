/*
 * sxbv.h -- shared types and declarations for sxbv.
 *
 * The MuPDF backend (fitz.h, fz_context, fz_document, fz_pixmap, fz_quad)
 * has been replaced by the vendored Poppler/Splash backend exposed through
 * src/pdfshim.h.  This file includes pdfshim.h; no other source file
 * should include poppler headers directly.
 */

#ifndef SXBV_H
#define SXBV_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include "pdfshim.h"
#include "config.h"

/* ------------------------------------------------------------------ */
/* Enumerations                                                        */
/* ------------------------------------------------------------------ */

typedef enum { FIT_NONE, FIT_WIDTH, FIT_HEIGHT, FIT_PAGE } FitMode;

typedef enum {
    CMD_TOGGLE_BAR,
    CMD_TOGGLE_FILENAME,
    CMD_TOGGLE_ZOOM,
    CMD_TOGGLE_FITMODE,
    CMD_TOGGLE_ROTATION_IND,
    CMD_TOGGLE_FULLPATH,

    CMD_TOGGLE_THUMB,
    CMD_THUMB_OPEN,
    CMD_THUMB_DOWN,
    CMD_THUMB_UP,
    CMD_THUMB_LEFT,
    CMD_THUMB_RIGHT,
    CMD_THUMB_FIRST,
    CMD_THUMB_LAST,

    CMD_NONE,
    CMD_DOWN,
    CMD_UP,
    CMD_LEFT,
    CMD_RIGHT,
    CMD_SCROLL_DOWN,
    CMD_SCROLL_UP,
    CMD_SCROLL_LEFT,
    CMD_SCROLL_RIGHT,
    CMD_SCREEN_DOWN, CMD_SCREEN_UP,
    CMD_NEXT_PAGE,   CMD_PREV_PAGE,
    CMD_FIRST_PAGE,  CMD_LAST_PAGE,
    CMD_ZOOM_IN,     CMD_ZOOM_OUT,    CMD_ZOOM_RESET,
    CMD_FIT_WIDTH,   CMD_FIT_HEIGHT,  CMD_FIT_PAGE,
    CMD_ROTATE_CW,   CMD_ROTATE_CCW,
    CMD_FULLSCREEN,
    CMD_SEARCH_START, CMD_SEARCH_NEXT, CMD_SEARCH_PREV,

    CMD_TOGGLE_HIGHLIGHT, CMD_TOGGLE_PENCIL,
    CMD_ANNOT_COLOR_PREV, CMD_ANNOT_COLOR_NEXT,
    CMD_ANNOT_THICK_DEC,  CMD_ANNOT_THICK_INC,
    CMD_ANNOT_COLOR_INPUT,
    CMD_ANNOT_UNDO,

    CMD_QUIT,
} Command;

/* ------------------------------------------------------------------ */
/* Keybinding                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    KeySym       ks;
    unsigned int mod;
    Command      cmd;
} Keybind;

/* ------------------------------------------------------------------ */
/* View modes                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    MODE_NORMAL,
    MODE_THUMB,
} ViewMode;

/* ------------------------------------------------------------------ */
/* Annotation tools (pencil / highlighter)                             */
/* ------------------------------------------------------------------ */

typedef enum {
    ANNOT_NONE,
    ANNOT_PENCIL,
    ANNOT_HIGHLIGHT,
} AnnotMode;

/* One drawn segment. Endpoints and thickness are normalised to the
 * page pixmap's width/height *at the zoom level they were drawn*, so
 * strokes rescale correctly when the user zooms in or out afterwards.
 * (Rotation changes swap pixmap w/h; re-mapping annotations across a
 * rotation change is a known limitation left for a later pass.) */
typedef struct {
    float nx0, ny0, nx1, ny1;  /* endpoints, 0..1 range            */
    float thickness;           /* fraction of page pixmap width    */
    unsigned char r, g, b;
    unsigned char alpha;       /* 255 = opaque pencil, translucent for highlighter */
} AnnotSeg;

typedef struct {
    AnnotSeg *segs;
    int       count;
    int       cap;
} PageAnnots;

/* ------------------------------------------------------------------ */
/* Thumbnail browser entry                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    char          *path;       /* full path                 */
    char          *name;       /* basename only             */
    unsigned char *data;       /* BGRx pixel data or NULL   */
    XImage        *img;        /* X image or NULL           */
    int            rendered;   /* 1 once thumb is ready     */
} ThumbEntry;

/* ------------------------------------------------------------------ */
/* Resolved colour: Xft + raw pixel for XSetForeground                */
/* ------------------------------------------------------------------ */

typedef struct {
    XftColor     xft;
    unsigned long pixel;
} SpdfColor;

/* ------------------------------------------------------------------ */
/* Main viewer state                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    /* PDF backend (Poppler via pdfshim) */
    PdfDoc  *doc;           /* open document, or NULL         */
    PdfPix  *pix;           /* last rendered page pixmap      */
    int      page_count;
    int      page;          /* current page, 0-based          */
    float    zoom;          /* linear scale: 1.0 = 72 dpi     */
    int      rotation;      /* degrees: 0 / 90 / 180 / 270   */
    FitMode  fit;

    /* rendered page dimensions (pixels) */
    int pix_w, pix_h;
    int scroll_x, scroll_y;

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
    int          bar_h;     /* status bar height in pixels */

    /* resolved colours */
    SpdfColor    c_bg;      /* window / bar background */
    SpdfColor    c_fg;      /* bar text                */
    SpdfColor    c_mark;    /* search hit              */
    SpdfColor    c_sel;     /* active search hit       */
    SpdfColor    c_pagebg;  /* page area background    */

    /* search */
    char     search_buf[MAX_SEARCH];
    int      search_mode;
    int      search_dir;
    PdfRect  hits[MAX_HITS]; /* rects in PDF point space, 72-dpi origin */
    int      hit_count;
    int      hit;

    /* view (thumbnail) */
    ViewMode  mode;

    /* file browser */
    ThumbEntry *files;
    int         file_count;
    int         thumb_sel;
    int         thumb_scroll;
    int         thumb_cols;
    int         thumb_next;
    int         thumb_w;
    int         thumb_h;
    char       *thumb_dir;

    /* double buffer */
    Pixmap  backbuf;
    int     backbuf_w;
    int     backbuf_h;

    /* numeric prefix (for "5j" style motions) */
    int  num_buf;
    int  num_valid;

    const char *filename;
    int         filename_owned;

    /* status bar toggles */
    int bar_visible;
    int show_filename;
    int show_pagelabel;
    int show_zoom;
    int show_fitmode;
    int show_rotation;
    int show_fullscreen_indicator;
    int show_fullpath;

    /* ---- annotation tools ---- */
    AnnotMode annot_mode;

    float pencil_thickness;
    unsigned char pencil_r, pencil_g, pencil_b;
    int pencil_palette_idx;

    float highlight_thickness;
    unsigned char highlight_r, highlight_g, highlight_b;
    int highlight_palette_idx;

    PageAnnots *page_annots;     /* one entry per page   */
    int         annot_page_count; /* size of page_annots  */

    /* Persistent cache: current page's pixels, pre-converted to BGRX,
     * with all of that page's strokes already blended in. Rebuilt in
     * full only when the base image changes (page/zoom/rotate) or a
     * stroke is removed; updated incrementally (just the new
     * segment's bbox) while actively drawing. This is what keeps
     * drawing responsive -- see annot_rebuild()/annot_composite_last_segment(). */
    unsigned char *annot_bgrx;
    int annot_bgrx_w, annot_bgrx_h;

    int   annot_drawing;         /* button currently held while drawing */
    float annot_last_nx, annot_last_ny;

    int have_pointer;            /* pointer position known this session */
    int ptr_x, ptr_y;            /* last pointer position, window coords */

    int  color_input_mode;
    char color_input_buf[64];

    int bar_forced;              /* bar was auto-shown by entering an annot mode */
} Viewer;

/* ------------------------------------------------------------------ */
/* Function declarations                                               */
/* ------------------------------------------------------------------ */

/* image.c */
void           render_page(Viewer *v);
unsigned char *to_bgrx(Viewer *v);

/* window.c */
int  win_init(Viewer *v);
void win_draw(Viewer *v);
void win_update_title(Viewer *v);
void win_toggle_fullscreen(Viewer *v);
void win_draw_bar(Viewer *v);
void win_draw_bar_to(Viewer *v, Drawable dst);

/* search.c */
void search_free(Viewer *v);
void search_do(Viewer *v, int dir);

/* main.c */
void clamp_scroll(Viewer *v);
void go_page(Viewer *v, int p);
void zoom_by(Viewer *v, float d);

/* thumb.c */
void thumb_init_dir(Viewer *v, const char *dir);
void thumb_free(Viewer *v);
void thumb_draw(Viewer *v);
void thumb_render_next(Viewer *v);
void thumb_scroll_to_sel(Viewer *v);

/* annotate.c */
void annot_config_defaults(Viewer *v);   /* call once, after win_init() */
void annot_init(Viewer *v);              /* (re)allocate per-page storage */
void annot_free_all(Viewer *v);
int  annot_active(Viewer *v);            /* mode != ANNOT_NONE */

void annot_toggle(Viewer *v, AnnotMode m);
void annot_color_cycle(Viewer *v, int dir);
void annot_thickness_adjust(Viewer *v, float d);
void annot_undo(Viewer *v);

void annot_color_input_start(Viewer *v);
void annot_color_input_key(Viewer *v, KeySym ks, const char *buf, int len);

void annot_button(Viewer *v, XButtonEvent *be, int press);
void annot_motion(Viewer *v, XMotionEvent *me);

/* Blend this page's strokes into a BGRX (0x00RRGGBB words) pixel buffer
 * of the given dimensions, matching to_bgrx()'s output format. */
void annot_composite(Viewer *v, unsigned char *bgrx, int w, int h);

/* Full rebuild of v->annot_bgrx from v->pix + all stored segments.
 * Call after render_page() (page/zoom/rotate changed) and after undo. */
void annot_rebuild(Viewer *v);

/* Incremental: blend only the most-recently-added segment onto the
 * already-cached v->annot_bgrx. Call right after adding a segment
 * while actively drawing. */
void annot_composite_last_segment(Viewer *v);

/* Draw the thickness-preview ring around the last known pointer
 * position, when in pencil/highlight mode. */
void annot_draw_cursor(Viewer *v, Drawable dst);

#endif /* SXBV_H */
