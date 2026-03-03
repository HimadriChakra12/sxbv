#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sxbv.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                              */

void clamp_scroll(Viewer *v)
{
    if (v->pix_w <= v->win_w) {
        v->scroll_x = (v->win_w - v->pix_w) / 2;
    } else {
        if (v->scroll_x > 0) v->scroll_x = 0;
        if (v->scroll_x < v->win_w - v->pix_w)
            v->scroll_x = v->win_w - v->pix_w;
    }
    if (v->pix_h <= v->win_h) {
        v->scroll_y = (v->win_h - v->pix_h) / 2;
    } else {
        if (v->scroll_y > 0) v->scroll_y = 0;
        if (v->scroll_y < v->win_h - v->pix_h)
            v->scroll_y = v->win_h - v->pix_h;
    }
}

void go_page(Viewer *v, int p)
{
    if (p < 0) p = 0;
    if (p >= v->page_count) p = v->page_count - 1;
    if (p == v->page) return;
    v->page = p;
    search_free(v);
    render_page(v);
}

void zoom_by(Viewer *v, float d)
{
    v->fit  = FIT_NONE;
    v->zoom = fmaxf(ZOOM_MIN, fminf(ZOOM_MAX, v->zoom + d));
    render_page(v);
}

/* ------------------------------------------------------------------ */
/* Keybind table (generated from config.h macros)                      */

static const Keybind keybinds[] = {
#define BIND(ks, mod, cmd) { ks, mod, cmd },
    KEYBINDINGS
#undef BIND
};
static const int n_keybinds = sizeof keybinds / sizeof keybinds[0];

static Command lookup_key(KeySym ks, unsigned int mod)
{
    /* Strip Lock keys from modifier */
    mod &= ~(LockMask | Mod2Mask | Mod3Mask | Mod4Mask | Mod5Mask);
    for (int i = 0; i < n_keybinds; i++)
        if (keybinds[i].ks == ks && keybinds[i].mod == mod)
            return keybinds[i].cmd;
    return CMD_NONE;
}

/* ------------------------------------------------------------------ */
/* Input handling                                                       */

static void handle_search_key(Viewer *v, KeySym ks, const char *buf, int len)
{
    if (ks == XK_Return || ks == XK_KP_Enter) {
        v->search_mode = 0;
        search_do(v, v->search_dir);
        render_page(v);
    } else if (ks == XK_Escape) {
        v->search_mode    = 0;
        v->search_buf[0]  = '\0';
        render_page(v);
    } else if (ks == XK_BackSpace) {
        int sl = strlen(v->search_buf);
        if (sl > 0) v->search_buf[sl - 1] = '\0';
    } else if (len > 0 && buf[0] >= 32) {
        int sl = strlen(v->search_buf);
        if (sl + len < MAX_SEARCH - 1)
            strcat(v->search_buf, buf);
    }
    win_draw(v);
}

static void run_command(Viewer *v, Command cmd, int cnt)
{
    int sl = SCROLL_LINE;
    int sp = (int)(v->win_h * SCROLL_PAGE_FRAC);

    switch (cmd) {
        case CMD_TOGGLE_FULLPATH:
            v->show_fullpath = !v->show_fullpath;
            win_draw(v); break;
        case CMD_TOGGLE_BAR:
            v->bar_visible = !v->bar_visible;
            render_page(v); win_draw(v); break;
        case CMD_TOGGLE_FILENAME:
            v->show_filename = !v->show_filename;
            win_draw(v); break;
        case CMD_TOGGLE_ZOOM:
            v->show_zoom = !v->show_zoom;
            win_draw(v); break;
        case CMD_TOGGLE_FITMODE:
            v->show_fitmode = !v->show_fitmode;
            win_draw(v); break;
        case CMD_TOGGLE_ROTATION_IND:
            v->show_rotation = !v->show_rotation;
            win_draw(v); break;
        case CMD_SCROLL_DOWN:
            if (v->pix_h > v->win_h) {
                v->scroll_y -= sl * cnt;
                clamp_scroll(v);
            } else {
                go_page(v, v->page + cnt);
            }
            break;
        case CMD_SCROLL_UP:
            if (v->pix_h > v->win_h) {
                v->scroll_y += sl * cnt;
                clamp_scroll(v);
            } else {
                go_page(v, v->page - cnt);
            }
            break;
        case CMD_SCROLL_LEFT:
            v->scroll_x += sl * cnt;
            clamp_scroll(v);
            break;
        case CMD_SCROLL_RIGHT:
            v->scroll_x -= sl * cnt;
            clamp_scroll(v);
            break;
        case CMD_SCREEN_DOWN:
            if (v->pix_h > v->win_h) {
                int prev = v->scroll_y;
                v->scroll_y -= sp;
                clamp_scroll(v);
                if (v->scroll_y == prev) go_page(v, v->page + 1);
            } else {
                go_page(v, v->page + cnt);
            }
            break;
        case CMD_SCREEN_UP:
            if (v->pix_h > v->win_h) {
                int prev = v->scroll_y;
                v->scroll_y += sp;
                clamp_scroll(v);
                if (v->scroll_y == prev) go_page(v, v->page - 1);
            } else {
                go_page(v, v->page - cnt);
            }
            break;
        case CMD_NEXT_PAGE:    go_page(v, v->page + cnt); break;
        case CMD_PREV_PAGE:    go_page(v, v->page - cnt); break;
        case CMD_FIRST_PAGE:   go_page(v, cnt > 1 ? cnt - 1 : 0); break;
        case CMD_LAST_PAGE:    go_page(v, cnt > 1 ? cnt - 1 : v->page_count - 1); break;
        case CMD_ZOOM_IN:      zoom_by(v,  ZOOM_STEP * cnt); break;
        case CMD_ZOOM_OUT:     zoom_by(v, -ZOOM_STEP * cnt); break;
        case CMD_ZOOM_RESET:   v->fit = FIT_NONE; v->zoom = DEFAULT_ZOOM; render_page(v); break;
        case CMD_FIT_WIDTH:    v->fit = FIT_WIDTH;  render_page(v); break;
        case CMD_FIT_HEIGHT:   v->fit = FIT_HEIGHT; render_page(v); break;
        case CMD_FIT_PAGE:     v->fit = FIT_PAGE;   render_page(v); break;
        case CMD_ROTATE_CW:    v->rotation = (v->rotation + 90)  % 360; render_page(v); break;
        case CMD_ROTATE_CCW:   v->rotation = (v->rotation + 270) % 360; render_page(v); break;
        case CMD_FULLSCREEN:   win_toggle_fullscreen(v); break;
        case CMD_SEARCH_START:
            v->search_mode   = 1;
            v->search_dir    = 1;
            v->search_buf[0] = '\0';
            break;
        case CMD_SEARCH_NEXT:
            if (v->hit_count > 0)
                v->hit = (v->hit + 1) % v->hit_count;
            else
                search_do(v, 1);
            break;
        case CMD_SEARCH_PREV:
            if (v->hit_count > 0)
                v->hit = (v->hit - 1 + v->hit_count) % v->hit_count;
            else
                search_do(v, -1);
            break;
        case CMD_QUIT:
            exit(0);
        default:
            return; /* unknown command, don't redraw */
    }
    win_draw(v);
}

static void handle_key(Viewer *v, XKeyEvent *ke)
{
    char   buf[32];
    int    len = XLookupString(ke, buf, 31, NULL, NULL);
    buf[len]   = '\0';
    KeySym ks  = XLookupKeysym(ke, 0);

    if (v->search_mode) {
        handle_search_key(v, ks, buf, len);
        return;
    }

    /* Accumulate numeric prefix (e.g. 5j = scroll 5 lines) */
    if (ks >= XK_0 && ks <= XK_9 && !(ke->state & ControlMask)) {
        v->num_buf   = v->num_valid ? v->num_buf * 10 + (ks - XK_0) : (ks - XK_0);
        v->num_valid = 1;
        return;
    }

    int cnt      = v->num_valid ? v->num_buf : 1;
    v->num_valid = v->num_buf = 0;

    Command cmd = lookup_key(ks, ke->state);
    if (cmd != CMD_NONE)
        run_command(v, cmd, cnt);
}

static void handle_button(Viewer *v, XButtonEvent *be)
{
    if (be->button == 4) {         /* scroll up */
        if (be->state & ControlMask)
            zoom_by(v, ZOOM_STEP);
        else
            run_command(v, CMD_SCROLL_UP, 2);
    } else if (be->button == 5) {  /* scroll down */
        if (be->state & ControlMask)
            zoom_by(v, -ZOOM_STEP);
        else
            run_command(v, CMD_SCROLL_DOWN, 2);
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [options] file\n"
        "\n"
        "Options:\n"
        "  -p N    open at page N (1-based)\n"
        "  -z Z    initial zoom (e.g. 1.5 for 150%%)\n"
        "  -f      start in fit-page mode (default)\n"
        "  -W      start in fit-width mode\n"
        "  -F      start fullscreen\n"
        "\n"
        "Keys (see config.h to remap):\n"
        "  j/k  Down/Up      scroll or prev/next page\n"
        "  h/l  PgUp/PgDn    prev / next page\n"
        "  Space/BackSpace   screen down/up\n"
        "  g/G               first/last  (Ng = jump to page N)\n"
        "  +/-/0             zoom in/out/reset\n"
        "  w/e/f             fit width/height/page\n"
        "  r/R               rotate CW/CCW\n"
        "  F                 toggle fullscreen\n"
        "  /  n/N            search, next/prev hit\n"
        "  q/Esc             quit\n"
        "  Ctrl+scroll       zoom\n",
        prog);
}

int main(int argc, char **argv)
{
    Viewer v = {0};
    v.zoom       = DEFAULT_ZOOM;
    v.fit        = DEFAULT_FIT;
    v.search_dir = 1;

    /* Parse options */
    int opt_page = 0;
    int opt_fs   = 0;
    int i;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') break;
        if (!strcmp(argv[i], "-p") && i+1 < argc) {
            opt_page = atoi(argv[++i]) - 1;
        } else if (!strcmp(argv[i], "-z") && i+1 < argc) {
            v.zoom = (float)atof(argv[++i]);
            v.fit  = FIT_NONE;
        } else if (!strcmp(argv[i], "-f")) {
            v.fit = FIT_PAGE;
        } else if (!strcmp(argv[i], "-W")) {
            v.fit = FIT_WIDTH;
        } else if (!strcmp(argv[i], "-F")) {
            opt_fs = 1;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]); return 0;
        } else {
            fprintf(stderr, "sxbv: unknown option: %s\n", argv[i]);
            usage(argv[0]); return 1;
        }
    }
    if (i >= argc) { usage(argv[0]); return 1; }
    v.filename = argv[i];

    /* MuPDF init */
    v.ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (!v.ctx) { fprintf(stderr, "sxbv: cannot create mupdf context\n"); return 1; }
    fz_register_document_handlers(v.ctx);
    fz_try(v.ctx) { v.doc = fz_open_document(v.ctx, v.filename); }
    fz_catch(v.ctx) { v.doc = NULL; }
    if (!v.doc) { fprintf(stderr, "sxbv: cannot open: %s\n", v.filename); return 1; }
    v.page_count = fz_count_pages(v.ctx, v.doc);
    if (v.page_count <= 0) { fprintf(stderr, "sxbv: no pages\n"); return 1; }
    v.page = (opt_page >= 0 && opt_page < v.page_count) ? opt_page : 0;

    /* X11 init */
    if (!win_init(&v)) return 1;
    v.bar_visible              = showbar;
    v.show_filename            = show_filename;
    v.show_fullpath            = show_fullpath;
    v.show_pagelabel           = show_pagelabel;
    v.show_zoom                = show_zoom;
    v.show_fitmode             = show_fitmode;
    v.show_rotation            = show_rotation;
    v.show_fullscreen_indicator = show_fullscreen_indicator;
    /* -F flag overrides config, otherwise use config default */
    if (opt_fs || startfullscreen)
        win_toggle_fullscreen(&v);

    render_page(&v);
    win_draw(&v);

    /* Event loop */
    XEvent ev;
    for (;;) {
        XNextEvent(v.dpy, &ev);
        switch (ev.type) {
            case Expose:
                if (ev.xexpose.count == 0) win_draw(&v);
                break;
            case KeyPress:
                handle_key(&v, &ev.xkey);
                break;
            case ButtonPress:
                handle_button(&v, &ev.xbutton);
                break;
            case ConfigureNotify: {
                XConfigureEvent *ce = &ev.xconfigure;
                if (ce->width != v.win_w || ce->height != v.win_h) {
                    v.win_w = ce->width;
                    v.win_h = ce->height;
                    render_page(&v);
                    win_draw(&v);
                }
                break;
            }
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == v.wm_delete)
                    goto quit;
                break;
        }
    }
quit:
    if (v.pix) fz_drop_pixmap(v.ctx, v.pix);
    if (v.doc) fz_drop_document(v.ctx, v.doc);
    if (v.ctx) fz_drop_context(v.ctx);
    XCloseDisplay(v.dpy);
    return 0;
}
