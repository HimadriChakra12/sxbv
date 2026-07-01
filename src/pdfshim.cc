/*
 * pdfshim.cc -- C++ implementation of the pdfshim.h API.
 *
 * Performance notes vs the original implementation:
 *
 *   1. Cached SplashOutputDev per PdfDoc.
 *      startDoc() destroys and rebuilds the SplashFontEngine (FreeType font
 *      cache) and clears the Type3 cache.  Doing this once at pdf_open() and
 *      reusing the same dev for every displayPage() call saves the font-engine
 *      rebuild cost on every page switch -- the dominant cost for most PDFs.
 *
 *   2. Cached TextPage per (doc, page_num) pair.
 *      pdf_search_page() previously ran a full displayPage() through a
 *      TextOutputDev on every call.  Now the TextPage is built once per page
 *      (via takeText() after the first search on that page) and cached inside
 *      PdfDoc.  Subsequent searches on the same page skip displayPage()
 *      entirely and call TextPage::findText() directly.
 *
 *   3. to_bgrx() in image.c (not here) uses a 32-bit word pack trick that
 *      clang auto-vectorizes; see image.c.
 */

#include "pdfshim.h"

#include "poppler/PDFDoc.h"
#include "poppler/GlobalParams.h"
#include "poppler/Catalog.h"
#include "poppler/Page.h"
#include "poppler/SplashOutputDev.h"
#include "poppler/TextOutputDev.h"
#include "poppler/UTF.h"
#include "splash/SplashBitmap.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <string>

struct PdfDoc {
    std::unique_ptr<PDFDoc>           pdfdoc;
    std::unique_ptr<SplashOutputDev>  splash;
    std::unique_ptr<TextPage>         text_cache;
    int                               text_cache_page; /* 0-based, -1=none */
};

struct PdfPix {
    SplashBitmap *bmp;   /* non-owning; owned by doc->splash */
    int width;
    int height;
    int stride;
};

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
        splashModeRGB8,
        4,      /* row pad to 4-byte boundary */
        white,
        true    /* bitmapTopDown */
    );
    splash->startDoc(pdfdoc.get());

    PdfDoc *doc             = new PdfDoc;
    doc->pdfdoc             = std::move(pdfdoc);
    doc->splash             = std::move(splash);
    doc->text_cache_page    = -1;
    return doc;
}

void pdf_close(PdfDoc *doc)
{
    delete doc;
}

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

    /* Reuse the cached SplashOutputDev -- no startDoc() needed. */
    doc->pdfdoc->displayPage(
        doc->splash.get(),
        page_num + 1,
        dpi, dpi,
        rot,
        true, true, false
    );

    SplashBitmap *bmp = doc->splash->getBitmap();
    if (!bmp) return nullptr;

    PdfPix *pix = new PdfPix;
    pix->bmp    = bmp;
    pix->width  = bmp->getWidth();
    pix->height = bmp->getHeight();
    pix->stride = bmp->getRowSize();
    return pix;
}

void pdf_pix_free(PdfPix *pix)
{
    /* We don't own the bitmap -- just free the wrapper. */
    delete pix;
}

unsigned char *pdf_pix_samples(PdfPix *pix)
{
    return pix ? pix->bmp->getDataPtr() : nullptr;
}

int pdf_pix_width(PdfPix *pix)  { return pix ? pix->width  : 0; }
int pdf_pix_height(PdfPix *pix) { return pix ? pix->height : 0; }
int pdf_pix_stride(PdfPix *pix) { return pix ? pix->stride : 0; }

static TextPage *get_text_page(PdfDoc *doc, int page_num)
{
    if (doc->text_cache_page == page_num && doc->text_cache)
        return doc->text_cache.get();

    /* physLayout=false, fixedPitch=0, rawOrder=false, append=false */
    TextOutputDev tdev(nullptr, false, 0.0, false, false);
    if (!tdev.isOk()) return nullptr;

    doc->pdfdoc->displayPage(
        &tdev,
        page_num + 1,
        72.0, 72.0,
        0,
        true, true, false
    );

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
            false,  /* caseSensitive */
            false,  /* backward */
            false,  /* wholeWord */
            &x0, &y0, &x1, &y1
        );
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
