3DForge for Windows
===================

A small parametric CAD, easy to use: solid bodies made of simple steps, every step kept in a
history whose values can be changed afterwards. The same program as Onyx's 3DForge
(user/Apps/3dforge), built for Windows.

Needs: Windows 10 or 11 (64-bit); .NET Framework 4.8 is already part of them.
Keep these files together: 3DForge.exe, 3DForge.exe.config, forgecore.dll, printers.ini.

  3DForge.exe [part.3df]

Design
  Shapes     box, cylinder, sphere, torus, pyramid, prism, tapered prism: click the centre (a
             face, or the ground), move away, click, move up, click -- or type the values at
             the right and press OK.
  Sketch     on a flat face, or a plane of the axes (XY, XZ, YZ): lines, rectangles, circles,
             arcs, 3-point arcs, splines, points; each element keeps its own values.
  Extrude    a closed outline of a sketch, up or down (a cut when it goes into a body).
  Fillet, Chamfer   straight edges, circles and arcs.
  Move       move, turn, scale a body (or a clone of it).
  Union, Subtract, Intersect, Measure.
  The timeline under the view is the history: click a step to change its values, drag the
  blue bar to roll the part back, right click for its menu. Pictures (Import Canvas, or
  dropped on the window) can be laid on a plane to draw over.
  View: drag turns, the right button (or Shift) pans, the wheel zooms; the cube looks along
  an axis; Home, Fit and See-through are under it.

Manufacture
  Milling          a stock round the body, an origin, a flat end mill; clearing in levels
                   and contours (tabs); the cut simulated; G-code for GRBL (check it in the
                   air first).
  Resin printing   the body on the plate, supports, the layers cut and looked at, the
                   printer's file (Anycubic Photon .pm3n; more printers in printers.ini).
  Filament         the nozzle's path layer by layer (walls, solid layers, infill), played.

An assistant at the controls (MCP)
  3DForge.exe --mcp is a Model Context Protocol server (stdin / stdout): an assistant such
  as Claude Code drives the 3DForge window open on the PC (it starts one if none is), as
  you would -- tools, clicks in the view, the panel's fields, shapes made from their
  values, open / save / export, G-code, print files, pictures of the window. Register it:
      claude mcp add 3dforge -- "C:\path\to\3DForge.exe" --mcp
  (the window listens on the named pipe \\.\pipe\3DForge; only programs of this PC reach it).

Files
  .3df             the part (its history, as text), with its manufacturing setup.
  Export           STL, OBJ (meshes); DXF, SVG, PDF (a flat drawing seen from a side, at
                   the part's size); PNG (a picture).
  Presets          the router's tool and machine, the resin, the filament are remembered in
                   %APPDATA%\3DForge.

Licence: MIT (Onyx). It contains Manifold (Apache-2.0) and Clipper2 (Boost Software
Licence 1.0): their licences are in the licences folder.
