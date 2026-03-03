#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sxbv.h"

/* forward declaration */
static void draw_bar(Viewer *v);

/* ------------------------------------------------------------------ */
/* Color helpers                                                        */

static void load_color(Viewer *v, SpdfColor *c, const char *spec)
{
    if (!XftColorAllocName(v->dpy, v->visual, v->cmap, spec, &c->xft)) {
        fprintf(stderr, "sxbv: cannot allocate color '%s', using black\n", spec);
        XftColorAllocName(v->dpy, v->visual, v->cmap, "#000000", &c->xft);
    }
    XColor xc;
    xc.red   = c->xft.color.red;
    xc.green = c->xft.color.green;
    xc.blue  = c->xft.color.blue;
    xc.flags = DoRed | DoGreen | DoBlue;
    if (XAllocColor(v->dpy, v->cmap, &xc))
        c->pixel = xc.pixel;
    else
        c->pixel = BlackPixel(v->dpy, v->screen);
}

static void load_colors(Viewer *v)
{
    load_color(v, &v->c_bg,     bgcolor);    /* bar background */
    load_color(v, &v->c_fg,     fgcolor);    /* text + separator */
    load_color(v, &v->c_mark,   hitcolor);
    load_color(v, &v->c_sel,    hitselcolor);
    load_color(v, &v->c_pagebg, pagebg);
}

/* ------------------------------------------------------------------ */
/* Font helper                                                          */

static XftFont *load_font(Viewer *v, const char *spec)
{
    size_t len = strlen(spec) + 1;
    char  *buf = malloc(len);
    if (!buf) return NULL;
    memcpy(buf, spec, len);

    XftFont *f = NULL;
    char    *tok  = buf;
    char    *next;
    while (tok && *tok) {
        while (*tok == ' ') tok++;
        next = strchr(tok, ',');
        if (next) *next++ = '\0';
        f = XftFontOpenName(v->dpy, v->screen, tok);
        if (f) break;
        fprintf(stderr, "sxbv: font '%s' not found, trying next\n", tok);
        tok = next;
    }
    free(buf);
    if (!f) {
        fprintf(stderr, "sxbv: all fonts failed, falling back to 'fixed'\n");
        f = XftFontOpenName(v->dpy, v->screen, "fixed");
    }
    return f;
}

/* ------------------------------------------------------------------ */
/* Bar drawing                                                          */

static void draw_bar(Viewer *v)
{
    int bx = 0;
    int by = topbar ? 0 : v->win_h - v->bar_h;

    /* Background -- bgcolor */
    XSetForeground(v->dpy, v->gc, v->c_bg.pixel);
    XFillRectangle(v->dpy, v->win, v->gc, bx, by, v->win_w, v->bar_h);

    /* Separator line -- fgcolor */
    XSetForeground(v->dpy, v->gc, v->c_fg.pixel);
    int ly = topbar ? by + v->bar_h - 1 : by;
    XDrawLine(v->dpy, v->win, v->gc, 0, ly, v->win_w, ly);

    /* Resolve display name: basename or full path */
    const char *dispname;
    if (v->show_fullpath) {
        dispname = v->filename;
    } else {
        const char *slash = strrchr(v->filename, '/');
        dispname = slash ? slash + 1 : v->filename;
    }

    /* Build left string */
    char left[512] = {0};
    if (v->search_mode) {
        snprintf(left, sizeof left, "/ %s", v->search_buf);
    } else if (v->hit_count > 0) {
        snprintf(left, sizeof left, "match %d/%d  %s",
                 v->hit + 1, v->hit_count,
                 v->show_filename ? dispname : "");
    } else if (v->show_filename) {
        snprintf(left, sizeof left, "%s", dispname);
    }

    /* Build right string */
    char right[256] = {0};
    char tmp[64];

    if (v->show_pagelabel) {
        char label[32] = {0};
        fz_page *pg = fz_load_page(v->ctx, v->doc, v->page);
        const char *lb = fz_page_label(v->ctx, pg, label, sizeof label);
        fz_drop_page(v->ctx, pg);
        if (lb && lb[0])
            snprintf(tmp, sizeof tmp, "%s (%d/%d)  ",
                     lb, v->page + 1, v->page_count);
        else
            snprintf(tmp, sizeof tmp, "%d/%d  ",
                     v->page + 1, v->page_count);
        strcat(right, tmp);
    }

    if (v->show_zoom) {
        snprintf(tmp, sizeof tmp, "%.0f%%  ", v->zoom * 100.0f);
        strcat(right, tmp);
    }

    if (v->show_fitmode) {
        const char *fs = "";
        switch (v->fit) {
            case FIT_WIDTH:  fs = "[W]  "; break;
            case FIT_HEIGHT: fs = "[H]  "; break;
            case FIT_PAGE:   fs = "[F]  "; break;
            default: break;
        }
        strcat(right, fs);
    }

    if (v->show_rotation && v->rotation != 0) {
        snprintf(tmp, sizeof tmp, "%d°  ", v->rotation);
        strcat(right, tmp);
    }

    if (v->show_fullscreen_indicator && v->fullscreen)
        strcat(right, "[FS]");

    /* Baseline */
    int baseline = by + (v->bar_h + v->font->ascent - v->font->descent) / 2;

    /* Left text -- fgcolor */
    XftDrawStringUtf8(v->xftdraw, &v->c_fg.xft, v->font,
        4, baseline,
        (const FcChar8*)left, strlen(left));

    /* Right text -- selcolor, right-aligned */
    XGlyphInfo ext;
    XftTextExtentsUtf8(v->dpy, v->font,
        (const FcChar8*)right, strlen(right), &ext);
    int rx = v->win_w - ext.width - 6;
    if (rx > 4)
        XftDrawStringUtf8(v->xftdraw, &v->c_fg.xft, v->font,
                rx, baseline,
                (const FcChar8*)right, strlen(right));

    /* Cursor in search mode */
    if (v->search_mode) {
        XGlyphInfo cext;
        char cur[MAX_SEARCH + 4];
        snprintf(cur, sizeof cur, "/ %s", v->search_buf);
        XftTextExtentsUtf8(v->dpy, v->font,
            (const FcChar8*)cur, strlen(cur), &cext);
        XSetForeground(v->dpy, v->gc, v->c_fg.pixel);
        XFillRectangle(v->dpy, v->win, v->gc,
            4 + cext.width, by + 3, 1, v->bar_h - 6);
    }
}

/* ------------------------------------------------------------------ */
/* Main draw                                                            */

void win_draw(Viewer *v)
{
    int bar_off = 0;
    int page_h  = v->win_h;

    if (v->bar_visible) {
        bar_off = topbar ? v->bar_h : 0;
        page_h  = v->win_h - v->bar_h;
    }

    int page_y = topbar ? bar_off : 0;

    /* Page area background */
    XSetForeground(v->dpy, v->gc, v->c_pagebg.pixel);
    XFillRectangle(v->dpy, v->win, v->gc, 0, page_y, v->win_w, page_h);

    /* Page image */
    if (v->pix) {
        unsigned char *bgrx = to_bgrx(v);
        if (bgrx) {
            XImage *img = XCreateImage(v->dpy, v->visual,
                24, ZPixmap, 0, (char*)bgrx,
                v->pix_w, v->pix_h, 32, 0);
            if (img) {
                int sx=0, sy=0;
                int dx=v->scroll_x, dy=v->scroll_y + bar_off;
                int dw=v->pix_w,    dh=v->pix_h;
                if (dx < 0) { sx = -dx; dw += dx; dx = 0; }
                if (dy < page_y) { sy = page_y - dy; dh -= sy; dy = page_y; }
                if (dw > v->win_w - dx) dw = v->win_w - dx;
                int bot = page_y + page_h;
                if (dy + dh > bot) dh = bot - dy;
                if (dw > 0 && dh > 0)
                    XPutImage(v->dpy, v->win, v->gc, img,
                        sx, sy, dx, dy, dw, dh);
                img->data = NULL;
                XDestroyImage(img);
            }
            free(bgrx);
        }
    }

    /* Search hit rectangles */
    if (v->hit_count > 0) {
        fz_matrix m = page_matrix(v);
        for (int i = 0; i < v->hit_count; i++) {
            fz_quad  q      = v->hits[i];
            fz_point pts[4] = {q.ul, q.ur, q.lr, q.ll};
            float x0=1e9f, y0=1e9f, x1=-1e9f, y1=-1e9f;
            for (int j = 0; j < 4; j++) {
                fz_point p = fz_transform_point(pts[j], m);
                if (p.x < x0) x0 = p.x;
                if (p.y < y0) y0 = p.y;
                if (p.x > x1) x1 = p.x;
                if (p.y > y1) y1 = p.y;
            }
            XSetForeground(v->dpy, v->gc,
                (i == v->hit) ? v->c_sel.pixel : v->c_mark.pixel);
            XDrawRectangle(v->dpy, v->win, v->gc,
                (int)x0 + v->scroll_x,
                (int)y0 + v->scroll_y + bar_off,
                (int)(x1 - x0), (int)(y1 - y0));
        }
    }

    /* Status bar -- only if visible */
    if (v->bar_visible)
        draw_bar(v);
}

/* ------------------------------------------------------------------ */
/* Init                                                                 */

int win_init(Viewer *v)
{
    v->dpy = XOpenDisplay(NULL);
    if (!v->dpy) { fprintf(stderr, "sxbv: cannot open display\n"); return 0; }

    v->screen = DefaultScreen(v->dpy);
    v->visual = DefaultVisual(v->dpy, v->screen);
    v->cmap   = DefaultColormap(v->dpy, v->screen);
    v->win_w  = (int)(DisplayWidth(v->dpy, v->screen)  * WIN_SCREEN_FRAC);
    v->win_h  = (int)(DisplayHeight(v->dpy, v->screen) * WIN_SCREEN_FRAC);

    load_colors(v);

    v->font  = load_font(v, statusfont);
    v->bar_h = v->font
        ? v->font->ascent + v->font->descent + 6
        : 20;

    v->win = XCreateSimpleWindow(v->dpy, RootWindow(v->dpy, v->screen),
        0, 0, v->win_w, v->win_h, 0,
        v->c_bg.pixel, v->c_bg.pixel);

    v->wm_delete         = XInternAtom(v->dpy, "WM_DELETE_WINDOW", False);
    v->net_wm_state      = XInternAtom(v->dpy, "_NET_WM_STATE", False);
    v->net_wm_fullscreen = XInternAtom(v->dpy, "_NET_WM_STATE_FULLSCREEN", False);
    XSetWMProtocols(v->dpy, v->win, &v->wm_delete, 1);

    XSelectInput(v->dpy, v->win,
        ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
    v->gc = DefaultGC(v->dpy, v->screen);
    XMapWindow(v->dpy, v->win);

    XEvent ev;
    for (;;) { XNextEvent(v->dpy, &ev); if (ev.type == MapNotify) break; }
    XWindowAttributes wa;
    XGetWindowAttributes(v->dpy, v->win, &wa);
    v->win_w = wa.width;
    v->win_h = wa.height;

    v->xftdraw = XftDrawCreate(v->dpy, v->win, v->visual, v->cmap);
    if (!v->xftdraw) {
        fprintf(stderr, "sxbv: XftDrawCreate failed\n");
        return 0;
    }

    return 1;
}

/* ------------------------------------------------------------------ */
/* Title / fullscreen                                                   */

void win_update_title(Viewer *v)
{
    char buf[512];
    snprintf(buf, sizeof buf, "sxbv — %s [%d/%d] %.0f%%",
             v->filename, v->page + 1, v->page_count, v->zoom * 100.0f);
    XStoreName(v->dpy, v->win, buf);
}

/* saved geometry for restoring from fullscreen */
static int saved_x, saved_y, saved_w, saved_h;

void win_toggle_fullscreen(Viewer *v)
{
    if (!v->fullscreen) {
        XWindowAttributes wa;
        XGetWindowAttributes(v->dpy, v->win, &wa);
        Window child;
        XTranslateCoordinates(v->dpy, v->win,
            DefaultRootWindow(v->dpy), 0, 0,
            &saved_x, &saved_y, &child);
        saved_w = wa.width;
        saved_h = wa.height;

        Atom mwm = XInternAtom(v->dpy, "_MOTIF_WM_HINTS", False);
        if (mwm != None) {
            long hints[5] = { 2, 0, 0, 0, 0 };
            XChangeProperty(v->dpy, v->win, mwm, mwm,
                32, PropModeReplace, (unsigned char*)hints, 5);
        }

        XEvent e = {0};
        e.type                 = ClientMessage;
        e.xclient.window       = v->win;
        e.xclient.message_type = v->net_wm_state;
        e.xclient.format       = 32;
        e.xclient.data.l[0]    = 1;
        e.xclient.data.l[1]    = (long)v->net_wm_fullscreen;
        e.xclient.data.l[2]    = 0;
        e.xclient.data.l[3]    = 1;
        XSendEvent(v->dpy, DefaultRootWindow(v->dpy), False,
            SubstructureRedirectMask | SubstructureNotifyMask, &e);

        XMoveResizeWindow(v->dpy, v->win, 0, 0,
            DisplayWidth(v->dpy, v->screen),
            DisplayHeight(v->dpy, v->screen));
        XRaiseWindow(v->dpy, v->win);
        v->fullscreen = 1;

    } else {
        Atom mwm = XInternAtom(v->dpy, "_MOTIF_WM_HINTS", False);
        if (mwm != None) {
            long hints[5] = { 2, 0, 1, 0, 0 };
            XChangeProperty(v->dpy, v->win, mwm, mwm,
                32, PropModeReplace, (unsigned char*)hints, 5);
        }

        XEvent e = {0};
        e.type                 = ClientMessage;
        e.xclient.window       = v->win;
        e.xclient.message_type = v->net_wm_state;
        e.xclient.format       = 32;
        e.xclient.data.l[0]    = 0;
        e.xclient.data.l[1]    = (long)v->net_wm_fullscreen;
        e.xclient.data.l[2]    = 0;
        e.xclient.data.l[3]    = 1;
        XSendEvent(v->dpy, DefaultRootWindow(v->dpy), False,
            SubstructureRedirectMask | SubstructureNotifyMask, &e);

        XMoveResizeWindow(v->dpy, v->win, saved_x, saved_y, saved_w, saved_h);
        v->fullscreen = 0;
    }

    XFlush(v->dpy);
}
