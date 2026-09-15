# patternForge

> ## ⚠️ MAJOR WORK IN PROGRESS
>
> **Do not cut cloth from this yet.** Nothing here has been sewn. The
> blocks are simplified, dart-free slopers built from common ratios, not
> fitted commercial patterns — sew a toile first, and expect to correct
> it.
>
> Roughly where things stand:
>
> | | |
> |---|---|
> | Drafting, checks, PDF / DXF / CLO3D export | works, and is tested |
> | Outline editing, derived facings, muslin | works |
> | **Drape simulation** | **experimental, and visibly wrong at the armhole** |
> | Sizes | five graded presets, no free-form measurements |
> | Darts | geometry exists, off by default (breaks seam-length checks) |
>
> The drape is the part to distrust, and the dividing line is the
> **set-in sleeve**. Sleeveless garments sew and hang cleanly — a
> sleeveless dress, skirt, vest or trousers settle with no badly
> stretched cloth anywhere. Add a sleeve and the shoulder tears open,
> because a sleeve cap is a dome and the placement approximates it with a
> tube on the arm, so the armhole seam starts several centimetres apart.
> Coats are worst. Read [Known limitations](#known-limitations) before
> relying on any of it.
>
> APIs, parameter names and the design-JSON format all change without
> notice.

An interactive openFrameworks app that parametrically drafts garments and
exports print-ready pattern packages.

Every pattern piece is a **parametric 2D perimeter curve**, driven by a
size preset and a set of style sliders, offset outward for its seam
allowance, and checked for the geometric consistency a real pattern needs
— seams the same length on both pieces, sleeve caps that fit their
armholes, facings that still cover the neckline they finish. Pick a
garment and a size, move the sliders, watch every piece update live, then
export the package.

It also **drapes the finished pattern on a body**: the real panels are
triangulated, held off an avatar mesh, sewn along the garment's own seam
list, and then released under gravity. That is the only way to see what a
pattern *does* rather than what it measures.

The export format is not invented here. Two hand-built coat packages in
`data/references/` define the contract every design follows — same file
set, same board layout, same `sewing_cm`/`cutting_cm`/`cut`/`note` fields
in the geometry JSON — so a package this app generates is
interchangeable with one made by hand:

- `data/references/Facet-Coat-US4-Pattern-Package/`
- `data/references/03-Prism-Cape-Coat-US4-Pattern-Package-2/`

Each also carries the `refine_coats.py` that produced it, which is the
reference for the extract-and-reshape path described under
[Known limitations](#known-limitations).

## Addons

One, and **not the upstream one** — that is the only thing here that is
easy to get wrong:

| Addon | Source | Commit |
|---|---|---|
| `ofxImGui` | <https://github.com/danzeeeman/ofxImGui.git> | `3ddb3519ba5687e3f45257330e36d73fecef80de` |

That fork is one commit ahead of
[jvcleave/ofxImGui](https://github.com/jvcleave/ofxImGui), and the commit
does not exist upstream — clone upstream and this does not compile. imgui
(1.77 WIP) is vendored inside the addon, not a submodule, so a shallow or
partial clone will be missing `libs/imgui/src/`.

A script clones it into your openFrameworks tree at the pinned commit:

```
./scripts/install-addons.sh              # macOS / Linux
.\scripts\install-addons.ps1             # Windows
```

Both are safe to re-run; add `--update` / `-Update` to move an existing
clone onto the pinned commit. They read `OF_ROOT` out of `config.make` to
find the tree, warn if the openFrameworks version is not 0.12.x, check
every entry in `addons.make` is accounted for, and exit non-zero if
anything is missing.

## Build and run

From this directory, inside an openFrameworks 0.12.x checkout:

```
./scripts/install-addons.sh
make -j8
cd bin/patternForge.app/Contents/MacOS/ && ./patternForge
```

PDF output uses openFrameworks' own `ofCairoRenderer`, so there is no
extra PDF dependency. Polygon offset/boolean work uses a vendored copy of
Angus Johnson's Clipper (`src/geometry/thirdparty/clipper`, Boost
licence).

## Garments

Fourteen, all built on the shared blocks in `src/garments/BlockMath.*`:

| | |
|---|---|
| **Dress** | A-line, sheath, fit & flare, empire, trumpet |
| **Shirt** | collar, placket, cuffs |
| **Pants** | straight, skinny, bootcut, wide/flare |
| **Skirt** | including tiered and paneled |
| **Coat** | |
| **Suit Jacket**, **Suit** | notched, peak and shawl lapels |
| **Overcoat**, **Peacoat**, **Trench** | |
| **Jumpsuit**, **Bodysuit** | |
| **Cape**, **Vest** | |

Each offers five graded sizes (US4–US20), graded from that garment's own
baseline rather than from one shared table — a coat assumes a fuller
bicep than a dress does, and its style defaults are set against that.

## Using it

- **Design panel** (left) — garment, size preset, and the style sliders:
  lengths, flares, necklines, armhole depth and scoop, shoulder width,
  sleeve length and hem, waist shaping and waist height, ease. Every
  piece rebuilds live. Drop a `.json` on the window (or paste its path)
  to reopen a design exported earlier.
- **Canvas** (centre) — every piece laid out left to right. Scroll to
  zoom, drag to pan; toggle cutting/sewing lines, grainlines, notches and
  marks. Turn on outline editing to drag control points directly:
  rubber-band or shift-click to move several at once, and subdivide an
  edge where you need more control. Facings are *derived*, so they re-cut
  themselves to follow a panel you have edited.
- **Pieces & checks** (right) — the cutting list and a live pass/fail
  readout of every geometric check. "Export package" writes to
  `~/Desktop/patternForge-exports/`.
- **Drape simulation** — pick a body (female/male avatar mesh, or
  capsules), a fabric, and watch it assemble. The panels start held off
  the body and apart from each other; they are drawn together and sewn
  with gravity off; then gravity is applied and the garment falls. The
  panel says which phase is running.

## What gets exported

```
<Garment>-Pattern-Package-<timestamp>/
  Full-Size.pdf              one board per piece, actual size
  A4.pdf / US-Letter.pdf     the same boards tiled for a home printer,
                             with a 10 cm calibration square
  Instructions.pdf           what this design does, measurements, cutting
                             list, construction steps, the checks table
  Pattern.dxf                AAMA/ASTM DXF (R12/AC1009) for CLO3D,
                             Marvelous Designer, Optitex, Gerber
  Muslin-*.pdf, Muslin.dxf   the toile: shell pieces only, wider seam
                             allowances, no facings or interfacing
  Geometry-and-Checks.json   {units, design, checks, new_pieces[...]}
  clo3d_build.py             CLO3D script: imports the DXF, places every
                             panel, and sews the seam list
  READ-ME.txt
  source/design-project.json  garment + measurements + style values,
                              enough to reproduce the design exactly
<Garment>-Pattern-Package-<timestamp>.zip
```

The DXF carries the AAMA layer conventions (1 boundary, 2 turn, 3 curve,
4 notch, 6 mirror, 7 grain, 8 internal, 11 drill, 13 text, 14 sew) in
millimetres, with pieces as `BLOCK`/`INSERT` pairs and mirrored twins
emitted as real second panels — so a "CUT 2 MIRRORED" piece arrives in
CAD as two panels, not one.

The **seam list** is the part of a pattern that usually survives only in
the instructions: the outlines say what the pieces *are*, and the seam
list says how they become a garment. It is what `clo3d_build.py` sews and
what the drape simulation assembles.

## Headless hooks

Everything verifiable runs without touching the GUI, which is how this is
tested:

```
PATTERNFORGE_AUTOEXPORT=1     ./patternForge   # export every garment x style combo, check all
PATTERNFORGE_SIM_TEST=1       ./patternForge   # build, sew and drape; report seam gaps and stretch
PATTERNFORGE_EDIT_TEST=multi  ./patternForge   # outline editing: 1 | facing | multi | single | subdivide
PATTERNFORGE_DUMP_SPECS=1     ./patternForge   # every garment's style parameters, as a table
```

Modifiers: `PATTERNFORGE_3D_GARMENT="Dress:sleeveLengthCm=55,silhouette=2"`
picks the garment and overrides style values; `PATTERNFORGE_DRAPE=1`,
`PATTERNFORGE_BODY_DETAIL`, `PATTERNFORGE_BODY_MALE`,
`PATTERNFORGE_FABRIC`, `PATTERNFORGE_INFLATE`, `PATTERNFORGE_ASSEMBLY`,
`PATTERNFORGE_SIM_RES` and `PATTERNFORGE_SIM_STEPS` drive the simulation;
`PATTERNFORGE_3D_SNAPSHOT=/path.png` (with `PATTERNFORGE_SNAPSHOT_FRAMES`
and `PATTERNFORGE_3D_AZIMUTH`) saves a screenshot and quits;
`PATTERNFORGE_LOAD=/path.json` opens a design.

## How it fits together

- `src/geometry/` — `Perimeter` is the only place that touches Clipper:
  densify, offset, boolean. `Piece` is a perimeter plus its marks,
  notches, drill holes, closures and cutting label. `Checks` is the named
  pass/fail list. `OutlineEdits`/`EditablePath` handle dragging.
- `src/garments/` — `BlockMath` holds the shared drafting formulas (a
  dart-free torso block, sleeve, collar, skirt, trouser block);
  `GarmentCommon` holds what every module needs identically (graded
  sizes, facings, notches, landmarks, seam construction, darts, closures,
  the waist controls); one module per garment configures them.
- `src/sim/` — `Triangulate` meshes a panel, `Avatar` loads and
  simplifies a body OBJ and answers collision and measurement queries,
  `ClothSim` places the panels, sews them and runs XPBD.
- `src/export/` — turns a finished `DesignResult` into the folder above.
- `src/ui/` — the ImGui panels, the 2D canvas, the 3D preview.

See `docs/authoring-design-json.md` for the design-file format.

## Known limitations

- **Sizes are presets**, not free-form body measurements.
- **Coat is drafted from the body block**, not remixed from the source
  PDFs in `data/references/*/source/`. Extracting a supplied pattern's
  dashed cutlines needs a PDF-parsing dependency that is not here yet;
  the `refine_coats.py` inside each reference package is the working
  example of that path.
- Blocks are **simplified, dart-free slopers** built from common ratios
  (bust/4, hip/4, …) — a generative development draft, not a fitted
  commercial pattern. Every exported manual carries that caveat. Dart
  geometry exists but is off by default: cutting a dart makes a seam span
  detour through it, which the seam-length checks then read as a
  mismatch.
- The **drape is a preview, not a material simulation**: no measured
  fabric parameters, no self-collision. It shows silhouette and where
  cloth pulls, not how silk behaves. The armhole is its weak point — a
  sleeve cap is a dome and the placement approximates it with a tube on
  the arm, so the cap seam starts open and the shoulder region carries
  most of the residual strain. Sleeved coats and shirts show it worst.
