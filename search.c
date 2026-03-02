#include "sxbv.h"

void search_free(Viewer *v)
{
    v->hit_count = 0;
    v->hit       = -1;
}

void search_do(Viewer *v, int dir)
{
    if (!v->search_buf[0]) return;
    search_free(v);

    fz_page       *page = fz_load_page(v->ctx, v->doc, v->page);
    fz_stext_page *st   = fz_new_stext_page_from_page(v->ctx, page, NULL);
    v->hit_count = fz_search_stext_page(v->ctx, st, v->search_buf,
                       NULL, v->hits, MAX_HITS);
    fz_drop_stext_page(v->ctx, st);
    fz_drop_page(v->ctx, page);

    if (v->hit_count > 0)
        v->hit = (dir > 0) ? 0 : v->hit_count - 1;
}
