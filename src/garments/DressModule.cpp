#include "DressModule.h"
#include "GarmentCommon.h"
#include "BlockMath.h"
#include <algorithm>
#include <cmath>

namespace pf {

namespace {
enum Silhouette { kALine = 0, kSheath, kFitAndFlare, kEmpire, kTrumpet };
}

std::vector<SizePreset> DressModule::sizePresets() const {
    SizePreset base;
    base.name = "US4";
    base.cm = { {"bust",86.4},{"waist",68.6},{"hip",94.0},{"backWaistLength",40.0},{"hipDepth",20.0},{"shoulder",12.5},{"neck",36.0},{"bicep",27.0},{"wrist",22.0} };
    return common::gradeFrom(base);
}

std::vector<StyleParamSpec> DressModule::styleParamSpecs() const {
    std::vector<StyleParamSpec> v = {
        { "silhouette", "Silhouette", 0.f, 4.f, 0.f,
          { "A-Line", "Sheath / Column", "Fit & Flare", "Empire Waist", "Trumpet / Mermaid" } },
        { "hemLength",   "Length (nape to hem, cm)", 80.f, 450.f, 100.f },
        { "hemFlareCm",  "Hem flare (cm)", 0.f, 120.f, 10.f },
        { "neckDepthCm", "Front neckline depth (cm)", 4.f, 48.f, 7.f },
        { "neckWidthCm", "Neckline width, front and back (cm)", 8.f, 24.f, 12.f },
        { "opening", "How it opens", 0.f, 2.f, 0.f,
          { "Center back seam + zip", "Side seam zip", "Pull-on (no opening)" } },
        { "slitLengthCm", "Slit at the hem (cm, 0 = none)", 0.f, 90.f, 0.f },
        { "backNeckDepthCm", "Back neckline depth (cm)", 2.f, 48.f, 2.f },
        { "armholeDepthCm", "Armhole depth below the shoulder (cm)", 16.f, 40.f, 22.f },
        { "shoulderWidthCm", "Shoulder width (cm)", 8.f, 34.f, 12.5f },
        { "armholeScoop", "Armhole scoop (1 = standard)", 0.5f, 1.6f, 1.f },
        { "sleeveLengthCm", "Sleeve length (0 = sleeveless), cm", 0.f, 180.f, 0.f },
        { "sleeveHemCm", "Sleeve hem circumference (cm)", 10.f, 180.f, 20.f },
        { "sleeveFlareFrom", "Sleeve flare starts at (1 = straight taper)", 0.3f, 1.f, 1.f },
        { "easeCm",      "Overall ease added to the body (cm)", 0.f, 48.f, 6.f },
    };
    // The waist controls are the same on every garment that has a
    // torso, so the slider means one thing wherever it is met.
    for (auto& w : common::waistSpecs()) v.push_back(w);
    return v;
}

DesignResult DressModule::build(const SizePreset& size, const StyleParams& style) const {
    DesignResult out;
    out.garmentKey = key();
    out.stem = "Dress-Pattern-Package";
    out.size = size;
    out.style = style;

    int silhouette = std::clamp((int)std::lround(style.get("silhouette", 0.f)), 0, 4);
    static const char* kNames[] = { "A-Line", "Sheath / Column", "Fit & Flare", "Empire Waist", "Trumpet / Mermaid" };
    out.title = std::string("Studio ") + kNames[silhouette] + " Dress";

    double bust = size.get("bust", 86.4), hip = size.get("hip", 94.0);
    double ease = style.get("easeCm", 6.f);
    double hemLength = style.get("hemLength", 100.f);
    double hemFlare  = style.get("hemFlareCm", 10.f);

    block::TorsoParams tp;
    tp.bust = bust; tp.waist = size.get("waist", 68.6); tp.hip = hip;
    tp.backWaistLength = size.get("backWaistLength", 40.0);
    tp.hipDepth = size.get("hipDepth", 20.0);
    // Armhole controls: the shoulder point (the armhole's top), how far
    // below it the underarm sits, and how deeply the curve scoops in.
    tp.shoulder = style.get("shoulderWidthCm", 12.5f);
    tp.armholeDepth = style.get("armholeDepthCm", 22.f);
    tp.armholeScoop = style.get("armholeScoop", 1.f);
    tp.neck = size.get("neck", 36.0);
    // Choosing NO OPENING has a consequence for the cut, not just for the
    // markings: with nothing to undo, the dress has to pass over the
    // widest part of the body on the way on. A fitted woven dress simply
    // cannot, so pull-on means cutting it loose enough that it can -- a
    // shift, in other words. The ease is raised here rather than left to
    // fail a check, and the copy below says it happened and why.
    int opening = std::clamp((int)std::lround(style.get("opening", 0.f)), 0, 2);
    double pullOnEase = 0.0;
    if (opening == 2) {
        double mustClear = std::max(tp.bust, tp.hip) + 2.0;
        // The waist is the narrowest row, and carries 0.7 of the ease.
        double needed = (mustClear - tp.waist) / 0.7;
        if (needed > ease) { pullOnEase = needed; ease = needed; }
    }
    tp.easeBust = ease; tp.easeWaist = ease * 0.7; tp.easeHip = ease;
    common::applyWaist(tp, style);

    double hipHalf = hip / 4.0 + tp.easeHip / 4.0;
    double hipY = tp.backWaistLength + tp.hipDepth;

    block::TorsoParams front = tp;
    block::TorsoParams back  = tp;

    // Per-silhouette shaping: where the skirt hem lands, and what extra
    // waypoints (between hip and hem) the side seam runs through on its
    // way there. Empire is different enough (a cropped bodice plus a
    // separate gathered skirt piece) that it's handled on its own below.
    double bodiceHemY = hemLength, bodiceHemHalf = hipHalf + hemFlare / 4.0;
    std::vector<Pt> extraPts;
    std::string silhouetteNote;
    switch (silhouette) {
        case kSheath:
            bodiceHemHalf = hipHalf + hemFlare * 0.15 / 4.0;
            silhouetteNote = "A close, straight column from hip to hem; hem flare has a light touch here by design.";
            break;
        case kFitAndFlare: {
            double kneeY = hipY + (hemLength - hipY) * 0.55;
            extraPts.push_back(Pt((float)(hipHalf * 0.97), (float)kneeY));
            bodiceHemHalf = hipHalf + hemFlare * 1.3 / 4.0;
            silhouetteNote = "Fitted through the hip, then flares from the knee to the hem.";
            break;
        }
        case kTrumpet: {
            double kneeY = hipY + (hemLength - hipY) * 0.8;
            double kneeHalf = hipHalf * 0.92;
            extraPts.push_back(Pt((float)kneeHalf, (float)kneeY));
            bodiceHemHalf = kneeHalf + hemFlare * 2.2 / 4.0;
            silhouetteNote = "Hugs close through the hip and thigh, then flares hard below the knee.";
            break;
        }
        case kEmpire:
            // Bodice below is cropped at the empire seam; see the skirt
            // piece built further down.
            silhouetteNote = "A raised seam under the bust carries a separately gathered, fuller skirt.";
            break;
        default: // A-Line
            silhouetteNote = "Runs straight from hip to a moderately flared hem.";
            break;
    }

    double empireSeamY = tp.backWaistLength - 11.0; // roughly the underbust line
    if (silhouette == kEmpire) { bodiceHemY = empireSeamY; bodiceHemHalf = bust / 4.0 + ease * 0.6 / 4.0; }

    // Both necklines are cut after the bodice length is known, and neither
    // may reach its hem: on the Empire silhouette the bodice is only a
    // little over 25 cm long, and a neckline past that would invert the
    // piece. A deep back cut simply opens the center back further down.
    double neckLimit = std::max(4.0, bodiceHemY - 4.0);
    front.neckDrop = std::min((double)style.get("neckDepthCm", 7.f), neckLimit);
    back.neckDrop  = std::min((double)style.get("backNeckDepthCm", 2.f), neckLimit);
    // ONE neck width for both panels. The neck point -- where the
    // neckline meets the shoulder seam -- has to land in the same place on
    // the front and the back, or the two shoulder seams come out different
    // lengths and cannot be sewn to each other. The DEPTHS stay
    // independent, which is how a deeper front neckline is cut.
    front.neckWidth = back.neckWidth = style.get("neckWidthCm", 12.f);

    // The armhole the sleeve cap must fit: front plus back.
    double armholeTotal = block::torsoArmholeLength(front) + block::torsoArmholeLength(back);

    Piece F; F.code = "F"; F.name = "Front"; F.cutQty = "CUT 1 ON FOLD / SHELL";
    F.note = "Cut on the fold at center front."; F.seamAllowanceCm = 1.0f; F.foldAtCF = true;
    F.sewing = block::torsoHalf(front, bodiceHemY, bodiceHemHalf, extraPts);
    common::finish(F);

    // WHERE IT OPENS CHANGES THE PIECES, not just the markings. A zip in
    // the center back needs a center back SEAM to go into -- so the back
    // is cut as a mirrored pair rather than one panel on the fold. A fold
    // has no seam to open, and a zip marked down one cannot be inserted.
    const bool cbSeam = (opening == 0);

    Piece B; B.code = "B"; B.name = "Back"; B.seamAllowanceCm = 1.0f;
    if (cbSeam) {
        B.cutQty = "CUT 2 MIRRORED / SHELL";
        B.note = "Center back seam, which is what the zip is set into.";
        B.foldAtCF = false;
    } else {
        B.cutQty = "CUT 1 ON FOLD / SHELL";
        B.note = "Cut on the fold at center back.";
        B.foldAtCF = true;
    }
    B.sewing = block::torsoHalf(back, bodiceHemY, bodiceHemHalf, extraPts);
    common::finish(B);

    // Neckline facings: the top band of each panel, cut from the panel
    // itself so the curve they are sewn along is literally the same curve.
    // They are re-derived after any outline edit (see Piece::derivedFromCode).
    Piece NF; NF.code = "NF"; NF.name = "Front neckline facing"; NF.cutQty = "CUT 1 ON FOLD / FACING CLOTH";
    NF.note = "Finish outer edge; sew to neckline, understitch and turn."; NF.seamAllowanceCm = 1.0f; NF.foldAtCF = true;
    NF.derivedFromCode = "F"; NF.facingMarginCm = 6.0f;
    NF.sewing = common::facingBand(F.sewing, NF.facingMarginCm);
    common::finish(NF);

    Piece NB; NB.code = "NB"; NB.name = "Back neckline facing"; NB.cutQty = "CUT 1 ON FOLD / FACING CLOTH";
    NB.note = "Finish outer edge; sew to neckline, understitch and turn."; NB.seamAllowanceCm = 1.0f; NB.foldAtCF = true;
    NB.derivedFromCode = "B"; NB.facingMarginCm = 6.0f;
    NB.sewing = common::facingBand(B.sewing, NB.facingMarginCm);
    common::finish(NB);

    out.pieces = { F, B, NF, NB };

    if (silhouette == kEmpire) {
        double gatherFullness = 1.7; // flat cut width vs. the seam it gathers onto
        double skirtTopHalf = bodiceHemHalf * gatherFullness;
        // The hem is never narrower than the gathered top: a gathered skirt
        // hangs straight at minimum and flares from there. (It used to come
        // out narrower, which read as the piece being upside down.)
        double skirtHemHalf = skirtTopHalf * 1.15 + hemFlare / 4.0;
        double skirtLen = hemLength - empireSeamY;
        Piece SK; SK.code = "SK"; SK.name = "Front skirt"; SK.cutQty = "CUT 1 ON FOLD / SHELL";
        SK.note = "Gather the top edge to fit the bodice hem before joining."; SK.seamAllowanceCm = 1.0f; SK.foldAtCF = true;
        SK.sewing = { Pt(0, 0), Pt((float)skirtTopHalf, 0), Pt((float)skirtHemHalf, (float)skirtLen), Pt(0, (float)skirtLen) };
        common::finish(SK);
        Piece SKB = SK; SKB.code = "SKB"; SKB.name = "Back skirt";
        out.pieces.push_back(SK);
        out.pieces.push_back(SKB);
    }

    double sleeveLen = style.get("sleeveLengthCm", 0.f);
    if (sleeveLen > 0.5) {
        block::SleeveParams sp;
        sp.armholeLen = armholeTotal;
        sp.sleeveLength = sleeveLen;
        sp.bicep = size.get("bicep", 27.0) + ease * 0.6;
        sp.wrist = style.get("sleeveHemCm", 20.f);
        sp.flareFrom = style.get("sleeveFlareFrom", 1.f);
        // A deeper armhole wants a WIDER sleeve, not a taller cap: set the cap
        // height from the armhole (the usual armhole/3.2) and solve the bicep to
        // make the cap seam match. Only if that comes out narrower than the arm
        // needs is the bicep held at that floor and the cap height solved instead.
        double capTarget = armholeTotal * block::kSleeveCapEase;
        double armFloor = sp.bicep;
        sp.capHeight = armholeTotal / 3.2;
        double solvedBicep = block::solveSleeveBicep(sp, capTarget);
        if (solvedBicep >= armFloor) sp.bicep = solvedBicep;
        else sp.capHeight = block::solveSleeveCapHeight(sp, capTarget);
        Piece SL; SL.code = "SL"; SL.name = "Sleeve"; SL.cutQty = "CUT 2 / SHELL";
        SL.note = "Ease cap into armhole; join underarm seam."; SL.seamAllowanceCm = 1.0f;
        SL.sewing = block::sleeve(sp);
        common::finish(SL);
        out.pieces.push_back(SL);
        // The cap is sewn into the front and back armholes together, so it
        // must measure the same, plus a little ease to shape the cap.
        out.checks.expectNear("Sleeve cap vs armhole (with ease)",
                              block::sleeveCapLength(sp), armholeTotal * block::kSleeveCapEase, armholeTotal * 0.075);
    }

    // A fitted dress has to open somewhere or it cannot be got into.
    {
        double zipFrom = back.neckDrop;
        double zipTo = std::min(bodiceHemY - 2.0, tp.backWaistLength + tp.hipDepth);
        for (auto& piece : out.pieces) {
            if (piece.code != "B") continue;
            Pt blo, bhi; geo::bounds(piece.sewing, blo, bhi);
            zipTo = std::min(zipTo, (double)bhi.y - 2.0);
            if (opening == 0 && zipTo > zipFrom + 8.0) {
                // Down the center back seam, which x = 0 now is.
                common::addZipper(piece, Pt(0.f, (float)zipFrom), Pt(0.f, (float)zipTo),
                                  "invisible zip, center back seam");
                common::addSeamOpening(piece, Pt(0.f, (float)zipTo), Pt(0.f, (float)zipFrom),
                                       "ZIP OPENING -- leave seam unsewn");
            } else if (opening == 1) {
                double from = tp.armholeDepth + 1.0;
                double to = std::min(zipTo, (double)bhi.y - 2.0);
                if (to > from + 8.0) {
                    common::addZipper(piece,
                                      Pt((float)geo::maxXAtY(piece.sewing, from), (float)from),
                                      Pt((float)geo::maxXAtY(piece.sewing, to), (float)to),
                                      "invisible zip, left side seam");
                    // The SEAM is shared by the front and the back, so both
                    // need to know where to stop stitching. The zip itself
                    // is one item and stays on the back; the opening is a
                    // property of the seam and belongs on both edges.
                    for (auto& side : out.pieces) {
                        if (side.code != "F" && side.code != "B") continue;
                        common::addSeamOpening(side,
                            Pt((float)geo::maxXAtY(side.sewing, to), (float)to),
                            Pt((float)geo::maxXAtY(side.sewing, from), (float)from),
                            "ZIP OPENING -- leave seam unsewn");
                    }
                }
            }
        }
    }

    // A slit: a run of seam left unsewn at the hem so the wearer can walk.
    // It goes in whichever seam the garment has -- center back when there
    // is one, the side seam otherwise -- and nothing is cut for it.
    double slitLen = style.get("slitLengthCm", 0.f);
    if (slitLen > 1.0) {
        for (auto& piece : out.pieces) {
            if (piece.code != (hemLength > bodiceHemY + 1.0 ? std::string("SKB") : std::string("B")))
                continue;
            Pt lo, hi; geo::bounds(piece.sewing, lo, hi);
            double top = std::max((double)lo.y + 2.0, hi.y - slitLen);
            if (hi.y - top < 2.0) continue;
            if (cbSeam) {
                common::addSeamOpening(piece, Pt(0.f, (float)hi.y), Pt(0.f, (float)top),
                                       "WALKING SLIT -- leave seam unsewn");
            } else {
                // A side slit is in the side seam, so both panels carry it.
                for (auto& side : out.pieces) {
                    if (side.code != piece.code && side.code != "F" && side.code != "B") continue;
                    Pt slo, shi; geo::bounds(side.sewing, slo, shi);
                    if (shi.y < top + 2.0) continue;
                    common::addSeamOpening(side,
                        Pt((float)geo::maxXAtY(side.sewing, shi.y), (float)shi.y),
                        Pt((float)geo::maxXAtY(side.sewing, top), (float)top),
                        "WALKING SLIT -- leave seam unsewn");
                }
            }
        }
    }

    common::prepareSetInSleeve(out.checks, out.pieces, out.seams, tp.armholeDepth, tp.shoulder,
                               block::torsoNeckHalfWidth(front));

    common::addSanityChecks(out.checks, out.pieces);
    common::expectClosuresOnCloth(out.checks, out.pieces);
    if (opening == 2) {
        // No opening at all, so the dress has to pass over the widest
        // part of the body it travels across on the way on.
        double mustClear = std::max(tp.bust, tp.hip) + 2.0;
        common::expectPullsOnOverBody(out.checks,
            "Pull-on dress passes over the body (no opening)",
            F.sewing, B.sewing, tp.armholeDepth + 2.0, bodiceHemY, mustClear);
    }
    common::expectSeamsSewable(out.checks, out.pieces, out.seams);
    common::expectSeamFractionsMatch(out.checks, out.pieces, out.seams);
    out.armholeY = tp.armholeDepth;
    out.shoulderX = block::torsoShoulderX(tp);

    // Front and back are sewn down the side seam, so their outer edge must
    // agree at every height below the armhole. (Their necklines are cut
    // independently, so only this edge is compared.)
    double belowArmholeY = tp.armholeDepth + 2.5;
    if (bodiceHemY - belowArmholeY > 2.0) {
        common::expectSideSeamsMatch(out.checks, "Front/back side seams match",
                                      F.sewing, B.sewing, belowArmholeY, bodiceHemY, 0.05);
    }
    if (silhouette == kEmpire) {
        out.checks.expectOutlineMatch("Front/back skirt panels match", out.pieces[4].sewing, out.pieces[5].sewing, 0.001);
    }

    out.cuttingList = common::defaultCuttingList(out.pieces);
    std::vector<std::string> extraNotes;
    if (pullOnEase > 0.0) {
        extraNotes.push_back(
            "NO OPENING was chosen, so the ease was raised to " +
            ofToString(pullOnEase, 0) + " cm to let the dress pass over the hip -- without a zip "
            "it has to travel over the widest part of the body to be put on, and a fitted woven "
            "dress cannot. That makes this a shift. For a close fit, choose a center back or side "
            "opening instead, or cut it in a knit, which stretches to do the same job.");
    }
    out.whatChanged = {
        std::string("This is a dart-free, cut-on-fold sloper in the ") + kNames[silhouette] + " line: " + silhouetteNote +
        " It is a generative development draft, not a fitted commercial pattern -- sew a toile.",
        "Silhouette, length, hem flare, front and back neckline depth, sleeve length and overall "
        "ease are all adjustable in the Style panel and regenerate every piece from the same "
        "underlying body block. A deep back neckline opens the center back into a low back; its "
        "facing (NB) follows the cut automatically.",
    };
    for (auto& n : extraNotes) out.whatChanged.push_back(n);

    out.constructionSteps = {
        "1 / Stay-stitch both necklines. Join front to back at the shoulder seams and both side seams.",
        "2 / Apply the neckline facings: right sides together, stitch, grade the seam, understitch the "
        "facing, then turn and press.",
        "3 / If a sleeve is included, ease its cap into the armhole matching underarm points, then sew "
        "the underarm and side seam in one pass.",
    };
    if (silhouette == kEmpire) {
        out.constructionSteps.push_back(
            "4 / Gather each skirt panel's top edge to match its bodice hem width, then join skirt to "
            "bodice at the empire seam, right sides together.");
        out.constructionSteps.push_back("5 / Turn and press a narrow double hem at the finished length.");
    } else {
        out.constructionSteps.push_back("4 / Turn and press a narrow double hem at the finished length.");
    }
    return out;
}

} // namespace pf
