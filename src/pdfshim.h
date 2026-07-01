/*
 * pdfshim.h -- pure-C interface to the vendored Poppler/Splash backend.
 *
 * This is the ONLY file that the rest of sxbv's C code includes in order
 * to drive PDF rendering.  The implementation (pdfshim.cc) is the only
 * C++ translation unit in the project; everything else is plain C99.
 *
 * Design principles:
 *   - No context object.  Poppler uses a process-wide GlobalParams singleton.
 *     Call pdf_init() once at startup, pdf_shutdown() at exit.
 *   - PdfDoc  wraps a PDFDoc + a reusable SplashOutputDev.
 *   - PdfPix  wraps the SplashBitmap produced by one displayPage() call.
 *     It owns its pixel data; call pdf_pix_free() when done.
 *   - Search returns axis-aligned rects in PDF point space (72 pt = 1 inch).
 *     The caller applies its own zoom/rotation for display.
 *   - All coordinates use the PDF convention: origin at bottom-left of page,
 *     y increases upward.  The Splash backend flips to top-down in the bitmap,
 *     which is what X11 expects anyway.
 */

#ifndef PDFSHIM_H
#define PDFSHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

/* Axis-aligned rect in PDF point space (float). */
typedef struct {
    float x0, y0;   /* bottom-left  */
    float x1, y1;   /* top-right    */
} PdfRect;

/* ------------------------------------------------------------------ */
/* Opaque handles                                                      */
/* ------------------------------------------------------------------ */

typedef struct PdfDoc PdfDoc;
typedef struct PdfPix PdfPix;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/*
 * pdf_init() -- must be called once before any other pdf_* function.
 * Initialises Poppler's GlobalParams singleton.
 * Returns 0 on success, -1 on failure.
 */
int  pdf_init(void);

/* pdf_shutdown() -- release GlobalParams.  Call at process exit. */
void pdf_shutdown(void);

/*
 * pdf_open() -- open a PDF file by path.
 * Returns NULL if the file cannot be opened or is not a valid PDF.
 * The returned PdfDoc must be released with pdf_close().
 */
PdfDoc *pdf_open(const char *path);

/* pdf_close() -- release all resources for a document. */
void pdf_close(PdfDoc *doc);

/* pdf_page_count() -- number of pages in the document (1-indexed in PDF,
 * but this function returns the raw count; callers use 0-based page numbers
 * everywhere else in sxbv and this shim converts internally). */
int  pdf_page_count(PdfDoc *doc);

/*
 * pdf_page_label() -- copy the label string for page page_num (0-based)
 * into buf (size len).  Falls back to the 1-based numeric index if the
 * document has no named labels.  Always NUL-terminates buf.
 */
void pdf_page_label(PdfDoc *doc, int page_num, char *buf, int len);

/* ------------------------------------------------------------------ */
/* Page geometry (in PDF point space, before any zoom/rotation)        */
/* ------------------------------------------------------------------ */

/*
 * pdf_page_bounds() -- bounding box of page_num (0-based) in PDF points.
 * This is the MediaBox (or CropBox if set).  Use this to compute
 * zoom-to-fit values before calling pdf_render().
 */
PdfRect pdf_page_bounds(PdfDoc *doc, int page_num);

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

/*
 * pdf_render() -- rasterise page_num (0-based) at the given zoom factor
 * and rotation (degrees, must be 0/90/180/270).
 *
 * zoom is a linear scale factor relative to the page's natural size at
 * 72 dpi: zoom=1.0 → 72 dpi, zoom=2.0 → 144 dpi, etc.  sxbv passes
 * v->zoom directly.
 *
 * Returns a new PdfPix on success, NULL on failure.
 * The caller must release it with pdf_pix_free().
 */
PdfPix *pdf_render(PdfDoc *doc, int page_num, float zoom, int rotation);

/* pdf_pix_free() -- release a rendered bitmap. */
void pdf_pix_free(PdfPix *pix);

/*
 * Pixel data accessors.
 * pdf_pix_samples() returns a pointer to the raw RGB8 (3 bytes/pixel)
 * top-down row-major pixel buffer.  The pointer is valid until
 * pdf_pix_free() is called.  Row stride = pdf_pix_width(pix) * 3,
 * rounded up to a 4-byte boundary by Splash (use pdf_pix_stride()).
 */
unsigned char *pdf_pix_samples(PdfPix *pix);
int            pdf_pix_width(PdfPix *pix);
int            pdf_pix_height(PdfPix *pix);
int            pdf_pix_stride(PdfPix *pix); /* bytes per row */

/* ------------------------------------------------------------------ */
/* Text search                                                         */
/* ------------------------------------------------------------------ */

/*
 * pdf_search_page() -- search for needle (UTF-8) on page_num (0-based).
 *
 * Fills out[] with up to max_hits axis-aligned rects in PDF point space.
 * Returns the number of hits found (may be > max_hits; only the first
 * max_hits are written).  Returns 0 if no hits or on error.
 *
 * Rects are in the same coordinate space as pdf_page_bounds(): origin
 * bottom-left, y increasing upward.  The caller applies zoom/rotation
 * when drawing highlights (see window.c).
 */
int pdf_search_page(PdfDoc *doc, int page_num,
                    const char *needle,
                    PdfRect *out, int max_hits);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PDFSHIM_H */
