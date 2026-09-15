#pragma once
#include "Perimeter.h"
#include <string>
#include <vector>

namespace pf {

// A registration mark on a piece: a label plus a position in the piece's
// own local centimeter space (same space as `sewing`/`cutting`).
struct Mark {
    // What the mark IS, which decides the CAD layer it exports on -- and
    // they are not interchangeable. A notch is a cut in the piece's edge
    // that pattern CAD matches against the notch it sews to; an interior
    // construction line (a lapel roll line, say) is not a notch and must
    // not be exported as one, or CLO3D tries to align a seam to a point
    // sitting in the middle of the cloth.
    enum Kind {
        Notch,     // on the boundary: a seam-matching point (layer 4)
        Internal,  // an interior construction line or point (layer 8)
        Drill,     // an interior drill hole, e.g. a dart end (layer 11)
        // A named corner of the outline -- neck point, underarm, hem
        // corner. Not a mark to cut or draw, so it goes in no CAD layer:
        // it exists so a seam can say WHERE on the edge it runs, turning
        // "join front to back at the side seam" into two edge spans a
        // machine can actually sew.
        Landmark,
    };

    std::string label;
    Pt pos;
    Kind kind = Notch;
};

// A line drawn on a piece that is not part of its outline.
//
// There are several kinds and they are not interchangeable -- each one
// tells whoever cuts and sews the piece to do something different, and
// confusing them ruins cloth:
//
//   CutOnFold   The fabric fold the piece is laid against. Cut it and one
//               panel becomes two half-panels; the cloth is gone.
//   Fold        A fold made later, in construction: a lapel's roll line.
//               Pressed, never cut.
//   Cut         An opening cut INSIDE the panel, with no edge of its own
//               -- a cape's arm slit.
//   SeamOpen    A run of a seam deliberately left unsewn: the slit you
//               walk in, or the opening a zip goes into. Nothing is cut
//               and nothing is folded; a seam just stops early.
struct MarkedLine {
    enum Kind { CutOnFold, Fold, Cut, SeamOpen };

    std::string label;
    Pt a, b;
    Kind kind = Fold;

    bool isCutOnFold() const { return kind == CutOnFold; }
    bool isCutLine() const { return kind == Cut; }
};

// Where a closure goes: a button, its buttonhole, a zipper run, a buckle.
//
// These are positions on the PATTERN, not decoration. A button column that
// is not marked gets spaced by eye at the machine, and the coat hangs
// crooked; a fly zip needs its run marked or the topstitching has nothing
// to follow. They are also what pattern CAD places as drill holes.
struct Closure {
    enum Kind {
        Button,      // a button sewn at this point
        Buttonhole,  // the hole it passes through, on the opposing piece
        Zipper,      // a run from a to b, not a point
        Buckle,      // hardware at this point
        Snap,
    };

    Kind kind = Button;
    Pt a, b;                // a is the position; b is the far end of a zipper run
    double sizeMm = 20.0;   // button/buckle size, for the cutting list
    std::string label;

    bool isRun() const { return kind == Zipper; }
};

// Where this flat panel hangs on the body before anything is simulated.
//
// A pattern is 2D, but a simulator cannot drape one until the panels are
// arranged around a body -- two panels started on top of each other
// simulate into a knot. So the placement belongs WITH the pattern: the
// drafting code knows a front goes in front and a sleeve hangs off the
// shoulder at an angle, and nothing downstream can work that out from the
// outlines alone. (GarmentCode does the same thing, giving every panel a
// translation and rotation.)
//
// Centimetres, body-centred: x to the wearer's left, y up from the nape
// datum (so mostly negative), z toward the front. `rotationDeg` is applied
// about x, then y, then z, and turns a flat panel to face outward.
struct Placement {
    bool set = false;
    float x = 0.f, y = 0.f, z = 0.f;
    float rotX = 0.f, rotY = 0.f, rotZ = 0.f;
    std::string note;           // why it sits there, for the reader
};

// One flat pattern piece. `sewing` is the perimeter the piece is actually
// sewn along; `cutting` is `sewing` offset outward by `seamAllowanceCm`
// (or identical to `sewing` when seamAllowanceCm is 0, e.g. finished-edge
// templates). This mirrors the Python pipeline's sewing_cm/cutting_cm.
struct Piece {
    std::string code;    // short id, e.g. "B", "F6", "S2"
    std::string name;    // human name, e.g. "Revised back"
    Ring sewing;
    Ring cutting;
    float seamAllowanceCm = 1.0f;
    std::string cutQty = "CUT 2 MIRRORED / SHELL";
    std::string note   = "See instructions.";
    std::vector<Mark> marks;
    std::vector<MarkedLine> lines;
    std::vector<Closure> closures;
    Placement place;

    // True when x=0 is a fabric fold (piece cut "ON FOLD"), not a seam --
    // e.g. a coat front with no center opening. A fold isn't sewn, so it
    // gets no seam allowance: offsetting the whole ring uniformly (as for
    // every other edge) would push the fold line itself out past x=0 and
    // silently add overlap where the two mirrored halves meet.
    bool foldAtCF = false;

    // True when this outline was edited by hand, so the piece no longer
    // follows the style sliders and the exports can say so.
    bool handEdited = false;

    // Set when this piece is CUT FROM another piece rather than drafted on
    // its own -- a neckline facing, cut from the top band of the panel it
    // finishes. The two are sewn to each other along that neckline, so the
    // facing is re-derived whenever the panel changes (a slider moved, a
    // Bezier point dragged); editing a derived piece directly would only be
    // overwritten on the next rebuild.
    std::string derivedFromCode;   // "" when the piece stands on its own
    float facingMarginCm = 0.f;    // how far below the neckline the band runs

    bool isDerived() const { return !derivedFromCode.empty(); }

    // How many of this piece the garment physically contains, and whether
    // they come in mirrored pairs. Parsed once from `cutQty` in
    // common::finish(), because the cutting list is prose and a CAD export
    // needs a number: "CUT 2 MIRRORED" is ONE pattern piece but TWO
    // panels, and a file carrying one of them builds half a garment.
    //
    // A piece cut on the fold stays at 1 -- it is exported as the whole
    // panel already, with the fold mirrored into it.
    int cutCount = 1;
    bool cutMirrored = false;

    // Whether this piece belongs in a fitting muslin -- the test garment
    // sewn in cheap cloth to check fit before cutting the real thing.
    // Collars, cuffs, plackets, waistbands and facings finish a garment;
    // they don't change how it hangs on a body, so a muslin leaves them
    // out. Anything that carries the fit -- fronts, backs, sleeves,
    // skirts, legs, gussets -- stays in.
    bool inMuslin = true;

    bool hasSeamAllowance() const { return seamAllowanceCm > 1e-4f; }

    // Recompute `cutting` from `sewing` + `seamAllowanceCm`. Call after
    // building or editing `sewing`.
    void resolveCutting() {
        if (!hasSeamAllowance()) { cutting = sewing; return; }
        Ring offset = geo::offset(sewing, seamAllowanceCm);
        if (foldAtCF) {
            auto clipped = geo::clipToBox(offset, 0.0, -1e6, 1e6, 1e6);
            cutting = clipped.empty() ? offset : clipped[0];
        } else {
            cutting = offset;
        }
    }
};

} // namespace pf
