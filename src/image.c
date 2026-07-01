#include <stdlib.h>
#include <math.h>
#include "sxbv.h"

static float fit_zoom(Viewer *v, PdfRect bounds)
{
    float pw = bounds.x1 - bounds.x0;
    float ph = bounds.y1 - bounds.y0;
    if (pw <= 0) pw = 1;
    if (ph <= 0) ph = 1;

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
    if (v->pix) {
        pdf_pix_free(v->pix);
        v->pix = NULL;
    }

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
        const unsigned char *row = src + (size_t)y * stride;
        unsigned int        *out = (unsigned int *)(dst + (size_t)y * w * 4);
        for (int x = 0; x < w; x++) {
            unsigned int px;
            __builtin_memcpy(&px, row + x * 3, 4);
            unsigned int r = (px      ) & 0xffu;
            unsigned int g = (px >>  8) & 0xffu;
            unsigned int b = (px >> 16) & 0xffu;
            out[x] = (r << 16) | (g << 8) | b;
        }
    }
    return dst;
}
