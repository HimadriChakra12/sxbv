#include <stdlib.h>
#include <math.h>
#include "sxbv.h"

fz_matrix page_matrix(Viewer *v)
{
    return fz_pre_rotate(fz_scale(v->zoom, v->zoom), (float)v->rotation);
}

void render_page(Viewer *v)
{
    if (v->pix) {
        fz_drop_pixmap(v->ctx, v->pix);
        v->pix = NULL;
    }

    fz_page *page = fz_load_page(v->ctx, v->doc, v->page);
    fz_rect bounds = fz_bound_page(v->ctx, page);

    /* Recalculate zoom for fit modes */
    if (v->fit != FIT_NONE) {
        fz_rect b2 = fz_transform_rect(bounds, fz_rotate((float)v->rotation));
        float pw = b2.x1 - b2.x0; if (pw <= 0) pw = 1;
        float ph = b2.y1 - b2.y0; if (ph <= 0) ph = 1;
        float zw = (float)v->win_w / pw;
        float zh = (float)v->win_h / ph;
        switch (v->fit) {
            case FIT_WIDTH:  v->zoom = zw; break;
            case FIT_HEIGHT: v->zoom = zh; break;
            case FIT_PAGE:   v->zoom = fminf(zw, zh); break;
            default: break;
        }
        if (v->zoom < ZOOM_MIN) v->zoom = ZOOM_MIN;
        if (v->zoom > ZOOM_MAX) v->zoom = ZOOM_MAX;
    }

    fz_matrix mat  = page_matrix(v);
    fz_irect  ibox = fz_round_rect(fz_transform_rect(bounds, mat));

    v->pix = fz_new_pixmap_with_bbox(v->ctx, fz_device_rgb(v->ctx), ibox, NULL, 0);
    fz_clear_pixmap_with_value(v->ctx, v->pix, 255);

    fz_device *dev = fz_new_draw_device(v->ctx, mat, v->pix);
    fz_run_page(v->ctx, page, dev, fz_identity, NULL);
    fz_close_device(v->ctx, dev);
    fz_drop_device(v->ctx, dev);
    fz_drop_page(v->ctx, page);

    v->pix_w = fz_pixmap_width(v->ctx, v->pix);
    v->pix_h = fz_pixmap_height(v->ctx, v->pix);

    clamp_scroll(v);
    win_update_title(v);
}

/* Convert MuPDF RGB pixmap → X11 BGRx */
unsigned char *to_bgrx(Viewer *v)
{
    int n = v->pix_w * v->pix_h;
    unsigned char *s = fz_pixmap_samples(v->ctx, v->pix);
    unsigned char *d = malloc((size_t)n * 4);
    if (!d) return NULL;
    for (int i = 0; i < n; i++) {
        d[i*4+0] = s[i*3+2];
        d[i*4+1] = s[i*3+1];
        d[i*4+2] = s[i*3+0];
        d[i*4+3] = 0;
    }
    return d;
}
