# Cosmo Zoom — design

*A native 3D anti-gravity racer for Onyx (Raspberry Pi 4). It is **low-poly, a bit rounded**: soft bevelled
3D shapes in the KayKit / Kenney manner, soft lighting, no outlines, vivid pastel colours. Six chunky-but-sleek
hover ships race on floating teal tracks under a bright blue sky, among pastel towers and grassy floating
islands. It is friendly for children: no violence, no gore. The spirit comes from
`sdcard/basic/examples/antigrav.bas` (the spline track, the banked turns, the speed pads, the split screen).
From the kart-racing genre it takes the items, the cups and the catch-up. Our own twist is the **tube
sections** that you ride on the *outside* of a cylinder. Every name, model, texture and tune here is our own.
MIT licence (the Onyx authors, 2026).*

**App name**: `cosmozoom`. The program is `user/Apps/cosmozoom/`; on the card it is `sdcard/apps/cosmozoom.app/`
(`main`, `models/`, `lang/fr.txt`, `times.txt`, `save.ini`). On-screen title: **COSMO ZOOM**, with the subtitle
*Anti-Gravity Grand Prix* (FR: *Grand Prix Antigravité*).

**Mock-ups** are made by `python3 tools/cosmozoom/mockups.py` in `docs/cosmozoom/mockups/`. The script's
`CIRCUITS` / `CUPS` tables hold the same 13 circuits and 4 cups as §7 (the mock-ups show the night look; the
HUD mock-ups predate the final HUD layout of §9, which is the reference). It is one list: change both together.
**The mock-ups belong to the dropped directions (§1.2): the reference look is `refs/art-direction.png`.**

| | |
|---|---|
| ![Title](mockups/title.png) | ![Race](mockups/race_hud.png) |
| ![Hangar](mockups/garage.png) | ![Cups](mockups/cup_select.png) |

**Revision history**
- **2026-10-15 a**: a cute candy-cartoon version (animal mascots, chubby ships). Then, at the user's request:
  sci-fi ships, candy colours, less neon. Then the CC0 style references (§3.0).
- **2026-10-15 b** (this version): the user does **not want the cute style**. Their reference is the look of
  *Aero GPX*, an F-Zero-like indie racer: clean flat cel-shaded colours, a bright blue sky with big stylised
  clouds, mint/teal track surfaces, magenta/pink rails, white architecture, sleek low ships, a sleek HUD, and
  tube sections ridden on the outside. As a result:
  - no animal mascots, no faces on objects, no chubby proportions;
  - human pilots in helmets with visors, organised in teams;
  - sleek hulls;
  - sci-fi items without faces;
  - tube sections in many circuits, and one circuit that is mostly tube.
- **2026-10-15 c** (this version, **final art direction**): the user made a reference image. It shows a
  third-person chase view of their **6 ships** racing at **night through a neon city**: tall dark-blue towers
  with lit windows and big neon billboards; stacked elevated highways with light trails; a dark road streaked
  with cyan/white light; neon rails; a glowing boost pad; long coloured trails; a cel-shaded comic look with
  outlines; and a neon HUD. This reverses the earlier "less neon" request. Nothing cute comes back: helmeted
  pilots, sci-fi items without faces. The daylight "Sky Circuit" look of version b survives only as the
  variant for a few circuits (§7.3).
- **2026-10-15 d** (final): the user also wants **"the same style but in daytime"**: the same neon city by day,
  with a bright blue sky, light clouds, cream/sand/white towers with terraces, billboards still glowing, a
  grey-blue track with cyan streaks, and magenta/cyan rails. So it is **one style at three times of day**: the
  circuits alternate **day, dusk and night** (§7.4), and everything must be **polished like an AAA game** (§1.3).
- **2026-10-15 e** (**final, approved by the user**): the user disliked the drawn/comic styles. Their approved
  image is `refs/art-direction.png`: **3D low-poly, a bit rounded**, soft lighting, no outlines, vivid pastels.
  The neon-city and cartoon directions were tried and **dropped**. What stays from them: the six ships, the
  helmeted pilots, the gadgets, the circuits and tubes, the HUD layout, and the AAA polish bar.

---

## 1. Art direction

### 1.1 The final direction: low-poly, a bit rounded

![Art direction](refs/art-direction.png)

*The user's approved image (2026-10-15). It is the reference for every model, colour and effect.*

- **Shapes**: 3D low-poly with **soft bevels and rounded edges** (the KayKit / Kenney feel). The forms are
  chunky but clean; there are no sharp spikes and no thin details.
- **Light**: soft and sunny. A gentle key light, a sky-blue fill and a soft ambient occlusion feel at
  contacts. **No outlines, no cel bands, no neon.**
- **Colours**: vivid pastels: coral, peach, lemon, mint, lilac, sky, pink and teal. The ships are more
  saturated than the world, so that they read first.
- **Sky**: a bright blue gradient with **round white puffy clouds** (low-poly).
- **Track**: a **teal** floating ribbon with a slightly lighter centre and soft edges. The **rails are round
  tubes, candy-striped in pastels** (pink/mint/cream). **Glowing cyan chevron pads** are the boosts.
  **Translucent "?" cubes** are the item capsules.
- **Tubes and loops**: teal ribbed tubes and loops arching over the track, in the same colour as the road.
- **World**: **rounded pastel capsule towers** (pill-shaped buildings with stripes and caps), **floating grassy
  islands** with round low-poly trees and boulders, little arched bridges, and distant floating rocks.
- **Ships**: the same six ships (§4.1), after the user's **reference sheet of the six ships** (2026-10-15):
  **chamfered** low-poly — flat faces and crisp 45° bevels rather than round ones — vivid solid paint, wings in the body's colour (they read as part of the hull), opaque
  **slate-navy canopies, intakes and nozzles**, soft glowing exhaust plumes. Nova Streak a faceted arrowhead,
  Comet Chaser a narrow fuselage between two long boxes, Star Drifter big nacelles with lattice booms ahead,
  Plasma Runner two chunky octagonal pods pulling a little cockpit on glowing cables, Ringmaster a torpedo inside
  a big yellow ring, Manta Ray a wide delta with boxy pods at its tips (`docs/cosmozoom/models/ships.png`).
- **Pilots**: helmeted humans (§5), modelled in the same rounded low-poly way.

### 1.2 Directions tried and dropped (2026-10-15)

![Directions](mockups/proposals.png)

- **Candy-cartoon** (animal mascots, chubby toy ships): too cute.
- **Sky Circuit** (flat cel-shaded daylight, Aero GPX mood): a drawn look; the user disliked it.
- **Neon-city comic** (night, outlines, neon billboards, by day / dusk / night): also a drawn look; dropped.
- **Retro rockets**: too retro.

The mock-ups in `mockups/` show these dropped looks. They remain useful only for layouts: the screens, the
HUD, the circuit maps.

### 1.3 AAA polish (the quality bar)

- **Cohesion**: one art bible. The same bevel sizes, the same soft light ramp, the same pastel palette and the same
  HUD frame everywhere. No placeholder art ships.
- **Motion**: 60 fps with 1 player and a steady 30 fps with 4. Camera springs, FOV kick and speed lines in
  boosts. Trails always smooth. No pop-in (fog and LOD fades over 0.3 s).
- **Feedback**: every action has sound, a particle and a HUD reaction: pickups, hits, drift levels, boost,
  laps, overtakes (a "+1" flash on the place).
- **Presentation**:
  - a 3 s fly-over of each circuit, with the name card sliding in;
  - a countdown with lights and a camera push;
  - slow motion (0.5×, 1 s) at the finish line;
  - an animated results screen and podium;
  - every menu transition animated (slides, 0.2 s).
- **Soft glow**: additive glow sprites on the boost pads, plumes and cubes, and a soft vignette. These are cheap on the
  Pi and give the "lit" look.
- **Detail density**: billboards with our own animated words; flying traffic in the sky; window grids;
  stacked highways with moving light trails; crowds in the stands; sparks on wall scrapes.
- **Audio mix**: the music ducks under big events; engines pan and Doppler-shift as ships pass.

---

## 2. The game in one paragraph

Eight pilots race sleek anti-gravity ships on floating ribbons high above the clouds. Hover pads keep the ships
0.6 m above the surface. The ribbon banks, climbs, dives, jumps over gaps, sometimes loops (at most two loops
per circuit), and sometimes wraps into a **tube** that the ships ride on its outside. Driving through **item
capsules** gives a random **gadget**: boosts, shields, plasma bubbles, ion goo, gravity wells, a tractor beam…
Nothing is violent: a hit ship spins inside an energy bubble for about a second. **Boost pads** throw you
forward. **Drifting** charges a mini-boost. Collecting **energy cells** makes you a little faster. A race is
3 laps. There are four **cups** of four circuits each, and points decide the cup's podium.

---

## 3. Look and feel rules

### 3.0 Style references

- **The reference**: `refs/art-direction.png` (§1.1), approved by the user.
- **Asset style references** (CC0): KayKit "Space Base Bits" (Kay Lousberg) and Kenney "Toy Car Kit". Their
  modelling is the target: soft bevels, rounded boxes, chunky readable forms, soft colours. Being CC0, some of
  their props (rocks, trees, crates, antennas, domes) **may be imported**, provided they are:
  - converted to our `.obj` conventions (§11);
  - recoloured to our palette;
  - credited in the About box and in `docs/LICENSING.md`.

  Their cars are not used.

### 3.1 Rendering style (for the programmer and the modeller)

- **Low-poly, rounded, smooth-lit.**
  - Every edge is bevelled: 0.05–0.1 m with 2 segments on ships, 0.1–0.3 m on props.
  - Cylinders and capsules use 10–16 sides.
  - The normals are **smoothed across bevels** and flat on the big faces, so the shapes look soft.
- **Lighting** is per vertex on the CPU:
  - a soft key light from the upper left (`(-0.35, 0.85, 0.4)`), a sky-blue fill from above, a warm bounce from
    below at 15 %;
  - a **smooth** ramp, not banded; the shadow side is never darker than 0.6;
  - a fake contact shadow: a soft dark blob under every ship and prop, on the track and the islands.
- **No outlines. No neon.** Glow is used only, and softly (additive, ≤ 0.5), on:
  - the boost chevrons (cyan);
  - the engine plumes (pink, lilac or cyan);
  - the item cubes' inner light.
- **Translucency**: the "?" cubes (alpha 0.55, a white "?" inside), the canopies (dark glass with a white
  highlight), the shield bubble.
- **Fog**: a light blue haze from 140 m to 260 m that softens the distant islands and towers.
- **Textures** are few and tiny, generated at start-up:
  - the rail stripes;
  - the chevrons;
  - the "?" glyph;
  - the start checker;
  - the digits and team logos;
  - a soft radial glow;
  - the window bands of the towers.

### 3.2 The master palette (material `Kd` values for the `.mtl` files)

| Name | Hex | Kd (r g b) | Use |
|---|---|---|---|
| sky top | `#3E8EF0` | 0.243 0.557 0.941 | sky zenith |
| sky low | `#A9DBFF` | 0.663 0.859 1.000 | horizon, fog |
| cloud | `#FFFFFF` | 1.000 1.000 1.000 | clouds |
| track teal | `#2E9C93` | 0.180 0.612 0.576 | track surface, tubes, loops |
| track light | `#4FB7AC` | 0.310 0.718 0.675 | track centre, tube ribs |
| track side | `#1F7A73` | 0.122 0.478 0.451 | track sides and underside |
| rail pink | `#E89BB0` | 0.910 0.608 0.690 | rail stripes |
| rail mint | `#A8E0C2` | 0.659 0.878 0.761 | rail stripes |
| rail cream | `#F4EBD9` | 0.957 0.922 0.851 | rail stripes |
| boost cyan | `#7DF2FF` | 0.490 0.949 1.000 | chevron pads (soft glow) |
| coral | `#F4775E` | 0.957 0.467 0.369 | towers, ships |
| peach | `#F9A77C` | 0.976 0.655 0.486 | towers |
| lemon | `#F5DC5B` | 0.961 0.863 0.357 | towers, ships |
| mint | `#9FE3B5` | 0.624 0.890 0.710 | towers |
| lilac | `#B9A6F2` | 0.725 0.651 0.949 | towers |
| sky pastel | `#9CCBF2` | 0.612 0.796 0.949 | towers |
| grass | `#6CC46A` | 0.424 0.769 0.416 | island tops, trees |
| tree dark | `#3E9A55` | 0.243 0.604 0.333 | tree shade |
| earth | `#A0785A` | 0.627 0.471 0.353 | island undersides |
| rock | `#9A8E86` | 0.604 0.557 0.525 | boulders |
| ship magenta | `#C8329A` | 0.784 0.196 0.604 | Nova Streak |
| ship orange | `#FF8A2A` | 1.000 0.541 0.165 | Nova Streak nose, Star Drifter, Manta Ray |
| ship cyan | `#5FD6F0` | 0.373 0.839 0.941 | Comet Chaser |
| ship lime | `#A6E04A` | 0.651 0.878 0.290 | Plasma Runner |
| ship yellow | `#F7C832` | 0.969 0.784 0.196 | Star Drifter, Ringmaster |
| plume pink | `#FF8AD8` | 1.000 0.541 0.847 | engine plumes (soft glow) |
| canopy | `#2F3E66` | 0.184 0.243 0.400 | canopies, visors |

No pure black and no neon saturation. The world stays pastel; the ships carry the strongest colours.

### 3.3 HUD and menus look

- **Panels**: **rounded deep-blue translucent panels** (`#1E2A5A` at 70 %, corner radius 30 % of the height),
  with a 2 px **light rim** (white at 40 %) and a **soft drop shadow**. No neon frames.
- **Text**: **white bold italic**, with a soft dark shadow; labels in small caps. Accent colours (lemon, mint,
  coral, cyan) are used only for bars and highlights.
- **Bars**: rounded pills. The energy bar goes green → yellow → coral; the boost bar is peach.
- The font is DejaVu Sans Bold Oblique (a rounder OFL font may be added later). Every text goes through
  `TR ("…")`; buttons are sized for French.

---

## 4. The ships

Every ship has the same parts:
- a **hull**, with the canopy, the hover pads and the number;
- two mirrored **wings**;
- an **engine** module;
- a **livery**: the team colour, a trim colour, a pattern and the number.

The player picks the hull, the wings, the engine and the livery in the **Hangar**.

The stats are on a 1–5 scale:
- **Speed**: the top speed.
- **Acceleration**.
- **Handling**: turning and grip.
- **Weight**: who pushes whom in a bump. Heavy ships lose less speed when bumped and are less pulled by
  gravity wells.

On every hull, Speed + Acceleration + Handling makes 8–10; the lighter hulls have the higher totals.

**The roster**: the 6 ships of the user's concept sheet are the 6 hulls. Each comes with its default
engine, wings and colours (§4.1). The player can then change the parts and the livery.

**Shape language** (the user, 2026-10-15): start from the anti-grav racer silhouette, with **less rounding**.
- **Crisp edges**: chamfers of 0.03–0.05 m only; faceted panels, sharp wedge noses, knife-edge fins.
- **Vivid colour blocks**: large flat areas of the team colour, white panels, one contrast accent (yellow,
  cyan or magenta), dark canopy, the number.
- **Engines** are visible and dramatic: one central jet, side pods, a ring, or **detached pods** flying ahead
  of the hull or beside it, joined to it by glowing energy tethers.
- All designs are original: no copy of an existing ship.

### 4.1 The six ships (hulls) — the model files keep their names

| # | Ship EN / FR | Model file | Look (default parts and colours) | Speed | Accel | Handling | Weight | Role |
|---|---|---|---|---|---|---|---|---|
| 1 | **Nova Streak** / Nova Streak | `hull_comet.obj` | a **magenta wedge with an orange nose**, short swept fins, central jet | 3 | 3 | 3 | 3 | all-rounder (default) |
| 2 | **Comet Chaser** / Chasseur de Comètes | `hull_needle.obj` | a long **cyan** dart with **side pods** attached along its flanks | 5 | 2 | 3 | 2 | top speed |
| 3 | **Star Drifter** / Dériveur Stellaire | `hull_whale.obj` | a heavy **orange/yellow** racer with two engine nacelles on **open truss struts** | 4 | 2 | 2 | 5 | bruiser |
| 4 | **Manta Ray** / Raie Manta | `hull_manta.obj` | a wide **orange delta** with **teal** wingtip pods | 2 | 3 | 5 | 1 | cornering |
| 5 | **Plasma Runner** / Coureur Plasma | `hull_pod.obj` | a compact **lime/teal** cockpit towed by **detached podracer pods**, joined by a **pink lightning arc** | 3 | 5 | 2 | 2 | quick starts |
| 6 | **Ringmaster** / Maître de l'Anneau | `hull_cube.obj` | an angular **yellow/magenta** racer with a **big ring over its tail** | 4 | 3 | 1 | 5 | tough |

**Default parts** (the pilots' ships, `parts.h` PILOTS): every pilot starts on **No Fins** — the clean hulls of the
user's reference sheet — with the hull's own engine: Nova Streak + Ion Jet, Comet Chaser + Side Pods, Star Drifter +
Twin Nacelles, Manta Ray + Ion Jet (its pods are part of the hull), Plasma Runner + Plasma Pods, Ringmaster + Ring
Drive. The planforms follow the user's top view of the six ships, their heights its side view (2026-10-15).

The trail colours are: Nova Streak magenta, Comet Chaser cyan, Star Drifter yellow, Manta Ray orange, Plasma
Runner pink, Ringmaster yellow-magenta.

![Hulls](mockups/hulls.png)

### 4.2 Wings (4)

| Wing EN / FR | Look | Modifier |
|---|---|---|
| **Stub Fins** / Ailerons courts | short swept trapezoids | **+1 Acceleration, −1 Handling** |
| **Ray Wings** / Ailes Rayon | long straight blades with tip lights | **+1 Speed, −1 Acceleration**; jumps fly 15 % further |
| **Twin Blades** / Doubles lames | two stacked angled blades per side | **+1 Handling, −1 Speed** |
| **Swept Fins** / Ailes en flèche | swept-back crescent fins | no stat change; drift charges 15 % faster |
| **No Fins** / Sans ailerons | nothing: the clean hull (the default) | no stat change |

### 4.3 Engines (4) — the model files keep their names

| Engine EN / FR | File | Look | Modifier |
|---|---|---|---|
| **Central Jet** / Réacteur central | `engine_ion.obj` | one big angular nozzle at the tail, cyan exhaust | no change (balanced) |
| **Side Pods** / Nacelles latérales | `engine_twin.obj` | two long angular engine pods **attached on the sides** of the hull, two exhausts | **+1 Acceleration, −1 Speed** |
| **Ring Drive** / Anneau de poussée | `engine_ring.obj` | a vertical glowing ring around a short core at the tail | **+1 Handling, −1 Weight** |
| **Tether Pods** / Nacelles captives | `engine_star.obj` | the most spectacular: **two detached engine pods** flying ahead of and beside the hull (podracer-like), each joined to the hull by a **glowing energy tether**; the cockpit hull is towed | **+1 Speed, −1 Handling**; boosts last 10 % longer |

**Tether Pods in detail**:
- **Placement**: the two pods fly 1.2 m ahead of the hull's nose line, 1.6 m out from its centre line, at
  hull height. Each pod is 2.2 m long.
- **Tethers**: two glowing tethers per pod (magenta core, cyan edge), drawn by code as camera-facing quads
  between the sockets `tether_a` / `tether_b` on the pod and on the hull.
- **Motion**: the pods sway ±0.15 m with a 0.2 s lag behind the hull's turns and banks.
- **Collisions**: the pods do not collide; only the hull counts.
- **Width**: in the HUD's mini-map the ship looks wider.

![Parts](mockups/parts.png)

**Rule**: final stat = hull + wing + engine, **clamped to 1..5**. The Hangar shows the 5-pip bars and previews
a change: gained pips in mint with "+1", lost pips in magenta with "−1".

### 4.4 Livery

- **Main** and **trim** colours: 10 each (red, orange, yellow, lime, mint, cyan, blue, violet, magenta, white).
  The default is the team's colours (§5).
- **Patterns** (6): plain, racing stripe, split (two-tone front/back), chevrons, checker band, gradient fade.
  Each is a 32×32 tile texture tinted by the two colours (texture × vertex colour), on the `paint1` / `paint2`
  faces.
- **Number**: 1–99 on both sides and on the nose (digits atlas). The **team logo** is on the tail fin.
- The livery is cosmetic only.

### 4.5 From stats to physics (starting values; tune in play)

| Stat value | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| Top speed (× class speed) | 0.94 | 0.97 | 1.00 | 1.03 | 1.06 |
| 0 → top speed (s) | 4.2 | 3.6 | 3.0 | 2.5 | 2.0 |
| Turn rate (deg/s at top speed) | 70 | 80 | 90 | 100 | 110 |
| Side grip (lateral slip damping /s) | 2.5 | 3.0 | 3.5 | 4.0 | 4.5 |
| Weight (bump mass) | 0.7 | 0.85 | 1.0 | 1.2 | 1.4 |

- **Flight**: hover height 0.6 m. The ship rolls up to 25° into turns and pitches with the slope. The idle bob
  is small (±0.04 m).
- **Walls** (the rails) never stop a ship. It bounces softly, loses 15 % of its speed, and throws sparks.
- **Drift**: hold drift while turning to slide. The charge goes **cyan (0.8 s) → yellow (1.6 s) → magenta
  (2.6 s)**. On release you get a mini-boost of 0.5, 1.0 or 1.6 s (+25 % speed).
- **Energy cells** (Cellules d'énergie): 8–12 per lap. You hold up to 10, and each gives **+1 % top speed**.
  A hit drops 3. They fill the thin **energy bar** at the top of the HUD.
- **Boost pads**: +40 % speed for 1.2 s.
- **Falling off**: possible only on jumps over gaps. A **recovery drone** (a sleek white drone with a cyan light,
  no face) puts the ship back on the track after 1.5 s.
- **Tube sections**: see §7.2.

---

## 5. The pilots (8 teams)

The pilots are human racers. They are always shown in **helmets with closed tinted visors**, so no face is ever
seen. Each has a racing suit in the team colours, a number, and a short, international name (the same in French).
The team names are translated. The pilots are cosmetic (no stats).

| # | Pilot | Team EN / FR | Main colour | Trim | Number | Default ship (recoloured to the team) |
|---|---|---|---|---|---|---|
| 1 | **Kai Moreno** | Ember / Braise | red `#F2384B` | white | 07 | Nova Streak |
| 2 | **Lumi Sato** | Zenith / Zénith | white `#F2F6FF` | cyan | 01 | Comet Chaser |
| 3 | **Aya Okafor** | Volt / Volt | yellow `#FFD21F` | navy | 22 | Plasma Runner |
| 4 | **Nils Berg** | Glacier / Glacier | cyan `#3DE0FF` | white | 14 | Manta Ray |
| 5 | **Zara Haddad** | Orchid / Orchidée | magenta `#FF2E9A` | white | 09 | Comet Chaser |
| 6 | **Teo Rossi** | Verde / Verde | lime `#7CF06A` | navy | 33 | Star Drifter |
| 7 | **Mika Lind** | Nebula / Nébuleuse | violet `#7A5CFF` | pink | 18 | Manta Ray |
| 8 | **Ravi Kapoor** | Sunburst / Soleil | orange `#FF8A1F` | yellow | 42 | Ringmaster |

![Pilots](mockups/pilots.png)

**Pilot model**: a helmet and shoulders seen through the canopy, plus a podium figure (helmet, suit, arms).

**Pilot animations**:
- in the race: head turns into the bends, and the pilot leans with the ship;
- **fist pump** after an overtake or a good gadget;
- a short **shake** when bubbled;
- on the podium: **wave**, then **helmet-off is never shown** (the visor stays closed);
- 4th–8th: they clap on the podium screen.

---

## 6. Items ("gadgets")

**Item capsules** (Capsules) are translucent faceted crystals in the rainbow colours with a white core. They
spin at 90°/s and bob. They stand in rows of 4–5 across the track (or around a tube), 3–4 rows per lap.

- Driving through one gives a random gadget after a 1.5 s **roulette** in the HUD slot.
- A capsule reappears 2 s after being taken.
- You hold one gadget at a time; the triple items show 3 charges.

**Being hit** never destroys anything. The ship is held for 1.0–1.5 s in a cyan **energy bubble** and spins
once. Its speed drops to 40 %, and 3 energy cells fly out. Then the bubble pops into light sparks and the ship
carries on.

### 6.1 The ten gadgets

| # | Gadget EN / FR | Kind | Effect | Numbers |
|---|---|---|---|---|
| 1 | **Hyper Boost** / Hyper-Boost | helps | a burst of speed with a long exhaust trail | +50 % speed, 1.5 s |
| 2 | **Triple Hyper** / Triple Boost | helps | three Hyper Boosts, used one by one | 3 × 1.2 s |
| 3 | **Shield Dome** / Bouclier | helps | a hexagonal energy shell around the ship: blocks one hit or hazard, nudges others aside | 8 s or 1 hit |
| 4 | **Plasma Bubble** / Bulle Plasma | hinders | fired forward, follows the track; the ship it touches is bubbled; can be dropped backward | 60 m/s, lives 4 s; bubbled 1.2 s |
| 5 | **Ion Goo** / Gelée ionique | hinders | a puddle of fizzy green gel dropped behind; crossing it skids and slows | Ø 3 m, 20 s; −50 % speed 1 s, steering wobble |
| 6 | **Gravity Well** / Puits de gravité | hinders | a small spinning vortex thrown 20 m ahead or dropped; ships within 6 m are pulled in and slowed (heavy ships less) | 6 s; pull 8 m/s² × (1.5 − weight/3) |
| 7 | **Tractor Beam** / Rayon tracteur | helps (catch-up) | locks on the ship just ahead (≤ 80 m) and pulls you into its slipstream | 3 s, +20 % speed |
| 8 | **EMP Burst** / Impulsion IEM | hinders | a ring of static: those within 40 m behind see sparks over the lower half of their view, and their gadget slot is jammed | 2.5 s |
| 9 | **Ion Storm** / Orage ionique | hinders the leader | a small dark-blue storm cloud with lightning flies to the **leader** and hovers over them: their hover pads flicker | −25 % speed, 3 s; the Shield blocks it |
| 10 | **Warp Star** / Étoile Warp | helps (big catch-up) | the ship is wrapped in a star-shaped warp field and flies on autopilot; invulnerable; nudges others aside | 5 s at +60 % speed |

![Items](mockups/items.png)

### 6.2 Who gets what (the rubber band)

The table gives probabilities in %, by race position (8 racers).

| Gadget | 1st | 2nd–3rd | 4th–5th | 6th–7th | 8th |
|---|---|---|---|---|---|
| Hyper Boost | 15 | 20 | 20 | 10 | 5 |
| Triple Hyper | 0 | 5 | 15 | 20 | 15 |
| Shield Dome | 30 | 15 | 10 | 5 | 0 |
| Plasma Bubble | 20 | 20 | 15 | 10 | 5 |
| Ion Goo | 30 | 15 | 10 | 5 | 0 |
| Gravity Well | 5 | 10 | 10 | 10 | 5 |
| Tractor Beam | 0 | 5 | 10 | 15 | 15 |
| EMP Burst | 0 | 5 | 5 | 5 | 0 |
| Ion Storm | 0 | 0 | 5 | 10 | 15 |
| Warp Star | 0 | 0 | 0 | 10 | 40 |

There is also a mild speed rubber band for the computer pilots:
- a computer pilot behind the player gets up to +6 % top speed;
- one ahead gets up to −6 %;
- in the Orbit class it is only ±3 %;
- there is none in Time Trial.

---

## 7. Circuits

### 7.1 How a circuit is made

The track is **generated by code** from a closed spline, as in `antigrav.bas`. Each control point has:
- a height, a bank angle and a width (14 m normal, 10 m narrow, 18 m wide);
- **flags**: tunnel, loop, jump gap, **tube**, boost pad, capsule row, energy cells, shortcut.

The starting layouts are in `tools/cosmozoom/mockups.py`, in the `CIRCUITS` table. Its features use these
kinds: `start`, `boost`, `items`, `jump`, `tunnel`, `loop`, `hazard`, `bank`, plus `("tube", t0, t1)` and
`("short", t0, t1)` spans. The x/z points are in −1..1, scaled to the given length. Where a layout crosses
itself, one branch passes **over** the other, at least 8 m higher. **Loops**: at most 2 per circuit, each a
vertical circle of radius 12 m.

### 7.2 Tube sections (ridden on the outside)

- **Shape**: a cylinder of radius **R = 7 m** (circumference ≈ 44 m) following the spline. The ships ride its
  **outside**.
- **Steering** changes the ship's **angle θ around the tube** (one turn = 44 m of lateral travel). Gravity
  points toward the tube's axis, so nobody falls off.
- **Camera**: it **rolls with the ship**, so the ship always looks upright and the world turns around it.
- **On the tube**: capsules, boost pads and energy cells are placed at chosen angles, often a spiral of boost
  pads around it. There are no rails: the tube is endless sideways.
- **Way in and out**: a 30 m transition where the flat ribbon curls into the tube (and uncurls at the exit). On
  exit, the ship's θ maps back to a lateral position; if it is beyond the ribbon's edge, it is pushed gently
  inside.
- **Look**: teal with magenta guide stripes every 45°, a lighter panel grid, and white rings every 20 m.
- The physics are simple: on a tube, "lateral position" = θ × R, and "up" = the radial direction at θ.

![Tube](mockups/race_tube.png)

### 7.3 The circuits

(The look of each circuit is in §7.4.)

![Circuits](mockups/circuits.png)

| # | Circuit EN / FR | World (the look) | Length | Key features | Signature props | Hazards |
|---|---|---|---|---|---|---|
| 1 | **Neon Harbour** / Port Néon | a pastel harbour town of capsule towers; the league's home | 1500 m | wide, gentle banking, one jump between two piers, 2 boost pads; the tutorial | capsule towers, floating islands, docked ships, arched bridges | none |
| 2 | **Dome Bay** / Baie des Dômes | pastel glass domes on a turquoise sea | 1650 m | a **short tube** above the waves, a wave jump, a shortcut over a sandbar (needs a boost) | glowing domes, sea platforms, a lighthouse beam | water geysers (bump) |
| 3 | **Mech Foundry** / Fonderie Méca | a cheerful pastel factory island | 1700 m | right-angle turns, a **conveyor** section, a tunnel through the assembly hall, a crate jump | robot arms, cranes, rounded warning signs | pistons at the chicane (bump) |
| 4 | **Glow Jungle** / Jungle Lumineuse | a lush jungle of round trees and giant leaves on grassy islands | 1900 m | a **figure-8** with a bridge, a vine tunnel, a waterfall jump, a shortcut | giant leaves, round trees, research towers, waterfalls | bouncy pods on the shortcut |
| 5 | **Crystal Moon** / Lune de Cristal | a snowy moon with lilac crystals, cold daylight | 2000 m | hairpins, **slippery crystal** patches, an ice-cave tunnel, a big ski jump | glowing crystal spires, observatories | rolling ice blocks |
| 6 | **Asteroid Rings** / Anneaux d'Astéroïdes | a mining colony on a planet's ring, in pastel space | 2100 m | **low-gravity jumps**, a hollow-asteroid tunnel, a **tube** between two rocks | ringed planet, mining rigs with lights, beacons | drifting rocks (bump) |
| 7 | **Skyline City** / Cité Skyline | the tallest pastel capsule-tower city: the track weaves between towers | 2200 m | **one loop**, a banked spiral round a tower, a **tube** across the central gap | towers, sky-bridges, round signs, flying traffic | crosswinds between towers |
| 8 | **Twin Sun Dunes** / Dunes des Deux Soleils | a desert planet with two setting suns | 2150 m | big dune jumps, a canyon tunnel, a shortcut over a dune | sand-sail ships, crystal pyramids, a lit outpost | sand bursts |
| 9 | **Lava Moon** / Lune de Lave | a volcano moon at sunset, soft rounded volcanoes, glowing lava rivers | 2400 m | a **corkscrew tube** around the cone, lava-rock jumps, a tunnel through the volcano | volcano, glowing rivers, cooling towers | lava splashes (a bounce, never a fall) |
| 10 | **Hyper Cylinder** / Hyper Cylindre | a long tube course between floating islands above the clouds | 2300 m | **mostly tube (≈ 70 %)**: three long tubes joined by short flat roads and one jump; spiral boost lines | towers, giant rings, holo-signs | gravity-well buoys on the tube |
| 11 | **Star Carnival** / Fête Spatiale | a pastel amusement park at sunset | 2600 m | **one loop** through a giant ring, a **tube** around the ferris wheel's axle, a bumper zone | lit ferris wheel, carousel rings, fireworks | bumper saucers |
| 12 | **Aurora Forest** / Forêt Aurore | a forest of giant crystal trees under a soft aurora night (the only night) | 2500 m | a root tunnel, root jumps, a **tube** along a giant trunk, a shortcut through a hollow log | crystal trees, light-pods, lit walkways | falling seed-pods (slow puddles) |
| 13 | **Starlight Ribbon** / Ruban d'Étoiles | the final: a ribbon through a pastel galaxy | 3200 m | **two loops**, a long **spiral tube**, the most jumps, boost chains | the league's stadium, giant ring gates, the city far below | none: speed is the hazard |

**Remix** circuits (cup 4) are the same tracks **driven in reverse**, at dusk. Their boost pads
and capsule rows are rearranged. Nothing new needs to be modelled.

### 7.4 The circuits' looks (intent)

All circuits share the §1.1 style: the teal ribbon, the pastel candy rails, the round clouds and the rounded
props. **Most are by day; a few at sunset; two in space; at most one soft night.** The level designer is
re-colouring the circuits now, so this table gives the **intent**; the exact colours live in the circuit
files.

| # | Circuit | Time | Intent |
|---|---|---|---|
| 1 | Neon Harbour | day | the home circuit: a pastel harbour town of capsule towers on islands, a blue bay, white boats |
| 2 | Dome Bay | day | glassy pastel domes on the sea, turquoise water, a short tube above the waves |
| 3 | Mech Foundry | day | a cheerful pastel factory island: rounded cranes, gears, conveyor lanes in lemon and coral |
| 4 | Glow Jungle | day | lush round trees, giant leaves, waterfalls between grassy islands, mint and lime |
| 5 | Crystal Moon | day (cold) | a snowy moon with lilac crystals under a pale blue sky |
| 6 | Asteroid Rings | **space** | a bright pastel starfield, a big ringed planet, rounded asteroids; the track floats in space |
| 7 | Skyline City | day | the tallest pastel capsule-tower city; the track weaves between towers and bridges |
| 8 | Twin Sun Dunes | **sunset** | peach dunes, two warm suns low on the horizon, a coral and lilac sky |
| 9 | Lava Moon | **sunset** | a warm orange sky, soft rounded volcanoes, glowing (not scary) lava rivers |
| 10 | Hyper Cylinder | day | long teal tubes strung between floating islands high above the clouds |
| 11 | Star Carnival | **sunset** | a pastel amusement park: ferris wheel, carousel rings, balloons, a pink sky |
| 12 | Aurora Forest | **soft night** (the only one) | a deep-blue but light night with green-lilac aurora and round glowing pods; never dark |
| 13 | Starlight Ribbon | **space** | the final: a ribbon through a pastel galaxy, giant rings, planets passing by |

The **Remixes** (Galaxy Cup) run at sunset. Some circuit names still carry words from the dropped
directions ("Neon Harbour", "Glow Jungle"); they may be renamed with the level designer.

### 7.5 Cups

| Cup EN / FR | Circuits (in order) | Difficulty | Unlock |
|---|---|---|---|
| **Moonbeam Cup** / Coupe Rayon de Lune | Neon Harbour, Dome Bay, Mech Foundry, Glow Jungle | easy | open |
| **Nebula Cup** / Coupe Nébuleuse | Crystal Moon, Asteroid Rings, Skyline City, Twin Sun Dunes | medium | open |
| **Supernova Cup** / Coupe Supernova | Lava Moon, Hyper Cylinder, Star Carnival, Aurora Forest | hard | a trophy (top 3) in the Nebula Cup |
| **Galaxy Cup** / Coupe Galaxie | Glow Jungle Remix, Skyline City Remix, Star Carnival Remix, Starlight Ribbon | expert | a trophy in each of the other three cups (any class) |

**Other unlocks** (generous, since the game is for children):
- the **Mirror** class: after a gold in the Galaxy Cup;
- one livery pattern per trophy;
- Twin Blades and the Ring Drive: after the first trophy; Tether Pods: after the first gold.

---

## 8. Game modes

- **Grand Prix**: a cup of 4 races, 3 laps each, 8 racers. The points per race are **10, 8, 6, 5, 4, 3, 2, 1**.
  A tie goes to whoever did better in the last race. The top 3 get trophies.
- **Single Race**: any unlocked circuit, 1–5 laps, items on or off, 0–7 computer pilots.
- **Time Trial**: alone, 3 laps, starting with a Triple Hyper. A ghost of the best race is kept. The best lap
  and the best race are saved in `SD:/apps/cosmozoom.app/times.txt`.
- **1–4 players**: split screen (2: top/bottom; 3–4: quarters, and the 4th quarter shows the map with 3
  players). Player 1 can also use the keyboard.

**Speed classes**

| Class EN / FR | Base top speed | Computer pilots |
|---|---|---|
| **Orbit** / Orbite | 30 m/s (108 km/h) | gentle |
| **Hyper** / Hyper | 40 m/s (144 km/h) | normal |
| **Warp** / Warp | 50 m/s (180 km/h) | smart, take shortcuts |
| **Mirror** / Miroir | 50 m/s, the tracks mirrored | smart (unlocked) |

---

## 9. Screens and HUD

```
Title ─► Main menu ─► (Grand Prix | Single Race | Time Trial | Options)
   ─► Players join ─► Pilot / team select ─► Hangar ─► Class + Cup / Circuit
   ─► Fly-over intro (3 s) ─► Countdown ─► Race ─► Results ─► next race | Podium
```

- **Title**: the logo COSMO ZOOM in rounded white bold italic with a soft shadow. Behind it is a day circuit
  like the reference image, with the six ships flying by; "PRESS START". After 20 s idle, a demo race plays.
- **Pilot select**: 8 helmet portraits in rounded panels, each with the team logo, name, number and colours.
- **Hangar** (`garage.png`):
  - the ship on a lit platform;
  - tabs Pilot / Ship / Wings / Engine / Livery, with a grid of choices;
  - the stat bars with the preview of the change, the livery colours and patterns;
  - Random (Y) and Test drive (X).
- **Cup select** (`cup_select.png`): 4 rounded cards, each with a trophy, the 4 circuits and the medals
  per class. The class tabs are on top.
- **Race HUD** (`race_hud.png`; the layout below is kept): **rounded deep-blue translucent panels with a light
  rim and a soft shadow**, with **white bold italic text** (§3.3).
  - **top left**:
    - a **shield emblem** next to a segmented **ENERGY** bar (green → yellow → red; it is the shield/energy:
      hits and wall scrapes drain it, energy cells refill it);
    - under it, a thin orange **BOOST** bar (the drift / boost charge);
    - under that, the **time panel**: the race time and the last split (+/− against the best lap, green or red).
  - **top centre**: the **gadget slot**. When a boost is running, a **"BOOST ACTIVE"** banner appears between
    chevrons (also "FINAL LAP", "WRONG WAY", "3 · 2 · 1 · GO").
  - **top right**: the **LEADERBOARD** panel: 8 rows, each with the place, the team colour chip and the
    pilot's name; the player's row is highlighted.
  - **bottom left**: a round **speedometer dial** (0–10 ×100 km/h scale, with a red zone at the top), plus a
    digital **SPEED** box below it.
  - **bottom right**: a framed **mini-map** (tube sections drawn thicker), and next to it **LAP 2/4** and
    **PLACE 3rd** in big white text.
- **Results / Podium**: slanted rows that slide in, with the points flying into the totals. On the podium the
  top-3 ships hover over a lit podium stage; the pilots stand and wave; pastel confetti and fireworks.
- **Split screen** (`race_split4.png`): the HUD shrinks (scale = view height / 720, at least 0.5). The
  leaderboard and the dial are hidden; only the energy bar, the gadget slot, the digital speed, LAP and PLACE
  remain.

![Split screen](mockups/race_split4.png)

---

## 10. Animation, camera, sound

**Ships**
- a small hover bob;
- a roll of up to 25° and a lean in turns;
- a short squash on landing (0.1 s, very slight: 0.92 / 1.06);
- a pulsing glow on the hover pads;
- an engine trail: a ribbon of the last 12 positions, in the engine's colour;
- in a boost: a doubled trail, FOV +8°, and **speed lines** at the screen edges;
- drift sparks in the charge colour.

**Track and world**
- boost pads scroll and flash; the capsules spin; the energy cells twinkle;
- light strips run along the gates;
- holo-signs flicker in daylight colours;
- clouds drift slowly; airships and ring stations turn;
- daytime fireworks on the last lap;
- the tube's guide stripes scroll as the ship goes round.

**Camera**
- chase cam 6 m behind and 2.0 m up, looking 10 m ahead, spring-damped;
- it follows the track's bank at 60 %, and at **100 % on tubes and loops** (it rolls with the ship);
- FOV 70°, plus 8° in a boost;
- a small shake on bumps;
- an orbit camera at the finish line, and 3 fixed shots for the podium.

**Music** uses the General MIDI synthesizer, with an **upbeat electronic arcade** style per world:

| World | Style | GM programs | Tempo |
|---|---|---|---|
| Title / menus | bright synth-pop theme | Synth Brass (63), Saw Lead (82), Synth Bass 1 (39) | 128 |
| Neon Harbour, Skyline City | sunny electro-pop | Square Lead (81), Pad Warm (90), Electric Piano (5) | 132 |
| Dome Bay, Asteroid Rings | chill drum-and-bass | Pad New Age (89), Fretless Bass (36), Vibraphone (12) | 170 (half-time feel) |
| Mech Foundry, Hyper Cylinder | driving techno-funk | Saw Lead (82), Clavinet (8), Synth Bass 2 (40) | 136 |
| Glow Jungle, Aurora Forest | tropical house | Kalimba (109), Marimba (13), Pan Flute (76) | 124 |
| Crystal Moon | glassy trance | Crystal (99), Celesta (9), Strings (49) | 138 |
| Twin Sun Dunes | desert synthwave in daylight | Clean Guitar (28), Sitar (105), Saw Lead (82) | 120 |
| Lava Moon | arcade rock | Overdrive Guitar (30), Brass (62) | 145 |
| Star Carnival | big-beat | Brass Section (62), Calliope (83), Synth Drum (119) | 140 |
| Starlight Ribbon | epic finale | Choir (53), Strings (49), Synth Brass (63) | 140 |
| Podium | fanfare | Trumpet (57), Timpani (48) | — |

The last lap plays 6 % faster.

**SFX** use FM voices:
- engine hum (the pitch follows the speed, per engine type), boost whoosh;
- drift hiss and a charge "ping" per level;
- capsule pickup chime, roulette ticks, gadget launch, bubble "thoom" and pop;
- goo splash, gravity-well "wub", tractor-beam rising tone, EMP crackle, storm thunder-crackle, warp chord;
- shield ping, rail scrape, landing thud;
- energy-cell tick;
- countdown beeps and the GO chord, lap chime, final-lap stinger, crowd cheer.

There are no voices: the pilots never speak.

---

## 11. The 3D asset list (for the modeller)

**Modelling style**: sleek and low.
- Long horizontal lines, wedge noses, **crisp faceted edges** (chamfers 0.03–0.05 m on ships, 0.1–0.3 m on
  props); no chubby or rounded forms; vivid colour blocks.
- White and light-grey bodies with colour accents on 30–50 % (the team colour on ships).
- Flat-shaded, thin outlines by code.

**Conventions**
- Wavefront `.obj` + `.mtl`; **+Z forward, +Y up, +X to the ship's right**; 1 unit = 1 metre; triangles only.
- Flat shading: no `vn`, or per-face normals only.
- Materials are named after §3.2, with `Kd` from its table.
- `*_glow` materials are drawn additively; `glass` is drawn with alpha 0.4, last.
- The livery faces use `paint1` / `paint2`, with UVs for the pattern; the number decals use `decal` quads
  with UVs into the digits atlas.
- **Sockets** are comment lines `#@socket <name> x y z`. A hull gives `wing_l`, `wing_r`, `engine`, `pilot`,
  `pad_fl`, `pad_fr`, `pad_bl`, `pad_br`, `trail`.
- One object per file; animated parts are `o` groups.
- The files go to `sdcard/apps/cosmozoom.app/models/`, the sources to `tools/cosmozoom/models/`.

**Budget**
- a whole ship (hull + 2 wings + engine + pilot) **≤ 900 triangles**, and LOD1 (beyond 40 m) **≤ 300**;
- props 50–400 triangles each;
- **30–60 k triangles a frame** in all: the ships ≈ 6 k, the track and tubes ≈ 12–20 k, the props ≈ 15–25 k.

### 11.1 Ships

| File | Object | Tris LOD0 / LOD1 | Origin | Notes |
|---|---|---|---|---|
| `hull_comet.obj` | Nova Streak | 350 / 120 | ship centre at pad level (y = 0 is the pads' bottom) | 3.4 × 1.6 × 0.8 m; faceted wedge nose, short side intakes, mid canopy, tail fin with the logo decal |
| `hull_needle.obj` | Comet Chaser | 320 / 110 | same | 4.0 × 1.3 × 0.7 m; needle nose, canopy far back |
| `hull_whale.obj` | Star Drifter | 380 / 130 | same | 3.6 × 2.1 × 1.0 m; wide, big front intake (`glass` dark), twin side pods |
| `hull_manta.obj` | Manta Ray | 300 / 100 | same | 3.4 × 2.4 × 0.6 m; flat, blended wing roots, low canopy |
| `hull_pod.obj` | Plasma Runner | 300 / 100 | same | 3.0 × 1.6 × 0.9 m; short body, oversized rear housing |
| `hull_cube.obj` | Ringmaster | 280 / 100 | same | 3.6 × 1.9 × 0.9 m; angular faceted armour panels, low canopy |
| `wing_stub.obj` | Stub Fins (right; the left is mirrored by code) | 40 / 16 | the root (socket `wing_r`) | span 0.6, swept 30° |
| `wing_ray.obj` | Ray Wings | 60 / 20 | root | span 1.4, chord 0.5, `cyan_glow` tip light |
| `wing_twin.obj` | Twin Blades | 90 / 30 | root | two blades 1.0 and 0.8 m, 0.2 m apart, angled 10° |
| `wing_swept.obj` | Swept Fins | 70 / 24 | root | crescent, swept 50°, span 1.1 |
| `engine_ion.obj` | Central Jet | 100 / 30 | front centre of the module (socket `engine`) | angular nozzle 0.7 × 0.5, length 1.0, socket `trail` |
| `engine_twin.obj` | Side Pods | 200 / 60 | the hull's centre (sockets on the sides) | two angular pods 2.0 m long, attached at x = ±0.95; sockets `trail_l`, `trail_r` |
| `engine_ring.obj` | Ring Drive | 160 / 40 | front centre of the module | ring Ø 1.1, 12 segments (octagonal is fine), inner `cyan_glow`; `o ring` spins |
| `engine_star.obj` | Tether Pods | 260 / 80 | the hull's centre | `o pod_l`, `o pod_r` (each 2.2 m, angular, an intake at the front, a nozzle at the back) placed at (±1.6, 0.4, +1.2); sockets `tether_a_l/r`, `tether_b_l/r` on the pods and the hull; the tethers are drawn by code |
| `pad.obj` | hover-pad glow | 8 | centre | a flat octagon, `cyan_glow` |
| `pilot_helmet.obj` | the in-race pilot | 120 | the neck (socket `pilot`) | helmet with visor (`canopy` + a white highlight strip) and shoulders; `paint1` / `paint2` faces take the team colours (one model for the 8 pilots) |
| `pilot_podium.obj` | the podium pilot | 400 | between the feet | helmet, suit, arms (`o arm_l`, `o arm_r` for the wave), visor always closed |
| `drone_recovery.obj` | the recovery drone | 200 | centre | sleek white body, cyan light, 4 small rotors (`o rotor*`), a clamp |

### 11.2 Track pieces and race objects

The ribbon, the rails, the loops, the **tubes**, the tunnels and the transitions are **generated by code**.
The modeller makes these objects:

| File | Tris | Origin | Notes |
|---|---|---|---|
| `capsule.obj` | 60 | centre | a faceted crystal 1.2 m, rainbow-tinted faces at alpha 0.75, a white core |
| `energy_cell.obj` | 24 | centre | a small hexagonal prism 0.5 m, `yellow` + `cyan_glow` |
| `start_gate.obj` | 400 | centre on the track, at the line | white arch 16 m wide, 8 m high, 3 lights (`o light1..3`), checker band, magenta light strips |
| `ring_gate.obj` | 200 | centre | a slim white ring with pink light strips (checkpoints, also placed around tubes) |
| `boost_pad.obj` | 16 | centre | 4 × 6 m, chevron texture, `orange_glow` (scrolls) |
| `rail_post.obj` | 24 | base | a light-grey post every 8 m (the rail tube itself is generated) |
| `gravity_well.obj` | 120 | centre | a flat spiral disc 4 m, `violet` + `cyan_glow`, spins |
| `goo.obj` | 40 | centre on the ground | a splat 3 m, `lime` |
| `plasma_bubble.obj` | 80 | centre | icosphere 1.6 m, `magenta` alpha 0.5 |
| `shield_dome.obj` | 120 | ship centre | a hexagon-faceted half-sphere 2.8 m, `cyan` alpha 0.35 |
| `ion_storm.obj` | 150 | centre | a flat stylised dark-blue cloud (5 blobs) and 2 lightning bolts (`yellow_glow`); no face |
| `warp_field.obj` | 60 | ship centre | a 5-point star frame 3.5 m, `yellow_glow` |
| `trophy.obj` × 4 | 300 | base | a slim white-and-gold trophy, each with its cup's emblem (crescent, spiral, burst, galaxy) |
| `podium.obj` | 200 | floor centre | white 3-step podium with magenta light strips |

### 11.3 Props per world (50–400 triangles each; recoloured across worlds)

| World | Props |
|---|---|
| shared | `capsule_tower_a/b/c` (60–200 each: rounded pastel pill towers with stripes and caps), `island_grass` (150–300: floating grassy island, earth underside), `tree_round` (40–80), `boulder` (30–60), `arch_bridge` (120), `cloud_puff` (60–120), `sign_round` (40), `airship` (250), `grandstand` (300), `planet_ringed` (300, space circuits), `antenna` (80) |
| Neon Harbour | `boat` (120), `landing_pad` (100), `control_tower` (300), `hangar` (250) |
| Dome Bay | `glass_dome` (200), `sea_platform` (200), `lighthouse` (250), `buoy` (60) |
| Mech Foundry | `gear` (120, spins), `robot_arm` (300, 3 parts), `crane` (250), `crate` (12), `piston` (60) |
| Glow Jungle | `giant_leaf` (80), `seed_pod` (120), `waterfall` (40, scrolling), `research_station` (300) |
| Crystal Moon | `crystal_spire` (150), `observatory` (300), `ice_block` (12) |
| Asteroid Rings | `asteroid_a/b/c` (100–200), `mining_drone` (150), `beacon` (60) |
| Skyline City | `city_tower` (300), `ring_gate_big` (250) |
| Twin Sun Dunes | `sand_sailer` (250), `crystal_pyramid` (60), `wind_turbine` (150, `o blades` spins) |
| Lava Moon | `volcano` (400, distant), `obsidian_arch` (150), `cooling_tower` (200), `lava_splash` (by code) |
| Hyper Cylinder | `sky_tower` (300), `tube_ring` (60, every 20 m) |
| Star Carnival | `ferris_wheel` (400, `o wheel` spins), `carousel_ring` (300), `balloon_arch` (250), `bumper_saucer` (150) |
| Aurora Forest | `crystal_tree` (250), `light_pod` (60), `walkway` (200) |
| Starlight Ribbon | `stadium` (400), `ring_gate_big` (250) |

**Sky**: always bright. A gradient dome (3 colours per circuit in `CIRCUITS`: the sky, the ground haze, the
track, an accent) carries the cloud cards and 1–2 big pale planets.

---

## 12. Technical constraints

- **Resolution**: 640×480 to 1920×1080. The HUD is laid out on a 1280×720 virtual canvas; on 4:3 screens it
  stays inside a 4:3 safe area.
- **Frame rate**: 60 fps with 1 player, at least 30 fps with 4 players on a Pi 4 (V3D).
  - **≤ 60 k triangles a frame** over all views;
  - LOD1 ships beyond 40 m; props culled beyond 200 m; outlines skipped when needed.
- **Lighting**: per vertex on the CPU (no shaders to write); the cel ramp is in §3.1; glows by additive blending.
- **Input**:
  - keyboard: arrows steer, Up/X accelerate, Down brake, Z/C or Shift drift, Space gadget, P/Esc pause,
    F full screen;
  - gamepads: stick/d-pad, A accelerate, B brake, L/R drift, X/Y gadget, Start pause;
  - an **auto-accelerate** option (on by default in Orbit), so that small children only steer.
- **Players**: 1–4, 8 racers in total.
- **Language**: English and French from the first version (`TR`, `lang/fr.txt`).
- **Saves**: `SD:/apps/cosmozoom.app/save.ini` (unlocks, trophies, the last ship per player) and `times.txt`.
- **Licence**: MIT; everything is ours except the optional CC0 props (credited).
