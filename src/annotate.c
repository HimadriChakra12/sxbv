/*
 * annotate.c -- pencil & highlighter annotation tools.
 *
 * Strokes are stored per-page as a flat list of thick line segments,
 * with endpoints/thickness normalised to the page pixmap's width and
 * height at the zoom level active when they were drawn. This lets
 * strokes rescale correctly on zoom changes. See the AnnotSeg comment
 * in sxbv.h for the current limitation around page rotation.
 *
 * Compositing happens directly on the BGRX pixel buffer produced by
 * to_bgrx(), before it becomes an XImage -- this gives true alpha
 * blending for the highlighter without needing an X compositor.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sxbv.h"

/* ------------------------------------------------------------------ */
/* Color parsing (accepts "#rrggbb" or any X11 color name)             */

static void parse_color(Viewer *v, const char *spec,
                         unsigned char *r, unsigned char *g, unsigned char *b)
{
    XColor xc;
    if (XParseColor(v->dpy, v->cmap, spec, &xc)) {
        *r = xc.red   >> 8;
        *g = xc.green >> 8;
        *b = xc.blue  >> 8;
    } else {
        fprintf(stderr, "sxbv: cannot parse color '%s'\n", spec);
    }
}

/* ------------------------------------------------------------------ */
/* Defaults / lifecycle                                                 */

void annot_config_defaults(Viewer *v)
{
    v->pencil_thickness    = PENCIL_DEFAULT_THICKNESS;
    v->highlight_thickness = HIGHLIGHT_DEFAULT_THICKNESS;

    v->pencil_palette_idx    = PENCIL_DEFAULT_PRESET;
    v->highlight_palette_idx = HIGHLIGHT_DEFAULT_PRESET;
    parse_color(v, annot_palette[v->pencil_palette_idx],
                &v->pencil_r, &v->pencil_g, &v->pencil_b);
    parse_color(v, annot_palette[v->highlight_palette_idx],
                &v->highlight_r, &v->highlight_g, &v->highlight_b);

    v->annot_mode   = ANNOT_NONE;
    v->annot_drawing = 0;
    v->have_pointer  = 0;
    v->color_input_mode = 0;
    v->color_input_buf[0] = '\0';
    v->bar_forced = 0;

    v->stroke_touched = NULL;
    v->stroke_touched_w = v->stroke_touched_h = 0;
    v->stroke_pending_start = 1;
}

void annot_free_all(Viewer *v)
{
    if (v->page_annots) {
        for (int i = 0; i < v->annot_page_count; i++)
            free(v->page_annots[i].segs);
        free(v->page_annots);
        v->page_annots = NULL;
    }
    v->annot_page_count = 0;

    free(v->annot_bgrx);
    v->annot_bgrx   = NULL;
    v->annot_bgrx_w = 0;
    v->annot_bgrx_h = 0;

    free(v->stroke_touched);
    v->stroke_touched = NULL;
    v->stroke_touched_w = v->stroke_touched_h = 0;
}

void annot_init(Viewer *v)
{
    annot_free_all(v);
    if (v->page_count <= 0) return;
    v->page_annots = calloc((size_t)v->page_count, sizeof(PageAnnots));
    v->annot_page_count = v->page_annots ? v->page_count : 0;
}

int annot_active(Viewer *v)
{
    return v->annot_mode != ANNOT_NONE;
}

/* ------------------------------------------------------------------ */
/* Mode toggling                                                        */

void annot_toggle(Viewer *v, AnnotMode m)
{
    if (v->mode == MODE_THUMB) return; /* tools only make sense on a page */

    if (v->annot_mode == m) {
        /* toggling the same tool off */
        v->annot_mode = ANNOT_NONE;
        v->annot_drawing = 0;
        if (v->bar_forced) {
            v->bar_forced = 0;
            /* leave the bar visible; user can hit 'b' to hide it again */
        }
    } else {
        v->annot_mode = m;
        v->annot_drawing = 0;
        if (!v->bar_visible) {
            v->bar_visible = 1;
            v->bar_forced   = 1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Per-tool color / thickness accessors                                */

static unsigned char *cur_r(Viewer *v) { return v->annot_mode == ANNOT_PENCIL ? &v->pencil_r : &v->highlight_r; }
static unsigned char *cur_g(Viewer *v) { return v->annot_mode == ANNOT_PENCIL ? &v->pencil_g : &v->highlight_g; }
static unsigned char *cur_b(Viewer *v) { return v->annot_mode == ANNOT_PENCIL ? &v->pencil_b : &v->highlight_b; }
static float          *cur_thick(Viewer *v) { return v->annot_mode == ANNOT_PENCIL ? &v->pencil_thickness : &v->highlight_thickness; }
static int            *cur_pidx(Viewer *v) { return v->annot_mode == ANNOT_PENCIL ? &v->pencil_palette_idx : &v->highlight_palette_idx; }

static int find_palette_idx(Viewer *v, unsigned char r, unsigned char g, unsigned char b)
{
    for (int i = 0; i < ANNOT_PALETTE_LEN; i++) {
        unsigned char pr, pg, pb;
        parse_color(v, annot_palette[i], &pr, &pg, &pb);
        if (pr == r && pg == g && pb == b) return i;
    }
    return -1;
}

void annot_color_cycle(Viewer *v, int dir)
{
    if (!annot_active(v)) return;
    int *idx = cur_pidx(v);
    if (*idx < 0) {
        int found = find_palette_idx(v, *cur_r(v), *cur_g(v), *cur_b(v));
        *idx = (found >= 0) ? found : 0;
    }
    *idx = ((*idx + dir) % ANNOT_PALETTE_LEN + ANNOT_PALETTE_LEN) % ANNOT_PALETTE_LEN;
    parse_color(v, annot_palette[*idx], cur_r(v), cur_g(v), cur_b(v));
}

void annot_select_preset(Viewer *v, int idx)
{
    if (!annot_active(v)) return;
    if (idx < 0 || idx >= ANNOT_PALETTE_LEN) return;
    *cur_pidx(v) = idx;
    parse_color(v, annot_palette[idx], cur_r(v), cur_g(v), cur_b(v));
}

void annot_thickness_adjust(Viewer *v, float d)
{
    if (!annot_active(v)) return;
    float *t = cur_thick(v);
    *t += d;
    if (*t < ANNOT_THICK_MIN) *t = ANNOT_THICK_MIN;
    if (*t > ANNOT_THICK_MAX) *t = ANNOT_THICK_MAX;
}

void annot_undo(Viewer *v)
{
    if (!annot_active(v)) return;
    if (v->page < 0 || v->page >= v->annot_page_count) return;
    PageAnnots *pa = &v->page_annots[v->page];
    if (pa->count > 0) {
        pa->count--;
        annot_rebuild(v); /* can't un-blend a pixel, so replay from scratch */
    }
}

/* ------------------------------------------------------------------ */
/* Color-input prompt (mirrors search_mode's bar prompt)               */

void annot_color_input_start(Viewer *v)
{
    if (!annot_active(v)) return;
    v->color_input_mode = 1;
    v->color_input_buf[0] = '\0';
}

void annot_color_input_key(Viewer *v, KeySym ks, const char *buf, int len)
{
    if (ks == XK_Return || ks == XK_KP_Enter) {
        if (v->color_input_buf[0]) {
            XColor xc;
            if (XParseColor(v->dpy, v->cmap, v->color_input_buf, &xc)) {
                *cur_r(v) = xc.red   >> 8;
                *cur_g(v) = xc.green >> 8;
                *cur_b(v) = xc.blue  >> 8;
                *cur_pidx(v) = -1; /* custom color, not a palette slot */
            } else {
                fprintf(stderr, "sxbv: unknown color '%s'\n", v->color_input_buf);
            }
        }
        v->color_input_mode = 0;
    } else if (ks == XK_Escape) {
        v->color_input_mode = 0;
    } else if (ks == XK_BackSpace) {
        int sl = strlen(v->color_input_buf);
        if (sl > 0) v->color_input_buf[sl - 1] = '\0';
    } else if (len > 0 && buf[0] >= 32) {
        int sl = strlen(v->color_input_buf);
        if (sl + len < (int)sizeof(v->color_input_buf) - 1)
            strcat(v->color_input_buf, buf);
    }
}

/* ------------------------------------------------------------------ */
/* Pointer -> normalised page coordinates                               */

static int win_to_page_norm(Viewer *v, int wx, int wy, float *nx, float *ny)
{
    if (!v->pix || v->pix_w <= 0 || v->pix_h <= 0) return 0;
    int bar_off = (v->bar_visible && topbar) ? v->bar_h : 0;
    float px = (float)(wx - v->scroll_x);
    float py = (float)(wy - v->scroll_y - bar_off);
    *nx = px / (float)v->pix_w;
    *ny = py / (float)v->pix_h;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Stroke capture                                                       */

static void push_seg(Viewer *v, float nx0, float ny0, float nx1, float ny1)
{
    if (v->page < 0 || v->page >= v->annot_page_count) return;
    PageAnnots *pa = &v->page_annots[v->page];

    if (pa->count == pa->cap) {
        int newcap = pa->cap ? pa->cap * 2 : 64;
        AnnotSeg *n = realloc(pa->segs, (size_t)newcap * sizeof(AnnotSeg));
        if (!n) return;
        pa->segs = n;
        pa->cap  = newcap;
    }

    AnnotSeg *s = &pa->segs[pa->count++];
    s->nx0 = nx0; s->ny0 = ny0; s->nx1 = nx1; s->ny1 = ny1;
    s->thickness = *cur_thick(v) / (float)v->pix_w;
    s->r = *cur_r(v); s->g = *cur_g(v); s->b = *cur_b(v);
    s->alpha = (v->annot_mode == ANNOT_HIGHLIGHT) ? HIGHLIGHT_ALPHA : 255;
    s->multiply = (v->annot_mode == ANNOT_HIGHLIGHT) ? 1 : 0;
    s->stroke_start = (unsigned char)v->stroke_pending_start;
    v->stroke_pending_start = 0;
}

void annot_button(Viewer *v, XButtonEvent *be, int press)
{
    if (!annot_active(v) || v->mode == MODE_THUMB) return;
    if (be->button != Button1) return;

    if (press) {
        float nx, ny;
        if (!win_to_page_norm(v, be->x, be->y, &nx, &ny)) return;
        v->annot_drawing  = 1;
        v->annot_last_nx  = nx;
        v->annot_last_ny  = ny;
        v->stroke_pending_start = 1;
        /* dot on click-without-drag: zero-length segment still rasterises
         * as a filled circle in annot_composite(). */
        push_seg(v, nx, ny, nx, ny);
        annot_composite_last_segment(v);
    } else {
        v->annot_drawing = 0;
    }
}

void annot_motion(Viewer *v, XMotionEvent *me)
{
    v->ptr_x = me->x;
    v->ptr_y = me->y;
    v->have_pointer = 1;

    if (!annot_active(v) || !v->annot_drawing || v->mode == MODE_THUMB) return;

    float nx, ny;
    if (!win_to_page_norm(v, me->x, me->y, &nx, &ny)) return;
    push_seg(v, v->annot_last_nx, v->annot_last_ny, nx, ny);
    annot_composite_last_segment(v);
    v->annot_last_nx = nx;
    v->annot_last_ny = ny;
}

/* ------------------------------------------------------------------ */
/* Compositing onto the rendered page                                   */

static inline void blend_px(unsigned int *px, unsigned char r, unsigned char g,
                             unsigned char b, unsigned char a, int multiply)
{
    if (!multiply && a == 255) {
        *px = ((unsigned int)r << 16) | ((unsigned int)g << 8) | b;
        return;
    }
    unsigned int old = *px;
    unsigned int or_ = (old >> 16) & 0xff, og = (old >> 8) & 0xff, ob = old & 0xff;

    unsigned int tr, tg, tb; /* blend target: multiplied color, or flat color */
    if (multiply) {
        /* Real highlighter behavior: saturates light/white background,
         * leaves dark text close to untouched (base*color/255 stays
         * near 0 when base is already dark) -- unlike a flat alpha-over
         * blend, which washes out text too and reads as "watercolor". */
        tr = (or_ * r) / 255;
        tg = (og * g) / 255;
        tb = (ob * b) / 255;
    } else {
        tr = r; tg = g; tb = b;
    }

    unsigned int nr = (tr * a + or_ * (255 - a)) / 255;
    unsigned int ng = (tg * a + og * (255 - a)) / 255;
    unsigned int nb = (tb * a + ob * (255 - a)) / 255;
    *px = (nr << 16) | (ng << 8) | nb;
}

static void raster_thick_segment(unsigned int *buf, int w, int h,
                                  float x0, float y0, float x1, float y1,
                                  float half_thick,
                                  unsigned char r, unsigned char g, unsigned char b,
                                  unsigned char a, int multiply,
                                  unsigned char *touched)
{
    if (half_thick < 0.5f) half_thick = 0.5f;

    int minx = (int)floorf(fminf(x0, x1) - half_thick);
    int maxx = (int)ceilf (fmaxf(x0, x1) + half_thick);
    int miny = (int)floorf(fminf(y0, y1) - half_thick);
    int maxy = (int)ceilf (fmaxf(y0, y1) + half_thick);

    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > w) maxx = w;
    if (maxy > h) maxy = h;
    if (minx >= maxx || miny >= maxy) return;

    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    float ht2 = half_thick * half_thick;

    for (int y = miny; y < maxy; y++) {
        unsigned int *row = buf + (size_t)y * w;
        unsigned char *trow = touched ? touched + (size_t)y * w : NULL;
        for (int x = minx; x < maxx; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float dist2;
            if (len2 < 1e-6f) {
                float ex = px - x0, ey = py - y0;
                dist2 = ex * ex + ey * ey;
            } else {
                float t = ((px - x0) * dx + (py - y0) * dy) / len2;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                float cx = x0 + t * dx, cy = y0 + t * dy;
                float ex = px - cx, ey = py - cy;
                dist2 = ex * ex + ey * ey;
            }
            if (dist2 <= ht2) {
                /* Coverage semantics, not accumulation: once this
                 * stroke has colored a pixel, going over it again
                 * (e.g. a scribble crossing itself) must not darken it
                 * further -- that's what real highlighters/browser
                 * text-highlight do, and what made overlaps look
                 * patchy instead of a uniform solid color. */
                if (trow) {
                    if (trow[x]) continue;
                    trow[x] = 1;
                }
                blend_px(&row[x], r, g, b, a, multiply);
            }
        }
    }
}

static void composite_seg(unsigned int *buf, int w, int h, const AnnotSeg *s,
                           unsigned char *touched)
{
    float x0 = s->nx0 * (float)w, y0 = s->ny0 * (float)h;
    float x1 = s->nx1 * (float)w, y1 = s->ny1 * (float)h;
    float half = (s->thickness * (float)w) / 2.0f;
    raster_thick_segment(buf, w, h, x0, y0, x1, y1, half, s->r, s->g, s->b, s->alpha,
                          s->multiply, s->multiply ? touched : NULL);
}

void annot_composite(Viewer *v, unsigned char *bgrx, int w, int h)
{
    if (!bgrx) return;
    if (v->page < 0 || v->page >= v->annot_page_count) return;

    PageAnnots *pa = &v->page_annots[v->page];
    if (pa->count == 0) return;

    /* One global coverage mask for the entire highlight layer on this
     * page. A pixel that has been multiplied by ANY highlight stroke is
     * marked touched and skipped by every subsequent one -- this is
     * what real highlighters and browser text-highlight do: overlapping
     * the same region again produces the same solid color, not a darker
     * compounded result. The mask is NOT reset between strokes.
     * Pencil segments (multiply==0) ignore the mask entirely. */
    unsigned char *touched = calloc((size_t)w * h, 1);

    unsigned int *buf = (unsigned int *)bgrx;
    for (int i = 0; i < pa->count; i++)
        composite_seg(buf, w, h, &pa->segs[i], touched);

    free(touched);
}

/* Full rebuild: re-converts the current page to BGRX and replays every
 * stored segment onto it. This is O(page pixels + total segments) and
 * is only called when the *base* image changes (new page rendered,
 * zoom/rotate change) or when a stroke is removed (undo) -- never on
 * every mouse-move. */
void annot_rebuild(Viewer *v)
{
    free(v->annot_bgrx);
    v->annot_bgrx   = NULL;
    v->annot_bgrx_w = 0;
    v->annot_bgrx_h = 0;

    if (!v->pix) return;

    unsigned char *base = to_bgrx(v);
    if (!base) return;

    v->annot_bgrx   = base;
    v->annot_bgrx_w = v->pix_w;
    v->annot_bgrx_h = v->pix_h;

    /* Invalidate the live touched mask -- the full replay below will
     * build fresh coverage from scratch, and subsequent live segments
     * need to start from that same state, not the previous page's mask. */
    if (v->stroke_touched) {
        memset(v->stroke_touched, 0,
               (size_t)v->stroke_touched_w * v->stroke_touched_h);
        v->stroke_touched_w = 0; /* force realloc to new page size */
        v->stroke_touched_h = 0;
    }

    annot_composite(v, v->annot_bgrx, v->annot_bgrx_w, v->annot_bgrx_h);
}

/* Ensure the per-page highlight coverage mask matches the current page
 * size. Never reset between strokes -- the whole point is that any
 * pixel already multiplied by a previous stroke on this page is skipped
 * by every subsequent one (same behavior as full rebuild). The mask is
 * only cleared by annot_rebuild() when the base image changes. */
static unsigned char *stroke_touched_for(Viewer *v)
{
    if (v->stroke_touched_w != v->annot_bgrx_w || v->stroke_touched_h != v->annot_bgrx_h) {
        free(v->stroke_touched);
        v->stroke_touched = calloc((size_t)v->annot_bgrx_w * v->annot_bgrx_h, 1);
        v->stroke_touched_w = v->annot_bgrx_w;
        v->stroke_touched_h = v->annot_bgrx_h;
    }
    return v->stroke_touched;
}

/* Incremental update: blend just the most-recently-added segment onto
 * the already-cached composited buffer. O(that segment's bounding
 * box) -- this is what keeps drawing responsive regardless of how
 * many segments a page already has or how high the zoom is. */
void annot_composite_last_segment(Viewer *v)
{
    if (!v->annot_bgrx) return;
    if (v->page < 0 || v->page >= v->annot_page_count) return;

    PageAnnots *pa = &v->page_annots[v->page];
    if (pa->count == 0) return;

    AnnotSeg *s = &pa->segs[pa->count - 1];
    unsigned char *touched = s->multiply ? stroke_touched_for(v) : NULL;
    composite_seg((unsigned int *)v->annot_bgrx, v->annot_bgrx_w, v->annot_bgrx_h, s, touched);
}

/* ------------------------------------------------------------------ */
/* Thickness-preview cursor ring                                        */

void annot_draw_cursor(Viewer *v, Drawable dst)
{
    if (!annot_active(v) || !v->have_pointer || v->mode == MODE_THUMB) return;

    float thick_px = *cur_thick(v);
    int radius = (int)(thick_px / 2.0f);
    if (radius < 1) radius = 1;

    XColor ring;
    XParseColor(v->dpy, v->cmap, ANNOT_CURSOR_RING_COLOR, &ring);
    XAllocColor(v->dpy, v->cmap, &ring);

    GC gc = v->gc;
    unsigned long saved_fg = 0; /* GC has no query in Xlib without XGetGCValues; use dedicated calls */
    (void)saved_fg;

    XSetForeground(v->dpy, gc, ring.pixel);
    XDrawArc(v->dpy, dst, gc,
              v->ptr_x - radius, v->ptr_y - radius,
              radius * 2, radius * 2, 0, 360 * 64);

    /* Small filled dot at the true center so thin strokes are still
     * easy to place precisely. */
    XFillArc(v->dpy, dst, gc,
              v->ptr_x - 1, v->ptr_y - 1, 2, 2, 0, 360 * 64);
}
