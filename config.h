/* sxbv config -- edit this file to configure sxbv */

/* Bar position: 0 = bottom, 1 = top */
static const int  topbar    = 0;

/* Colors (any valid X11 color string: "#rrggbb", "rgb:rr/gg/bb", named) */
static const char bgcolor[]   = "#7daea3"; /* this becomes the bar color */
static const char fgcolor[]   = "#1d2021"; /* this becomes the text color */
static const char markcolor[] = "#fe8019"; /* bright orange      */
static const char selcolor[]  = "#fbf1c7"; /* light0             */

/* Xft font string -- fontconfig format, comma-separated fallbacks */
static const char statusfont[] =
    "JetBrains Mono Medium:pixelsize=15:antialias=true:autohint=true,"
    "monospace:pixelsize=15";

/* Page background color */
static const char pagebg[] = "#1d2021";
/* Start in fullscreen: 1 = yes, 0 = no */
static const int startfullscreen = 1;

/* Search highlight colors */
static const char hitcolor[]    = "#fabd2f"; /* yellow  */
static const char hitselcolor[] = "#fe8019"; /* orange  */

/* File browser thumbnail config */
#define THUMB_WIDTH      180
#define THUMB_PADDING    50
#define THUMB_SELBORDER  2
#define THUMB_LABEL_H    20   /* height reserved below thumb for filename */


/* Toggle thumbnail mode */
/* Enter in thumb mode opens page, t toggles thumb mode */

static const int showbar                   = 0;
static const int show_filename             = 1;
static const int show_fullpath             = 0;
static const int show_pagelabel            = 1;
static const int show_zoom                 = 0;
static const int show_fitmode              = 1;
static const int show_rotation             = 1;
static const int show_fullscreen_indicator = 1;

/* Default zoom fit mode: FIT_PAGE, FIT_WIDTH, FIT_HEIGHT, FIT_NONE */
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

/* Keybindings */
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
    BIND(XK_G,         ShiftMask,   CMD_LAST_PAGE)           \
    BIND(XK_End,       0,           CMD_LAST_PAGE)           \
    BIND(XK_plus,      0,           CMD_ZOOM_IN)             \
    BIND(XK_equal,     0,           CMD_ZOOM_IN)             \
    BIND(XK_minus,     0,           CMD_ZOOM_OUT)            \
    BIND(XK_0,         0,           CMD_ZOOM_RESET)          \
    BIND(XK_w,         0,           CMD_FIT_WIDTH)           \
    BIND(XK_e,         0,           CMD_FIT_HEIGHT)          \
    BIND(XK_f,         0,           CMD_FIT_PAGE)            \
    BIND(XK_r,         0,           CMD_ROTATE_CW)           \
    BIND(XK_R,         ShiftMask,   CMD_ROTATE_CCW)          \
    BIND(XK_F,         ShiftMask,   CMD_FULLSCREEN)          \
    BIND(XK_slash,     0,           CMD_SEARCH_START)        \
    BIND(XK_n,         0,           CMD_SEARCH_NEXT)         \
    BIND(XK_N,         ShiftMask,   CMD_SEARCH_PREV)         \
    BIND(XK_t,         0,           CMD_TOGGLE_THUMB)        \
    BIND(XK_Return,    0,           CMD_THUMB_OPEN)          \
    BIND(XK_b,         0,           CMD_TOGGLE_BAR)          \
    BIND(XK_B,         ShiftMask,   CMD_TOGGLE_FILENAME)     \
    BIND(XK_Z,         ShiftMask,   CMD_TOGGLE_ZOOM)         \
    BIND(XK_T,         ShiftMask,   CMD_TOGGLE_FITMODE)      \
    BIND(XK_D,         ShiftMask,   CMD_TOGGLE_ROTATION_IND) \
    BIND(XK_p,         ShiftMask,   CMD_TOGGLE_FULLPATH)     \
    BIND(XK_q,         0,           CMD_QUIT)                \
    BIND(XK_Escape,    0,           CMD_QUIT)
