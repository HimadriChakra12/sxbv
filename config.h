/* sxbv config -- edit this file to configure sxbv */

/* Bar position: 0 = bottom, 1 = top */
static const int  topbar        = 0;

static const char bgcolor[]     = "#7daea3";
static const char fgcolor[]     = "#1d2021";
static const char markcolor[]   = "#fe8019";
static const char selcolor[]    = "#fbf1c7";

static const char statusfont[] =
    "JetBrains Mono Medium:pixelsize=15:antialias=true:autohint=true,"
    "monospace:pixelsize=15";

static const char pagebg[] = "#1d2021";
static const int startfullscreen = 1;

static const char hitcolor[]    = "#fabd2f"; /* yellow  */
static const char hitselcolor[] = "#fe8019"; /* orange  */

#define THUMB_WIDTH      180
#define THUMB_PADDING    50
#define THUMB_SELBORDER  2
#define THUMB_LABEL_H    20   /* height reserved below thumb for filename */

static const int showbar_thumb             = 1;
static const int showbar_normal            = 0;
static const int show_filename             = 1;
static const int show_fullpath             = 0;
static const int show_pagelabel            = 1;
static const int show_zoom                 = 0;
static const int show_fitmode              = 0;
static const int show_rotation             = 0;
static const int show_fullscreen_indicator = 0;

#define DEFAULT_FIT       FIT_PAGE
#define DEFAULT_ZOOM      1.0f
#define ZOOM_STEP         0.1f
#define ZOOM_MIN          0.05f
#define ZOOM_MAX          16.0f
#define SCROLL_LINE       50
#define SCROLL_PAGE_FRAC  0.9f
#define MAX_HITS          512
#define MAX_SEARCH        256
#define WIN_SCREEN_FRAC   0.667f

/* ---- Annotation tools: pencil (opaque) & highlighter (translucent) ---- */

/* Exactly 5 presets, selectable by pressing 1-5 while a tool is active
 * (also cycled with '[' / ']'). Accepts any X11 color name or
 * "#rrggbb" hex string. Keep this at 5 entries -- the 1-5 keys map
 * directly to slots 0-4. */
static const char *annot_palette[] = {
    "#ffff98",  /* 1: yellow (matches the ringed/default swatch in your screenshot) */
    "#53ffbc",  /* 2: green  */
    "#80ebff",  /* 3: cyan   */
    "#ffcbe6",  /* 4: pink   */
    "#ff4f5f",  /* 5: red    */
};
#define ANNOT_PALETTE_LEN ((int)(sizeof(annot_palette) / sizeof(annot_palette[0])))

/* Default preset (index into annot_palette above, 0-based) each tool
 * starts on. Highlighter starts on yellow (slot 0); pencil starts on
 * red (slot 4). Change these to start on a different preset. */
#define HIGHLIGHT_DEFAULT_PRESET 0
#define PENCIL_DEFAULT_PRESET    4

/* Thickness is stored normalised to page width, so strokes stay visually
 * consistent across zoom levels. These defaults are in pixels at the
 * zoom level active when a document is first opened. */
#define PENCIL_DEFAULT_THICKNESS     3.0f
#define HIGHLIGHT_DEFAULT_THICKNESS 18.0f
#define ANNOT_THICK_MIN    1.0f
#define ANNOT_THICK_MAX   80.0f
#define ANNOT_THICK_STEP   1.0f

/* Highlighter blend strength, 0 (no effect) - 255 (fully multiplied).
 * The highlighter uses a multiply blend (like a real marker): it
 * saturates the light page background while leaving dark text mostly
 * untouched, instead of washing everything toward a flat color like a
 * plain alpha-over blend would (that's what read as "watercolor").
 * This is left at full strength (255) on purpose -- a real highlighter
 * band is essentially solid color over paper, not a faded tint; the
 * multiply blend itself (not this value) is what keeps text readable
 * underneath, so turning this down just makes the *background* paler
 * without helping text legibility. Lower it only if you want a
 * deliberately faint/translucent look. */
#define HIGHLIGHT_ALPHA 255

/* Outline color of the thickness-preview ring around the cursor. */
#define ANNOT_CURSOR_RING_COLOR "#ffffff"

#define KEYBINDINGS \
    BIND(XK_j,         0,           CMD_SCROLL_DOWN)         \
    BIND(XK_Down,      0,           CMD_SCROLL_DOWN)         \
    BIND(XK_k,         0,           CMD_SCROLL_UP)           \
    BIND(XK_Up,        0,           CMD_SCROLL_UP)           \
    BIND(XK_l,         0,           CMD_NEXT_PAGE)           \
    BIND(XK_Page_Down, 0,           CMD_NEXT_PAGE)           \
    BIND(XK_h,         0,           CMD_PREV_PAGE)           \
    BIND(XK_Page_Up,   0,           CMD_PREV_PAGE)           \
    BIND(XK_space,     0,           CMD_SCREEN_DOWN)         \
    BIND(XK_BackSpace, 0,           CMD_SCREEN_UP)           \
    BIND(XK_Left,      0,           CMD_SCROLL_LEFT)         \
    BIND(XK_Right,     0,           CMD_SCROLL_RIGHT)        \
    BIND(XK_g,         0,           CMD_FIRST_PAGE)          \
    BIND(XK_Home,      0,           CMD_FIRST_PAGE)          \
    BIND(XK_g,         ShiftMask,   CMD_LAST_PAGE)           \
    BIND(XK_End,       0,           CMD_LAST_PAGE)           \
    BIND(XK_plus,      0,           CMD_ZOOM_IN)             \
    BIND(XK_equal,     0,           CMD_ZOOM_IN)             \
    BIND(XK_minus,     0,           CMD_ZOOM_OUT)            \
    BIND(XK_0,         0,           CMD_ZOOM_RESET)          \
    BIND(XK_w,         0,           CMD_FIT_WIDTH)           \
    BIND(XK_e,         0,           CMD_FIT_HEIGHT)          \
    BIND(XK_f,         0,           CMD_FIT_PAGE)            \
    BIND(XK_r,         0,           CMD_ROTATE_CW)           \
    BIND(XK_r,         ShiftMask,   CMD_ROTATE_CCW)          \
    BIND(XK_f,         ShiftMask,   CMD_FULLSCREEN)          \
    BIND(XK_slash,     0,           CMD_SEARCH_START)        \
    BIND(XK_n,         0,           CMD_SEARCH_NEXT)         \
    BIND(XK_n,         ShiftMask,   CMD_SEARCH_PREV)         \
    BIND(XK_t,         0,           CMD_TOGGLE_THUMB)        \
    BIND(XK_Return,    0,           CMD_THUMB_OPEN)          \
    BIND(XK_b,         0,           CMD_TOGGLE_BAR)          \
    BIND(XK_b,         ShiftMask,   CMD_TOGGLE_FILENAME)     \
    BIND(XK_z,         ShiftMask,   CMD_TOGGLE_ZOOM)         \
    BIND(XK_t,         ShiftMask,   CMD_TOGGLE_FITMODE)      \
    BIND(XK_d,         ShiftMask,   CMD_TOGGLE_ROTATION_IND) \
    BIND(XK_p,         ShiftMask,   CMD_TOGGLE_FULLPATH)     \
    BIND(XK_q,         0,           CMD_QUIT)                \
    BIND(XK_Escape,    0,           CMD_QUIT)                \
    /* -- annotation tools -- */                             \
    BIND(XK_h,         ControlMask, CMD_TOGGLE_HIGHLIGHT)    \
    BIND(XK_p,         ControlMask, CMD_TOGGLE_PENCIL)       \
    BIND(XK_bracketleft,  0,        CMD_ANNOT_COLOR_PREV)    \
    BIND(XK_bracketright, 0,        CMD_ANNOT_COLOR_NEXT)    \
    BIND(XK_comma,     ShiftMask,   CMD_ANNOT_THICK_DEC)     \
    BIND(XK_period,    ShiftMask,   CMD_ANNOT_THICK_INC)     \
    BIND(XK_c,         ShiftMask,   CMD_ANNOT_COLOR_INPUT)   \
    BIND(XK_u,         ControlMask, CMD_ANNOT_UNDO)
