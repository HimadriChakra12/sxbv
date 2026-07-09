/*
 * pdfshim.cc -- C++ implementation of the pdfshim.h API.
 */

#include "pdfshim.h"

#include "poppler/PDFDoc.h"
#include "poppler/GlobalParams.h"
#include "poppler/Catalog.h"
#include "poppler/Page.h"
#include "poppler/PDFRectangle.h"
#include "poppler/SplashOutputDev.h"
#include "poppler/TextOutputDev.h"
#include "poppler/Annot.h"
#include "poppler/UTF.h"
#include "splash/SplashBitmap.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <memory>
#include <vector>
#include <string>
#include <unistd.h>

struct PdfDoc {
    std::unique_ptr<PDFDoc>           pdfdoc;
    std::unique_ptr<SplashOutputDev>  splash;
    std::unique_ptr<TextPage>         text_cache;
    int                               text_cache_page;
};

struct PdfPix {
    unsigned char *data;   /* owned copy of pixel data, freed by pdf_pix_free */
    int width, height, stride;
};

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */

int pdf_init(void)
{
    if (!globalParams) {
        globalParams = std::make_unique<GlobalParams>();
        if (!globalParams) return -1;
    }
    return 0;
}

void pdf_shutdown(void)
{
    globalParams.reset();
}

PdfDoc *pdf_open(const char *path)
{
    if (!path || !globalParams) return nullptr;

    auto gpath  = std::make_unique<GooString>(path);
    auto pdfdoc = std::make_unique<PDFDoc>(std::move(gpath));
    if (!pdfdoc->isOk()) return nullptr;

    SplashColor white;
    white[0] = white[1] = white[2] = 0xff;
    auto splash = std::make_unique<SplashOutputDev>(
        splashModeRGB8, 4, white, true);
    splash->startDoc(pdfdoc.get());

    PdfDoc *doc          = new PdfDoc;
    doc->pdfdoc          = std::move(pdfdoc);
    doc->splash          = std::move(splash);
    doc->text_cache_page = -1;
    return doc;
}

void pdf_close(PdfDoc *doc) { delete doc; }

int pdf_page_count(PdfDoc *doc)
{
    return doc ? doc->pdfdoc->getNumPages() : 0;
}

void pdf_page_label(PdfDoc *doc, int page_num, char *buf, int len)
{
    if (!doc || !buf || len <= 0) return;
    std::string label;
    if (doc->pdfdoc->getCatalog()->indexToLabel(page_num, &label))
        snprintf(buf, (size_t)len, "%s", label.c_str());
    else
        snprintf(buf, (size_t)len, "%d", page_num + 1);
}

PdfRect pdf_page_bounds(PdfDoc *doc, int page_num)
{
    PdfRect r = {0, 0, 0, 0};
    if (!doc) return r;
    Page *page = doc->pdfdoc->getPage(page_num + 1);
    if (!page) return r;
    const PDFRectangle &box = page->getCropBox();
    r.x0 = (float)box.x1;
    r.y0 = (float)box.y1;
    r.x1 = (float)box.x2;
    r.y1 = (float)box.y2;
    return r;
}

/* ------------------------------------------------------------------ */
/* Rendering                                                            */

PdfPix *pdf_render(PdfDoc *doc, int page_num, float zoom, int rotation)
{
    if (!doc) return nullptr;

    int rot = 0;
    switch (((rotation % 360) + 360) % 360) {
        case 90:  rot = 90;  break;
        case 180: rot = 180; break;
        case 270: rot = 270; break;
        default:  rot = 0;   break;
    }

    double dpi = (double)zoom * 72.0;
    if (dpi < 1.0) dpi = 1.0;

    doc->pdfdoc->displayPage(
        doc->splash.get(), page_num + 1,
        dpi, dpi, rot, true, true, false);

    SplashBitmap *bmp = doc->splash->getBitmap();
    if (!bmp) return nullptr;

    int w      = bmp->getWidth();
    int h      = bmp->getHeight();
    int stride = bmp->getRowSize();

    /* Copy the pixel data out now -- the bitmap is owned by doc->splash
     * and will be invalidated the moment we render another page or close
     * the doc. The caller must be able to use the PdfPix after pdf_close. */
    unsigned char *data = (unsigned char*)malloc((size_t)stride * h);
    if (!data) return nullptr;
    memcpy(data, bmp->getDataPtr(), (size_t)stride * h);

    PdfPix *pix = new PdfPix;
    pix->data   = data;
    pix->width  = w;
    pix->height = h;
    pix->stride = stride;
    return pix;
}

void           pdf_pix_free   (PdfPix *pix) { if (pix) { free(pix->data); delete pix; } }
unsigned char *pdf_pix_samples(PdfPix *pix) { return pix ? pix->data   : nullptr; }
int            pdf_pix_width  (PdfPix *pix) { return pix ? pix->width  : 0; }
int            pdf_pix_height (PdfPix *pix) { return pix ? pix->height : 0; }
int            pdf_pix_stride (PdfPix *pix) { return pix ? pix->stride : 0; }

/* ------------------------------------------------------------------ */
/* Search                                                               */

static TextPage *get_text_page(PdfDoc *doc, int page_num)
{
    if (doc->text_cache_page == page_num && doc->text_cache)
        return doc->text_cache.get();

    TextOutputDev tdev(nullptr, false, 0.0, false, false);
    if (!tdev.isOk()) return nullptr;

    doc->pdfdoc->displayPage(
        &tdev, page_num + 1, 72.0, 72.0, 0, true, true, false);

    doc->text_cache      = tdev.takeText();
    doc->text_cache_page = page_num;
    return doc->text_cache.get();
}

int pdf_search_page(PdfDoc *doc, int page_num,
                    const char *needle,
                    PdfRect *out, int max_hits)
{
    if (!doc || !needle || !needle[0] || !out || max_hits <= 0) return 0;

    std::vector<Unicode> ustr = utf8ToUCS4(needle);
    if (ustr.empty()) return 0;

    TextPage *tp = get_text_page(doc, page_num);
    if (!tp) return 0;

    int count = 0;
    double x0, y0, x1, y1;
    bool first = true;

    while (count < max_hits) {
        bool found = tp->findText(
            ustr.data(), (int)ustr.size(),
            first, true, !first, false,
            false, false, false,
            &x0, &y0, &x1, &y1);
        if (!found) break;
        out[count].x0 = (float)x0;
        out[count].y0 = (float)y0;
        out[count].x1 = (float)x1;
        out[count].y1 = (float)y1;
        count++;
        first = false;
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Annotation saving                                                    */

int pdf_annot_save(PdfDoc *doc,
                   const PdfInkStroke **strokes,
                   const int *n_per_page,
                   int page_count,
                   const char *out_path)
{
    if (!doc || !out_path) return -1;

    PDFDoc *pdfdoc = doc->pdfdoc.get();

    for (int pg = 0; pg < page_count && pg < pdfdoc->getNumPages(); pg++) {
        const PdfInkStroke *segs = strokes[pg];
        int n = n_per_page[pg];
        if (n == 0) continue;

        Page *page = pdfdoc->getPage(pg + 1);
        if (!page) continue;

        const PDFRectangle &box = page->getCropBox();
        double pw = box.x2 - box.x1;
        double ph = box.y2 - box.y1;
        if (pw <= 0 || ph <= 0) continue;

        /* Group consecutive segments of the same type+color into one
         * AnnotInk annotation. Each segment is two points (one sub-path).
         * PDF ink annotations are one object per original brush stroke;
         * grouping by color is close enough for typical usage. */
        /* Highlights → AnnotTextMarkup(typeHighlight): proper PDF highlight
         * with multiply blend mode, preserved correctly by all viewers.
         * Pencil → AnnotInk: freehand drawing annotation. */
        int i = 0;
        while (i < n) {
            char cur_type = segs[i].type;
            unsigned char r = segs[i].r, g = segs[i].g, b = segs[i].b;
            float thick = segs[i].thickness_norm;

            if (cur_type == 'H') {
                /* Collect consecutive highlight segs of the same color */
                int seg_start = i;
                while (i < n && segs[i].type == 'H' &&
                       segs[i].r == r && segs[i].g == g && segs[i].b == b)
                    i++;
                int nsegs = i - seg_start;

                auto quads = std::make_unique<
                    AnnotQuadrilaterals::AnnotQuadrilateral[]>(nsegs);
                double bx0=1e9, by0=1e9, bx1=-1e9, by1=-1e9;

                for (int k = 0; k < nsegs; k++) {
                    const PdfInkStroke &s = segs[seg_start + k];
                    double x0 = s.nx0 * pw + box.x1;
                    double y0 = (1.0 - s.ny0) * ph + box.y1;
                    double x1 = s.nx1 * pw + box.x1;
                    double y1 = (1.0 - s.ny1) * ph + box.y1;
                    double half = (double)s.thickness_norm * pw / 2.0;

                    double lx = std::min(x0,x1), rx = std::max(x0,x1);
                    double yt = std::max(y0,y1) + half; /* top in PDF coords */
                    double yb = std::min(y0,y1) - half; /* bottom */
                    if (rx - lx < 1.0) { lx -= half; rx += half; }

                    /* PDF quad order: UL, UR, LL, LR */
                    quads[k] = AnnotQuadrilaterals::AnnotQuadrilateral(
                        lx, yt,  rx, yt,
                        lx, yb,  rx, yb);

                    if (lx < bx0) bx0=lx; if (yb < by0) by0=yb;
                    if (rx > bx1) bx1=rx; if (yt > by1) by1=yt;
                }

                PDFRectangle rect(bx0, by0, bx1, by1);
                auto annot = std::make_shared<AnnotTextMarkup>(
                    pdfdoc, rect, Annot::typeHighlight);
                AnnotQuadrilaterals aq(std::move(quads), nsegs);
                annot->setQuadrilaterals(aq);
                annot->setColor(std::make_unique<AnnotColor>(
                    r/255.0, g/255.0, b/255.0));
                page->addAnnot(annot);

            } else {
                /* Pencil ink strokes */
                std::vector<std::unique_ptr<AnnotPath>> paths;
                while (i < n && segs[i].type == 'I' &&
                       segs[i].r == r && segs[i].g == g && segs[i].b == b) {
                    std::vector<AnnotCoord> coords;
                    double x0 = segs[i].nx0 * pw + box.x1;
                    double y0 = (1.0 - segs[i].ny0) * ph + box.y1;
                    double x1 = segs[i].nx1 * pw + box.x1;
                    double y1 = (1.0 - segs[i].ny1) * ph + box.y1;
                    coords.emplace_back(x0, y0);
                    coords.emplace_back(x1, y1);
                    paths.push_back(std::make_unique<AnnotPath>(std::move(coords)));
                    i++;
                }
                if (paths.empty()) continue;

                double margin = (double)thick * pw / 2.0;
                double bx0=1e9, by0=1e9, bx1=-1e9, by1=-1e9;
                for (auto &p : paths) {
                    for (int k = 0; k < p->getCoordsLength(); k++) {
                        double cx = p->getX(k), cy = p->getY(k);
                        if (cx-margin<bx0) bx0=cx-margin;
                        if (cy-margin<by0) by0=cy-margin;
                        if (cx+margin>bx1) bx1=cx+margin;
                        if (cy+margin>by1) by1=cy+margin;
                    }
                }
                PDFRectangle rect(bx0, by0, bx1, by1);

                auto annot = std::make_shared<AnnotInk>(pdfdoc, rect);
                annot->setInkList(paths);
                annot->setColor(std::make_unique<AnnotColor>(
                    r/255.0, g/255.0, b/255.0));
                {
                    auto border = std::make_unique<AnnotBorderArray>();
                    border->setWidth((double)thick * pw);
                    annot->setBorder(std::move(border));
                }
                page->addAnnot(annot);
            }
        }
    }

    /* Write to a temp file alongside the original, then rename atomically.
     * Writing directly to out_path while it's open for reading by Poppler
     * truncates it before the write completes, producing a zero-byte file. */
    size_t plen   = strlen(out_path);
    char *tmp_path = (char*)malloc(plen + 8);
    if (!tmp_path) return -1;
    memcpy(tmp_path, out_path, plen + 1);
    strcat(tmp_path, ".sxbvtmp");

    int rc = pdfdoc->saveAs(std::string(tmp_path)) == errNone ? 0 : -1;
    if (rc == 0) {
        if (rename(tmp_path, out_path) != 0) {
            perror("sxbv: rename");
            unlink(tmp_path);
            rc = -1;
        }
    } else {
        unlink(tmp_path);
    }
    free(tmp_path);
    return rc;
}
