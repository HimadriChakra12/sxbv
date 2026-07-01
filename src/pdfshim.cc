/*
 * pdfshim.cc -- C++ implementation of the pdfshim.h API.
 *
 * This is the ONLY C++ file in sxbv.  It wraps Poppler's Splash-backend
 * rendering path and exposes a clean, minimal extern "C" interface that
 * the rest of the codebase (plain C99) can call without knowing anything
 * about C++ or Poppler internals.
 *
 * Internal conventions:
 *   - Poppler page numbers are 1-based; every public pdf_* function takes
 *     0-based page numbers and converts internally.
 *   - zoom is a linear scale relative to 72 dpi: zoom=1.0 → 72 dpi.
 *     We convert to DPI for displayPage(): dpi = zoom * 72.0.
 *   - SplashOutputDev is recreated per render.  Keeping one per PdfDoc
 *     would require careful startDoc() management across page switches and
 *     across concurrent thumbnail docs; a fresh dev per call is simpler and
 *     fast enough (the expensive part is parsing + rasterising, not
 *     constructing the output device).
 */

#include "pdfshim.h"

/* Poppler core */
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

/* ------------------------------------------------------------------ */
/* Internal struct definitions                                         */
/* ------------------------------------------------------------------ */

struct PdfDoc {
    std::unique_ptr<PDFDoc> pdfdoc;
};

/*
 * PdfPix owns a SplashOutputDev whose lifetime is tied to the bitmap
 * we return.  The dev keeps the SplashBitmap alive; once we call
 * pdf_pix_free() and destroy the dev, the bitmap memory is gone.
 *
 * We copy the bitmap dimensions and stride at render time so the
 * accessor functions don't have to touch the dev at all.
 */
struct PdfPix {
    std::unique_ptr<SplashOutputDev> dev;
    int width;
    int height;
    int stride;             /* bytes per row (Splash pads to 4-byte boundary) */
};

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

int pdf_init(void)
{
    /* GlobalParams is a std::unique_ptr<GlobalParams> in globalParams.
     * It must be set before any PDFDoc is opened. */
    if (!globalParams) {
        globalParams = std::make_unique<GlobalParams>();
        if (!globalParams)
            return -1;
    }
    return 0;
}

void pdf_shutdown(void)
{
    globalParams.reset();
}

PdfDoc *pdf_open(const char *path)
{
    if (!path || !globalParams)
        return nullptr;

    auto gpath = std::make_unique<GooString>(path);
    auto pdfdoc = std::make_unique<PDFDoc>(std::move(gpath));

    if (!pdfdoc->isOk())
        return nullptr;

    PdfDoc *doc = new PdfDoc;
    doc->pdfdoc = std::move(pdfdoc);
    return doc;
}

void pdf_close(PdfDoc *doc)
{
    delete doc;
}

int pdf_page_count(PdfDoc *doc)
{
    if (!doc) return 0;
    return doc->pdfdoc->getNumPages();
}

void pdf_page_label(PdfDoc *doc, int page_num, char *buf, int len)
{
    if (!doc || !buf || len <= 0)
        return;

    /* Poppler Catalog::indexToLabel() takes a 0-based index. */
    std::string label;
    if (doc->pdfdoc->getCatalog()->indexToLabel(page_num, &label)) {
        snprintf(buf, (size_t)len, "%s", label.c_str());
    } else {
        /* Fall back to the 1-based page number as a string. */
        snprintf(buf, (size_t)len, "%d", page_num + 1);
    }
}

/* ------------------------------------------------------------------ */
/* Page geometry                                                       */
/* ------------------------------------------------------------------ */

PdfRect pdf_page_bounds(PdfDoc *doc, int page_num)
{
    PdfRect r = {0, 0, 0, 0};
    if (!doc) return r;

    /* getPage() takes a 1-based page number. */
    Page *page = doc->pdfdoc->getPage(page_num + 1);
    if (!page) return r;

    /* Use the CropBox when present (it clips to the visible area);
     * fall back to the MediaBox.  This matches how Poppler's own tools
     * (pdftoppm etc.) determine page extent. */
    const PDFRectangle &box = page->getCropBox();
    r.x0 = (float)box.x1;
    r.y0 = (float)box.y1;
    r.x1 = (float)box.x2;
    r.y1 = (float)box.y2;
    return r;
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

PdfPix *pdf_render(PdfDoc *doc, int page_num, float zoom, int rotation)
{
    if (!doc) return nullptr;

    /* Clamp rotation to valid Poppler values. */
    int rot = 0;
    switch (((rotation % 360) + 360) % 360) {
        case 90:  rot = 90;  break;
        case 180: rot = 180; break;
        case 270: rot = 270; break;
        default:  rot = 0;   break;
    }

    /* Poppler DPI = zoom * 72.0 (PDF user-space unit = 1/72 inch). */
    double dpi = (double)zoom * 72.0;
    if (dpi < 1.0) dpi = 1.0;

    /* White paper background. */
    SplashColor white;
    white[0] = white[1] = white[2] = 0xff;

    auto dev = std::make_unique<SplashOutputDev>(
        splashModeRGB8,
        4,          /* row pad to 4-byte boundary */
        white,
        true        /* bitmapTopDown: row 0 = top of page, matches X11 */
    );
    dev->startDoc(doc->pdfdoc.get());

    /* displayPage() takes a 1-based page number.
     * useMediaBox=true, crop=true, printing=false */
    doc->pdfdoc->displayPage(
        dev.get(),
        page_num + 1,
        dpi, dpi,
        rot,
        true,   /* useMediaBox */
        true,   /* crop */
        false   /* printing */
    );

    SplashBitmap *bmp = dev->getBitmap();
    if (!bmp) return nullptr;

    PdfPix *pix = new PdfPix;
    pix->dev    = std::move(dev);
    pix->width  = bmp->getWidth();
    pix->height = bmp->getHeight();
    pix->stride = bmp->getRowSize();   /* may be > width*3 due to padding */
    return pix;
}

void pdf_pix_free(PdfPix *pix)
{
    delete pix;
}

unsigned char *pdf_pix_samples(PdfPix *pix)
{
    if (!pix) return nullptr;
    return pix->dev->getBitmap()->getDataPtr();
}

int pdf_pix_width(PdfPix *pix)
{
    return pix ? pix->width : 0;
}

int pdf_pix_height(PdfPix *pix)
{
    return pix ? pix->height : 0;
}

int pdf_pix_stride(PdfPix *pix)
{
    return pix ? pix->stride : 0;
}

/* ------------------------------------------------------------------ */
/* Text search                                                         */
/* ------------------------------------------------------------------ */

/*
 * pdf_search_page() -- find all occurrences of needle on page_num.
 *
 * We create a TextOutputDev (null output -- just builds the text layout
 * tree), run displayPage through it, then iterate findText() calls to
 * collect all hit rects.  TextOutputDev::findText() returns one hit per
 * call, advancing its internal cursor; we loop until no more hits.
 *
 * The returned rects are in PDF point space (same coords as
 * pdf_page_bounds()), y-axis bottom-up.
 */
int pdf_search_page(PdfDoc *doc, int page_num,
                    const char *needle,
                    PdfRect *out, int max_hits)
{
    if (!doc || !needle || !needle[0] || !out || max_hits <= 0)
        return 0;

    /* Convert needle from UTF-8 to Unicode (UCS-4) for findText(). */
    std::vector<Unicode> ustr = utf8ToUCS4(needle);
    if (ustr.empty()) return 0;

    /*
     * TextOutputDev(nullptr, ...) discards output (no file written)
     * but builds the internal text-selection tree.
     * physLayout=false, fixedPitch=0, rawOrder=false, append=false.
     */
    TextOutputDev tdev(nullptr, false, 0.0, false, false);
    if (!tdev.isOk()) return 0;

    doc->pdfdoc->displayPage(
        &tdev,
        page_num + 1,
        72.0, 72.0,  /* always 72 dpi for search -- coords are in points */
        0,           /* rotation handled by page's own /Rotate entry */
        true, true, false
    );

    int count = 0;
    double x0, y0, x1, y1;

    /*
     * First call: startAtTop=true, stopAtBottom=true.
     * Subsequent calls: startAtTop=false, startAtLast=true (continue from
     * where the previous hit ended).
     */
    bool first = true;
    while (count < max_hits) {
        bool found = tdev.findText(
            ustr.data(), (int)ustr.size(),
            first,   /* startAtTop */
            true,    /* stopAtBottom */
            !first,  /* startAtLast */
            false,   /* stopAtLast */
            false,   /* caseSensitive */
            false,   /* backward */
            false,   /* wholeWord */
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
