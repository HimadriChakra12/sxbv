#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>
#include "sxbv.h"

/* forward declaration from window.c */
void ensure_backbuf(Viewer *v);

static const char *thumb_exts[] = {
    ".pdf",
    NULL   /* Poppler only handles PDF; drop epub/cbz/xps/fb2/mobi */
};

/* ------------------------------------------------------------------ */
/* helpers                                                             */

static int supported_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    for (int i = 0; thumb_exts[i]; i++)
        if (strcasecmp(dot, thumb_exts[i]) == 0)
            return 1;
    return 0;
}

static int cmp_entry(const void *a, const void *b)
{
    return strcasecmp(((ThumbEntry*)a)->name, ((ThumbEntry*)b)->name);
}

/* ------------------------------------------------------------------ */
/* directory scan                                                      */

static void scan_dir(Viewer *v, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "sxbv: cannot open dir: %s\n", dir); return; }

    int cap = 64;
    v->files      = malloc((size_t)cap * sizeof(ThumbEntry));
    v->file_count = 0;
    if (!v->files) { closedir(d); return; }

    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        if (!supported_ext(de->d_name)) continue;

        if (v->file_count == cap) {
            cap *= 2;
            ThumbEntry *tmp = realloc(v->files, (size_t)cap * sizeof(ThumbEntry));
            if (!tmp) break;   /* keep what we have so far */
            v->files = tmp;
        }

        ThumbEntry *e = &v->files[v->file_count++];
        memset(e, 0, sizeof *e);

        size_t dlen = strlen(dir);
        /* Strip trailing slash so we never produce "dir//file" */
        while (dlen > 1 && dir[dlen-1] == '/') dlen--;
        size_t nlen = strlen(de->d_name);
        e->path = malloc(dlen + nlen + 2);
        if (!e->path) { v->file_count--; continue; }
        memcpy(e->path, dir, dlen);
        e->path[dlen] = '/';
        memcpy(e->path + dlen + 1, de->d_name, nlen + 1);
        e->name = strdup(de->d_name);
        if (!e->name) { free(e->path); e->path = NULL; v->file_count--; continue; }
    }
    closedir(d);
    if (v->file_count > 0)
        qsort(v->files, (size_t)v->file_count, sizeof(ThumbEntry), cmp_entry);
}

/* ------------------------------------------------------------------ */
/* render one file's first page as a centred BGRx thumbnail           */

static unsigned char *render_file_thumb(Viewer *v, const char *path)
{
    PdfDoc *doc = pdf_open(path);
    if (!doc) return NULL;

    if (pdf_page_count(doc) < 1) {
        pdf_close(doc);
        return NULL;
    }

    /* Fit the first page into the thumbnail cell. */
    PdfRect bounds = pdf_page_bounds(doc, 0);
    float pw = bounds.x1 - bounds.x0; if (pw <= 0) pw = 1;
    float ph = bounds.y1 - bounds.y0; if (ph <= 0) ph = 1;
    float zoom = fminf((float)v->thumb_w / pw, (float)v->thumb_h / ph);

    PdfPix *pix = pdf_render(doc, 0, zoom, 0);
    pdf_close(doc);
    if (!pix) return NULL;

    int rw     = pdf_pix_width(pix);
    int rh     = pdf_pix_height(pix);
    int stride = pdf_pix_stride(pix);
    int tw     = v->thumb_w;
    int th     = v->thumb_h;

    unsigned char *dst = malloc((size_t)tw * th * 4);
    if (!dst) { pdf_pix_free(pix); return NULL; }

    /* Fill background with the configured page-background colour. */
    unsigned long bg  = v->c_pagebg.pixel;
    unsigned char bg_b = (bg >> 16) & 0xff;
    unsigned char bg_g = (bg >>  8) & 0xff;
    unsigned char bg_r =  bg        & 0xff;
    for (int i = 0; i < tw * th; i++) {
        dst[i*4+0] = bg_b;
        dst[i*4+1] = bg_g;
        dst[i*4+2] = bg_r;
        dst[i*4+3] = 0;
    }

    /* Blit rendered page centred inside the thumbnail cell.
     * Source is RGB8 with per-row stride (Splash pads to 4 bytes). */
    int ox = (tw - rw) / 2;
    int oy = (th - rh) / 2;
    unsigned char *src = pdf_pix_samples(pix);

    for (int y = 0; y < rh; y++) {
        int dy = oy + y;
        if (dy < 0 || dy >= th) continue;
        unsigned char *srow = src + y * stride;
        for (int x = 0; x < rw; x++) {
            int dx = ox + x;
            if (dx < 0 || dx >= tw) continue;
            int di = (dy * tw + dx) * 4;
            dst[di+0] = srow[x*3+2]; /* B */
            dst[di+1] = srow[x*3+1]; /* G */
            dst[di+2] = srow[x*3+0]; /* R */
            dst[di+3] = 0;
        }
    }
    pdf_pix_free(pix);
    return dst;
}

/* ------------------------------------------------------------------ */
/* public API                                                          */

void thumb_free(Viewer *v)
{
    if (!v->files) return;
    for (int i = 0; i < v->file_count; i++) {
        ThumbEntry *e = &v->files[i];
        free(e->path);
        free(e->name);
        if (e->img)  { e->img->data = NULL; XDestroyImage(e->img); }
        if (e->data) free(e->data);
    }
    free(v->files);
    if (v->thumb_dir) { free(v->thumb_dir); v->thumb_dir = NULL; }
    v->files      = NULL;
    v->file_count = 0;
    v->thumb_next = 0;
}

void thumb_init_dir(Viewer *v, const char *dir)
{
    thumb_free(v);

    v->thumb_dir    = strdup(dir);
    v->thumb_w      = THUMB_WIDTH;
    v->thumb_h      = (int)(THUMB_WIDTH * 1.414f);
    v->thumb_sel    = 0;
    v->thumb_scroll = 0;
    v->thumb_next   = 0;
    v->files        = NULL;     /* scan_dir will malloc this */
    v->file_count   = 0;

    scan_dir(v, dir);

    if (v->filename) {
        for (int i = 0; i < v->file_count; i++) {
            if (strcmp(v->files[i].path, v->filename) == 0) {
                v->thumb_sel = i;
                break;
            }
        }
    }

    int usable_w = v->win_w > 0 ? v->win_w : 800; /* safe fallback before first resize */
    v->thumb_cols = usable_w / (v->thumb_w + THUMB_PADDING);
    if (v->thumb_cols < 1) v->thumb_cols = 1;
    thumb_scroll_to_sel(v);
}

void thumb_render_next(Viewer *v)
{
    if (v->thumb_next >= v->file_count) return;
    int i = v->thumb_next++;
    ThumbEntry *e = &v->files[i];

    e->data = render_file_thumb(v, e->path);
    if (e->data) {
        e->img = XCreateImage(v->dpy,
            DefaultVisual(v->dpy, v->screen),
            24, ZPixmap, 0,
            (char*)e->data,
            v->thumb_w, v->thumb_h, 32, 0);
    }
    e->rendered = 1;
}

void thumb_scroll_to_sel(Viewer *v)
{
    int cell_h  = v->thumb_h + THUMB_PADDING + THUMB_LABEL_H;
    int sel_row = v->thumb_sel / (v->thumb_cols > 0 ? v->thumb_cols : 1);
    int page_h  = v->win_h - (v->bar_visible ? v->bar_h : 0);
    int top     = sel_row * cell_h;
    int bot     = top + cell_h;

    if (top < v->thumb_scroll)
        v->thumb_scroll = top;
    else if (bot > v->thumb_scroll + page_h)
        v->thumb_scroll = bot - page_h;
    if (v->thumb_scroll < 0) v->thumb_scroll = 0;
}

void thumb_draw(Viewer *v)
{
    ensure_backbuf(v);
    Drawable dst = v->backbuf;

    if (v->win_w <= 0 || v->win_h <= 0) return;

    v->thumb_cols = v->win_w / (v->thumb_w + THUMB_PADDING);
    if (v->thumb_cols < 1) v->thumb_cols = 1;

    int cell_w = v->thumb_w + THUMB_PADDING;
    int cell_h = v->thumb_h + THUMB_PADDING + THUMB_LABEL_H;
    int page_y = (v->bar_visible && topbar) ? v->bar_h : 0;
    int page_h = v->win_h - (v->bar_visible ? v->bar_h : 0);

    /* background */
    XSetForeground(v->dpy, v->gc, v->c_pagebg.pixel);
    XFillRectangle(v->dpy, dst, v->gc, 0, page_y, v->win_w, page_h);

    int grid_w = v->thumb_cols * cell_w - THUMB_PADDING;
    int grid_x = (v->win_w - grid_w) / 2;

    for (int i = 0; i < v->file_count; i++) {
        int row = i / v->thumb_cols;
        int col = i % v->thumb_cols;
        int x   = grid_x + col * cell_w;
        int y   = page_y + THUMB_PADDING/2 + row * cell_h - v->thumb_scroll;

        if (y + cell_h < page_y) continue;
        if (y > page_y + page_h) break;

        ThumbEntry *e = &v->files[i];

        /* selection border */
        if (i == v->thumb_sel) {
            XSetForeground(v->dpy, v->gc, v->c_bg.pixel);
            XFillRectangle(v->dpy, dst, v->gc,
                x - THUMB_SELBORDER, y - THUMB_SELBORDER,
                v->thumb_w + THUMB_SELBORDER*2,
                v->thumb_h + THUMB_LABEL_H + THUMB_SELBORDER*2);
        }

        if (e->img) {
            XPutImage(v->dpy, dst, v->gc, e->img,
                0, 0, x, y, v->thumb_w, v->thumb_h);
        } else {
            XSetForeground(v->dpy, v->gc, v->c_bg.pixel);
            XFillRectangle(v->dpy, dst, v->gc,
                x, y, v->thumb_w, v->thumb_h);
            char num[16];
            snprintf(num, sizeof num, "%d", i + 1);
            XGlyphInfo ext;
            XftTextExtentsUtf8(v->dpy, v->font,
                (const FcChar8*)num, strlen(num), &ext);
            XftDrawChange(v->xftdraw, dst);
            XftDrawStringUtf8(v->xftdraw, &v->c_fg.xft, v->font,
                x + (v->thumb_w - ext.width) / 2,
                y + (v->thumb_h + v->font->ascent) / 2,
                (const FcChar8*)num, strlen(num));
        }

        /* label background */
        XSetForeground(v->dpy, v->gc, v->c_pagebg.pixel);
        XFillRectangle(v->dpy, dst, v->gc,
            x, y + v->thumb_h, v->thumb_w, THUMB_LABEL_H);

        /* label text — truncate with "..." if too wide */
        int label_y = y + v->thumb_h + THUMB_PADDING/2 + v->font->ascent;
        char label[256];
        snprintf(label, sizeof label, "%s", e->name);
        XGlyphInfo ext;
        XftTextExtentsUtf8(v->dpy, v->font,
            (const FcChar8*)label, strlen(label), &ext);
        while (ext.width > v->thumb_w && strlen(label) > 4) {
            size_t l = strlen(label);
            label[l-4] = '.'; label[l-3] = '.';
            label[l-2] = '.'; label[l-1] = '\0';
            XftTextExtentsUtf8(v->dpy, v->font,
                (const FcChar8*)label, strlen(label), &ext);
        }
        int lx = x + (v->thumb_w - ext.width) / 2;
        XftDrawChange(v->xftdraw, dst);
        XftDrawStringUtf8(v->xftdraw, &v->c_fg.xft, v->font,
            lx, label_y, (const FcChar8*)label, strlen(label));
    }

    if (v->bar_visible)
        win_draw_bar_to(v, dst);

    XftDrawChange(v->xftdraw, v->win);
    XCopyArea(v->dpy, dst, v->win, v->gc,
        0, 0, v->win_w, v->win_h, 0, 0);
    XFlush(v->dpy);
}
