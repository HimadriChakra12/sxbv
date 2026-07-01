#include <stdlib.h>
#include <math.h>
#include "sxbv.h"

/*
 * fit_zoom() -- compute the zoom value that fits the page into the window
 * for the current fit mode and rotation.
 *
 * bounds is the page's natural size in PDF points (72 pt = 1 inch).
 * After applying rotation, we know which dimension is "width" vs "height"
 * on screen, then compute the zoom that maps each to the window dimension.
 */
static float fit_zoom(Viewer *v, PdfRect bounds)
{
    float pw = bounds.x1 - bounds.x0;
    float ph = bounds.y1 - bounds.y0;
    if (pw <= 0) pw = 1;
    if (ph <= 0) ph = 1;

    /* 90/270 degree rotation swaps width and height on screen. */
    int r = ((v->rotation % 360) + 360) % 360;
    if (r == 90 || r == 270) {
        float tmp = pw; pw = ph; ph = tmp;
    }

    float zw = (float)v->win_w / pw;
    float zh = (float)v->win_h / ph;

    switch (v->fit) {
        case FIT_WIDTH:  return zw;
        case FIT_HEIGHT: return zh;
        case FIT_PAGE:   return fminf(zw, zh);
        default:         return v->zoom;
    }
}

void render_page(Viewer *v)
{
    /* Drop any previous pixmap. */
    if (v->pix) {
        pdf_pix_free(v->pix);
        v->pix = NULL;
    }

    /* Query page dimensions before rendering so fit-mode can set zoom. */
    PdfRect bounds = pdf_page_bounds(v->doc, v->page);

    if (v->fit != FIT_NONE) {
        v->zoom = fit_zoom(v, bounds);
        if (v->zoom < ZOOM_MIN) v->zoom = ZOOM_MIN;
        if (v->zoom > ZOOM_MAX) v->zoom = ZOOM_MAX;
    }

    v->pix = pdf_render(v->doc, v->page, v->zoom, v->rotation);
    if (!v->pix) return;

    v->pix_w = pdf_pix_width(v->pix);
    v->pix_h = pdf_pix_height(v->pix);

    clamp_scroll(v);
    win_update_title(v);
}

/*
 * to_bgrx() -- convert the current page's RGB8 pixmap to BGRx (4 bytes/pixel)
 * as required by X11's XPutImage on typical little-endian 32-bpp visuals.
 *
 * Splash uses a per-row stride (padded to 4 bytes) that may differ from
 * width*3; we must use pdf_pix_stride() to index source rows correctly.
 */
unsigned char *to_bgrx(Viewer *v)
{
    if (!v->pix) return NULL;
    int w      = v->pix_w;
    int h      = v->pix_h;
    int stride = pdf_pix_stride(v->pix);
    unsigned char *src = pdf_pix_samples(v->pix);
    unsigned char *dst = malloc((size_t)w * (size_t)h * 4);
    if (!dst) return NULL;

    for (int y = 0; y < h; y++) {
        unsigned char *row = src + y * stride;
        unsigned char *out = dst + y * w * 4;
        for (int x = 0; x < w; x++) {
            out[x*4+0] = row[x*3+2]; /* B */
            out[x*4+1] = row[x*3+1]; /* G */
            out[x*4+2] = row[x*3+0]; /* R */
            out[x*4+3] = 0;           /* x (padding) */
        }
    }
    return dst;
}
