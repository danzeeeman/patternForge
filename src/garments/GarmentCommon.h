#pragma once
#include "GarmentModule.h"
#include "BlockMath.h"

// Small pieces of bookkeeping every GarmentModule needs identically, so
// each module's build() can stay focused on the garment-specific geometry.

namespace pf { namespace common {

// A partly-built garment: the pieces and seams a shared drafting routine
// produced, before a module wraps them in titles and prose. Two garments
// that are "the same jacket" can then literally share one routine rather
// than two copies that drift.
struct Draft {
    std::vector<Piece> pieces;
    std::vector<Seam> seams;
    Checks checks;
    double armholeY = 0.0;
    double shoulderX = 0.0;
    std::string styleNote;
};

// The graded size set every garment offers.
//
// One preset per garment was a development convenience that became a
// limitation: nothing could be drafted for anyone but a US4, and a
// pattern app that only makes one size is a demo. These are graded the
// way a real size run is -- the girths grow fastest, the lengths slowly,
// because people of different widths are not proportionally taller.
std::vector<SizePreset> standardSizes();

// The same run, graded from a garment's OWN baseline measurements.
//
// Modules had each tuned their block against slightly different figures --
// a coat assumes a fuller bicep than a dress does -- and their style
// defaults are set against those. Replacing them all with one table broke
// those assumptions. This keeps each module's baseline as its smallest
// size and grades upward from it, so bigger sizes arrive without
// re-tuning anything.
std::vector<SizePreset> gradeFrom(const SizePreset& base);

// Flags a piece whose sewing perimeter is degenerate (near-zero area) or
// self-intersects -- a build bug, not a design choice -- instead of
// letting it reach the canvas or exporter silently.
void addSanityChecks(Checks& checks, const std::vector<Piece>& pieces);

// One cutting-list row per piece, "<code>: <cutQty>; <name>" -- the
// generic equivalent of the coats' per-piece cutting list rows.
std::vector<std::pair<std::string, std::string>> defaultCuttingList(const std::vector<Piece>& pieces);

// Front and back panels are sewn to each other down the side seam, so
// their outer edge must sit at the same width at every height. This
// compares just that edge (not the whole outline), so an independent
// neckline, placket or center-back cut doesn't register as a mismatch.
// `frontOffsetX` is how far the front panel has been shifted off true
// center front -- by a button placket or extension -- since that shift
// moves its side seam out with it and would otherwise read as a mismatch.
void expectSideSeamsMatch(Checks& checks, const std::string& name,
                          const Ring& front, const Ring& back,
                          double yFrom, double yTo, double tolCm,
                          double frontOffsetX = 0.0);

// Finish a piece: resolve its cutting line from sewing + seam allowance.
// Call once a piece's `sewing` ring and `seamAllowanceCm` are set.
void finish(Piece& piece);

// The waist controls, shared by every garment with a torso.
//
// The waist was the one major line of a garment with no control at all:
// it came out as a fixed fraction of the overall ease, so the only way to
// change it was to change the ease everywhere. These two sliders are the
// same on every module deliberately -- the waist means the same thing on
// a dress as on a coat, and a control that moves between garments is one
// the user has to relearn each time.
//
// Append to a module's styleParamSpecs(), then call applyWaist().
std::vector<StyleParamSpec> waistSpecs();
void applyWaist(block::TorsoParams& tp, const StyleParams& style);

// A neckline facing: the band of the panel lying within `marginCm` below
// the neckline, which is the part turned to the inside and stitched down.
//
// A facing is DERIVED from its panel, never drafted alongside it. The two
// are sewn to each other along the neckline, so if the panel's neckline
// moves -- by a slider or by a dragged Bezier point -- and the facing
// keeps the older curve, they no longer meet. Re-deriving is what keeps
// them agreeing, which is why this is a function and not a one-off copy.
Ring facingBand(const Ring& panel, double marginCm);

// The one way that derivation can still go wrong: an edited neckline that
// dives deeper than the band, leaving the facing's lower edge cutting
// across the neckline it is supposed to finish.
void expectFacingCoversNeckline(Checks& checks, const std::string& name,
                                const Ring& panel, const Ring& facing, double minClearCm);

// A lapel stands off its roll line by about its own width, on the side
// away from the body -- that is what a lapel IS, and it holds however the
// coat buttons. Measuring how far it reaches past the FRONT EDGE instead
// only works single-breasted: widen the button wrap and the roll line
// moves out with it, so a perfectly good lapel falls inside the front edge
// and an honest check calls it missing.
void expectLapelStandsOff(Checks& checks, const std::string& name,
                          const Ring& front, Pt breakPt, Pt neckPt, double lapelWidthCm);

// --- Darts -------------------------------------------------------------
//
// A dart is a wedge taken OUT of a panel and sewn shut. It is how flat
// cloth is made to fit a body that is not flat: the cloth that would have
// bagged over the waist is removed, and closing the wedge cones the panel
// into three dimensions.
//
// Every block in this app is otherwise dart-free, which is why they all
// carry that caveat in their instructions. A dart-free block can be eased
// or gathered onto a seam, but it cannot follow a shape closely.
//
// The V is the shape GarmentCode uses: two legs of equal length meeting at
// an apex, cut into an edge. Its depth follows from the width and the leg
// length, depth = sqrt(side^2 - (width/2)^2).
struct Dart {
    Pt legA, apex, legB;
    bool ok = false;
};

// Cuts a dart into `piece` at `distanceCm` along its outline, measured
// from `from` heading toward `toward`. The apex points into the panel.
//
// Marks the legs as notches (they are matched when the dart is sewn) and
// the apex as a drill hole, which is exactly what drill marks are for on a
// pattern -- you cannot notch a point in the middle of the cloth.
Dart cutDart(Piece& piece, Pt from, Pt toward, double distanceCm,
             double widthCm, double depthCm, const std::string& label);

// A dart has to fit the edge it is cut into and stay inside the panel.
void expectDartsValid(Checks& checks, const std::vector<Piece>& pieces);

// Records whether a dart that was ASKED for actually got cut. Without
// this a dart that fails its geometry checks vanishes silently and the
// garment quietly reverts to the dart-free block it was before.
void expectDartWasCut(Checks& checks, const std::string& name, const Dart& d);

// --- Closures ---------------------------------------------------------

// A column of evenly spaced buttons down `x`, from `fromY` to `toY`.
// Marked on the pattern because a column spaced by eye at the machine is
// the reason a coat front hangs crooked.
void addButtonColumn(Piece& piece, double x, double fromY, double toY, int count,
                     double diameterMm, const std::string& label);

// The buttonholes that column passes through. On a garment cut as a
// mirrored pair the holes are on the opposing front, at the same heights.
void addButtonholeColumn(Piece& piece, double x, double fromY, double toY, int count,
                         double diameterMm, const std::string& label);

void addZipper(Piece& piece, Pt from, Pt to, const std::string& label);

// A run of seam left unsewn -- the slit you walk in, or the opening a zip
// is set into. Marks the line AND puts a notch at its top end, because
// that notch is what tells the machinist where to stop stitching.
void addSeamOpening(Piece& piece, Pt hemEnd, Pt topEnd, const std::string& label);

// A garment with no opening has to pass over the body to be put on. That
// is a real constraint and the one that decides whether "pull-on" is an
// option at all: the narrowest circumference anywhere between the armhole
// and the hem must clear the widest part it has to travel over.
//
// `front` and `back` are half-panels cut on the fold, so the garment's
// circumference at a row is twice their combined half-widths.
void expectPullsOnOverBody(Checks& checks, const std::string& name,
                           const Ring& front, const Ring& back,
                           double yFrom, double yTo, double mustClearCm);
void addBuckle(Piece& piece, Pt at, double widthCm, const std::string& label);

// Every closure has to sit somewhere it can actually be attached, and
// what that means differs by kind:
//
//   A BUTTON needs cloth under it, clear of the edge -- one marked at the
//   very edge has nothing to sew through, which is why a coat that opens
//   needs a button extension past center front in the first place.
//
//   A ZIPPER is sewn INTO a seam, so lying on the boundary is correct.
//   What it must never lie on is a FOLD: a fold has no seam to open, so a
//   zip marked down one cannot be inserted at all.
void expectClosuresOnCloth(Checks& checks, const std::vector<Piece>& pieces,
                           double buttonClearanceCm = 0.8);

// --- Balance notches -------------------------------------------------
//
// A notch is a mark cut into a piece's edge, and its whole purpose is that
// the notch on one piece meets the notch on the piece it is sewn to. Two
// seams of equal length can still be sewn wrong -- stretched, eased or
// shifted -- and notches are what stop that, by hand and in pattern CAD
// alike: CLO3D matches notch to notch when it sews panels together.
//
// So a notch only means something as one of a PAIR, placed at the same
// arc length from the landmark the two seams share (the underarm, the
// crotch point, the hem). Placing one by eyeball on each piece would give
// two marks that happen to be near each other, which is not the same
// thing at all.

// Walks `distanceCm` along the outline from the vertex nearest `from`,
// heading the way that leads toward `toward`. Returns the point reached,
// clamped to the end of the outline.
Pt pointAlongOutline(const Ring& r, Pt from, Pt toward, double distanceCm);

// The arc length actually travelled along the outline from `from` to the
// vertex nearest `at`, going the way that leads toward `toward`. Used to
// measure back what a placed notch really came out at, so a notch that hit
// the end of a short seam is visible rather than silently misplaced.
double arcDistanceAlongOutline(const Ring& r, Pt from, Pt toward, Pt at);

// Places one notch on `piece` at `distanceCm` along its edge from `from`
// toward `toward`, and returns where it landed.
Pt addSeamNotch(Piece& piece, const std::string& label,
                Pt from, Pt toward, double distanceCm);

// Checks that a pair of notches meant to meet actually sit at the same
// distance along their respective seams -- which is the only property that
// makes them useful. Catches a notch that ran off the end of a seam
// shorter than the distance asked for.
void expectNotchesMeet(Checks& checks, const std::string& name,
                       const Ring& ringA, Pt fromA, Pt towardA, Pt notchA,
                       const Ring& ringB, Pt fromB, Pt towardB, Pt notchB,
                       double tolCm);

// Two edges of equal length can still sew wrong.
//
// If a seam carries notches, those notches divide it into runs, and the
// runs have to line up in the same PROPORTIONS on both sides -- a notch a
// third of the way down one edge meeting one halfway down the other pulls
// the seam askew even though the two edges measure the same. (The idea is
// GarmentCode's: it matches interfaces by comparing the fractional
// positions along each edge, not just the totals.)
//
// This compares where each seam's notches fall as fractions of that edge,
// which is what the two sides are actually matched by when sewn.
void expectSeamFractionsMatch(Checks& checks, const std::vector<Piece>& pieces,
                              const std::vector<Seam>& seams, double tolFraction = 0.06);

// The notches a set-in-sleeve garment needs, and the checks that they
// meet: one pair down the side seam where front sews to back, and a pair
// on each armhole where the sleeve cap sews in. Landmarks are taken from
// the pieces themselves, so a module only has to say where its armhole
// and shoulder sit -- which it already knows.
void addSetInSleeveNotches(Checks& checks, Piece& front, Piece& back, Piece* sleeve,
                           double armholeY, double shoulderX,
                           double sideSeamDropCm = 15.0, double armholeRunCm = 7.0);

// The notches a trouser leg needs: front sews to back down the inseam and
// the outseam, and those two seams are long, curved and easy to ease
// wrong -- which is exactly what notches exist to prevent.
void addTrouserNotches(Checks& checks, Piece& front, Piece& back,
                       double kneeFromCrotchCm = 30.0, double outseamFromHemCm = 40.0);

// The same two, looking the pieces up by code in an assembled piece list
// (prefix + "F"/"B"/"SL"). Modules build their pieces in different orders
// and some add the sleeve conditionally, so notching after assembly is
// simpler than threading the pieces through by reference -- and it notches
// the copies that actually get exported.
void notchSetInSleeve(Checks& checks, std::vector<Piece>& pieces,
                      double armholeY, double shoulderX, const std::string& prefix = "");
void notchTrousers(Checks& checks, std::vector<Piece>& pieces, const std::string& prefix = "");

// --- Landmarks and the seam graph ------------------------------------

// Records a named corner on a piece, snapped onto its outline.
void addLandmark(Piece& piece, const std::string& label, Pt at);

// The landmark's recorded position, or false when the piece has no such
// landmark.
bool findLandmark(const Piece& piece, const std::string& label, Pt& out);

// Length of the outline span between two landmarks, walked the short way.
double seamEndLength(const Piece& piece, const std::string& from, const std::string& to);

// Two edges can only be sewn to each other if they are the same length.
// Checking every seam in the graph catches the case the side-seam check
// cannot see: a seam whose ENDS were named wrong, so it spans the right
// shape between the wrong corners.
void expectSeamsSewable(Checks& checks, const std::vector<Piece>& pieces,
                        const std::vector<Seam>& seams, double tolCm = 1.0);

// Names the corners of a torso panel and a sleeve, so seams can refer to
// them. These are the same landmarks the notch placement already works
// from, read off the drafted outline rather than recomputed.
// `bodyOffsetX` is how far the whole panel has been shifted off true
// center front -- a shirt front is translated out by its button placket,
// so its neck and shoulder sit that much further along than the block
// coordinates say. (A jacket front is NOT shifted: only its front edge
// runs past CF, the body stays put, so it passes 0.)
void addTorsoLandmarks(Piece& panel, double armholeY, double shoulderX, double neckHalfWidth,
                       double bodyOffsetX = 0.0);
void addSleeveLandmarks(Piece& sleeve);
void addTrouserLandmarks(Piece& leg);

// Notches, landmarks and the seam graph for a set-in-sleeve garment, in
// one call: they describe the same joins and would drift apart if each
// module assembled them separately.
void prepareSetInSleeve(Checks& checks, std::vector<Piece>& pieces, std::vector<Seam>& seams,
                        double armholeY, double shoulderX, double neckHalfWidth,
                        const std::string& prefix = "", double frontOffsetX = 0.0);
void prepareTrousers(Checks& checks, std::vector<Piece>& pieces, std::vector<Seam>& seams,
                     const std::string& prefix = "");

} } // namespace pf::common
