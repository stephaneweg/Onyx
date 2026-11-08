---
name: cad-app-decisions
description: "3DForge, Onyx's parametric CAD — the user's choices (Manifold, no solver, GPU, Z up, arc angle + clockwise), Manufacture (G-code) built 2026-10-06, how he steers the work, publishing notes"
metadata:
  node_type: memory
  type: project
  originSessionId: 69132181-1444-460f-b8c7-d1f8be4baeba
  modified: 2026-10-05T21:48:40.205Z
---

**3DForge** (the user's name) is Onyx's small, easy parametric CAD, asked and built on 2026-10-05: `user/Apps/3dforge`,
package `3dforge` 1.0.0 (+ `3dforge-samples`). The state, the code's pieces and what is left: docs/HANDOFF.md (the
3DForge section), docs/3dforge/README.md, docs/03 and docs/04 *3DForge*.

The user's choices (2026-10-05):
- **Scope:** sketch + extrude, union / subtraction / intersection, bodies only (no loft, no surfaces), chamfers /
  fillets as far as they go, STL / OBJ. **Kernel: Manifold** (no STEP; fillets on straight edges and circles only).
  No port of FreeCAD / SolveSpace / OpenCASCADE.
- **Sketch: no constraint solver** — each element a recipe replayed in order.
- **Rendering: the GPU as much as possible.**
- **Z is up** (the table is XY), as slicers read STL — he first thought of the table as XZ, then agreed to keep Z up
  when I explained (2026-10-05). A sketch plane's offset is along the third axis.
- **Design first:** he stopped me when I started coding and asked for a mock-up, then for real mock-ups in the Milk
  theme "as the other apps", approved them, and only then let me build.
- **Changed while I built** (his messages during the work): no left panel — the bodies in a panel floating over the
  view, the history as a **timeline of icons** at the bottom (arrows / drag to scroll); all shapes in **one fold-out**
  button; more shapes (pyramid, prism, tapered prism, torus, sphere) with the gestures he spelled out (centre click,
  sides typed first for prisms, the field of the current value has the keyboard); a shape's values typed directly
  in the right panel; Move also rotates and scales, with a **Clone** check box.

**Why:** he wants something clean and simple, not a "usine à gaz" like FreeCAD, and he steers the interface by
seeing it — expect more such changes.

**How to apply:** show him screenshots of the real app (the desktop simulator: `sh tools/tests/desktop_sim/shots.sh
3dforge`, scenes in `forge_scene.py`) when proposing interface changes. He tries each package on the Pi at once and
sends a screenshot of what looks wrong (2026-10-05: the GPU path works there; his remark on fillets not meeting at a
corner led to the mitre and the sphere / torus corner blend, `corner_reach` / `corner_blend` in `fdoc.h`). The mock-up script needs `pip install manifold3d` (I used a venv in the scratchpad). My working clone of the
upstreams: WSL `~/src/3dforge`. Follow [[kits-first-rule]] for reusable parts.

Publishing note (my experience that day): the shared checkout had someone's uncommitted `sdcard/config.txt`
(`core_freq=300`); publishing from it would have bumped `pi-firmware` with it. I published from a clean
`git worktree` at my commit instead (with my own packages clone, as in [[basic-native-test-setup]]), then removed it.

Later the same session (2026-10-06): **Manufacture** (G-code for his GRBL router, `fcam.h`) built and published in
`3dforge` 1.0.3 after he approved its mock-ups; his conventions that I adopted: a sketch arc's angle is **positive
clockwise**; an arc is centre -> start -> end; angles snap by 5 degrees and hold on multiples of 45. He keeps
sending ideas while I work and expects me to gather them and do them in the same run. Still to ask him: his
router's exact model and travel (I assumed a Two Trees TTC 450), the firmware of his rotary 4th axis (wanted later).
Next idea he raised: writing **Anycubic Photon** files (`.pm3n`, Photon Mono 2: 4096 x 2560 px of 35 um, pw0Img
RLE layers) from a body -- I only assessed it (easy; supports are the missing part), nothing built.
Pushing the packages clone from WSL hangs (no credentials there): push it with Windows git on the
`//wsl.localhost/Ubuntu/...` path (`-c safe.directory='*'`).

2026-10-06, later: **resin printing built** (`fprint.h`, the Process button Milling / Resin printing in Manufacture's
setup; `3dforge` 1.0.6): the user chose "PrusaSlicer's supports if they perform, else ours" -> ours, MIT. He wants
generators as plug-ins (other printers, filament later): `PrintFormat` + `printers.ini` for now. The simulations play
(his wish). Nothing was printed or milled for real yet; the mirror question for his Photon Mono 2 is open.
His question about computing on a second core: not answered by code -- app cores forbid kapi calls and malloc.
Publishing lesson: before pushing packages, read mkrepo's "bumped:" line -- more than my package means my tree is
behind main (another session had merged Elegant, kapi v90): merge in a clean worktree made from origin/main (the
shared checkout could not merge: `pc/dist/OnyxRemote.exe` was running) and rebuild the app there with main's kits.
