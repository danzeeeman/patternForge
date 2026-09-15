#pragma once
#include "../geometry/Perimeter.h"

// Shared block-drafting formulas used by Dress/Shirt/Coat (torso + sleeve
// + collar) and Pants (trouser block). These are simplified, dart-free
// slopers built from common flat-pattern ratios (bust/4, hip/4, etc.) --
// a generative development draft in the same spirit as the two reference
// coat packages, NOT a substitute for a fitted commercial pattern. Every
// module built on top of these carries that disclaimer through to its
// exported instructions, exactly as the existing packages do.
//
// All returned Rings are one half-panel in local centimeters, with the
// center-front/center-back line running along x = 0 and y increasing
// downward from the high-shoulder/nape datum (y = 0). Callers mirror at
// export/render time and note "CUT 2 MIRRORED" on the piece.

namespace pf { namespace block {

// Shoulder point's drop below the high-shoulder/nape datum (y = 0), cm.
// Shared so the 3D preview can hang a sleeve from the same point the
// torso block puts it.
constexpr double kShoulderDropCm = 4.0;

// A sleeve cap is cut LONGER than the armhole it sews into, and the extra
// is eased in around the crown so the sleeve turns over the shoulder
// instead of pulling flat across it. So a cap seam that measured exactly
// the armhole would be wrong. 4.5% is a modest, woven-cloth allowance.
constexpr double kSleeveCapEase = 1.045;

struct TorsoParams {
    double bust = 86.4, waist = 68.6, hip = 94.0;   // circumference, cm
    double backWaistLength = 40.0;                   // nape/high-shoulder to waist
    double hipDepth = 20.0;                          // waist to hip
    double shoulder = 12.5;                          // one shoulder's width, CF/CB to arm point
    double neck = 36.0;                              // neck circumference
    double neckDrop = 7.0;                           // how far the neckline drops below the datum
    double neckWidth = 0.0;                          // full width across the neckline; 0 = derive from `neck`
    double armholeDepth = 22.0;                      // underarm's drop below the datum (the armhole's depth)
    double armholeScoop = 1.0;                       // 1 = the plain curve; less is straighter, more cuts deeper in
    double easeBust = 5.0, easeWaist = 4.0, easeHip = 5.0; // total garment ease over the body, cm

    // How hard the side seam draws in at the waist.
    //
    // 1 is the drafted waist: the body's waist plus its ease. 0 leaves the
    // seam running straight from the underarm down to the hip, which is
    // what a shift, a box jacket or a camp shirt actually is -- not a
    // fitted block with the ease turned up, which is what you get by
    // widening the waist alone. Above 1 takes it in tighter than the body
    // measures; a woven can only do that with a dart, so it is the
    // drafter's call rather than something to clamp away.
    double waistShaping = 1.0;
    // Where that happens, cm ABOVE the body's own waist. Positive raises
    // it toward an empire line, negative drops it. The hip does not move:
    // it is a separate landmark on the same body, and the waist seam
    // sliding up must not drag it along.
    double waistRise = 0.0;
};

// Half torso panel: neckline -> shoulder -> armhole curve -> side seam ->
// hem -> straight back up the center front/back fold to the neckline.
// `hemY`/`hemHalfWidthCm` place the hem directly. The side seam is a
// smooth curve through the waist and hip points, then through any
// `extraSideSeamPoints` (each an (x=halfWidth, y) point, in order down
// the body), to the hem point -- how a dress module tells a straight
// sheath from a fit-and-flare or trumpet line: same block, different
// waypoints between hip and hem.
Ring torsoHalf(const TorsoParams& p, double hemY, double hemHalfWidthCm,
               const std::vector<Pt>& extraSideSeamPoints = {});

// The same block, but with the neckline replaced by a caller-supplied
// upper edge and the center-front line moved to `cfX`. A jacket front
// needs both: its front edge sits out past CF by the button wrap (so
// cfX is negative), and its top edge is a lapel rather than a neckline.
// `upperEdge` runs from the top of the front edge to the neck/shoulder
// point, where the shoulder seam takes over.
Ring torsoHalfShaped(const TorsoParams& p, double hemY, double hemHalfWidthCm,
                     const std::vector<Pt>& extraSideSeamPoints,
                     const std::vector<Pt>& upperEdge, double cfX);

enum class LapelStyle { Notched = 0, Peak, Shawl };

struct LapelParams {
    LapelStyle style = LapelStyle::Notched;
    double wrap = 2.0;      // front extension past CF for the buttons, cm
    double breakY = 46.0;   // how far down the front edge the lapel rolls open
    double width = 8.0;     // lapel width, measured square off the roll line
};

// The roll line -- the crease the lapel folds back along, from the break
// point on the front edge up to the neck/shoulder point. Worth marking on
// the pattern: it is the line the tailor presses, not a cut edge.
void lapelRollLine(const TorsoParams& p, const LapelParams& lp, Pt& breakPt, Pt& neckPt);

// The jacket front's upper edge, from the break point up over the lapel
// to the neck/shoulder point.
//
// Drafted the way a lapel actually is: the shape is laid out against the
// roll line as it sits when worn (lying on the chest), then REFLECTED
// across that line. The flat piece therefore carries the lapel sticking
// out past the front edge, and folding it back along the roll line lands
// it on the chest. Drawing it on the chest side instead would cut the
// lapel out of the body of the jacket.
std::vector<Pt> lapelEdge(const TorsoParams& p, const LapelParams& lp);

// The front facing: the lapel again, plus a strip down the front edge.
// It is sewn on face to face and turned, so the lapel shows cloth on the
// side that flips outward. Its inner edge has to stay outboard of where
// the folded lapel lands, or the lapel's edge would show raw.
Ring jacketFacing(const TorsoParams& p, const LapelParams& lp,
                  double hemY, double facingWidthCm);

// Sewing length of this panel's neckline (center front/back out to the
// shoulder), so a collar can be cut to the neckline actually drafted
// rather than to the raw neck measurement.
double torsoNeckLength(const TorsoParams& p);

// This panel's armhole, from the shoulder point down to the underarm.
// A sleeve cap is sewn to the front and back armholes together, so its
// length must be checked against both.
std::vector<Pt> torsoArmholeCurve(const TorsoParams& p);
double torsoArmholeLength(const TorsoParams& p);

// The neckline's half width after the block's own limit (it can never
// reach the shoulder point).
// The shoulder point the block will actually use. `shoulder` is what was
// asked for; this is what the armhole can accommodate, which is anything
// up to the bust line. Modules should report THIS as the design's
// shoulderX, so the landmarks, the checks and the 3D placement all agree
// with the panel that was drawn.
double torsoShoulderX(const TorsoParams& p);

double torsoNeckHalfWidth(const TorsoParams& p);

// A leotard block: the torso carried on down through the crotch, with the
// legs cut away at the sides. Unlike every other block here it is meant
// for KNIT cloth and NEGATIVE ease -- a bodysuit is held on by being
// smaller than the body, so drafting it with woven ease would give a bag.
struct BodysuitParams {
    TorsoParams torso;
    double riseY = 68.0;        // crotch level below the nape datum
    double legOpeningY = 58.0;  // how high the leg is cut at the side seam
    double crotchHalf = 5.0;    // half the crotch width at its narrowest
    bool isFront = true;        // the back is cut a little fuller over the seat
};

// Half a bodysuit panel: neckline, shoulder, armhole, side seam down to
// the leg opening, then the leg curve in to the crotch, then straight up
// the center line. Cut 1 on the fold.
Ring bodysuitHalf(const BodysuitParams& p);

struct SleeveParams {
    double armholeLen = 44.0;   // armhole this sleeve must fit, informational
    double sleeveLength = 58.0; // shoulder point to hem
    double bicep = 34.0;        // upper-arm circumference target
    double wrist = 26.0;        // finished hem circumference (not a half width)
    // Where a flare begins, as a fraction of the sleeve length: 1 means a
    // straight taper from the bicep to the hem, lower values hold the arm's
    // own shape to that point and then flare out to the hem (a bell). It
    // only has an effect when the hem is wider than the arm at that point.
    double flareFrom = 1.0;
    double capHeight = 14.0;
};
// Symmetric sleeve panel (cap curve at y=0, hem at y=sleeveLength), x
// centered on 0. Cut 2 (one per arm); no separate front/back cap.
Ring sleeve(const SleeveParams& p);

// Length of the sleeve cap seam (both halves) -- the edge sewn into the
// front and back armholes together.
double sleeveCapLength(const SleeveParams& p);

// The cap height whose cap seam measures `targetLength`, so a sleeve fits
// the armhole it is sewn into whatever the armhole is set to. Returns a
// height clamped to what the sleeve's own length allows, so a cap that
// cannot reach the target stays visible to the checks instead of being
// silently forced.
double solveSleeveCapHeight(const SleeveParams& p, double targetLength);

// The bicep width whose cap seam measures `targetLength` at the given cap
// height. This is the variable to solve first: a deeper armhole wants a
// wider sleeve, not a taller cap. Fall back to solveSleeveCapHeight when
// the result would be narrower than the arm needs.
double solveSleeveBicep(const SleeveParams& p, double targetLength);

struct CollarParams {
    double neckLen = 18.0;  // half neckline this collar must match (CF to CB)
    double depth = 4.0;
    double spread = 0.0;    // front-opening spread added at CF end, cm
};
// A simple banded collar half, CB at x=0 to CF at x=neckLen(+spread).
Ring shirtCollar(const CollarParams& p);

struct SkirtParams {
    double waist = 68.6, hip = 94.0;
    double hipDepth = 20.0;     // waist down to the fullest part of the hip
    double length = 60.0;       // waist to hem
    double hemHalfWidth = 26.0; // set by the caller from the style
    double easeWaist = 2.0, easeHip = 4.0;
};

// Half a skirt panel, waist at y = 0: center front/back down the fold,
// out along the waist, then a side seam curving through the hip to the
// hem. The hip is the constraint -- a skirt narrower than the hip cannot
// be got on however it is shaped at the waist and hem.
Ring skirtHalf(const SkirtParams& p);

// A flat sector of an annulus: the draft behind anything cut as a circle
// rather than as a panel -- a circle skirt, a cape. `innerCircumference`
// is the edge that goes round the body (the waist, the neck) spread over
// the WHOLE circle, `sweepDeg` how much of that circle this one piece is.
//
// The inner radius comes from the circumference, not from a width: that
// is the whole difference between this and a panel. A circle skirt hangs
// in folds because its hem is vastly longer than its waist, and that
// falls out of the geometry rather than being drawn in.
Ring annulusSector(double innerCircumference, double widthCm, double sweepDeg);

struct TrouserParams {
    double waist = 68.6, hip = 94.0;
    double rise = 27.0;     // waist to crotch level
    double inseam = 74.0;
    double thigh = 58.0;    // circumference target at the crotch line
    double kneeWidth = 40.0;  // finished knee circumference -- where leg styles differ
    double ankleWidth = 36.0; // finished hem circumference
    bool isFront = true;
    double easeWaist = 2.0, easeHip = 4.0;
};
// One trouser leg panel (front or back). Local x = 0 is the center
// front/back seam at the waist; the outseam runs toward -x, the crotch
// extension toward +x. Outline: waist -> straight CF/CB line down to hip
// level -> crotch curve out to the crotch point -> inseam (through the
// knee) -> hem -> outseam (through the knee) -> hip -> waist. The knee and
// hem are centered on the crease line, halfway between the side hip and
// the crotch point, and each panel carries half of the knee and hem
// circumference, so front + back make the full leg. Front and back differ
// in crotch extension only.
Ring trouserHalf(const TrouserParams& p);

// Sewing-line lengths of the panel's inseam (crotch point to hem) and
// outseam (hem to waist), measured on the same polyline trouserHalf
// builds, so front/back panels can be checked to actually sew together.
double trouserInseamLength(const TrouserParams& p);
double trouserOutseamLength(const TrouserParams& p);

// Elliptical arc, `steps` extra points from fromDeg to toDeg inclusive
// (0deg = +x axis, 90deg = +y axis, matching screen-style y-down math).
std::vector<Pt> arc(Pt center, double rx, double ry, double fromDeg, double toDeg, int steps);

} } // namespace pf::block
