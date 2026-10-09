# AutoDev — the user's queue (2026-10-06)

The Product Manager takes the **first item not yet done** (check STATE.md's history) instead of
choosing freely; it still writes `01-product-manager.md` (why, the scope for one round, what
existing code to build on). When the queue is empty, it picks freely as PIPELINE.md §1 says.
The details of each item are in `IDEAS.md` (Applications table).

1. **Circuits** — a logic-circuit game in the line of Turtle Quest (`user/Apps/turtle`): missions
   with an objective (a truth table / a timing chart to obtain), allowed parts, a target gate count
   → 1-3 stars, automatic checking, step-by-step simulation, parts unlocked level after level, your
   own circuits reused as chips, a level editor, saved progress, English + French (CLAUDE.md).
   **The user's priority.**
2. **Turtle Quest: more missions** — new worlds and mechanics (keys and doors, ordered pick-ups,
   teleporters, several turtles, Logo drawings to reproduce, shortest-program challenges,
   variables / functions / recursion: spirals, fractals, a daily challenge). Reuse what Circuits
   added for progression if it fits.
3. **Pinball** — a physics pinball (flippers, bumpers, ramps, targets, multiball), several tables
   described as data, scores, gamepad + keyboard.
4. **Lemmings-like** — creatures guided to the exit by giving them roles (dig, block, build, climb,
   float, explode), pixel-destructible terrain, levels with objectives (n saved out of m), a level
   editor; our own name and graphics (Lemmings is a trademark).


## Series 2 (2026-10-07, rounds 6-10)

Items 1-4 above are done (rounds 2-5). Round 6 (in progress when this was written) picked the Clock freely.
**The user's new queue (2026-10-07)** -- the Product Manager takes the first item not done; when none is left,
free pick (PIPELINE.md §1), never an app an earlier round built:

5. **A vector drawing program worthy of Inkscape / Adobe Illustrator** (the user: "en haut de la liste").
   **ONE round, as long as it needs** (the user, 2026-10-07: no time limit): items 5 and 6 together -- the
   foundation below AND item 6's features; split into many Developer subagents, not into two rounds.
   A document of shapes (rectangle, ellipse, polygon, star, line, **Bézier paths drawn with a pen tool**),
   selection / move / scale / rotate with handles, **node editing** (move nodes and handles, add / delete,
   corner / smooth), fill and stroke (colour, width, dashes, caps, joins, opacity), layers, z-order, groups,
   zoom / pan, snapping to a grid, undo / redo, **SVG as the native format** (read and write the subset it
   draws, so Inkscape opens the files and the other way round), PNG export, the clipboard. English + French.
   **Kits first**: the geometry (Béziers, flattening, hit tests, bounds, transforms), the SVG reading / writing
   and the vector renderer (anti-aliased, UIKit's VPath to build on) belong in a kit that the technical-drawing
   app (item 7) and others reuse -- a new kit (e.g. VectorKit) or DocumentKit's start (see IDEAS.md), the
   Technical Analyst decides. Existing code to look at: UIKit's VPath, 3DForge's 2D drawings
   (`user/Apps/3dforge/fdraw.h`: SVG / DXF / PDF writing), `user/Libs/pdf` (PDF writing), Paint (layers, its UI).
6. **(Same round as item 5)** the vector drawing program's advanced features: gradients (linear, radial) and their on-canvas editing, text
   (FontKit faces, text on the canvas, converted to paths), **boolean operations** (union, difference,
   intersection, exclusion), align and distribute, guides and smart snapping (to nodes, centres, edges), the
   layers panel and an objects panel, clones / duplicates, PDF export (`user/Libs/pdf`), import of a raster
   image, printing (PrinterKit), more SVG read (what Inkscape writes commonly: `transform`, `<use>`, styles).
7. **A technical drawing program (2D CAD)** -- plans of objects, parts, rooms: lines, polylines, arcs, circles
   by precise values (typed coordinates and lengths, relative / polar), **snapping to endpoints, midpoints,
   centres, intersections, perpendicular, tangent**, orthogonal mode, layers with line types (continuous,
   hidden, centre) and weights, **dimensions** (linear, aligned, radius, diameter, angle) that follow their
   geometry, hatching, text and a title block, a drawing scale and paper sizes, trim / extend / offset /
   fillet / chamfer / mirror / array, measuring. **DXF** read and write (the CAD exchange format) and SVG /
   PDF export, printing. Built on the vector kit of items 5-6 and on 3DForge's 2D drawing code.
