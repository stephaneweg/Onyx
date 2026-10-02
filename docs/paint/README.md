# Paint, made "pro" — the study and the mock-ups

Asked by the user (2026-10-02): Paint should look more professional, use FreeType, have blend modes per
layer composited by the GPU (a hidden layer = as if it did not exist), the selections and the brushes of a
real drawing program; and wtk's dialogs must take Tab from one field to the next.

The mock-ups (`python3 tools/screenshot/mockup_paint.py` → `mockups/paint-*.png`, on the real desktop):

| Mock-up | What it shows |
|---|---|
| `paint-main.png` | The whole window: the ribbon in FreeType's text, the options bar of the tool, the dark neutral desk, the layers' panel (blend, opacity), the status bar with its zoom slider. |
| `paint-brushes.png` | The Brushes gallery: Pencil, Brush, Soft round, Calligraphy, Airbrush, Marker, Crayon, and the pattern brushes (Dots, Lines, Checks, Bricks, Hatching, Grid). |
| `paint-select.png` | The Select menu (Rectangle, Free-form, Magic wand, Select All, Invert…) and a magic-wand selection with its marching ants; the wand's options (New / Add / Subtract, Tolerance, Contiguous, All layers). |
| `paint-layers.png` | A layer's blend mode chosen: Normal, Multiply, Screen, Add, Subtract, Lighten, Mask, Cut out. The picture shows them at work: a warm tint (Multiply 60 %), the sun's glow (Add), the vignette (a white rounded shape as a Mask: the picture shows only inside it). |
| `paint-text.png` | The Text tool: font, size, bold / italic / underline, alignment, smooth edges; the text in its box on the canvas, movable until it is put down. |
| `paint-gradient-fill.png` | The paint bucket in **gradient** mode (the user's ask): the press picks the zone (its colour, the tolerance, contiguous), the line dragged gives the gradient's direction and length; its stops can be dragged on the line; Enter applies, Esc cancels. The gradients' list (presets and yours); the shapes: linear, bi-linear, radial, square, conical; repeat (none, sawtooth, triangular); reverse. |
| `paint-gradient-editor.png` | The gradient editor, as GIMP's: stops (colour, opacity, position), midpoints, the presets; saved in GIMP's `.ggr` format (`SD:/apps/paint.app/gradients/`). The Gradient tool uses the same gradients over the selection or the whole layer. |
| `paint-resize.png` | The Resize dialog redone; Tab / Shift+Tab go from one field to the next (fixed in wtk for every app). |

## The look

- **Ribbon** (light tone of the theme's face): Clipboard (Paste, Cut, Copy, Undo, Redo) · Image (Select ▾,
  Crop, Resize, Rotate) · Tools (Pencil, Fill, Text, Eraser, Colour picker, Magnifier, Gradient, Adjust *fx*) ·
  Brushes ▾ (the current brush's stroke) · Shapes (the gallery) · Colours (1 and 2 as round swatches, the
  palette, your colours, Edit). Text in DejaVu Sans through wtk's FreeType face.
- **Options bar** under the ribbon: what the current tool takes — a brush: its kind, size, opacity, hardness,
  "inside the selection"; the wand: new / add / subtract, tolerance, contiguous, all layers; the text: font,
  size, B I U, alignment; the gradient: linear / radial / reflected, opacity; a shape: outline / fill, width.
- **Desk** dark and neutral around the picture (colours read true), a soft shadow under it, thin scroll bars.
- **Layers' panel**: the current layer's blend mode and opacity at the top; each row its eye, thumbnail,
  name, mode · opacity; a mask layer marked; New, Duplicate, Delete, Up, Down, Merge, Effects at the bottom.
- **Status bar**: the pointer, the selection's size, the picture's size, the current layer; grid, fit, the
  zoom slider (12 % – 3200 %); Ctrl + wheel zooms around the pointer.

## How (the plan)

- Paint becomes a newlib app (FreeType's text, the Text tool's fonts: `ft/fonts.h`).
- The canvas is composited by the GPU (`user/gpucomp`): a texture per layer (only the changed rectangles
  uploaded), zoomed with the nearest texel above 100 %, bilinear below; the hidden layers left out. The
  blend modes are gpucomp's (`GPC_B_*`), done by the V3D's blender from **kapi v72** (`KAPI_GPU_BLEND_*`
  presets 5–12; multiply and subtract in two passes so that they are right over transparency too), by the
  CPU before that — the same pixels: Paint flattens (Export, the picker) with gpucomp's own `gpc_blend_pixel`.
- OpenRaster keeps the modes (`composite-op`: `svg:multiply`, `svg:screen`, `svg:plus`, `svg:lighten`,
  `svg:dst-in`, `svg:dst-out`, `krita:subtract`), as GIMP and Krita read them.
- The selection becomes a mask (rectangle, lasso, wand, invert; Shift adds, Alt subtracts); the brushes a
  stroke engine (coverage per stroke: no build-up but the airbrush's; soft edges; patterns anchored to the
  picture); a gradient tool; the Text tool; Adjust: brightness / contrast, hue / saturation, invert, black and
  white, sepia, blur, sharpen.

## Where it landed (2026-10-02)

Implemented the same day (`user/Apps/paint/`, docs/04 *Paint*, docs/03 *Paint*), with what the user asked
for while it was being made:

- **Kernel kapi v72**: `gpu_render`'s compositing presets (docs/02 §8, §15); **gpucomp**: the layers' blend
  modes, GPU and CPU alike (`tools/tests/run_gpucomp_test.sh`: 54 checks, 28 of them the modes).
- **wtk**: Tab / Shift+Tab between a dialog's controls and between a window's text fields; the first field
  focused when a dialog opens; Enter reaches the dialog's OK from a field; `Textbox::changed`.
- **Paint**: the ribbon and options bar of the mock-ups, the canvas composited by gpucomp, blend modes and
  the **Mask / Cut out on the layer below only** (a layer mask: the user's fade between two pictures), the
  brushes, the selections (rectangle, lasso, magic wand), the **fill's gradient along a line** and the
  gradient editor (GIMP `.ggr`), the Text tool, **Open as Layer** and **Paste as New Layer**, the **Colours**
  menu (brightness / contrast, hue / saturation, desaturate, colorize, remap the channels, invert, sepia,
  posterize, threshold) and **Filters** (blur, sharpen, pixelate) on the selection, the layer or every layer.
- Samples: `SD:/docs/pictures/sunset-sea.jpg`, `sunny-mountains.jpg` (`tools/gen_paint_samples.py`; the
  package `paint-samples`). Screenshots: `paint.png`, `paint-fade.png`, `paint-brushes.png`, `paint-grid.png`.
- **To try on the Pi**: the GPU's blend modes (they need the v72 kernel — an older one: the same pictures by
  the CPU, slower), the canvas's speed on a big picture (each change sends only its rectangle), the brushes'
  feel (spacing, soft edges), the text's fonts.
- **Next ideas**: a selection's handles (resize, rotate freely), curves / levels, layer groups, more
  blend modes (overlay, colour dodge / burn: they need a shader — gpu_render2), a history panel.
