#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>
#include "sxbv.h"

static const char *thumb_exts[] = {
    ".pdf", ".epub", ".cbz", ".cbr", ".xps", ".fb2",
    ".mobi", ".svg", ".png", ".jpg", ".jpeg",
    NULL
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
    v->files      = malloc(cap * sizeof(ThumbEntry));
    v->file_count = 0;

    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        if (!supported_ext(de->d_name)) continue;

        if (v->file_count == cap) {
            cap *= 2;
            v->files = realloc(v->files, cap * sizeof(ThumbEntry));
        }

        ThumbEntry *e = &v->files[v->file_count++];
        memset(e, 0, sizeof *e);

        size_t dlen = strlen(dir);
        size_t nlen = strlen(de->d_name);
        e->path = malloc(dlen + nlen + 2);
        snprintf(e->path, dlen + nlen + 2, "%s/%s", dir, de->d_name);
        e->name = strdup(de->d_name);
    }
    closedir(d);
    qsort(v->files, v->file_count, sizeof(ThumbEntry), cmp_entry);
}

/* ------------------------------------------------------------------ */
/* render one file's first page into BGRx                             */

static unsigned char *render_file_thumb(Viewer *v, const char *path)
{
    fz_document * volatile doc;
    fz_try(v->ctx) {
        doc = fz_open_document(v->ctx, path);
    } fz_catch(v->ctx) {
        return NULL;
    }
    if (!doc) return NULL;

    if (fz_count_pages(v->ctx, doc) < 1) {
        fz_drop_document(v->ctx, doc);
        return NULL;
    }

    fz_page *page   = fz_load_page(v->ctx, doc, 0);
    fz_rect  bounds = fz_bound_page(v->ctx, page);

    float pw = bounds.x1 - bounds.x0; if (pw <= 0) pw = 1;
    float ph = bounds.y1 - bounds.y0; if (ph <= 0) ph = 1;
    float zoom = fminf((float)v->thumb_w / pw, (float)v->thumb_h / ph);

    fz_matrix  mat  = fz_scale(zoom, zoom);
    fz_irect   ibox = fz_round_rect(fz_transform_rect(bounds, mat));
    int rw = ibox.x1 - ibox.x0; if (rw < 1) rw = 1;
    int rh = ibox.y1 - ibox.y0; if (rh < 1) rh = 1;

    fz_pixmap *pix = fz_new_pixmap_with_bbox(v->ctx,
        fz_device_rgb(v->ctx), ibox, NULL, 0);
    fz_clear_pixmap_with_value(v->ctx, pix, 255);
    fz_device *dev = fz_new_draw_device(v->ctx, mat, pix);
    fz_run_page(v->ctx, page, dev, fz_identity, NULL);
    fz_close_device(v->ctx, dev);
    fz_drop_device(v->ctx, dev);
    fz_drop_page(v->ctx, page);
    fz_drop_document(v->ctx, doc);

    int tw = v->thumb_w, th = v->thumb_h;
    unsigned char *dst = malloc((size_t)tw * th * 4);
    if (!dst) { fz_drop_pixmap(v->ctx, pix); return NULL; }

    /* fill bg with pagebg pixel */
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

    /* blit centered */
    int ox = (tw - rw) / 2;
    int oy = (th - rh) / 2;
    unsigned char *src = fz_pixmap_samples(v->ctx, pix);
    for (int y = 0; y < rh; y++) {
        int dy = oy + y;
        if (dy < 0 || dy >= th) continue;
        for (int x = 0; x < rw; x++) {
            int dx = ox + x;
            if (dx < 0 || dx >= tw) continue;
            int si = (y * rw + x) * 3;
            int di = (dy * tw + dx) * 4;
            dst[di+0] = src[si+2];
            dst[di+1] = src[si+1];
            dst[di+2] = src[si+0];
            dst[di+3] = 0;
        }
    }
    fz_drop_pixmap(v->ctx, pix);
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
    thumb_free(v);    /* always reset first */

    v->thumb_dir    = strdup(dir);
    v->thumb_w      = THUMB_WIDTH;
    v->thumb_h      = (int)(THUMB_WIDTH * 1.414f);
    v->thumb_sel    = 0;
    v->thumb_scroll = 0;
    v->thumb_next   = 0;

    scan_dir(v, dir);

    /* pre-select current file if we came from normal mode */
    if (v->filename) {
        for (int i = 0; i < v->file_count; i++) {
            if (strcmp(v->files[i].path, v->filename) == 0) {
                v->thumb_sel = i;
                break;
            }
        }
    }

    v->thumb_cols = v->win_w / (v->thumb_w + THUMB_PADDING);
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
    v->thumb_cols = v->win_w / (v->thumb_w + THUMB_PADDING);
    if (v->thumb_cols < 1) v->thumb_cols = 1;

    int cell_w = v->thumb_w + THUMB_PADDING;
    int cell_h = v->thumb_h + THUMB_PADDING + THUMB_LABEL_H;
    int page_y = (v->bar_visible && topbar) ? v->bar_h : 0;
    int page_h = v->win_h - (v->bar_visible ? v->bar_h : 0);

    /* background */
    XSetForeground(v->dpy, v->gc, v->c_pagebg.pixel);
    XFillRectangle(v->dpy, v->win, v->gc, 0, page_y, v->win_w, page_h);

    int grid_w = v->thumb_cols * cell_w - THUMB_PADDING;
    int grid_x = (v->win_w - grid_w) / 2;

    for (int i = 0; i < v->file_count; i++) {
        int row = i / v->thumb_cols;
        int col = i % v->thumb_cols;

        int x = grid_x + col * cell_w;
        int y = page_y + THUMB_PADDING/2 + row * cell_h - v->thumb_scroll;

        if (y + cell_h < page_y) continue;
        if (y > page_y + page_h) break;

        ThumbEntry *e = &v->files[i];

        /* selection border -- fgcolor (bgcolor = dark, so border = fgcolor = teal) */
        if (i == v->thumb_sel) {
            XSetForeground(v->dpy, v->gc, v->c_bg.pixel);
            XFillRectangle(v->dpy, v->win, v->gc,
                x - THUMB_SELBORDER,
                y - THUMB_SELBORDER,
                v->thumb_w + THUMB_SELBORDER * 2,
                v->thumb_h + THUMB_LABEL_H + THUMB_SELBORDER * 2);
        }

        /* placeholder or rendered image */
        if (e->img) {
            XPutImage(v->dpy, v->win, v->gc, e->img,
                0, 0, x, y, v->thumb_w, v->thumb_h);
        } else {
            /* placeholder box in bar bg color */
            XSetForeground(v->dpy, v->gc, v->c_bg.pixel);
            XFillRectangle(v->dpy, v->win, v->gc,
                x, y, v->thumb_w, v->thumb_h);
            /* centered index number */
            char num[16];
            snprintf(num, sizeof num, "%d", i + 1);
            XGlyphInfo ext;
            XftTextExtentsUtf8(v->dpy, v->font,
                (const FcChar8*)num, strlen(num), &ext);
            XftDrawStringUtf8(v->xftdraw, &v->c_fg.xft, v->font,
                x + (v->thumb_w - ext.width) / 2,
                y + (v->thumb_h + v->font->ascent) / 2,
                (const FcChar8*)num, strlen(num));
        }

        /* label background -- pagebg so it's clean */
        XSetForeground(v->dpy, v->gc, v->c_pagebg.pixel);
        XFillRectangle(v->dpy, v->win, v->gc,
            x, y + v->thumb_h, v->thumb_w, THUMB_LABEL_H);

        /* filename label -- centered, fgcolor normally, bgcolor if selected */
        int label_y = y + v->thumb_h + THUMB_PADDING/2 + v->font->ascent;
        char label[256];
        snprintf(label, sizeof label, "%s", e->name);
        XGlyphInfo ext;
        XftTextExtentsUtf8(v->dpy, v->font,
            (const FcChar8*)label, strlen(label), &ext);
        /* truncate with ellipsis */
        while (ext.width > v->thumb_w && strlen(label) > 4) {
            size_t l = strlen(label);
            label[l-4] = '.';
            label[l-3] = '.';
            label[l-2] = '.';
            label[l-1] = '\0';
            XftTextExtentsUtf8(v->dpy, v->font,
                (const FcChar8*)label, strlen(label), &ext);
        }
        int lx = x + (v->thumb_w - ext.width) / 2;
        /* selected label = bgcolor (dark text on teal border) else fgcolor */
        XftColor *lcol = (i == v->thumb_sel) ? &v->c_fg.xft : &v->c_fg.xft;
        XftDrawStringUtf8(v->xftdraw, lcol, v->font,
            lx, label_y,
            (const FcChar8*)label, strlen(label));
    }

    if (v->bar_visible)
        win_draw_bar(v);
}
