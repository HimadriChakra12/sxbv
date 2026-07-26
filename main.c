#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sxbv.h"

/* ------------------------------------------------------------------ */
static const char *expand_path(const char *path)
{
    static char buf[4096];

    /* Fast path: nothing to expand */
    if (path[0] != '~' && !strchr(path, '$'))
        return path;

    /* ~ expansion */
    const char *src = path;
    if (src[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(buf, sizeof buf, "%s%s", home, src + 1);
            src = buf;
        }
    }

    /* $VAR / ${VAR} expansion: only if a $ is present */
    if (!strchr(src, '$'))
        return src;

    static char out[4096];
    char *d = out;
    char *end = out + sizeof(out) - 1;
    const char *s = src;
    while (*s && d < end) {
        if (*s != '$') { *d++ = *s++; continue; }
        s++; /* skip $ */
        /* ${VAR} or $VAR */
        int braced = (*s == '{');
        if (braced) s++;
        char var[256]; int vl = 0;
        while (*s && vl < (int)sizeof(var)-1 &&
               ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') ||
                (*s >= '0' && *s <= '9') || *s == '_')) {
            var[vl++] = *s++;
        }
        var[vl] = '\0';
        if (braced && *s == '}') s++;
        const char *val = vl ? getenv(var) : NULL;
        if (val) {
            size_t vlen = strlen(val);
            if (d + vlen > end) vlen = (size_t)(end - d);
            memcpy(d, val, vlen);
            d += vlen;
        }
    }
    *d = '\0';
    return out;
}
/* Helpers                                                              */
void clamp_scroll(Viewer *v)
{
    if (v->continuous) {
        /* In continuous mode, doc_scroll is the global canvas offset.
         * Minimum: 0 (top of doc). Maximum: total height - viewport height. */
        if (v->doc_scroll < 0) v->doc_scroll = 0;
        /* Estimate total document height */
        int total_h = 0;
        for (int pg = 0; pg < v->page_count; pg++) {
            PdfRect b = pdf_page_bounds(v->doc, pg);
            float ph = b.y1 - b.y0;
            total_h += (int)(ph * v->zoom) + PAGE_GAP;
        }
        int page_area = v->win_h - (v->bar_visible ? v->bar_h : 0);
        int max_scroll = total_h - page_area;
        if (max_scroll < 0) max_scroll = 0;
        if (v->doc_scroll > max_scroll) v->doc_scroll = max_scroll;

        /* Horizontal center — use current page width */
        if (v->pix_w <= v->win_w)
            v->scroll_x = (v->win_w - v->pix_w) / 2;
        else {
            if (v->scroll_x > 0) v->scroll_x = 0;
            if (v->scroll_x < v->win_w - v->pix_w)
                v->scroll_x = v->win_w - v->pix_w;
        }
        return;
    }

    /* Normal single-page mode */
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

    if (v->continuous) {
        /* Compute doc_scroll offset so the target page is at the viewport top */
        int y = 0;
        for (int pg = 0; pg < p; pg++) {
            PdfRect b = pdf_page_bounds(v->doc, pg);
            float ph = b.y1 - b.y0;
            y += (int)(ph * v->zoom) + PAGE_GAP;
        }
        v->doc_scroll = y;
        v->page = p;
        search_free(v);
        render_page(v);
        return;
    }

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
        if (keybinds[i].ks == ks && keybinds[i].mod == mod) {
            return keybinds[i].cmd;
        }
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

    /* ---- thumb mode ---- */
    if (v->mode == MODE_THUMB) {
        switch (cmd) {
            /* h -- prev file */
            case CMD_PREV_PAGE:
                if (v->thumb_sel > 0) v->thumb_sel--;
                thumb_scroll_to_sel(v); thumb_draw(v);
                return;
            /* l -- next file */
            case CMD_NEXT_PAGE:
                if (v->thumb_sel < v->file_count - 1) v->thumb_sel++;
                thumb_scroll_to_sel(v); thumb_draw(v);
                return;
            case CMD_TOGGLE_THUMB:
                /* t in thumb mode = back to open doc if any */
                if (v->doc) {
                    thumb_free(v);
                    v->mode = MODE_NORMAL;
                    render_page(v);
                    win_draw(v);
                }
                return;
            case CMD_THUMB_OPEN: {
                if (v->file_count == 0) return;
                ThumbEntry *e = &v->files[v->thumb_sel];
                /* strdup path BEFORE thumb_free destroys it */
                char *newpath = strdup(e->path);
                if (v->doc) { pdf_close(v->doc); v->doc = NULL; }
                if (v->pix) { pdf_pix_free(v->pix); v->pix = NULL; }
                v->doc = pdf_open(newpath);
                if (!v->doc) { free(newpath); return; }
                /* free previous owned filename if any */
                if (v->filename_owned) free((char*)v->filename);
                v->filename       = newpath;
                v->filename_owned = 1;
                v->page_count = pdf_page_count(v->doc);
                v->page       = 0;
                v->annot_mode = ANNOT_NONE;
                annot_init(v);
                thumb_free(v);
                v->mode = MODE_NORMAL;
                v->bar_visible = showbar_normal;
                render_page(v);
                win_draw(v);
                return;
            }
            /* j/Down */
            case CMD_SCROLL_DOWN:
                v->thumb_sel += v->thumb_cols;
                if (v->thumb_sel >= v->file_count)
                    v->thumb_sel = v->file_count - 1;
                thumb_scroll_to_sel(v); thumb_draw(v);
                return;
            /* k/Up */
            case CMD_SCROLL_UP:
                v->thumb_sel -= v->thumb_cols;
                if (v->thumb_sel < 0) v->thumb_sel = 0;
                thumb_scroll_to_sel(v); thumb_draw(v);
                return;
            /* h/Left */
            case CMD_SCROLL_LEFT:
                if (v->thumb_sel > 0) v->thumb_sel--;
                thumb_scroll_to_sel(v); thumb_draw(v);
                return;
            /* l/Right */
            case CMD_SCROLL_RIGHT:
                if (v->thumb_sel < v->file_count - 1) v->thumb_sel++;
                thumb_scroll_to_sel(v); thumb_draw(v);
                return;
            /* g -- go to first or Ng */
            case CMD_FIRST_PAGE:
                v->thumb_sel = cnt > 1 ? cnt - 1 : 0;
                if (v->thumb_sel >= v->file_count)
                    v->thumb_sel = v->file_count - 1;
                v->thumb_scroll = 0;
                thumb_scroll_to_sel(v); thumb_draw(v);
                return;
            /* G -- go to last or Ng */
            case CMD_LAST_PAGE:
                v->thumb_sel = cnt > 1 ? cnt - 1 : v->file_count - 1;
                if (v->thumb_sel >= v->file_count)
                    v->thumb_sel = v->file_count - 1;
                if (v->thumb_sel < 0) v->thumb_sel = 0;
                thumb_scroll_to_sel(v); thumb_draw(v);
                return;
            case CMD_TOGGLE_BAR:
                v->bar_visible = !v->bar_visible;
                thumb_draw(v);
                return;
            case CMD_TOGGLE_FULLPATH:
                v->show_fullpath = !v->show_fullpath;
                thumb_draw(v);
                return;
            case CMD_QUIT:
                exit(0);
            default:
                return;
        }
    }

    /* ---- normal mode ---- */
    switch (cmd) {
        case CMD_THUMB_OPEN: {
            /* Enter in normal mode = go to thumb browser */
            char dir[4096];
            snprintf(dir, sizeof dir, "%s", v->filename);
            char *slash = strrchr(dir, '/');
            if (slash) *slash = '\0';
            else snprintf(dir, sizeof dir, ".");
            v->mode = MODE_THUMB;
            v->bar_visible = showbar_thumb;
            thumb_init_dir(v, dir);
            thumb_draw(v);
            return;
        }
        case CMD_TOGGLE_THUMB:
            /* t in normal mode = same as Enter */
            {
                char dir[4096];
                snprintf(dir, sizeof dir, "%s", v->filename);
                char *slash = strrchr(dir, '/');
                if (slash) *slash = '\0';
                else snprintf(dir, sizeof dir, ".");
                v->mode = MODE_THUMB;
                v->bar_visible = showbar_thumb;
                thumb_init_dir(v, dir);
                thumb_free(v);
                v->mode = MODE_NORMAL;
                v->bar_visible = showbar_normal;
                render_page(v);
                win_draw(v);
            }
            return;
        case CMD_TOGGLE_FULLPATH:
            v->show_fullpath = !v->show_fullpath;
            win_draw(v); break;
        case CMD_TOGGLE_BAR:
            v->bar_visible = !v->bar_visible;
            if (v->mode == MODE_THUMB)
                thumb_draw(v);
            else {
                render_page(v);
                win_draw(v);
            }
            break;
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
            if (v->continuous) {
                v->doc_scroll += sl * cnt;
                clamp_scroll(v);
            } else if (v->pix_h > v->win_h) {
                v->scroll_y -= sl * cnt; clamp_scroll(v);
            } else {
                go_page(v, v->page + cnt);
            }
            break;
        case CMD_SCROLL_UP:
            if (v->continuous) {
                v->doc_scroll -= sl * cnt;
                clamp_scroll(v);
            } else if (v->pix_h > v->win_h) {
                v->scroll_y += sl * cnt; clamp_scroll(v);
            } else {
                go_page(v, v->page - cnt);
            }
            break;
        case CMD_SCROLL_LEFT:
            v->scroll_x += sl*cnt; clamp_scroll(v); break;
        case CMD_SCROLL_RIGHT:
            v->scroll_x -= sl*cnt; clamp_scroll(v); break;
        case CMD_SCREEN_DOWN:
            if (v->continuous) {
                v->doc_scroll += sp; clamp_scroll(v);
            } else if (v->pix_h > v->win_h) {
                int prev = v->scroll_y;
                v->scroll_y -= sp; clamp_scroll(v);
                if (v->scroll_y == prev)
                    go_page(v, v->page + 1);
            } else {
                go_page(v, v->page + cnt);
            }
            break;
        case CMD_SCREEN_UP:
            if (v->continuous) {
                v->doc_scroll -= sp; clamp_scroll(v);
            } else if (v->pix_h > v->win_h) {
                int prev = v->scroll_y;
                v->scroll_y += sp; clamp_scroll(v);
                if (v->scroll_y == prev)
                    go_page(v, v->page - 1);
            } else {
                go_page(v, v->page - cnt);
            }
            break;
        case CMD_NEXT_PAGE:
            go_page(v, v->page + cnt); break;
        case CMD_PREV_PAGE:
            go_page(v, v->page - cnt); break;
        case CMD_FIRST_PAGE: go_page(v, cnt > 1 ? cnt - 1 : 0); break;
        case CMD_LAST_PAGE:  go_page(v, cnt > 1 ? cnt - 1 : v->page_count - 1); break;
        case CMD_ZOOM_IN:    zoom_by(v,  ZOOM_STEP * cnt); break;
        case CMD_ZOOM_OUT:   zoom_by(v, -ZOOM_STEP * cnt); break;
        case CMD_ZOOM_RESET: v->fit = FIT_NONE; v->zoom = DEFAULT_ZOOM; render_page(v); break;
        case CMD_FIT_WIDTH:  v->fit = FIT_WIDTH;  render_page(v); break;
        case CMD_FIT_HEIGHT: v->fit = FIT_HEIGHT; render_page(v); break;
        case CMD_FIT_PAGE:   v->fit = FIT_PAGE;   render_page(v); break;
        case CMD_ROTATE_CW:  v->rotation = (v->rotation + 90)  % 360; render_page(v); break;
        case CMD_ROTATE_CCW: v->rotation = (v->rotation + 270) % 360; render_page(v); break;
        case CMD_FULLSCREEN: win_toggle_fullscreen(v); break;
        case CMD_SEARCH_START:
            v->search_mode = 1; v->search_dir = 1; v->search_buf[0] = '\0'; break;
        case CMD_SEARCH_NEXT:
            if (v->hit_count > 0) v->hit = (v->hit + 1) % v->hit_count;
            else search_do(v, 1);
            break;
        case CMD_SEARCH_PREV:
            if (v->hit_count > 0) v->hit = (v->hit - 1 + v->hit_count) % v->hit_count;
            else search_do(v, -1);
            break;
        case CMD_TOGGLE_HIGHLIGHT:  annot_toggle(v, ANNOT_HIGHLIGHT); break;
        case CMD_TOGGLE_PENCIL:     annot_toggle(v, ANNOT_PENCIL);    break;
        case CMD_TOGGLE_TEXT_BG:
            annot_sel_clear(v);
            v->annot_mode     = ANNOT_NONE;
            v->text_mode_armed = 1;
            v->text_mode_bg    = 1;
            if (!v->bar_visible) { v->bar_visible = 1; v->bar_forced = 1; }
            break;
        case CMD_TOGGLE_TEXT_NOBG:
            annot_sel_clear(v);
            v->annot_mode     = ANNOT_NONE;
            v->text_mode_armed = 1;
            v->text_mode_bg    = 0;
            if (!v->bar_visible) { v->bar_visible = 1; v->bar_forced = 1; }
            break;
        case CMD_ANNOT_COLOR_PREV:  annot_color_cycle(v, -1); break;
        case CMD_ANNOT_COLOR_NEXT:  annot_color_cycle(v,  1); break;
        case CMD_ANNOT_THICK_DEC:   annot_thickness_adjust(v, -ANNOT_THICK_STEP * cnt); break;
        case CMD_ANNOT_THICK_INC:   annot_thickness_adjust(v,  ANNOT_THICK_STEP * cnt); break;
        case CMD_ANNOT_UNDO:        annot_undo(v); break;
        case CMD_ANNOT_REDO:        annot_redo(v); break;
        case CMD_ANNOT_SAVE:        annot_save(v); return;
        case CMD_TOGGLE_CONTINUOUS:
            v->continuous = !v->continuous;
            if (v->continuous) v->doc_scroll = 0;
            render_page(v);
            break;
        case CMD_QUIT:
            exit(0);
        default:
            return;
    }
    win_draw(v);
}

static void handle_key(Viewer *v, XKeyEvent *ke)
{
    char   buf[32];
    int    len = XLookupString(ke, buf, 31, NULL, NULL);
    buf[len]   = '\0';
    KeySym ks  = XLookupKeysym(ke, 0);

    /* Text note input mode: eat all keys */
    if (v->text_input_mode) {
        annot_text_key(v, ks, buf, len, ke->state);
        win_draw(v);
        return;
    }

    if (v->search_mode) {
        handle_search_key(v, ks, buf, len);
        return;
    }

    /* Escape: cancel text mode / deselect / exit annotation mode */
    if (ks == XK_Escape) {
        if (v->text_input_mode) {
            annot_text_cancel(v);
            win_draw(v);
            return;
        }
        if (v->text_mode_armed) {
            v->text_mode_armed = 0;
            win_draw(v);
            return;
        }
        if (annot_has_selection(v)) {
            annot_sel_clear(v);
            win_draw(v);
            return;
        }
        if (annot_active(v)) {
            v->annot_mode    = ANNOT_NONE;
            v->annot_drawing = 0;
            if (v->bar_forced) { v->bar_forced = 0; }
            win_draw(v);
            return;
        }
        /* fall through to normal CMD_QUIT */
    }

    /* While a drawing tool is active, number keys pick palette presets */
    if (annot_active(v) && v->annot_mode != ANNOT_TEXT &&
        !(ke->state & (ControlMask | ShiftMask))) {
        if (v->annot_mode == ANNOT_HIGHLIGHT && ks >= XK_1 && ks <= XK_5) {
            annot_select_preset(v, (int)(ks - XK_1));
            win_draw(v);
            return;
        }
        if (v->annot_mode == ANNOT_PENCIL &&
            ((ks >= XK_1 && ks <= XK_9) || ks == XK_0)) {
            int idx = (ks == XK_0) ? 9 : (int)(ks - XK_1);
            annot_select_preset(v, idx);
            win_draw(v);
            return;
        }
    }

    /* Accumulate numeric prefix (e.g. 5j = scroll 5 lines) */
    if (ks >= XK_0 && ks <= XK_9 && !(ke->state & ControlMask) && !annot_active(v)) {
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
        "  Ctrl+scroll       zoom\n"
        "\n"
        "Annotation tools:\n"
        "  Ctrl+h/p          toggle highlighter / pencil\n"
        "  [  ]              cycle palette color\n"
        "  1-5               highlighter preset (1-5)\n"
        "  1-0               pencil preset (1-9, 0=slot10)\n"
        "  <  >              decrease/increase thickness\n"
        "  u / Ctrl+r        undo / redo stroke\n"
        "  w                 save annotations to .annot file\n",
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
    v.filename       = expand_path(argv[i]);
    v.filename_owned = 0;

    /* Poppler one-time init */
    if (pdf_init() != 0) { fprintf(stderr, "sxbv: cannot init pdf backend\n"); return 1; }

    /* MUST come before any pdf_open call */
    struct stat st;
    int is_dir = (stat(v.filename, &st) == 0 && S_ISDIR(st.st_mode));

    if (!is_dir) {
        v.doc = pdf_open(v.filename);
        if (!v.doc) { fprintf(stderr, "sxbv: cannot open: %s\n", v.filename); return 1; }
        v.page_count = pdf_page_count(v.doc);
        if (v.page_count <= 0) { fprintf(stderr, "sxbv: no pages\n"); return 1; }
        v.page = (opt_page >= 0 && opt_page < v.page_count) ? opt_page : 0;
    }

    /* X11 init */
    v.show_filename             = show_filename;
    v.show_pagelabel            = show_pagelabel;
    v.show_zoom                 = show_zoom;
    v.show_fitmode              = show_fitmode;
    v.show_rotation             = show_rotation;
    v.show_fullscreen_indicator = show_fullscreen_indicator;
    v.show_fullpath             = show_fullpath;

    if (!win_init(&v)) return 1;   /* only once */
    annot_config_defaults(&v);
    if (!is_dir) annot_init(&v);
    v.bar_visible = (is_dir) ? showbar_thumb : showbar_normal;
    v.continuous  = start_continuous;

    if (is_dir) {
        v.mode = MODE_THUMB;
        thumb_init_dir(&v, v.filename);
        if (opt_fs || startfullscreen)
            win_toggle_fullscreen(&v);
        /* Pump X events until we get a ConfigureNotify so win_w/win_h
         * are set before the first thumb_draw. Without this, win_w=0
         * causes XCreatePixmap(w=0,h=0) → X protocol error → segfault. */
        XEvent ev_init;
        while (v.win_w == 0 || v.win_h == 0) {
            XNextEvent(v.dpy, &ev_init);
            if (ev_init.type == ConfigureNotify) {
                v.win_w = ev_init.xconfigure.width;
                v.win_h = ev_init.xconfigure.height;
                /* Recompute thumb cols now that we have real dimensions */
                v.thumb_cols = v.win_w / (v.thumb_w + THUMB_PADDING);
                if (v.thumb_cols < 1) v.thumb_cols = 1;
                thumb_scroll_to_sel(&v);
            }
        }
        thumb_draw(&v);
    } else {
        if (opt_fs || startfullscreen)
            win_toggle_fullscreen(&v);
        v.mode = MODE_NORMAL;
        render_page(&v);
        win_draw(&v);
    }

    /* Event loop */
    XEvent ev;
    for (;;) {
        if (v.mode == MODE_THUMB && v.thumb_next < v.file_count) {
            if (!XPending(v.dpy)) {
                thumb_render_next(&v);
                thumb_draw(&v);
                continue;
            }
        }
        XNextEvent(v.dpy, &ev);
        switch (ev.type) {
            case Expose:
                if (ev.xexpose.count == 0) {
                    if (v.mode == MODE_THUMB) thumb_draw(&v);
                    else win_draw(&v);
                }
                break;
            case KeyPress:
                handle_key(&v, &ev.xkey);
                break;
            case ButtonPress:
                if (v.mode == MODE_NORMAL) {
                    Command pending_text = CMD_NONE;
                    /* Check if a text-note tool is "armed" — we use a
                     * flag rather than a keybind dispatch because text
                     * placement needs the click position. */
                    if (v.text_mode_armed) {
                        annot_text_place(&v, ev.xbutton.x, ev.xbutton.y,
                                         v.text_mode_bg);
                        v.text_mode_armed = 0;
                        win_draw(&v);
                        break;
                    }
                    handle_button(&v, &ev.xbutton);
                    annot_button(&v, &ev.xbutton, 1);
                    win_draw(&v);
                } else {
                    handle_button(&v, &ev.xbutton);
                }
                break;
            case ButtonRelease:
                if (v.mode == MODE_NORMAL) {
                    annot_button(&v, &ev.xbutton, 0);
                    if (v.sel_dragging == 0 && annot_has_selection(&v))
                        win_draw(&v);
                }
                break;
            case MotionNotify: {
                XEvent latest = ev;
                while (XPending(v.dpy)) {
                    XEvent peek;
                    XPeekEvent(v.dpy, &peek);
                    if (peek.type != MotionNotify) break;
                    XNextEvent(v.dpy, &latest);
                }
                annot_motion(&v, &latest.xmotion);
                if (v.mode != MODE_THUMB &&
                    (annot_active(&v) || v.sel_dragging || v.have_pointer))
                    win_draw(&v);
                break;
            }
            case ConfigureNotify: {
                                      XConfigureEvent *ce = &ev.xconfigure;
                                      if (ce->width != v.win_w || ce->height != v.win_h) {
                                          v.win_w = ce->width;
                                          v.win_h = ce->height;
                                          if (v.mode == MODE_THUMB) thumb_draw(&v);
                                          else { render_page(&v); win_draw(&v); }
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
    annot_free_all(&v);
    if (v.filename_owned) free((char*)v.filename);
    if (v.pix) pdf_pix_free(v.pix);
    if (v.doc) pdf_close(v.doc);
    pdf_shutdown();
    XCloseDisplay(v.dpy);
    return 0;
}
