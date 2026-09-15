#include "SuitDraft.h"
#include "GarmentCommon.h"
#include <algorithm>
#include <cmath>

namespace pf { namespace suit {

Draft draftJacket(const JacketSpec& spec, const std::string& p) {
    Draft out;

    block::TorsoParams tp;
    tp.bust = spec.bust; tp.waist = spec.waist; tp.hip = spec.hip;
    tp.backWaistLength = spec.backWaistLength;
    tp.hipDepth = spec.hipDepth;
    tp.shoulder = spec.shoulder;
    tp.neck = spec.neck;
    tp.armholeDepth = spec.armholeDepth;
    tp.armholeScoop = spec.armholeScoop;
    tp.easeBust = spec.ease; tp.easeWaist = spec.ease * 0.7; tp.easeHip = spec.ease;
    tp.waistShaping = spec.waistShaping; tp.waistRise = spec.waistRise;

    double hipHalf = spec.hip / 4.0 + tp.easeHip / 4.0;
    double hemHalfWidth = hipHalf + spec.hemFlare / 4.0;

    // A jacket is shaped through the waist -- that is what separates it
    // from a boxy coat -- so the side seam takes a waypoint pulled in at
    // the waist before it releases over the hip to the hem.
    std::vector<Pt> waistShaping;
    double waistHalf = spec.waist / 4.0 + spec.ease * 0.3 / 4.0;
    if (spec.jacketLength > spec.backWaistLength + 6.0)
        waistShaping.push_back(Pt((float)waistHalf, (float)(spec.backWaistLength + 2.0)));

    block::TorsoParams front = tp;
    block::TorsoParams back  = tp;
    back.neckDrop  = std::min(spec.backNeckDepth, std::max(1.0, spec.jacketLength - 4.0));
    back.neckWidth = spec.backNeckWidth;
    // The front's neckline is replaced by the lapel, but the block still
    // reads neckDrop/neckWidth to place the neck/shoulder point the lapel
    // runs up to, so they must match the back or the shoulder seams would
    // not meet at the neck.
    front.neckDrop = back.neckDrop;
    front.neckWidth = spec.backNeckWidth;

    block::LapelParams lp;
    lp.style = spec.lapel;
    lp.wrap = spec.wrap;
    // The lapel cannot roll open past the hem, nor so high there is no
    // lapel left to fold.
    lp.breakY = std::clamp(spec.breakY, spec.armholeDepth + 2.0, spec.jacketLength - 6.0);
    lp.width = spec.lapelWidth;

    double armholeTotal = block::torsoArmholeLength(front) + block::torsoArmholeLength(back);

    Pt breakPt, neckPt;
    block::lapelRollLine(front, lp, breakPt, neckPt);
    auto upper = block::lapelEdge(front, lp);

    static const char* kLapelNames[] = { "notched", "peak", "shawl" };
    out.styleNote = kLapelNames[(int)spec.lapel];

    Piece F; F.code = p + "F"; F.name = "Front (with lapel)";
    F.cutQty = "CUT 2 MIRRORED / SHELL";
    F.note = "Front edge runs " + std::to_string((int)std::lround(spec.wrap)) +
             " cm past center front for the buttons. Press the roll line; do not cut it.";
    F.seamAllowanceCm = 1.0f;
    F.sewing = block::torsoHalfShaped(front, spec.jacketLength, hemHalfWidth,
                                      waistShaping, upper, -lp.wrap);
    // The roll line is the fold the lapel turns back along -- a line to
    // press, never a line to cut. Two loose points left it to the reader
    // to guess that they were the ends of one line.
    F.lines.push_back({ "ROLL LINE -- PRESS, DO NOT CUT", breakPt, neckPt, MarkedLine::Fold });
    common::finish(F);

    Piece B; B.code = p + "B"; B.name = "Back";
    if (spec.centerBackSeam) {
        B.cutQty = "CUT 2 MIRRORED / SHELL";
        B.note = "Center back seam, which is where a jacket takes its shaping through the blade.";
    } else {
        B.cutQty = "CUT 1 ON FOLD / SHELL";
        B.note = "Cut on the fold at center back.";
        B.foldAtCF = true;
    }
    B.seamAllowanceCm = 1.0f;
    B.sewing = block::torsoHalf(back, spec.jacketLength, hemHalfWidth, waistShaping);
    common::finish(B);

    block::SleeveParams sp;
    sp.armholeLen = armholeTotal;
    sp.sleeveLength = spec.sleeveLength;
    sp.bicep = spec.bicep + spec.ease * 0.7;
    sp.wrist = spec.cuffWidth;
    double capTarget = armholeTotal * block::kSleeveCapEase;
    double armFloor = sp.bicep;
    sp.capHeight = armholeTotal / 3.2;
    double solvedBicep = block::solveSleeveBicep(sp, capTarget);
    if (solvedBicep >= armFloor) sp.bicep = solvedBicep;
    else sp.capHeight = block::solveSleeveCapHeight(sp, capTarget);

    Piece SL; SL.code = p + "SL"; SL.name = "Sleeve"; SL.cutQty = "CUT 2 / SHELL";
    SL.note = "One-piece sleeve. A tailored jacket normally takes a two-piece sleeve with an "
              "elbow curve; this draft keeps the single panel the rest of this app uses.";
    SL.seamAllowanceCm = 1.0f;
    SL.sewing = block::sleeve(sp);
    common::finish(SL);

    // The under collar covers the back neck and runs forward to the collar
    // point, so it is cut to the back neckline plus the gorge, not to the
    // raw neck measurement.
    block::CollarParams cp;
    double backNeck = block::torsoNeckLength(back);
    double gorge = glm::distance(upper[upper.size() - 2], neckPt);
    cp.neckLen = backNeck + gorge;
    cp.depth = std::max(3.0, spec.lapelWidth * 0.8);
    cp.spread = 0.0;
    Piece C; C.code = p + "UC"; C.name = "Under collar"; C.cutQty = "CUT 2 MIRRORED / SHELL";
    C.note = "Cut on the bias if your cloth allows -- a bias under collar rolls around the neck "
             "instead of standing away from it.";
    C.seamAllowanceCm = 1.0f;
    C.sewing = block::shirtCollar(cp);
    C.inMuslin = false;  // an under collar finishes the garment; it does not carry the fit
    common::finish(C);

    Piece FF; FF.code = p + "FF"; FF.name = "Front facing"; FF.cutQty = "CUT 2 MIRRORED / SHELL";
    FF.note = "Sewn to the front edge face to face and turned, so the folded-back lapel shows "
              "cloth. Its lapel edge is cut from the same curve as the front's.";
    FF.seamAllowanceCm = 1.0f;
    FF.sewing = block::jacketFacing(front, lp, spec.jacketLength, spec.facingWidth);
    FF.inMuslin = false;  // a front facing finishes the garment; it does not carry the fit
    common::finish(FF);

    // A jacket buttons below the break point; how many rows depends on
    // whether the wrap is wide enough to cross the body.
    double firstY = lp.breakY + 1.0;
    double lastY = std::max(firstY + 8.0, spec.jacketLength - 16.0);
    bool dbl = spec.wrap >= 6.0;
    if (dbl) {
        double colX = std::max(1.5, spec.wrap - 2.0);
        common::addButtonColumn(F, -colX, firstY, lastY, 3, 22.0, "button");
        common::addButtonColumn(F, colX, firstY, lastY, 3, 22.0, "button");
        common::addButtonholeColumn(F, -colX, firstY, lastY, 3, 22.0, "buttonhole");
    } else {
        common::addButtonColumn(F, 0.0, firstY, lastY, 2, 22.0, "button (left front)");
        common::addButtonholeColumn(F, 0.0, firstY, lastY, 2, 22.0, "buttonhole (right front)");
    }

    out.pieces = { F, B, SL, C, FF };
    common::prepareSetInSleeve(out.checks, out.pieces, out.seams, tp.armholeDepth, tp.shoulder,
                               block::torsoNeckHalfWidth(front), p);
    out.armholeY = tp.armholeDepth;
    out.shoulderX = block::torsoShoulderX(tp);

    common::addSanityChecks(out.checks, out.pieces);
    common::expectSeamsSewable(out.checks, out.pieces, out.seams);
    common::expectSeamFractionsMatch(out.checks, out.pieces, out.seams);
    out.checks.expectNear(p + "Sleeve cap vs armhole (with ease)",
                          block::sleeveCapLength(sp), capTarget, armholeTotal * 0.075);
    common::expectSideSeamsMatch(out.checks, p + "Front/back side seams match",
                                 F.sewing, B.sewing, spec.backWaistLength, spec.jacketLength, 0.05);

    // The lapel has to actually stick out past the front edge, or it is a
    // neckline with extra steps. Measured as the furthest the front piece
    // reaches beyond the front edge line.
    common::expectLapelStandsOff(out.checks, p + "Lapel stands off the roll line",
                                 F.sewing, breakPt, neckPt, lp.width);

    // The facing carries the same lapel curve as the front: sewn edge
    // to edge and turned, any difference would show along the lapel.
    common::expectLapelStandsOff(out.checks, p + "Facing lapel matches the front's",
                                 FF.sewing, breakPt, neckPt, lp.width);

    // Front and back shoulder seams are sewn to each other.
    out.checks.expectNear(p + "Front/back neck point meets at the shoulder",
                          block::torsoNeckHalfWidth(front), block::torsoNeckHalfWidth(back), 0.01);

    out.checks.add(p + "Front outline does not cross itself",
                   "lapel and side seam stay clear of each other",
                   !geo::selfIntersects(F.sewing));
    out.checks.add(p + "Facing outline does not cross itself",
                   "facing inner edge stays clear of the lapel",
                   !geo::selfIntersects(FF.sewing));
    return out;
}

Draft draftTrousers(const TrouserSpec& spec, const std::string& p) {
    Draft out;

    block::TrouserParams front;
    front.waist = spec.waist; front.hip = spec.hip; front.thigh = spec.thigh;
    front.rise = spec.rise; front.inseam = spec.inseam;
    front.kneeWidth = spec.kneeWidth; front.ankleWidth = spec.ankleWidth;
    front.easeWaist = spec.easeWaist; front.easeHip = spec.easeHip;
    front.isFront = true;
    block::TrouserParams back = front;
    back.isFront = false;

    Piece F; F.code = p + "F"; F.name = "Trouser front"; F.cutQty = "CUT 2 MIRRORED / SHELL";
    F.note = "Front rise carries a shallower crotch curve than the back."; F.seamAllowanceCm = 1.0f;
    F.sewing = block::trouserHalf(front);
    common::finish(F);

    Piece B; B.code = p + "B"; B.name = "Trouser back"; B.cutQty = "CUT 2 MIRRORED / SHELL";
    B.note = "Back rise carries extra seat room in the crotch curve."; B.seamAllowanceCm = 1.0f;
    B.sewing = block::trouserHalf(back);
    common::finish(B);

    double waistband = spec.waist + spec.easeWaist;
    Piece WB; WB.code = p + "WB"; WB.name = "Trouser waistband";
    WB.cutQty = "CUT 1 / SHELL + 1 / INTERFACING";
    WB.note = "Cut flat at full length -- this piece is the whole band, not half of one.";
    WB.seamAllowanceCm = 1.0f;
    WB.sewing = { {0,0}, {(float)(waistband + 4.0), 0}, {(float)(waistband + 4.0), 5.f}, {0, 5.f} };
    WB.inMuslin = false;  // a waistband finishes the garment; it does not carry the fit
    common::finish(WB);

    out.pieces = { F, B, WB };
    common::prepareTrousers(out.checks, out.pieces, out.seams, p);
    common::addSanityChecks(out.checks, out.pieces);
    common::expectSeamsSewable(out.checks, out.pieces, out.seams);
    common::expectSeamFractionsMatch(out.checks, out.pieces, out.seams);
    out.checks.expectNear(p + "Front/back inseam lengths match",
        block::trouserInseamLength(front), block::trouserInseamLength(back), 1.0);
    out.checks.expectNear(p + "Front/back outseam lengths match",
        block::trouserOutseamLength(front), block::trouserOutseamLength(back), 1.0);
    return out;
}

} } // namespace pf::suit
