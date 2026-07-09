/*
 * annotate.c -- pencil & highlighter annotation tools.
 *
 * Two separate palettes (highlight_palette[], pencil_palette[]) are
 * defined in config.h. Highlighter: 5 colors, 1-5 to select.
 * Pencil: 10 colors, 1-0 to select. Both share [ / ] for cycling.
 *
 * Blending:
 *   Pencil    -- flat opaque overwrite.
 *   Highlight -- multiply blend (real marker behavior: saturates white
 *                background, leaves dark text untouched). One global
 *                coverage mask per page ensures overlapping highlight
 *                strokes don't compound (same color result as one pass).
 *
 * Performance:
 *   v->annot_bgrx is a persistent buffer: full rebuild only on
 *   page/zoom change or undo/redo. Each new segment during live
 *   drawing is blended incrementally (its bounding box only).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sxbv.h"

/* ------------------------------------------------------------------ */
/* Color parsing                                                        */

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
    v->pencil_palette_idx    = PENCIL_DEFAULT_PRESET;
    v->highlight_palette_idx = HIGHLIGHT_DEFAULT_PRESET;

    parse_color(v, pencil_palette[v->pencil_palette_idx],
                &v->pencil_r, &v->pencil_g, &v->pencil_b);
    parse_color(v, highlight_palette[v->highlight_palette_idx],
                &v->highlight_r, &v->highlight_g, &v->highlight_b);

    v->pencil_thickness    = PENCIL_DEFAULT_THICKNESS;
    v->highlight_thickness = HIGHLIGHT_DEFAULT_THICKNESS;

    v->annot_mode          = ANNOT_NONE;
    v->annot_drawing       = 0;
    v->have_pointer        = 0;
    v->bar_forced          = 0;

    v->stroke_touched      = NULL;
    v->stroke_touched_w    = v->stroke_touched_h = 0;
    v->stroke_pending_start = 1;

    v->redo_stack = NULL;
    v->redo_count = v->redo_cap = 0;
    v->redo_page  = -1;
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
    v->annot_bgrx = NULL;
    v->annot_bgrx_w = v->annot_bgrx_h = 0;

    free(v->stroke_touched);
    v->stroke_touched = NULL;
    v->stroke_touched_w = v->stroke_touched_h = 0;

    free(v->redo_stack);
    v->redo_stack = NULL;
    v->redo_count = v->redo_cap = 0;
    v->redo_page  = -1;
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
/* Per-tool current-state accessors                                    */

static unsigned char *cur_r(Viewer *v)    { return v->annot_mode == ANNOT_PENCIL ? &v->pencil_r    : &v->highlight_r; }
static unsigned char *cur_g(Viewer *v)    { return v->annot_mode == ANNOT_PENCIL ? &v->pencil_g    : &v->highlight_g; }
static unsigned char *cur_b(Viewer *v)    { return v->annot_mode == ANNOT_PENCIL ? &v->pencil_b    : &v->highlight_b; }
static float         *cur_thick(Viewer *v){ return v->annot_mode == ANNOT_PENCIL ? &v->pencil_thickness : &v->highlight_thickness; }
static int           *cur_pidx(Viewer *v) { return v->annot_mode == ANNOT_PENCIL ? &v->pencil_palette_idx : &v->highlight_palette_idx; }

static int cur_palette_len(Viewer *v) {
    return v->annot_mode == ANNOT_PENCIL ? PENCIL_PALETTE_LEN : HIGHLIGHT_PALETTE_LEN;
}
static const char *cur_palette_entry(Viewer *v, int i) {
    return v->annot_mode == ANNOT_PENCIL ? pencil_palette[i] : highlight_palette[i];
}

/* ------------------------------------------------------------------ */
/* Mode toggling                                                        */

void annot_toggle(Viewer *v, AnnotMode m)
{
    if (v->mode == MODE_THUMB) return;
    if (v->annot_mode == m) {
        v->annot_mode  = ANNOT_NONE;
        v->annot_drawing = 0;
        if (v->bar_forced) v->bar_forced = 0;
    } else {
        v->annot_mode  = m;
        v->annot_drawing = 0;
        if (!v->bar_visible) { v->bar_visible = 1; v->bar_forced = 1; }
    }
}

/* ------------------------------------------------------------------ */
/* Color / thickness                                                   */

static void apply_palette(Viewer *v, int idx)
{
    parse_color(v, cur_palette_entry(v, idx), cur_r(v), cur_g(v), cur_b(v));
    *cur_pidx(v) = idx;
}

void annot_select_preset(Viewer *v, int idx)
{
    if (!annot_active(v)) return;
    int plen = cur_palette_len(v);
    if (idx < 0 || idx >= plen) return;
    apply_palette(v, idx);
}

void annot_color_cycle(Viewer *v, int dir)
{
    if (!annot_active(v)) return;
    int plen = cur_palette_len(v);
    int idx  = *cur_pidx(v);
    idx = ((idx + dir) % plen + plen) % plen;
    apply_palette(v, idx);
}

void annot_thickness_adjust(Viewer *v, float d)
{
    if (!annot_active(v)) return;
    float *t = cur_thick(v);
    *t += d;
    if (*t < ANNOT_THICK_MIN) *t = ANNOT_THICK_MIN;
    if (*t > ANNOT_THICK_MAX) *t = ANNOT_THICK_MAX;
}

/* ------------------------------------------------------------------ */
/* Undo / redo                                                          */

static void clear_redo(Viewer *v)
{
    v->redo_count = 0;
    v->redo_page  = -1;
}

void annot_undo(Viewer *v)
{
    if (v->page < 0 || v->page >= v->annot_page_count) return;
    PageAnnots *pa = &v->page_annots[v->page];
    if (pa->count == 0) return;

    /* Find the start of the last stroke (last segment with stroke_start=1,
     * or seg 0 if none). Remove all segments from there to end. */
    int stroke_begin = 0;
    for (int i = pa->count - 1; i > 0; i--) {
        if (pa->segs[i].stroke_start) { stroke_begin = i; break; }
    }

    /* Push removed segments onto redo stack (in order) */
    if (v->redo_page != v->page) clear_redo(v);
    v->redo_page = v->page;
    int n = pa->count - stroke_begin;
    if (v->redo_count + n > v->redo_cap) {
        int newcap = v->redo_cap + n + 64;
        AnnotSeg *ns = realloc(v->redo_stack, (size_t)newcap * sizeof(AnnotSeg));
        if (ns) { v->redo_stack = ns; v->redo_cap = newcap; }
    }
    if (v->redo_stack && v->redo_count + n <= v->redo_cap) {
        memcpy(v->redo_stack + v->redo_count, pa->segs + stroke_begin,
               (size_t)n * sizeof(AnnotSeg));
        v->redo_count += n;
    }

    pa->count = stroke_begin;
    v->stroke_pending_start = 1;
    annot_rebuild(v);
}

void annot_redo(Viewer *v)
{
    if (v->redo_count == 0) return;
    if (v->page < 0 || v->page >= v->annot_page_count) return;
    if (v->redo_page != v->page) { clear_redo(v); return; }

    /* Find the last stroke on redo stack -- work backwards from top
     * to find stroke_start boundary, then pop that whole stroke. */
    int stroke_begin = v->redo_count - 1;
    while (stroke_begin > 0 && !v->redo_stack[stroke_begin].stroke_start)
        stroke_begin--;

    int n = v->redo_count - stroke_begin;
    PageAnnots *pa = &v->page_annots[v->page];
    if (pa->count + n > pa->cap) {
        int newcap = pa->cap + n + 64;
        AnnotSeg *ns = realloc(pa->segs, (size_t)newcap * sizeof(AnnotSeg));
        if (!ns) return;
        pa->segs = ns; pa->cap = newcap;
    }
    memcpy(pa->segs + pa->count, v->redo_stack + stroke_begin,
           (size_t)n * sizeof(AnnotSeg));
    pa->count += n;
    v->redo_count -= n;

    annot_rebuild(v);
}

/* ------------------------------------------------------------------ */
/* Save (export annotations to a flat text file alongside the PDF)     */

void annot_save(Viewer *v)
{
    if (!v->filename || !v->doc) return;
    if (v->annot_page_count == 0) return;

    /* Build PdfInkStroke arrays per page */
    const PdfInkStroke **stroke_arrays = calloc((size_t)v->annot_page_count,
                                                 sizeof(PdfInkStroke*));
    int *n_per_page = calloc((size_t)v->annot_page_count, sizeof(int));
    if (!stroke_arrays || !n_per_page) { free(stroke_arrays); free(n_per_page); return; }

    for (int pg = 0; pg < v->annot_page_count; pg++) {
        PageAnnots *pa = &v->page_annots[pg];
        if (pa->count == 0) { stroke_arrays[pg] = NULL; n_per_page[pg] = 0; continue; }

        PdfInkStroke *sarr = malloc((size_t)pa->count * sizeof(PdfInkStroke));
        if (!sarr) { stroke_arrays[pg] = NULL; n_per_page[pg] = 0; continue; }

        for (int i = 0; i < pa->count; i++) {
            AnnotSeg *s = &pa->segs[i];
            sarr[i].nx0            = s->nx0;
            sarr[i].ny0            = s->ny0;
            sarr[i].nx1            = s->nx1;
            sarr[i].ny1            = s->ny1;
            sarr[i].thickness_norm = s->thickness;
            sarr[i].r              = s->r;
            sarr[i].g              = s->g;
            sarr[i].b              = s->b;
            sarr[i].type           = s->multiply ? 'H' : 'I';
        }
        stroke_arrays[pg] = sarr;
        n_per_page[pg]    = pa->count;
    }

    int rc = pdf_annot_save(v->doc,
                            stroke_arrays, n_per_page,
                            v->annot_page_count,
                            v->filename);

    for (int pg = 0; pg < v->annot_page_count; pg++)
        free((void*)stroke_arrays[pg]);
    free(stroke_arrays);
    free(n_per_page);

    if (rc == 0)
        fprintf(stderr, "sxbv: annotations saved to '%s'\n", v->filename);
    else
        fprintf(stderr, "sxbv: failed to save annotations to '%s'\n", v->filename);
}

/* ------------------------------------------------------------------ */
/* Stroke capture                                                       */

static int win_to_page_norm(Viewer *v, int wx, int wy, float *nx, float *ny)
{
    if (!v->pix || v->pix_w <= 0 || v->pix_h <= 0) return 0;
    int bar_off = (v->bar_visible && topbar) ? v->bar_h : 0;
    *nx = (float)(wx - v->scroll_x)            / (float)v->pix_w;
    *ny = (float)(wy - v->scroll_y - bar_off)  / (float)v->pix_h;
    return 1;
}

static void push_seg(Viewer *v, float nx0, float ny0, float nx1, float ny1)
{
    if (v->page < 0 || v->page >= v->annot_page_count) return;
    PageAnnots *pa = &v->page_annots[v->page];
    if (pa->count == pa->cap) {
        int newcap = pa->cap ? pa->cap * 2 : 64;
        AnnotSeg *n = realloc(pa->segs, (size_t)newcap * sizeof(AnnotSeg));
        if (!n) return;
        pa->segs = n; pa->cap = newcap;
    }
    AnnotSeg *s = &pa->segs[pa->count++];
    s->nx0 = nx0; s->ny0 = ny0; s->nx1 = nx1; s->ny1 = ny1;
    s->thickness   = *cur_thick(v) / (float)v->pix_w;
    s->r = *cur_r(v); s->g = *cur_g(v); s->b = *cur_b(v);
    s->alpha       = HIGHLIGHT_ALPHA;
    s->multiply    = (v->annot_mode == ANNOT_HIGHLIGHT) ? 1 : 0;
    s->stroke_start = (unsigned char)v->stroke_pending_start;
    v->stroke_pending_start = 0;

    /* New segment invalidates redo stack */
    clear_redo(v);
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
        push_seg(v, nx, ny, nx, ny);
        annot_composite_last_segment(v);
    } else {
        v->annot_drawing = 0;
        v->stroke_pending_start = 1; /* next mousedown starts fresh stroke */
    }
}

void annot_motion(Viewer *v, XMotionEvent *me)
{
    v->ptr_x = me->x; v->ptr_y = me->y; v->have_pointer = 1;
    if (!annot_active(v) || !v->annot_drawing || v->mode == MODE_THUMB) return;
    float nx, ny;
    if (!win_to_page_norm(v, me->x, me->y, &nx, &ny)) return;
    push_seg(v, v->annot_last_nx, v->annot_last_ny, nx, ny);
    annot_composite_last_segment(v);
    v->annot_last_nx = nx;
    v->annot_last_ny = ny;
}

/* ------------------------------------------------------------------ */
/* Pixel blending                                                      */

static inline void blend_px(unsigned int *px,
                             unsigned char r, unsigned char g, unsigned char b,
                             unsigned char a, int multiply)
{
    unsigned int old = *px;
    unsigned int or_ = (old >> 16) & 0xff;
    unsigned int og  = (old >>  8) & 0xff;
    unsigned int ob  =  old        & 0xff;

    unsigned int tr, tg, tb;
    if (multiply) {
        tr = (or_ * r) / 255;
        tg = (og  * g) / 255;
        tb = (ob  * b) / 255;
    } else {
        if (a == 255) { *px = ((unsigned int)r<<16)|((unsigned int)g<<8)|b; return; }
        tr = r; tg = g; tb = b;
    }
    unsigned int nr = (tr * a + or_ * (255 - a)) / 255;
    unsigned int ng = (tg * a + og  * (255 - a)) / 255;
    unsigned int nb = (tb * a + ob  * (255 - a)) / 255;
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
    int minx = (int)floorf(fminf(x0,x1) - half_thick);
    int maxx = (int)ceilf (fmaxf(x0,x1) + half_thick);
    int miny = (int)floorf(fminf(y0,y1) - half_thick);
    int maxy = (int)ceilf (fmaxf(y0,y1) + half_thick);
    if (minx < 0) minx = 0;  if (miny < 0) miny = 0;
    if (maxx > w) maxx = w;  if (maxy > h) maxy = h;
    if (minx >= maxx || miny >= maxy) return;

    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx*dx + dy*dy;
    float ht2  = half_thick * half_thick;

    for (int y = miny; y < maxy; y++) {
        unsigned int  *row  = buf     + (size_t)y * w;
        unsigned char *trow = touched ? touched + (size_t)y * w : NULL;
        for (int x = minx; x < maxx; x++) {
            float px = x + 0.5f, py = y + 0.5f, dist2;
            if (len2 < 1e-6f) {
                float ex = px-x0, ey = py-y0; dist2 = ex*ex + ey*ey;
            } else {
                float t = ((px-x0)*dx + (py-y0)*dy) / len2;
                if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
                float cx = x0+t*dx, cy = y0+t*dy;
                float ex = px-cx,   ey = py-cy; dist2 = ex*ex + ey*ey;
            }
            if (dist2 <= ht2) {
                if (trow) { if (trow[x]) continue; trow[x] = 1; }
                blend_px(&row[x], r, g, b, a, multiply);
            }
        }
    }
}

static void composite_seg(unsigned int *buf, int w, int h,
                           const AnnotSeg *s, unsigned char *touched)
{
    float x0   = s->nx0 * (float)w, y0 = s->ny0 * (float)h;
    float x1   = s->nx1 * (float)w, y1 = s->ny1 * (float)h;
    float half = (s->thickness * (float)w) / 2.0f;
    raster_thick_segment(buf, w, h, x0, y0, x1, y1, half,
                         s->r, s->g, s->b, s->alpha, s->multiply,
                         s->multiply ? touched : NULL);
}

/* ------------------------------------------------------------------ */
/* Full and incremental compositing                                     */

void annot_composite(Viewer *v, unsigned char *bgrx, int w, int h)
{
    if (!bgrx) return;
    if (v->page < 0 || v->page >= v->annot_page_count) return;
    PageAnnots *pa = &v->page_annots[v->page];
    if (pa->count == 0) return;

    /* One global coverage mask for the entire highlight layer on this
     * page. Never reset between strokes -- overlapping highlights stay
     * the same color (no compounding darkening). */
    unsigned char *touched = calloc((size_t)w * h, 1);
    unsigned int  *buf     = (unsigned int *)bgrx;
    for (int i = 0; i < pa->count; i++)
        composite_seg(buf, w, h, &pa->segs[i], touched);
    free(touched);
}

void annot_rebuild(Viewer *v)
{
    free(v->annot_bgrx);
    v->annot_bgrx = NULL;
    v->annot_bgrx_w = v->annot_bgrx_h = 0;
    if (!v->pix) return;
    unsigned char *base = to_bgrx(v);
    if (!base) return;
    v->annot_bgrx   = base;
    v->annot_bgrx_w = v->pix_w;
    v->annot_bgrx_h = v->pix_h;
    /* Invalidate live mask so it rebuilds at correct page size */
    v->stroke_touched_w = v->stroke_touched_h = 0;
    annot_composite(v, v->annot_bgrx, v->annot_bgrx_w, v->annot_bgrx_h);
    /* Sync the live mask to match the state after the full replay */
    if (v->stroke_touched) {
        free(v->stroke_touched);
        v->stroke_touched = NULL;
    }
}

void annot_composite_last_segment(Viewer *v)
{
    if (!v->annot_bgrx) return;
    if (v->page < 0 || v->page >= v->annot_page_count) return;
    PageAnnots *pa = &v->page_annots[v->page];
    if (pa->count == 0) return;
    AnnotSeg *s = &pa->segs[pa->count - 1];

    unsigned char *touched = NULL;
    if (s->multiply) {
        /* Reuse or allocate the persistent live mask */
        int w = v->annot_bgrx_w, h = v->annot_bgrx_h;
        if (!v->stroke_touched || v->stroke_touched_w != w || v->stroke_touched_h != h) {
            free(v->stroke_touched);
            v->stroke_touched   = calloc((size_t)w * h, 1);
            v->stroke_touched_w = w;
            v->stroke_touched_h = h;
        }
        touched = v->stroke_touched;
    }
    composite_seg((unsigned int *)v->annot_bgrx,
                  v->annot_bgrx_w, v->annot_bgrx_h, s, touched);
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
    XSetForeground(v->dpy, v->gc, ring.pixel);
    XDrawArc(v->dpy, dst, v->gc,
             v->ptr_x - radius, v->ptr_y - radius,
             radius * 2, radius * 2, 0, 360 * 64);
    XFillArc(v->dpy, dst, v->gc,
             v->ptr_x - 1, v->ptr_y - 1, 2, 2, 0, 360 * 64);
}
