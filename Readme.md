# boi — a fully intact book viewer

## Packaging note
`include/poppler-src/` was stripped out of this zip to keep it small.
Get `poppler-src.zip` too, and unpack it as:

    sxbv/include/poppler-src/

before building, so the tree looks like the original layout.

## Building

    make SHELL=/bin/bash

(SHELL=/bin/bash is needed because the Makefile relies on a couple of
bash-isms that `dash`/`sh` chokes on in a plain POSIX `make` invocation
on Debian/Ubuntu-family systems. This will get cleaned up in the
refactor pass.)

Build/runtime deps (Debian/Ubuntu package names): `clang`, `libx11-dev`,
`libxft-dev`, `libfreetype6-dev`, `libfontconfig1-dev`, `libopenjp2-7-dev`,
`libpng-dev`, `libtiff-dev`, `liblcms2-dev`, `libjpeg-dev`, `zlib1g-dev`.

## Annotation tools (pencil & highlighter)

| Key             | Action                                    |
|-----------------|--------------------------------------------|
| `Ctrl+p`        | toggle pencil mode                        |
| `Ctrl+h`        | toggle highlighter mode                   |
| `1`-`5`         | jump directly to preset color 1-5         |
| `[` / `]`       | previous / next palette color             |
| `<` / `>`       | decrease / increase thickness             |
| `Shift+C`       | type a custom color (name or `#rrggbb`)   |
| `Ctrl+u`        | undo last stroke segment (repeat for more)|
| left-drag       | draw, while a tool is active              |

- The palette is exactly 5 presets (`annot_palette[]` in `config.h`):
  yellow, green, cyan, pink, red. `1`-`5` pick one directly; `[`/`]`
  still cycle through them. Each tool (pencil/highlighter) tracks its
  own current preset independently.
- Default preset per tool is config-driven: `HIGHLIGHT_DEFAULT_PRESET`
  (0 = yellow) and `PENCIL_DEFAULT_PRESET` (4 = red), both 0-based
  indices into `annot_palette[]`.
- `1`-`5` only act as color picks while a tool is active; otherwise
  they still work as the existing vim-style numeric prefix (`5j` etc).
- Highlighter opacity is `HIGHLIGHT_ALPHA` in `config.h` (0-255).
  Raised to 190 from the original 90 so it reads as a bold marker
  instead of a faint watercolor wash — lower it if you want a lighter
  look.
- The status bar auto-shows when you enter either mode (if it was
  hidden) and displays the active tool + thickness on the right, and
  the color-input prompt on the left (mirrors the existing search
  prompt UI).
- A ring showing the exact stroke thickness follows your cursor while
  a tool is active.
- Strokes are stored per-page, normalized to the page's pixel
  dimensions at draw time, so they rescale correctly when you zoom.
  **Known limitation:** rotating the page after drawing will
  misplace existing strokes — this needs coordinate re-mapping across
  rotation changes, planned as a follow-up rather than bundled into
  this pass.
- The highlighter blends with real alpha transparency directly into
  the rendered page bitmap (not an X overlay), so it looks like an
  actual highlighter over the text.

## Performance fix

The first cut of the annotation tools had a real bug: every single
mouse-move re-ran the full pixel-format conversion of the page
(`to_bgrx`, O(width×height)) *and* re-rasterized every stroke segment
on the page from scratch — even just to move the thickness-preview
cursor ring. Motion events can arrive faster than that full pipeline
can run, so a backlog built up during any real drag and drained late,
which is exactly the "delayed" feeling — worse at higher zoom (more
pixels per conversion) and worse over a longer stroke (more segments
replayed every frame).

Fixed with two changes:
1. **Persistent composited buffer.** The page is converted to BGRX and
   has all its strokes blended in once, cached in `v->annot_bgrx`. It's
   only fully rebuilt when the *base* image changes (page/zoom/rotate)
   or a stroke is undone (can't un-blend a pixel). While actively
   drawing, each new segment is blended incrementally — just that
   segment's bounding box, not the whole page or whole stroke history.
2. **Motion-event coalescing.** The event loop drains any backlog of
   queued `MotionNotify` events and acts only on the latest one, so a
   fast drag can't outrun the redraw loop. Segments still connect from
   the last *processed* point to the new one, so strokes don't gap —
   very fast movement just skips drawing every micro-step.

Measured with a synthetic 300-event motion flood at high zoom in this
project's own test environment: the old code did 307 full redraws and
kept draining the backlog for ~0.62s after input had already stopped.
The fixed code did 9 redraws for the same input and finished with the
input, no trailing lag.

All of the above (drawing, color cycling, thickness, hex/named color
input, undo, and the performance fix) was smoke-tested end-to-end under
Xvfb with simulated input and verified pixel-exact / timing-measured —
not just "it builds."

## Highlighter color fix + 5-preset palette

The highlighter used a flat alpha-over blend, which blends *everything*
underneath toward the highlight color — including dark text — so text
under a stroke visibly faded ("watercolor" look). Real highlighters
don't do that: they saturate the light page background while leaving
dark text alone. Switched the highlighter (only the highlighter —
pencil is unaffected) to a multiply blend: `result = base × color / 255`.

First pass left `HIGHLIGHT_ALPHA` at 190 (a holdover from the old
"opacity" knob), which faded the blend 25% back toward the original
background — visibly paler than a real marker. Measured directly
against the reference screenshot: the reference's highlight band
samples at `(255,255,153)` over white, which is just the raw palette
color, i.e. *no* fade at all. `HIGHLIGHT_ALPHA` is now `255`
(full-strength) to match. Text protection comes from the multiply
blend itself, not from this value, so turning it down only makes the
background paler — it doesn't help text legibility, which is why it's
now documented as "leave at 255 unless you want a deliberately faint
look" rather than treated as a general opacity dial.

Verified pixel-exact after the change: over white background the
result is `(255,255,152)` = `#FFFF98`, matching the reference's
measured `(255,255,153)` almost exactly (1-unit rounding). Directly
over black text the pixel comes back pure `(0,0,0)`, completely
untouched.

The 5 presets and the `1`-`5` direct-select keys use the exact colors
from your reference screenshot (yellow `#ffff98`, green `#53ffbc`, cyan
`#80ebff`, pink `#ffcbe6`, red `#ff4f5f`), with yellow as the default —
matching the ringed/selected swatch in that screenshot. `1`-`5` only
intercept as presets while a tool is active; otherwise they still work
as the normal vim-style repeat-count prefix (`5j` etc.), so nothing
existing broke.

## Roadmap (by design, done incrementally — see project discussion)

- [x] Phase 1 — annotation core
- [x] Phase 2a — performance fix (this drop)
- [ ] Phase 2b — codebase refactor / de-bloat pass
- [ ] Phase 3 — Wayland backend
- [ ] Phase 4 — further poppler stripping
- [ ] Phase 5 — final packaging pass

This drop only touches the X11 path; Wayland, the refactor, and further
poppler trimming are intentionally deferred to their own passes.
