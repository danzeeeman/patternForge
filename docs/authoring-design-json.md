# Authoring a patternForge design file

A guide for a model or script generating `design-project.json` files —
the only file patternForge reads back in.

## What the file is

patternForge does not store patterns as drawings. It stores the
**parameters a pattern is drafted from**, and re-drafts every piece on
load. A design file therefore names a garment, a size preset and a set of
style values; the outlines, seam allowances, notches, seams, fold lines
and closures are all computed.

The practical consequence: **you cannot author geometry here.** There is
no way to say "make the hem this shape" in this file. You choose a
garment and move its numbers. (The one exception is `outlineEdits`, which
records hand edits made in the app's Bézier editor — see the last
section. Do not author those by hand.)

Round trip: the app writes this file into every exported package at
`<Package>/source/design-project.json`, and loads it back via
**Load design-project.json…** in the Design panel.

## Schema

```json
{
  "garmentKey": "Peacoat",
  "sizePreset": "US4",
  "style": {
    "lengthCm": 78.0,
    "wrapCm": 9.0,
    "lapel": 0.0
  }
}
```

| field | required | type | meaning |
|---|---|---|---|
| `garmentKey` | **yes** | string | Which garment. Must match a key in the table below **exactly**. |
| `sizePreset` | no | string | Name of a size preset, e.g. `"US4"`. |
| `style` | no | object | Style parameter → number. Any subset. |
| `outlineEdits` | no | object | Hand edits to piece outlines. Omit unless copying from an export. |
| `sizeMeasurementsCm` | no | object | **Written on export, never read on load.** See below. |

### Rules the loader actually applies

These come from `ofApp::doLoadDesign`, and each one bites differently:

- **An unknown `garmentKey` fails the whole load** with
  `Load failed: unknown garment "X"`. Nothing is applied. This is the one
  error you get told about.
- **An unknown `sizePreset` is ignored silently** — the garment keeps
  preset index 0. With one preset per garment today, any wrong name
  behaves exactly like the right one, so a typo here is invisible.
- **Every style key is applied first from the garment's defaults, then
  overwritten by yours.** So a partial `style` object is fine and
  normal: omit a key and you get that garment's default, which is
  usually what you want.
- **Unknown style keys are accepted and ignored.** A misspelled key is
  silently a no-op — you get a valid pattern that is not the one you
  described. There is no warning. Spell them from the tables below.
- **Non-numeric style values are skipped.** Write `0.0`, not `"0"` and
  not `false`.
- **Values are not clamped on load.** The min/max in the tables are the
  GUI slider bounds, not validation. A value outside them reaches the
  drafting code and may produce a piece that fails its checks or is
  geometrically impossible. Stay inside the range unless you intend to
  explore past it and are reading the checks.

### `sizeMeasurementsCm` is output only

The app writes the resolved body measurements into the file for the
reader's benefit, then **ignores them on load** — measurements come from
the named preset, not from the file. Editing those numbers changes
nothing. This is deliberate (patternForge uses fixed size presets rather
than free measurement entry), but it does mean the file contains
authoritative-looking numbers that are not inputs. Do not try to draft a
custom body by editing them.

## Choosing values

**Combo parameters are indices stored as floats.** A `choices` row means
the value is `0.0`, `1.0`, … selecting from that list. `"lapel": 1.0` is
a peak lapel. Out-of-range indices are clamped by the modules.

**Lengths are centimetres from the nape** (the back neck bone), not from
the waist or the floor. `lengthCm: 78` on a coat means 78 cm measured
down the back from the nape — roughly hip length, not knee length.

**Ease is added to the body measurement**, and is a total circumference:
`easeCm: 18` means the garment measures 18 cm more around than the body.
Negative ease is meaningful only on `Bodysuit`, which is drafted for
knit.

## Verifying what you wrote

Do not trust a file because it loaded. Load it and read the **Pieces &
checks** panel: every garment runs geometric checks (seams that must be
the same length, notches that must meet, closures that must sit on
cloth), and a design can load cleanly and still be unsewable.

Headless, without the GUI:

```sh
# every garment and every discrete style option, with checks
PATTERNFORGE_AUTOEXPORT=1 ./patternForge

# one specific combination
PATTERNFORGE_3D_GARMENT="Peacoat:lapel=1,lengthCm=92" PATTERNFORGE_AUTOEXPORT=1 ./patternForge

# the authoritative parameter list, as JSON (this document's tables come from it)
PATTERNFORGE_DUMP_SPECS=1 ./patternForge
```

`PATTERNFORGE_3D_GARMENT` takes `Key:key=value,key=value` and is the
fastest way to test a design without writing a file at all.

## Garments and their parameters

Generated from `PATTERNFORGE_DUMP_SPECS=1`. Regenerate rather than
trusting this copy if the app has moved on.

### `Dress` — Dress

Size presets: `US4`

| key | range | default | notes |
|---|---|---|---|
| `silhouette` | 0–4 | 0 | 0 = A-Line / 1 = Sheath / Column / 2 = Fit & Flare / 3 = Empire Waist / 4 = Trumpet / Mermaid |
| `hemLength` | 80–450 | 100 | Length (nape to hem, cm) |
| `hemFlareCm` | 0–120 | 10 | Hem flare (cm) |
| `neckDepthCm` | 4–48 | 7 | Front neckline depth (cm) |
| `neckWidthCm` | 8–24 | 12 | Front neckline width (cm, capped at the shoulder) |
| `backNeckDepthCm` | 2–48 | 2 | Back neckline depth (cm) |
| `backNeckWidthCm` | 8–24 | 12 | Back neckline width (cm, capped at the shoulder) |
| `armholeDepthCm` | 16–40 | 22 | Armhole depth below the shoulder (cm) |
| `shoulderWidthCm` | 8–22 | 12.5 | Shoulder width (cm) |
| `armholeScoop` | 0.5–1.6 | 1 | Armhole scoop (1 = standard) |
| `sleeveLengthCm` | 0–180 | 0 | Sleeve length (0 = sleeveless), cm |
| `sleeveHemCm` | 10–180 | 20 | Sleeve hem circumference (cm) |
| `sleeveFlareFrom` | 0.3–1 | 1 | Sleeve flare starts at (1 = straight taper) |
| `easeCm` | 0–48 | 6 | Overall ease added to the body (cm) |

### `Coat` — Coat

Size presets: `US4`

| key | range | default | notes |
|---|---|---|---|
| `silhouette` | 0–2 | 0 | 0 = A-Line / 1 = Tailored / 2 = Cocoon |
| `front` | 0–1 | 0 | 0 = Center seam (opens for closure) / 1 = No center seam (cut on fold) |
| `coatLength` | 90–390 | 115 | Length (nape to hem, cm) |
| `hemFlareCm` | 0–120 | 14 | Hem flare (total circumference add, cm) |
| `armholeDepthCm` | 16–40 | 22 | Armhole depth below the shoulder (cm) |
| `shoulderWidthCm` | 8–22 | 13 | Shoulder width (cm) |
| `armholeScoop` | 0.5–1.6 | 1 | Armhole scoop (1 = standard) |
| `sleeveLengthCm` | 45–198 | 58 | Sleeve length (cm) |
| `cuffWidthCm` | 24–180 | 40 | Finished sleeve-hem circumference (cm) |
| `sleeveFlareFrom` | 0.3–1 | 1 | Sleeve flare starts at (1 = straight taper) |
| `neckDepthCm` | 4–48 | 8 | Front neckline depth (cm) |
| `neckWidthCm` | 8–24 | 12 | Front neckline width (cm, capped at the shoulder) |
| `backNeckDepthCm` | 2–48 | 2.5 | Back neckline depth (cm) |
| `backNeckWidthCm` | 8–24 | 12 | Back neckline width (cm, capped at the shoulder) |
| `collarDepthCm` | 4–36 | 7 | Collar depth (cm) |
| `easeCm` | 10–78 | 16 | Overall ease added to the body (cm) |

### `Overcoat` — Overcoat

Size presets: `US4`

| key | range | default | notes |
|---|---|---|---|
| `lapel` | 0–2 | 0 | 0 = Notched / 1 = Peak / 2 = Shawl |
| `lengthCm` | 60–330 | 112 | Length (nape to hem, cm) |
| `wrapCm` | 1–30 | 3 | Front extension past center front (cm) |
| `lapelWidthCm` | 3–27 | 9 | Lapel width (cm) |
| `breakYCm` | 26–120 | 46 | Lapel break point (cm below the nape) |
| `collarDepthCm` | 4–36 | 9 | Collar depth (cm) |
| `facingWidthCm` | 5–36 | 12 | Front facing width at the hem (cm) |
| `hemFlareCm` | 0–120 | 12 | Hem flare (total circumference add, cm) |
| `ventLengthCm` | 0–60 | 30 | Back vent length (cm, 0 = no vent) |
| `armholeDepthCm` | 16–42 | 24 | Armhole depth below the shoulder (cm) |
| `shoulderWidthCm` | 8–24 | 13.5 | Shoulder width (cm) |
| `sleeveLengthCm` | 45–198 | 62 | Sleeve length (cm) |
| `cuffWidthCm` | 24–180 | 36 | Finished sleeve-hem circumference (cm) |
| `easeCm` | 8–78 | 22 | Overall ease added to the body (cm) |

### `Peacoat` — Peacoat

Size presets: `US4`

| key | range | default | notes |
|---|---|---|---|
| `lapel` | 0–2 | 0 | 0 = Notched / 1 = Peak / 2 = Shawl |
| `lengthCm` | 60–330 | 78 | Length (nape to hem, cm) |
| `wrapCm` | 1–30 | 9 | Front extension past center front (cm) |
| `lapelWidthCm` | 3–27 | 11 | Lapel width (cm) |
| `breakYCm` | 26–120 | 44 | Lapel break point (cm below the nape) |
| `collarDepthCm` | 4–36 | 12 | Collar depth (cm) |
| `facingWidthCm` | 5–36 | 14 | Front facing width at the hem (cm) |
| `hemFlareCm` | 0–120 | 4 | Hem flare (total circumference add, cm) |
| `ventLengthCm` | 0–60 | 0 | Back vent length (cm, 0 = no vent) |
| `armholeDepthCm` | 16–42 | 24 | Armhole depth below the shoulder (cm) |
| `shoulderWidthCm` | 8–24 | 13.5 | Shoulder width (cm) |
| `sleeveLengthCm` | 45–198 | 60 | Sleeve length (cm) |
| `cuffWidthCm` | 24–180 | 34 | Finished sleeve-hem circumference (cm) |
| `easeCm` | 8–78 | 18 | Overall ease added to the body (cm) |

### `Trench` — Trench Coat

Size presets: `US4`

| key | range | default | notes |
|---|---|---|---|
| `lapel` | 0–2 | 0 | 0 = Notched / 1 = Peak / 2 = Shawl |
| `lengthCm` | 60–330 | 116 | Length (nape to hem, cm) |
| `wrapCm` | 1–30 | 9 | Front extension past center front (cm) |
| `lapelWidthCm` | 3–27 | 10 | Lapel width (cm) |
| `breakYCm` | 26–120 | 48 | Lapel break point (cm below the nape) |
| `collarDepthCm` | 4–36 | 10 | Collar depth (cm) |
| `facingWidthCm` | 5–36 | 14 | Front facing width at the hem (cm) |
| `hemFlareCm` | 0–120 | 16 | Hem flare (total circumference add, cm) |
| `ventLengthCm` | 0–60 | 34 | Back vent length (cm, 0 = no vent) |
| `armholeDepthCm` | 16–42 | 24 | Armhole depth below the shoulder (cm) |
| `shoulderWidthCm` | 8–24 | 13.5 | Shoulder width (cm) |
| `sleeveLengthCm` | 45–198 | 62 | Sleeve length (cm) |
| `cuffWidthCm` | 24–180 | 36 | Finished sleeve-hem circumference (cm) |
| `easeCm` | 8–78 | 22 | Overall ease added to the body (cm) |
| `stormFlap` | 0–1 | 1 | 0 = No / 1 = Yes |
| `belt` | 0–1 | 1 | 0 = No / 1 = Yes |
| `epaulettes` | 0–1 | 1 | 0 = No / 1 = Yes |
| `beltWidthCm` | 2–15 | 5 | Belt width (cm) |

### `SuitJacket` — Suit Jacket

Size presets: `US4`

| key | range | default | notes |
|---|---|---|---|
| `lapel` | 0–2 | 0 | 0 = Notched / 1 = Peak / 2 = Shawl |
| `back` | 0–1 | 0 | 0 = Center back seam / 1 = No center back seam (cut on fold) |
| `jacketLengthCm` | 55–210 | 70 | Length (nape to hem, cm) |
| `lapelWidthCm` | 3–24 | 8 | Lapel width (cm) |
| `breakYCm` | 26–120 | 46 | Lapel break point (cm below the nape) |
| `wrapCm` | 1–24 | 2 | Front extension past center front (cm) |
| `facingWidthCm` | 5–30 | 9 | Front facing width at the hem (cm) |
| `armholeDepthCm` | 16–40 | 22 | Armhole depth below the shoulder (cm) |
| `shoulderWidthCm` | 8–22 | 13 | Shoulder width (cm) |
| `armholeScoop` | 0.5–1.6 | 1 | Armhole scoop (1 = standard) |
| `sleeveLengthCm` | 45–198 | 60 | Sleeve length (cm) |
| `cuffWidthCm` | 20–180 | 28 | Finished sleeve-hem circumference (cm) |
| `backNeckDepthCm` | 1–20 | 2.5 | Back neckline depth (cm) |
| `backNeckWidthCm` | 8–24 | 12 | Back neckline width (cm, capped at the shoulder) |
| `hemFlareCm` | 0–90 | 0 | Hem flare (total circumference add, cm) |
| `easeCm` | 4–60 | 10 | Overall ease added to the body (cm) |

### `Suit` — Pant Suit

Size presets: `US4`

| key | range | default | notes |
|---|---|---|---|
| `lapel` | 0–2 | 0 | 0 = Notched / 1 = Peak / 2 = Shawl |
| `legStyle` | 0–3 | 0 | 0 = Straight / 1 = Tapered / 2 = Wide / 3 = Flared |
| `back` | 0–1 | 0 | 0 = Center back seam / 1 = No center back seam (cut on fold) |
| `jacketLengthCm` | 55–210 | 70 | Jacket length (nape to hem, cm) |
| `lapelWidthCm` | 3–24 | 8 | Lapel width (cm) |
| `breakYCm` | 26–120 | 46 | Lapel break point (cm below the nape) |
| `wrapCm` | 1–24 | 2 | Front extension past center front (cm) |
| `facingWidthCm` | 5–30 | 9 | Front facing width at the hem (cm) |
| `armholeDepthCm` | 16–40 | 22 | Armhole depth below the shoulder (cm) |
| `shoulderWidthCm` | 8–22 | 13 | Shoulder width (cm) |
| `sleeveLengthCm` | 45–198 | 60 | Sleeve length (cm) |
| `cuffWidthCm` | 20–180 | 28 | Finished sleeve-hem circumference (cm) |
| `easeCm` | 4–60 | 10 | Jacket ease added to the body (cm) |
| `inseamCm` | 50–130 | 76 | Trouser inseam length (cm) |
| `riseCm` | 22–48 | 28 | Trouser rise, waist to crotch (cm) |
| `legWidthCm` | 26–120 | 44 | Trouser hem circumference (cm) |
| `trouserEaseCm` | 0–30 | 6 | Trouser hip ease (cm) |

### `Jumpsuit` — Jumpsuit

Size presets: `US4`

| key | range | default | notes |
|---|---|---|---|
| `legStyle` | 0–3 | 0 | 0 = Straight / 1 = Tapered / 2 = Wide / 3 = Flared |
| `sleeveLengthCm` | 0–198 | 0 | Sleeve length (cm, 0 = sleeveless) |
| `neckDepthCm` | 4–48 | 10 | Front neckline depth (cm) |
| `neckWidthCm` | 8–26 | 13 | Front neckline width (cm) |
| `backNeckDepthCm` | 1–48 | 2.5 | Back neckline depth (cm) |
| `armholeDepthCm` | 16–40 | 21 | Armhole depth below the shoulder (cm) |
| `shoulderWidthCm` | 8–22 | 12.5 | Shoulder width (cm) |
| `inseamCm` | 40–130 | 76 | Inseam length (cm) |
| `riseCm` | 22–48 | 28 | Rise, waist to crotch (cm) |
| `legWidthCm` | 26–150 | 40 | Finished hem circumference (cm) |
| `bodiceEaseCm` | 2–40 | 8 | Bodice ease (cm) |
| `hipEaseCm` | 0–36 | 5 | Hip ease (cm) |

### `Bodysuit` — Bodysuit

Size presets: `US4`

| key | range | default | notes |
|---|---|---|---|
| `sleeveLengthCm` | 0–70 | 0 | Sleeve length (cm, 0 = sleeveless) |
| `neckDepthCm` | 4–40 | 12 | Front neckline depth (cm) |
| `neckWidthCm` | 8–26 | 14 | Front neckline width (cm) |
| `backNeckDepthCm` | 1–40 | 4 | Back neckline depth (cm) |
| `armholeDepthCm` | 14–34 | 20 | Armhole depth below the shoulder (cm) |
| `shoulderWidthCm` | 4–20 | 10 | Shoulder width (cm) |
| `riseYCm` | 52–96 | 68 | Crotch depth below the nape (cm) |
| `legOpeningYCm` | 44–90 | 58 | Leg cut height at the side (cm below the nape) |
| `crotchWidthCm` | 4–20 | 9 | Crotch width (cm) |
| `stretchEaseCm` | -20–12 | -6 | Ease (cm -- negative for knit) |

### `Shirt` — Shirt

Size presets: `US4`

| key | range | default | notes |
|---|---|---|---|
| `fit` | 0–3 | 1 | 0 = Slim / 1 = Classic / 2 = Relaxed / 3 = Oversized |
| `front` | 0–1 | 0 | 0 = Button-front (placket) / 1 = Pullover (cut on fold) |
| `shirtLength` | 60–270 | 72 | Length (nape to hem, cm) |
| `armholeDepthCm` | 16–40 | 22 | Armhole depth below the shoulder (cm) |
| `shoulderWidthCm` | 8–22 | 12.5 | Shoulder width (cm) |
| `armholeScoop` | 0.5–1.6 | 1 | Armhole scoop (1 = standard) |
| `sleeveLengthCm` | 20–198 | 58 | Sleeve length (cm) |
| `sleeveHemCm` | 10–180 | 24 | Sleeve hem circumference (cm) |
| `sleeveFlareFrom` | 0.3–1 | 1 | Sleeve flare starts at (1 = straight taper) |
| `neckDepthCm` | 4–48 | 7.5 | Front neckline depth (cm) |
| `neckWidthCm` | 8–24 | 12 | Front neckline width (cm, capped at the shoulder) |
| `backNeckDepthCm` | 2–48 | 2.2 | Back neckline depth (cm) |
| `backNeckWidthCm` | 8–24 | 12 | Back neckline width (cm, capped at the shoulder) |
| `collarDepthCm` | 2.5–18 | 3.5 | Collar band depth (cm) |
| `easeCm` | 6–72 | 14 | Overall ease added to the body (cm) |
| `hemFlareCm` | 0–36 | 2 | Hem flare below the hip (cm) |

### `Pants` — Pants

Size presets: `US4`

| key | range | default | notes |
|---|---|---|---|
| `legStyle` | 0–3 | 0 | 0 = Straight / 1 = Skinny / 2 = Bootcut / 3 = Wide-Leg / Flare |
| `inseamCm` | 50–130 | 74 | Inseam length (cm) |
| `riseCm` | 22–48 | 27 | Rise, waist to crotch (cm) |
| `legWidthCm` | 26–180 | 34 | Finished ankle-hem circumference (cm) |
| `waistEaseCm` | 0–24 | 2 | Waist ease (cm) |
| `hipEaseCm` | 0–42 | 4 | Hip ease (cm) |

## `outlineEdits` (do not author by hand)

Records points moved in the app's Bézier outline editor, keyed by piece
code:

```json
"outlineEdits": {
  "F": {
    "points": [ {"t": 0.42, "dx": 1.5, "dy": 0.0, "handles": false,
                 "ix": 0, "iy": 0, "ox": 0, "oy": 0, "added": false} ],
    "removed": [0.61]
  }
}
```

`t` is a position **around the generated outline**, 0–1, not a point
index — which is what lets an edit survive a slider move: the outline is
re-drafted, then each edit is re-attached at its `t` and offset by
`dx`/`dy`. Authoring these blind means guessing where `t` lands on a
curve you have not seen. Copy them from an export, or make them in the
editor.

## Worked example

A knee-length double-breasted overcoat with a peak lapel, in one file:

```json
{
  "garmentKey": "Overcoat",
  "sizePreset": "US4",
  "style": {
    "lapel": 1.0,
    "lengthCm": 105.0,
    "wrapCm": 9.0,
    "lapelWidthCm": 11.0,
    "breakYCm": 44.0,
    "ventLengthCm": 32.0,
    "easeCm": 24.0
  }
}
```

Note what is *not* there: no measurements, no piece outlines, no seam
allowances, no notch positions, no button spacing. All of it is drafted.
Setting `wrapCm` to 9 is what makes this coat double-breasted — the
module reads that width and lays out two button columns instead of one.
