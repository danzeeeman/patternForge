#include "OuterwearDraft.h"
#include <algorithm>
#include <cmath>

namespace pf { namespace outerwear {

void applyKindDefaults(Spec& s, Kind kind) {
    s.kind = kind;
    switch (kind) {
        case Kind::Peacoat:
            // A peacoat stops at the hip. Short enough that it needs no
            // vent, and double-breasted over a wide wrap with the broad
            // collar that is the whole character of the garment.
            s.length = 78.0;
            s.hemFlare = 4.0;
            s.doubleBreasted = true;
            s.wrap = 9.0;
            s.lapelWidth = 11.0;
            s.breakY = 44.0;
            s.collarDepth = 12.0;
            s.facingWidth = 14.0;
            s.ease = 18.0;
            s.ventLength = 0.0;
            s.sleeveLength = 60.0;
            s.cuffWidth = 34.0;
            break;
        case Kind::Trench:
            s.length = 116.0;
            s.hemFlare = 16.0;
            s.doubleBreasted = true;
            s.wrap = 9.0;
            s.lapelWidth = 10.0;
            s.breakY = 48.0;
            s.collarDepth = 10.0;
            s.facingWidth = 14.0;
            s.ease = 22.0;
            s.ventLength = 34.0;
            s.stormFlap = true;
            s.belt = true;
            s.epaulettes = true;
            break;
        case Kind::Overcoat:
        default:
            s.length = 112.0;
            s.hemFlare = 12.0;
            s.doubleBreasted = false;
            s.wrap = 3.0;
            s.lapelWidth = 9.0;
            s.breakY = 46.0;
            s.collarDepth = 9.0;
            s.facingWidth = 12.0;
            s.ease = 22.0;
            s.ventLength = 30.0;
            break;
    }
}

Ring withBackVent(const Ring& backHalf, double ventLength, double ventWidth) {
    if (ventLength <= 0.1 || backHalf.size() < 3) return backHalf;
    Pt lo, hi;
    geo::bounds(backHalf, lo, hi);
    double hemY = hi.y;
    double topY = hemY - ventLength;
    if (topY <= lo.y + 1.0) return backHalf;

    // The panel's last points run along the hem and back up the center
    // back to the neck. The vent replaces the corner at (cbX, hemY) with
    // an extension out past the seam: out at the hem, up to the vent's
    // top, then back in to the seam line.
    double cbX = lo.x;
    Ring out;
    out.reserve(backHalf.size() + 3);
    for (size_t i = 0; i < backHalf.size(); ++i) {
        const Pt& p = backHalf[i];
        bool isCbHem = std::fabs(p.x - cbX) < 0.01 && std::fabs(p.y - hemY) < 0.01;
        if (!isCbHem) { out.push_back(p); continue; }
        out.push_back(Pt((float)(cbX - ventWidth), (float)hemY));
        out.push_back(Pt((float)(cbX - ventWidth), (float)topY));
        out.push_back(Pt((float)cbX, (float)topY));
    }
    return out.size() > backHalf.size() ? out : backHalf;
}

common::Draft draft(const Spec& spec) {
    common::Draft out;

    block::TorsoParams tp;
    tp.bust = spec.bust; tp.waist = spec.waist; tp.hip = spec.hip;
    tp.backWaistLength = spec.backWaistLength;
    tp.hipDepth = spec.hipDepth;
    tp.shoulder = spec.shoulder;
    tp.neck = spec.neck;
    tp.armholeDepth = spec.armholeDepth;
    tp.armholeScoop = spec.armholeScoop;
    // An overcoat goes over a jacket, so it carries real ease -- and the
    // waist is not nipped in the way a suit jacket's is.
    tp.easeBust = spec.ease; tp.easeWaist = spec.ease * 0.9; tp.easeHip = spec.ease;
    tp.waistShaping = spec.waistShaping; tp.waistRise = spec.waistRise;

    double hipHalf = spec.hip / 4.0 + tp.easeHip / 4.0;
    double hemHalfWidth = hipHalf + spec.hemFlare / 4.0;

    block::TorsoParams front = tp, back = tp;
    back.neckDrop = 2.5;
    back.neckWidth = 13.0;
    front.neckDrop = back.neckDrop;
    front.neckWidth = back.neckWidth;

    block::LapelParams lp;
    lp.style = spec.lapel;
    lp.wrap = spec.wrap;
    lp.breakY = std::clamp(spec.breakY, spec.armholeDepth + 2.0, spec.length - 6.0);
    lp.width = spec.lapelWidth;

    double armholeTotal = block::torsoArmholeLength(front) + block::torsoArmholeLength(back);
    Pt breakPt, neckPt;
    block::lapelRollLine(front, lp, breakPt, neckPt);
    auto upper = block::lapelEdge(front, lp);

    Piece F; F.code = "F"; F.name = "Front (with lapel)";
    F.cutQty = "CUT 2 MIRRORED / SHELL";
    F.note = std::string(spec.doubleBreasted ? "Double-breasted: " : "Single-breasted: ") +
             "the front edge runs " + std::to_string((int)std::lround(spec.wrap)) +
             " cm past center front. Press the roll line; do not cut it.";
    F.seamAllowanceCm = 1.5f;   // heavy cloth wants a deeper allowance
    F.sewing = block::torsoHalfShaped(front, spec.length, hemHalfWidth, {}, upper, -lp.wrap);
    // The roll line is the fold the lapel turns back along -- a line to
    // press, never a line to cut. Two loose points left it to the reader
    // to guess that they were the ends of one line.
    F.lines.push_back({ "ROLL LINE -- PRESS, DO NOT CUT", breakPt, neckPt, MarkedLine::Fold });
    common::finish(F);

    Piece B; B.code = "B"; B.name = "Back";
    B.cutQty = "CUT 2 MIRRORED / SHELL";
    B.note = spec.ventLength > 0.1
        ? "Center back seam with a vent at the hem; the extension is the vent's underlap."
        : "Center back seam.";
    B.seamAllowanceCm = 1.5f;
    B.sewing = withBackVent(block::torsoHalf(back, spec.length, hemHalfWidth, {}),
                            spec.ventLength, spec.ventWidth);
    common::finish(B);

    block::SleeveParams sp;
    sp.armholeLen = armholeTotal;
    sp.sleeveLength = spec.sleeveLength;
    sp.bicep = spec.bicep + spec.ease * 0.75;
    sp.wrist = spec.cuffWidth;
    double capTarget = armholeTotal * block::kSleeveCapEase;
    double armFloor = sp.bicep;
    sp.capHeight = armholeTotal / 3.2;
    double solvedBicep = block::solveSleeveBicep(sp, capTarget);
    if (solvedBicep >= armFloor) sp.bicep = solvedBicep;
    else sp.capHeight = block::solveSleeveCapHeight(sp, capTarget);

    Piece SL; SL.code = "SL"; SL.name = "Sleeve"; SL.cutQty = "CUT 2 / SHELL";
    SL.note = "Ease the cap into the armhole. One-piece sleeve.";
    SL.seamAllowanceCm = 1.5f;
    SL.sewing = block::sleeve(sp);
    common::finish(SL);

    block::CollarParams cp;
    double backNeck = block::torsoNeckLength(back);
    double gorge = glm::distance(upper[upper.size() - 2], neckPt);
    cp.neckLen = backNeck + gorge;
    cp.depth = spec.collarDepth;
    cp.spread = 0.0;
    Piece C; C.code = "UC"; C.name = "Under collar"; C.cutQty = "CUT 2 MIRRORED / SHELL";
    C.note = spec.kind == Kind::Peacoat
        ? "Cut deep on purpose: a peacoat collar turns right up against weather, so it needs the "
          "height to stand."
        : "Interface and press the roll before setting it.";
    C.seamAllowanceCm = 1.5f;
    C.sewing = block::shirtCollar(cp);
    C.inMuslin = false;  // a collar finishes the garment; it does not carry the fit
    common::finish(C);

    Piece FF; FF.code = "FF"; FF.name = "Front facing"; FF.cutQty = "CUT 2 MIRRORED / SHELL";
    FF.note = "Cut from the same lapel curve as the front, so the two turn against each other.";
    FF.seamAllowanceCm = 1.5f;
    FF.sewing = block::jacketFacing(front, lp, spec.length, spec.facingWidth);
    FF.inMuslin = false;  // a facing finishes the garment; it does not carry the fit
    common::finish(FF);

    // Buttons. A single-breasted coat runs one column down center front;
    // a double-breasted one runs two, set either side of it, and the
    // fronts cross so each column needs its own holes on the opposing
    // front. Both start at the break point -- above it the lapel is
    // folded back, so a button there would be on the underside.
    double firstY = lp.breakY + 1.0;
    double lastY = std::max(firstY + 10.0, spec.length - 18.0);
    int rows = spec.kind == Kind::Peacoat ? 4 : 3;
    if (spec.doubleBreasted) {
        double colX = std::max(1.5, spec.wrap - 2.0);
        common::addButtonColumn(F, -colX, firstY, lastY, rows, 25.0, "button");
        common::addButtonColumn(F, colX, firstY, lastY, rows, 25.0, "button");
        common::addButtonholeColumn(F, -colX, firstY, lastY, rows, 25.0,
                                    "buttonhole (this front laps over)");
    } else {
        common::addButtonColumn(F, 0.0, firstY, lastY, rows, 25.0, "button (left front)");
        common::addButtonholeColumn(F, 0.0, firstY, lastY, rows, 25.0,
                                    "buttonhole (right front)");
    }

    out.pieces = { F, B, SL, C, FF };

    // The pieces that make a trench a trench.
    if (spec.stormFlap) {
        // A shield over the right chest, shed water running off the
        // shoulder clear of the front opening.
        double top = block::kShoulderDropCm;
        double w = spec.shoulder + 4.0;
        Piece SF; SF.code = "SF"; SF.name = "Storm flap"; SF.cutQty = "CUT 1 / SHELL";
        SF.note = "Right front only. Sewn into the shoulder and armhole seams, loose at the hem.";
        SF.seamAllowanceCm = 1.5f;
        SF.sewing = { Pt((float)-spec.wrap, (float)top),
                      Pt((float)w, (float)(top + 2.0)),
                      Pt((float)(w - 1.0), (float)(top + 30.0)),
                      Pt((float)-spec.wrap, (float)(top + 26.0)) };
        SF.inMuslin = false;
        common::finish(SF);
        out.pieces.push_back(SF);
    }
    if (spec.belt) {
        double beltLen = spec.waist + spec.ease + 45.0; // enough to buckle and hang
        Piece BL; BL.code = "BLT"; BL.name = "Belt"; BL.cutQty = "CUT 2 / SHELL (SELF-LINED)";
        BL.note = "Folded and turned. Length allows for the buckle and a tail.";
        BL.seamAllowanceCm = 1.0f;
        BL.sewing = { Pt(0, 0), Pt((float)beltLen, 0),
                      Pt((float)beltLen, (float)spec.beltWidth), Pt(0, (float)spec.beltWidth) };
        BL.inMuslin = false;
        common::addBuckle(BL, Pt(2.5f, (float)(spec.beltWidth / 2.0)), spec.beltWidth,
                          "buckle");
        common::finish(BL);
        out.pieces.push_back(BL);
    }
    if (spec.epaulettes) {
        Piece EP; EP.code = "EPL"; EP.name = "Epaulette"; EP.cutQty = "CUT 4 / SHELL (2 PER STRAP)";
        EP.note = "Caught in the shoulder seam, buttoned at the neck end.";
        EP.seamAllowanceCm = 1.0f;
        EP.sewing = { Pt(0, 0), Pt((float)(spec.shoulder - 1.0), 0),
                      Pt((float)(spec.shoulder - 1.0), 5.f), Pt(0, 4.f) };
        EP.inMuslin = false;
        common::addButtonColumn(EP, spec.shoulder - 3.0, 2.0, 2.0, 1, 15.0, "epaulette button");
        common::finish(EP);
        out.pieces.push_back(EP);
    }

    common::prepareSetInSleeve(out.checks, out.pieces, out.seams,
                               tp.armholeDepth, tp.shoulder, block::torsoNeckHalfWidth(front));
    out.armholeY = tp.armholeDepth;
    out.shoulderX = block::torsoShoulderX(tp);

    common::addSanityChecks(out.checks, out.pieces);
    common::expectSeamsSewable(out.checks, out.pieces, out.seams);
    common::expectSeamFractionsMatch(out.checks, out.pieces, out.seams);
    common::expectClosuresOnCloth(out.checks, out.pieces);
    out.checks.expectNear("Sleeve cap vs armhole (with ease)",
                          block::sleeveCapLength(sp), capTarget, armholeTotal * 0.075);
    common::expectSideSeamsMatch(out.checks, "Front/back side seams match",
                                 F.sewing, B.sewing, spec.backWaistLength, spec.length, 0.05);

    common::expectLapelStandsOff(out.checks, "Lapel stands off the roll line",
                                 F.sewing, breakPt, neckPt, lp.width);
    out.checks.add("Front outline does not cross itself",
                   "lapel and side seam stay clear of each other",
                   !geo::selfIntersects(F.sewing));

    // A double-breasted coat has to actually cross the body: the two
    // fronts overlap by twice the wrap, and under about 12 cm of overlap
    // the second row of buttons has nowhere to sit.
    if (spec.doubleBreasted) {
        out.checks.add("Double-breasted fronts overlap enough to button",
                       "overlap " + std::to_string(spec.wrap * 2.0) + " cm",
                       spec.wrap * 2.0 >= 12.0);
    }

    if (spec.ventLength > 0.1) {
        // The vent must be an addition to the back, never a bite out of it.
        double plain = std::fabs(geo::area(block::torsoHalf(back, spec.length, hemHalfWidth, {})));
        double vented = std::fabs(geo::area(B.sewing));
        out.checks.add("Back vent adds an underlap rather than cutting one away",
                       std::to_string(vented - plain) + " cm2 added",
                       vented > plain + 1.0);
    }
    return out;
}

} } // namespace pf::outerwear
