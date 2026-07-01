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

    v->hit_count = pdf_search_page(v->doc, v->page,
                                   v->search_buf,
                                   v->hits, MAX_HITS);

    if (v->hit_count > 0)
        v->hit = (dir > 0) ? 0 : v->hit_count - 1;
}
