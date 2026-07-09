/*
 * annotate.c -- pencil, highlighter, text notes, and selection.
 *
 * Drawing tools:
 *   Ctrl+h  highlighter (multiply blend, yellow by default)
 *   Ctrl+p  pencil (opaque flat strokes)
 *   I       text note with background
 *   i       text note with transparent background
 *
 * While a tool is active:
 *   1-5 / 1-0  select palette preset
 *   [ / ]      cycle palette color
 *   < / >      adjust thickness
 *   u / Ctrl+r undo / redo
 *   w          save annotations into the PDF
 *
 * Selection (no tool active, click on an existing annotation):
 *   click      select (shows handles)
 *   drag       move
 *   [ / ]      recolor selected annotation
 *   d / Del    delete selected annotation
 *   Escape     deselect
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
/* Window → normalized page coords                                      */

static int win_to_page_norm(Viewer *v, int wx, int wy, float *nx, float *ny)
{
    if (!v->pix || v->pix_w <= 0 || v->pix_h <= 0) return 0;
    int bar_off = (v->bar_visible && topbar) ? v->bar_h : 0;
    *nx = (float)(wx - v->scroll_x)           / (float)v->pix_w;
    *ny = (float)(wy - v->scroll_y - bar_off) / (float)v->pix_h;
    return 1;
}

static void page_norm_to_win(Viewer *v, float nx, float ny, int *wx, int *wy)
{
    int bar_off = (v->bar_visible && topbar) ? v->bar_h : 0;
    *wx = (int)(nx * (float)v->pix_w) + v->scroll_x;
    *wy = (int)(ny * (float)v->pix_h) + v->scroll_y + bar_off;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */

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

    v->annot_mode           = ANNOT_NONE;
    v->annot_drawing        = 0;
    v->have_pointer         = 0;
    v->bar_forced           = 0;

    v->stroke_touched       = NULL;
    v->stroke_touched_w     = v->stroke_touched_h = 0;
    v->stroke_pending_start = 1;

    v->redo_stack = NULL;
    v->redo_count = v->redo_cap = 0;
    v->redo_page  = -1;

    v->sel_type     = SEL_NONE;
    v->sel_id       = -1;
    v->sel_dragging = 0;

    v->text_input_mode = 0;
    v->text_note_id    = -1;
    v->text_input_buf[0] = '\0';

    v->next_annot_id = 1;
}

void annot_free_all(Viewer *v)
{
    if (v->page_annots) {
        for (int i = 0; i < v->annot_page_count; i++)
            free(v->page_annots[i].segs);
        free(v->page_annots);
        v->page_annots = NULL;
    }
    if (v->page_notes) {
        for (int i = 0; i < v->annot_page_count; i++) {
            PageNotes *pn = &v->page_notes[i];
            for (int j = 0; j < pn->count; j++)
                free(pn->notes[j].text);
            free(pn->notes);
        }
        free(v->page_notes);
        v->page_notes = NULL;
    }
    v->annot_page_count = 0;

    free(v->annot_bgrx);
    v->annot_bgrx   = NULL;
    v->annot_bgrx_w = v->annot_bgrx_h = 0;

    free(v->stroke_touched);
    v->stroke_touched   = NULL;
    v->stroke_touched_w = v->stroke_touched_h = 0;

    free(v->redo_stack);
    v->redo_stack = NULL;
    v->redo_count = v->redo_cap = 0;
    v->redo_page  = -1;

    v->sel_type = SEL_NONE;
    v->sel_id   = -1;
}

void annot_init(Viewer *v)
{
    annot_free_all(v);
    if (v->page_count <= 0) return;
    v->page_annots = calloc((size_t)v->page_count, sizeof(PageAnnots));
    v->page_notes  = calloc((size_t)v->page_count, sizeof(PageNotes));
    v->annot_page_count = (v->page_annots && v->page_notes) ? v->page_count : 0;
}

int annot_active(Viewer *v) { return v->annot_mode != ANNOT_NONE; }

/* ------------------------------------------------------------------ */
/* Per-tool accessors                                                   */

static unsigned char *cur_r(Viewer *v)     { return v->annot_mode == ANNOT_PENCIL ? &v->pencil_r    : &v->highlight_r; }
static unsigned char *cur_g(Viewer *v)     { return v->annot_mode == ANNOT_PENCIL ? &v->pencil_g    : &v->highlight_g; }
static unsigned char *cur_b(Viewer *v)     { return v->annot_mode == ANNOT_PENCIL ? &v->pencil_b    : &v->highlight_b; }
static float         *cur_thick(Viewer *v) { return v->annot_mode == ANNOT_PENCIL ? &v->pencil_thickness : &v->highlight_thickness; }
static int           *cur_pidx(Viewer *v)  { return v->annot_mode == ANNOT_PENCIL ? &v->pencil_palette_idx : &v->highlight_palette_idx; }

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
    annot_sel_clear(v);
    annot_text_cancel(v);
    if (v->annot_mode == m) {
        v->annot_mode    = ANNOT_NONE;
        v->annot_drawing = 0;
        if (v->bar_forced) v->bar_forced = 0;
    } else {
        v->annot_mode    = m;
        v->annot_drawing = 0;
        if (!v->bar_visible) { v->bar_visible = 1; v->bar_forced = 1; }
    }
}

/* ------------------------------------------------------------------ */
/* Color / thickness                                                    */

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
    /* If something is selected (no tool active), recolor the selection */
    if (!annot_active(v) && v->sel_type != SEL_NONE) {
        annot_sel_color_cycle(v, dir);
        return;
    }
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
/* Undo / redo (stroke-level)                                          */

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

    int stroke_begin = 0;
    for (int i = pa->count - 1; i > 0; i--)
        if (pa->segs[i].stroke_start) { stroke_begin = i; break; }

    if (v->redo_page != v->page) clear_redo(v);
    v->redo_page = v->page;
    int n = pa->count - stroke_begin;
    if (v->redo_count + n > v->redo_cap) {
        int nc = v->redo_cap + n + 64;
        AnnotSeg *ns = realloc(v->redo_stack, (size_t)nc * sizeof(AnnotSeg));
        if (ns) { v->redo_stack = ns; v->redo_cap = nc; }
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

    int stroke_begin = v->redo_count - 1;
    while (stroke_begin > 0 && !v->redo_stack[stroke_begin].stroke_start)
        stroke_begin--;

    int n = v->redo_count - stroke_begin;
    PageAnnots *pa = &v->page_annots[v->page];
    if (pa->count + n > pa->cap) {
        int nc = pa->cap + n + 64;
        AnnotSeg *ns = realloc(pa->segs, (size_t)nc * sizeof(AnnotSeg));
        if (!ns) return;
        pa->segs = ns; pa->cap = nc;
    }
    memcpy(pa->segs + pa->count, v->redo_stack + stroke_begin,
           (size_t)n * sizeof(AnnotSeg));
    pa->count    += n;
    v->redo_count -= n;
    annot_rebuild(v);
}

/* ------------------------------------------------------------------ */
/* Selection                                                            */

void annot_sel_clear(Viewer *v)
{
    v->sel_type     = SEL_NONE;
    v->sel_id       = -1;
    v->sel_dragging = 0;
}

int annot_has_selection(Viewer *v)
{
    return v->sel_type != SEL_NONE;
}

/* Hit-test a point against all strokes on the current page.
 * Returns the stroke_id of the first hit, or -1. */
static int hit_stroke(Viewer *v, float nx, float ny)
{
    if (v->page < 0 || v->page >= v->annot_page_count) return -1;
    PageAnnots *pa = &v->page_annots[v->page];
    float W = (float)v->pix_w, H = (float)v->pix_h;
    float px = nx * W, py = ny * H;

    /* Walk in reverse (topmost drawn = last) */
    for (int i = pa->count - 1; i >= 0; i--) {
        AnnotSeg *s = &pa->segs[i];
        float half = (s->thickness * W) / 2.0f + 4.0f; /* +4px tolerance */
        float x0 = s->nx0 * W, y0 = s->ny0 * H;
        float x1 = s->nx1 * W, y1 = s->ny1 * H;
        float dx = x1 - x0, dy = y1 - y0;
        float len2 = dx*dx + dy*dy;
        float dist2;
        if (len2 < 1e-6f) {
            float ex = px-x0, ey = py-y0; dist2 = ex*ex + ey*ey;
        } else {
            float t = ((px-x0)*dx + (py-y0)*dy) / len2;
            if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
            float cx = x0+t*dx, cy = y0+t*dy;
            float ex = px-cx, ey = py-cy; dist2 = ex*ex + ey*ey;
        }
        if (dist2 <= half * half) return s->stroke_id;
    }
    return -1;
}

/* Hit-test a point against text notes. Returns note index or -1. */
static int hit_note(Viewer *v, float nx, float ny)
{
    if (v->page < 0 || v->page >= v->annot_page_count) return -1;
    PageNotes *pn = &v->page_notes[v->page];
    for (int i = pn->count - 1; i >= 0; i--) {
        TextNote *t = &pn->notes[i];
        if (nx >= t->nx && nx <= t->nx + t->nw &&
            ny >= t->ny && ny <= t->ny + t->nh)
            return i;
    }
    return -1;
}

void annot_try_select(Viewer *v, int wx, int wy)
{
    float nx, ny;
    if (!win_to_page_norm(v, wx, wy, &nx, &ny)) return;

    /* Check notes first (they're on top visually) */
    int ni = hit_note(v, nx, ny);
    if (ni >= 0 && v->page < v->annot_page_count) {
        v->sel_type = SEL_TEXT;
        v->sel_id   = v->page_notes[v->page].notes[ni].id;
        return;
    }
    int sid = hit_stroke(v, nx, ny);
    if (sid >= 0) {
        v->sel_type = SEL_STROKE;
        v->sel_id   = sid;
        return;
    }
    annot_sel_clear(v);
}

void annot_sel_drag_start(Viewer *v, int wx, int wy)
{
    if (v->sel_type == SEL_NONE) return;
    v->sel_dragging = 1;
    float nx, ny;
    win_to_page_norm(v, wx, wy, &nx, &ny);
    v->sel_drag_nx0 = nx;
    v->sel_drag_ny0 = ny;

    if (v->sel_type == SEL_TEXT && v->page < v->annot_page_count) {
        PageNotes *pn = &v->page_notes[v->page];
        for (int i = 0; i < pn->count; i++) {
            if (pn->notes[i].id == v->sel_id) {
                v->sel_drag_ox = pn->notes[i].nx;
                v->sel_drag_oy = pn->notes[i].ny;
                break;
            }
        }
    } else if (v->sel_type == SEL_STROKE && v->page < v->annot_page_count) {
        /* Use centroid of first segment as the drag origin */
        PageAnnots *pa = &v->page_annots[v->page];
        for (int i = 0; i < pa->count; i++) {
            if (pa->segs[i].stroke_id == v->sel_id) {
                v->sel_drag_ox = (pa->segs[i].nx0 + pa->segs[i].nx1) / 2.0f;
                v->sel_drag_oy = (pa->segs[i].ny0 + pa->segs[i].ny1) / 2.0f;
                break;
            }
        }
    }
}

void annot_sel_drag(Viewer *v, int wx, int wy)
{
    if (!v->sel_dragging || v->page < 0 || v->page >= v->annot_page_count) return;
    float nx, ny;
    win_to_page_norm(v, wx, wy, &nx, &ny);
    float ddx = nx - v->sel_drag_nx0;
    float ddy = ny - v->sel_drag_ny0;

    if (v->sel_type == SEL_TEXT) {
        PageNotes *pn = &v->page_notes[v->page];
        for (int i = 0; i < pn->count; i++) {
            if (pn->notes[i].id == v->sel_id) {
                pn->notes[i].nx = v->sel_drag_ox + ddx;
                pn->notes[i].ny = v->sel_drag_oy + ddy;
                /* clamp to page */
                if (pn->notes[i].nx < 0.0f) pn->notes[i].nx = 0.0f;
                if (pn->notes[i].ny < 0.0f) pn->notes[i].ny = 0.0f;
                break;
            }
        }
    } else if (v->sel_type == SEL_STROKE) {
        PageAnnots *pa = &v->page_annots[v->page];
        for (int i = 0; i < pa->count; i++) {
            if (pa->segs[i].stroke_id == v->sel_id) {
                /* Compute original position from first drag position */
                float ox0 = pa->segs[i].nx0 - ddx + (nx - v->sel_drag_nx0);
                float oy0 = pa->segs[i].ny0 - ddy + (ny - v->sel_drag_ny0);
                (void)ox0; (void)oy0;
                pa->segs[i].nx0 += ddx - (nx - v->sel_drag_nx0);
                pa->segs[i].ny0 += ddy - (ny - v->sel_drag_ny0);
                pa->segs[i].nx1 += ddx - (nx - v->sel_drag_nx0);
                pa->segs[i].ny1 += ddy - (ny - v->sel_drag_ny0);
            }
        }
        /* Update drag origin to current pos (incremental delta) */
        v->sel_drag_nx0 = nx;
        v->sel_drag_ny0 = ny;
        annot_rebuild(v);
        return;
    }
    annot_rebuild(v);
}

void annot_sel_drag_end(Viewer *v)
{
    v->sel_dragging = 0;
}

void annot_sel_delete(Viewer *v)
{
    if (v->sel_type == SEL_NONE || v->page < 0 || v->page >= v->annot_page_count) return;

    if (v->sel_type == SEL_STROKE) {
        PageAnnots *pa = &v->page_annots[v->page];
        int w = 0;
        for (int i = 0; i < pa->count; i++)
            if (pa->segs[i].stroke_id != v->sel_id)
                pa->segs[w++] = pa->segs[i];
        pa->count = w;
        annot_rebuild(v);
    } else if (v->sel_type == SEL_TEXT) {
        PageNotes *pn = &v->page_notes[v->page];
        for (int i = 0; i < pn->count; i++) {
            if (pn->notes[i].id == v->sel_id) {
                free(pn->notes[i].text);
                memmove(&pn->notes[i], &pn->notes[i+1],
                        (size_t)(pn->count - i - 1) * sizeof(TextNote));
                pn->count--;
                break;
            }
        }
    }
    annot_sel_clear(v);
}

void annot_sel_color_cycle(Viewer *v, int dir)
{
    if (v->sel_type == SEL_NONE || v->page < 0 || v->page >= v->annot_page_count) return;

    /* Determine current palette for the selected type, cycle, apply */
    if (v->sel_type == SEL_STROKE) {
        PageAnnots *pa = &v->page_annots[v->page];
        /* Find the type of the selected stroke */
        int is_pencil = 0;
        unsigned char cr = 0, cg = 0, cb = 0;
        for (int i = 0; i < pa->count; i++) {
            if (pa->segs[i].stroke_id == v->sel_id) {
                is_pencil = !pa->segs[i].multiply;
                cr = pa->segs[i].r; cg = pa->segs[i].g; cb = pa->segs[i].b;
                break;
            }
        }
        /* Find index in palette */
        int plen = is_pencil ? PENCIL_PALETTE_LEN : HIGHLIGHT_PALETTE_LEN;
        int idx = 0;
        for (int p = 0; p < plen; p++) {
            unsigned char pr = 0, pg = 0, pb = 0;
            parse_color(v, is_pencil ? pencil_palette[p] : highlight_palette[p],
                        &pr, &pg, &pb);
            if (pr == cr && pg == cg && pb == cb) { idx = p; break; }
        }
        idx = ((idx + dir) % plen + plen) % plen;
        unsigned char nr = 0, ng = 0, nb = 0;
        parse_color(v, is_pencil ? pencil_palette[idx] : highlight_palette[idx],
                    &nr, &ng, &nb);
        for (int i = 0; i < pa->count; i++)
            if (pa->segs[i].stroke_id == v->sel_id)
                { pa->segs[i].r = nr; pa->segs[i].g = ng; pa->segs[i].b = nb; }
        annot_rebuild(v);
    } else if (v->sel_type == SEL_TEXT) {
        /* Text notes use pencil palette for color cycling */
        PageNotes *pn = &v->page_notes[v->page];
        for (int i = 0; i < pn->count; i++) {
            if (pn->notes[i].id == v->sel_id) {
                unsigned char cr = pn->notes[i].r, cg = pn->notes[i].g, cb = pn->notes[i].b;
                int idx = 0;
                for (int p = 0; p < PENCIL_PALETTE_LEN; p++) {
                    unsigned char pr = 0, pg = 0, pb = 0;
                    parse_color(v, pencil_palette[p], &pr, &pg, &pb);
                    if (pr == cr && pg == cg && pb == cb) { idx = p; break; }
                }
                idx = ((idx + dir) % PENCIL_PALETTE_LEN + PENCIL_PALETTE_LEN) % PENCIL_PALETTE_LEN;
                parse_color(v, pencil_palette[idx],
                            &pn->notes[i].r, &pn->notes[i].g, &pn->notes[i].b);
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Text notes                                                          */

void annot_text_place(Viewer *v, int wx, int wy, int has_bg)
{
    if (v->mode == MODE_THUMB) return;
    annot_sel_clear(v);
    /* Exit any previous annotation drawing mode */
    if (annot_active(v)) {
        v->annot_mode    = ANNOT_NONE;
        v->annot_drawing = 0;
    }

    float nx, ny;
    if (!win_to_page_norm(v, wx, wy, &nx, &ny)) return;
    if (v->page < 0 || v->page >= v->annot_page_count) return;

    PageNotes *pn = &v->page_notes[v->page];
    if (pn->count == pn->cap) {
        int nc = pn->cap ? pn->cap * 2 : 16;
        TextNote *nn = realloc(pn->notes, (size_t)nc * sizeof(TextNote));
        if (!nn) return;
        pn->notes = nn; pn->cap = nc;
    }

    TextNote *t  = &pn->notes[pn->count++];
    t->nx     = nx;
    t->ny     = ny;
    t->nw     = 0.3f;   /* will grow with text */
    t->nh     = 0.05f;
    t->r      = 0; t->g = 0; t->b = 0;  /* default black */
    t->has_bg = has_bg;
    t->text   = strdup("");
    t->id     = v->next_annot_id++;

    v->annot_mode      = ANNOT_TEXT;
    v->text_input_mode = 1;
    v->text_note_id    = t->id;
    v->text_input_buf[0] = '\0';
    if (!v->bar_visible) { v->bar_visible = 1; v->bar_forced = 1; }
}

void annot_text_key(Viewer *v, KeySym ks, const char *buf, int len)
{
    if (!v->text_input_mode || v->page < 0 || v->page >= v->annot_page_count) return;

    if (ks == XK_Escape) { annot_text_cancel(v); return; }
    if (ks == XK_Return || ks == XK_KP_Enter) { annot_text_commit(v); return; }

    PageNotes *pn = &v->page_notes[v->page];
    TextNote *t = NULL;
    for (int i = 0; i < pn->count; i++)
        if (pn->notes[i].id == v->text_note_id) { t = &pn->notes[i]; break; }
    if (!t) return;

    if (ks == XK_BackSpace) {
        size_t sl = strlen(t->text);
        if (sl > 0) {
            /* UTF-8 aware backspace: strip last codepoint */
            unsigned char *s = (unsigned char*)t->text;
            int i = (int)sl - 1;
            while (i > 0 && (s[i] & 0xC0) == 0x80) i--;
            t->text[i] = '\0';
        }
        return;
    }

    if (len > 0 && (unsigned char)buf[0] >= 0x20) {
        size_t sl = strlen(t->text);
        size_t ll = (size_t)len;
        char *nt = realloc(t->text, sl + ll + 1);
        if (nt) { t->text = nt; memcpy(t->text + sl, buf, ll); t->text[sl+ll] = '\0'; }
    }
}

void annot_text_commit(Viewer *v)
{
    v->text_input_mode = 0;
    v->annot_mode      = ANNOT_NONE;
    v->text_note_id    = -1;
    /* Keep the note in place — it's already in page_notes */
}

void annot_text_cancel(Viewer *v)
{
    if (v->text_input_mode && v->page >= 0 && v->page < v->annot_page_count) {
        /* Remove the in-progress note */
        PageNotes *pn = &v->page_notes[v->page];
        for (int i = 0; i < pn->count; i++) {
            if (pn->notes[i].id == v->text_note_id) {
                free(pn->notes[i].text);
                memmove(&pn->notes[i], &pn->notes[i+1],
                        (size_t)(pn->count - i - 1) * sizeof(TextNote));
                pn->count--;
                break;
            }
        }
    }
    v->text_input_mode = 0;
    v->annot_mode      = ANNOT_NONE;
    v->text_note_id    = -1;
}

/* ------------------------------------------------------------------ */
/* Save                                                                 */

void annot_save(Viewer *v)
{
    if (!v->filename || !v->doc) return;
    if (v->annot_page_count == 0) return;

    const PdfInkStroke **stroke_arrays = calloc((size_t)v->annot_page_count, sizeof(PdfInkStroke*));
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

    int rc = pdf_annot_save(v->doc, stroke_arrays, n_per_page, v->annot_page_count, v->filename);
    for (int pg = 0; pg < v->annot_page_count; pg++) free((void*)stroke_arrays[pg]);
    free(stroke_arrays); free(n_per_page);

    if (rc == 0) fprintf(stderr, "sxbv: annotations saved to '%s'\n", v->filename);
    else         fprintf(stderr, "sxbv: failed to save annotations\n");
}

/* ------------------------------------------------------------------ */
/* Stroke capture                                                       */

static void push_seg(Viewer *v, float nx0, float ny0, float nx1, float ny1)
{
    if (v->page < 0 || v->page >= v->annot_page_count) return;
    PageAnnots *pa = &v->page_annots[v->page];
    if (pa->count == pa->cap) {
        int nc = pa->cap ? pa->cap * 2 : 64;
        AnnotSeg *n = realloc(pa->segs, (size_t)nc * sizeof(AnnotSeg));
        if (!n) return;
        pa->segs = n; pa->cap = nc;
    }
    AnnotSeg *s = &pa->segs[pa->count++];
    s->nx0 = nx0; s->ny0 = ny0; s->nx1 = nx1; s->ny1 = ny1;
    s->thickness    = *cur_thick(v) / (float)v->pix_w;
    s->r = *cur_r(v); s->g = *cur_g(v); s->b = *cur_b(v);
    s->alpha        = HIGHLIGHT_ALPHA;
    s->multiply     = (v->annot_mode == ANNOT_HIGHLIGHT) ? 1 : 0;
    s->stroke_start = (unsigned char)v->stroke_pending_start;
    s->stroke_id    = v->stroke_pending_start
                      ? v->next_annot_id++
                      : pa->segs[pa->count - 2].stroke_id;
    v->stroke_pending_start = 0;
    clear_redo(v);
}

void annot_button(Viewer *v, XButtonEvent *be, int press)
{
    if (v->mode == MODE_THUMB) return;

    if (v->text_input_mode) {
        if (press && be->button == Button1) annot_text_commit(v);
        return;
    }

    if (!annot_active(v)) {
        /* Selection mode: click to select, drag to move */
        if (press && be->button == Button1) {
            annot_try_select(v, be->x, be->y);
            if (v->sel_type != SEL_NONE)
                annot_sel_drag_start(v, be->x, be->y);
        } else if (!press && be->button == Button1) {
            annot_sel_drag_end(v);
        }
        return;
    }

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
        v->stroke_pending_start = 1;
    }
}

void annot_motion(Viewer *v, XMotionEvent *me)
{
    v->ptr_x = me->x; v->ptr_y = me->y; v->have_pointer = 1;

    if (v->sel_dragging) {
        annot_sel_drag(v, me->x, me->y);
        return;
    }

    if (!annot_active(v) || !v->annot_drawing || v->mode == MODE_THUMB) return;
    float nx, ny;
    if (!win_to_page_norm(v, me->x, me->y, &nx, &ny)) return;
    push_seg(v, v->annot_last_nx, v->annot_last_ny, nx, ny);
    annot_composite_last_segment(v);
    v->annot_last_nx = nx;
    v->annot_last_ny = ny;
}

/* ------------------------------------------------------------------ */
/* Pixel blending                                                       */

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
        tr = (or_ * r) / 255; tg = (og * g) / 255; tb = (ob * b) / 255;
    } else {
        if (a == 255) { *px = ((unsigned int)r<<16)|((unsigned int)g<<8)|b; return; }
        tr = r; tg = g; tb = b;
    }
    *px = (((tr*a + or_*(255-a))/255) << 16) |
          (((tg*a + og *(255-a))/255) <<  8) |
          (((tb*a + ob *(255-a))/255)       );
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
    if (minx<0) minx=0; if (miny<0) miny=0;
    if (maxx>w) maxx=w; if (maxy>h) maxy=h;
    if (minx>=maxx || miny>=maxy) return;

    float dx=x1-x0, dy=y1-y0, len2=dx*dx+dy*dy, ht2=half_thick*half_thick;
    for (int y=miny; y<maxy; y++) {
        unsigned int  *row  = buf     + (size_t)y*w;
        unsigned char *trow = touched ? touched + (size_t)y*w : NULL;
        for (int x=minx; x<maxx; x++) {
            float px=x+0.5f, py=y+0.5f, dist2;
            if (len2<1e-6f) { float ex=px-x0,ey=py-y0; dist2=ex*ex+ey*ey; }
            else {
                float t=((px-x0)*dx+(py-y0)*dy)/len2;
                if(t<0.0f)t=0.0f; if(t>1.0f)t=1.0f;
                float cx=x0+t*dx,cy=y0+t*dy,ex=px-cx,ey=py-cy;
                dist2=ex*ex+ey*ey;
            }
            if (dist2<=ht2) {
                if (trow) { if(trow[x]) continue; trow[x]=1; }
                blend_px(&row[x], r, g, b, a, multiply);
            }
        }
    }
}

static void composite_seg(unsigned int *buf, int w, int h,
                           const AnnotSeg *s, unsigned char *touched)
{
    float x0=s->nx0*(float)w, y0=s->ny0*(float)h;
    float x1=s->nx1*(float)w, y1=s->ny1*(float)h;
    float half=(s->thickness*(float)w)/2.0f;
    raster_thick_segment(buf, w, h, x0, y0, x1, y1, half,
                         s->r, s->g, s->b, s->alpha, s->multiply,
                         s->multiply ? touched : NULL);
}

/* ------------------------------------------------------------------ */
/* Compositing                                                          */

void annot_composite(Viewer *v, unsigned char *bgrx, int w, int h)
{
    if (!bgrx || v->page<0 || v->page>=v->annot_page_count) return;
    PageAnnots *pa = &v->page_annots[v->page];
    if (pa->count == 0) return;
    unsigned char *touched = calloc((size_t)w*h, 1);
    unsigned int  *buf     = (unsigned int*)bgrx;
    for (int i=0; i<pa->count; i++) composite_seg(buf, w, h, &pa->segs[i], touched);
    free(touched);
}

void annot_rebuild(Viewer *v)
{
    free(v->annot_bgrx);
    v->annot_bgrx = NULL; v->annot_bgrx_w = v->annot_bgrx_h = 0;
    if (!v->pix) return;
    unsigned char *base = to_bgrx(v);
    if (!base) return;
    v->annot_bgrx = base; v->annot_bgrx_w = v->pix_w; v->annot_bgrx_h = v->pix_h;
    v->stroke_touched_w = v->stroke_touched_h = 0;
    annot_composite(v, v->annot_bgrx, v->annot_bgrx_w, v->annot_bgrx_h);
    free(v->stroke_touched); v->stroke_touched = NULL;
}

void annot_composite_last_segment(Viewer *v)
{
    if (!v->annot_bgrx || v->page<0 || v->page>=v->annot_page_count) return;
    PageAnnots *pa = &v->page_annots[v->page];
    if (pa->count == 0) return;
    AnnotSeg *s = &pa->segs[pa->count-1];
    unsigned char *touched = NULL;
    if (s->multiply) {
        int w=v->annot_bgrx_w, h=v->annot_bgrx_h;
        if (!v->stroke_touched || v->stroke_touched_w!=w || v->stroke_touched_h!=h) {
            free(v->stroke_touched);
            v->stroke_touched   = calloc((size_t)w*h, 1);
            v->stroke_touched_w = w; v->stroke_touched_h = h;
        }
        touched = v->stroke_touched;
    }
    composite_seg((unsigned int*)v->annot_bgrx,
                  v->annot_bgrx_w, v->annot_bgrx_h, s, touched);
}

/* ------------------------------------------------------------------ */
/* Overlay: cursor ring + selection handles + text notes               */

void annot_draw_overlay(Viewer *v, Drawable dst)
{
    if (v->mode == MODE_THUMB) return;

    /* ---- thickness-preview cursor ring ---- */
    if (annot_active(v) && v->annot_mode != ANNOT_TEXT && v->have_pointer) {
        float thick_px = (v->annot_mode == ANNOT_PENCIL)
                         ? v->pencil_thickness : v->highlight_thickness;
        int radius = (int)(thick_px / 2.0f);
        if (radius < 1) radius = 1;
        XColor ring;
        XParseColor(v->dpy, v->cmap, ANNOT_CURSOR_RING_COLOR, &ring);
        XAllocColor(v->dpy, v->cmap, &ring);
        XSetForeground(v->dpy, v->gc, ring.pixel);
        XDrawArc(v->dpy, dst, v->gc,
                 v->ptr_x-radius, v->ptr_y-radius,
                 radius*2, radius*2, 0, 360*64);
        XFillArc(v->dpy, dst, v->gc, v->ptr_x-1, v->ptr_y-1, 2, 2, 0, 360*64);
    }

    /* ---- selection outline ---- */
    if (v->sel_type == SEL_STROKE && v->page>=0 && v->page<v->annot_page_count) {
        XSetForeground(v->dpy, v->gc, WhitePixel(v->dpy, v->screen));
        XSetLineAttributes(v->dpy, v->gc, 1, LineOnOffDash, CapButt, JoinMiter);
        PageAnnots *pa = &v->page_annots[v->page];
        for (int i=0; i<pa->count; i++) {
            if (pa->segs[i].stroke_id != v->sel_id) continue;
            int wx0, wy0, wx1, wy1;
            page_norm_to_win(v, pa->segs[i].nx0, pa->segs[i].ny0, &wx0, &wy0);
            page_norm_to_win(v, pa->segs[i].nx1, pa->segs[i].ny1, &wx1, &wy1);
            XDrawLine(v->dpy, dst, v->gc, wx0, wy0, wx1, wy1);
        }
        XSetLineAttributes(v->dpy, v->gc, 0, LineSolid, CapButt, JoinMiter);
    }

    /* ---- text notes ---- */
    if (v->page<0 || v->page>=v->annot_page_count) return;
    PageNotes *pn = &v->page_notes[v->page];
    for (int i=0; i<pn->count; i++) {
        TextNote *t = &pn->notes[i];
        int wx, wy;
        page_norm_to_win(v, t->nx, t->ny, &wx, &wy);
        int pw = (int)(t->nw * (float)v->pix_w);
        int ph = (int)(t->nh * (float)v->pix_h);

        /* background */
        if (t->has_bg) {
            XSetForeground(v->dpy, v->gc, WhitePixel(v->dpy, v->screen));
            XFillRectangle(v->dpy, dst, v->gc, wx, wy, (unsigned)pw, (unsigned)ph);
            XSetForeground(v->dpy, v->gc, BlackPixel(v->dpy, v->screen));
            XDrawRectangle(v->dpy, dst, v->gc, wx, wy, (unsigned)pw, (unsigned)ph);
        }

        /* Text in typing: show cursor */
        const char *text = t->text;
        int is_typing = (v->text_input_mode && v->text_note_id == t->id);

        if (v->font && text && text[0]) {
            XColor tc;
            char spec[16];
            snprintf(spec, sizeof spec, "#%02x%02x%02x", t->r, t->g, t->b);
            XftColor xftc;
            XftColorAllocName(v->dpy, v->visual, v->cmap, spec, &xftc);
            XftDrawChange(v->xftdraw, dst);
            XftDrawStringUtf8(v->xftdraw, &xftc, v->font,
                              wx+2, wy + v->font->ascent + 2,
                              (const FcChar8*)text, (int)strlen(text));
            XftColorFree(v->dpy, v->visual, v->cmap, &xftc);
            XftDrawChange(v->xftdraw, v->win);
        }

        if (is_typing) {
            /* Cursor bar after the text */
            XGlyphInfo ext;
            XftTextExtentsUtf8(v->dpy, v->font,
                               (const FcChar8*)(text ? text : ""),
                               (int)strlen(text ? text : ""), &ext);
            XSetForeground(v->dpy, v->gc, BlackPixel(v->dpy, v->screen));
            XFillRectangle(v->dpy, dst, v->gc,
                           wx+2+ext.width, wy+2, 1, v->font->height);
        }

        /* Selection outline */
        if (v->sel_type == SEL_TEXT && v->sel_id == t->id) {
            XSetForeground(v->dpy, v->gc, WhitePixel(v->dpy, v->screen));
            XSetLineAttributes(v->dpy, v->gc, 2, LineOnOffDash, CapButt, JoinMiter);
            XDrawRectangle(v->dpy, dst, v->gc, wx-2, wy-2,
                           (unsigned)(pw+4), (unsigned)(ph+4));
            XSetLineAttributes(v->dpy, v->gc, 0, LineSolid, CapButt, JoinMiter);
        }
    }
}
