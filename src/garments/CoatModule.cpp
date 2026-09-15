#include "CoatModule.h"
#include "GarmentCommon.h"
#include "BlockMath.h"
#include <algorithm>
#include <cmath>

namespace pf {

namespace {
enum Silhouette { kALine = 0, kTailored, kCocoon };
enum Front { kSeamed = 0, kFold };
}

std::vector<SizePreset> CoatModule::sizePresets() const {
    SizePreset base;
    base.name = "US4";
    base.cm = { {"bust",86.4},{"waist",68.6},{"hip",94.0},{"backWaistLength",40.0},{"hipDepth",20.0},{"shoulder",13.0},{"neck",36.0},{"bicep",30.0},{"wrist",24.0} };
    return common::gradeFrom(base);
}

std::vector<StyleParamSpec> CoatModule::styleParamSpecs() const {
    std::vector<StyleParamSpec> v = {
        { "silhouette", "Silhouette", 0.f, 2.f, 0.f, { "A-Line", "Tailored", "Cocoon" } },
        { "front", "Front", 0.f, 1.f, 0.f, { "Center seam (opens for closure)", "No center seam (cut on fold)" } },
        { "coatLength",   "Length (nape to hem, cm)", 90.f, 390.f, 115.f },
        { "hemFlareCm",   "Hem flare (total circumference add, cm)", 0.f, 120.f, 14.f },
        { "armholeDepthCm", "Armhole depth below the shoulder (cm)", 16.f, 40.f, 22.f },
        { "shoulderWidthCm", "Shoulder width (cm)", 8.f, 34.f, 13.f },
        { "armholeScoop", "Armhole scoop (1 = standard)", 0.5f, 1.6f, 1.f },
        { "sleeveLengthCm","Sleeve length (cm)", 45.f, 198.f, 58.f },
        { "cuffWidthCm",  "Finished sleeve-hem circumference (cm)", 24.f, 180.f, 40.f },
        { "sleeveFlareFrom", "Sleeve flare starts at (1 = straight taper)", 0.3f, 1.f, 1.f },
        { "neckDepthCm", "Front neckline depth (cm)", 4.f, 48.f, 8.f },
        { "neckWidthCm", "Neckline width, front and back (cm)", 8.f, 24.f, 12.f },
        { "backNeckDepthCm", "Back neckline depth (cm)", 2.f, 48.f, 2.5f },
        { "collarDepthCm","Collar depth (cm)", 4.f, 36.f, 7.f },
        { "easeCm",       "Overall ease added to the body (cm)", 10.f, 78.f, 16.f },
    };
    // The waist controls are the same on every garment that has a
    // torso, so the slider means one thing wherever it is met.
    for (auto& w : common::waistSpecs()) v.push_back(w);
    return v;
}

DesignResult CoatModule::build(const SizePreset& size, const StyleParams& style) const {
    DesignResult out;
    out.garmentKey = key();
    out.stem = "Coat-Pattern-Package";
    out.size = size;
    out.style = style;

    int silhouette = std::clamp((int)std::lround(style.get("silhouette", 0.f)), 0, 2);
    int front_ = std::clamp((int)std::lround(style.get("front", 0.f)), 0, 1);
    static const char* kNames[] = { "A-Line", "Tailored", "Cocoon" };
    bool fold = (front_ == kFold);
    out.title = std::string("Studio ") + kNames[silhouette] + " Coat";

    double bust = size.get("bust", 86.4), hip = size.get("hip", 94.0);
    double ease = style.get("easeCm", 16.f);
    double coatLength = style.get("coatLength", 115.f);
    double hemFlare = style.get("hemFlareCm", 14.f);

    block::TorsoParams tp;
    tp.bust = bust; tp.waist = size.get("waist", 68.6); tp.hip = hip;
    tp.backWaistLength = size.get("backWaistLength", 40.0);
    tp.hipDepth = size.get("hipDepth", 20.0);
    // Armhole controls: the shoulder point (the armhole's top), how far
    // below it the underarm sits, and how deeply the curve scoops in.
    tp.shoulder = style.get("shoulderWidthCm", 13.0f);
    tp.armholeDepth = style.get("armholeDepthCm", 22.f);
    tp.armholeScoop = style.get("armholeScoop", 1.f);
    tp.neck = size.get("neck", 36.0);
    tp.easeBust = ease; tp.easeWaist = ease * 0.8; tp.easeHip = ease;
    common::applyWaist(tp, style);

    double hipHalf = hip / 4.0 + tp.easeHip / 4.0;
    double hemHalfWidth = hipHalf + hemFlare / 4.0;
    std::vector<Pt> extraPts;
    std::string silhouetteNote;
    switch (silhouette) {
        case kTailored: {
            double waistY = tp.backWaistLength;
            double waistHalf = tp.waist / 4.0 + ease * 0.35 / 4.0; // nips in more than the default block waist
            extraPts.push_back(Pt((float)waistHalf, (float)(waistY + 4.0)));
            silhouetteNote = "Nipped in at the waist, then releases to the hem.";
            break;
        }
        case kCocoon:
            tp.easeBust *= 1.6; tp.easeWaist = tp.easeBust; tp.easeHip *= 1.3;
            hemHalfWidth = (hip / 4.0 + tp.easeHip / 4.0) * 0.82; // rounds back in toward the hem
            silhouetteNote = "Rounded and oversized through the body, tapering back in at the hem.";
            break;
        default:
            silhouetteNote = "Runs straight from hip to a flared hem.";
            break;
    }

    double neckLimit = std::max(4.0, coatLength - 4.0); // a neckline may never reach the hem
    block::TorsoParams front = tp;
    block::TorsoParams back  = tp;
    front.neckDrop = std::min((double)style.get("neckDepthCm", 8.f), neckLimit);
    back.neckDrop  = std::min((double)style.get("backNeckDepthCm", 2.5f), neckLimit);
    // ONE neck width for both panels. The neck point -- where the
    // neckline meets the shoulder seam -- has to land in the same place on
    // the front and the back, or the two shoulder seams come out different
    // lengths and cannot be sewn to each other. The DEPTHS stay
    // independent, which is how a deeper front neckline is cut.
    front.neckWidth = back.neckWidth = style.get("neckWidthCm", 12.f);

    // The armhole the sleeve cap must fit: front plus back.
    double armholeTotal = block::torsoArmholeLength(front) + block::torsoArmholeLength(back);

    // A coat that opens at center front needs cloth PAST center front to
    // put the buttons on -- without it a button sits on the cut edge with
    // nothing to sew through. Cut on the fold there is no opening, so no
    // extension either.
    double buttonExt = fold ? 0.0 : 3.0;

    Piece F; F.code = "F"; F.name = "Front"; F.seamAllowanceCm = 1.0f;
    if (fold) {
        F.cutQty = "CUT 1 ON FOLD / SHELL";
        F.note = "Cut on the fold at center front. No front opening -- pull on, or add a separate zip elsewhere.";
        F.foldAtCF = true;
    } else {
        F.cutQty = "CUT 2 MIRRORED / SHELL";
        F.note = "Front edge carries a 3 cm button extension past center front.";
    }
    // The extension ADDS cloth past center front rather than moving the
    // panel: translating it would leave the button on the cut edge again,
    // just further out. cfX = -buttonExt is the same way the jacket front
    // carries its button wrap.
    {
        auto neckline = block::arc(Pt(0, 0), block::torsoNeckHalfWidth(front),
                                   front.neckDrop, 90.0, 0.0, 6);
        // The extension has to run the WHOLE length of the front edge, up
        // to the neckline. Starting the upper edge at center front instead
        // leaves the edge slanting in from the hem to the neck, which
        // crowds the lower buttons against it.
        if (buttonExt > 0.01)
            neckline.insert(neckline.begin(), Pt((float)-buttonExt, (float)front.neckDrop));
        F.sewing = buttonExt > 0.01
            ? block::torsoHalfShaped(front, coatLength, hemHalfWidth, extraPts, neckline, -buttonExt)
            : block::torsoHalf(front, coatLength, hemHalfWidth, extraPts);
    }
    common::finish(F);

    Piece B; B.code = "B"; B.name = "Back"; B.cutQty = "CUT 1 ON FOLD / SHELL"; B.foldAtCF = true;
    B.note = "Cut on the fold at center back."; B.seamAllowanceCm = 1.0f;
    B.sewing = block::torsoHalf(back, coatLength, hemHalfWidth, extraPts);
    common::finish(B);

    block::SleeveParams sp;
    sp.armholeLen = armholeTotal;
    sp.sleeveLength = style.get("sleeveLengthCm", 58.f);
    sp.bicep = size.get("bicep", 30.0) + ease * 0.7;
    // cuffWidthCm is a finished circumference, which is exactly what
    // SleeveParams::wrist wants -- halving it made the sleeve hem come
    // out half the width the slider asked for.
    sp.wrist = style.get("cuffWidthCm", 40.f);
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

    block::CollarParams cp;
    // Half the neckline as actually drafted (center front, over one
    // shoulder, to center back), so the collar follows the neckline sliders.
    double halfNeckline = block::torsoNeckLength(front) + block::torsoNeckLength(back);
    cp.neckLen = halfNeckline;
    cp.depth = style.get("collarDepthCm", 7.f);
    cp.spread = 3.0;
    Piece C; C.code = "C"; C.name = "Collar"; C.cutQty = "CUT 4 / SHELL: TWO MIRRORED PAIRS";
    C.note = "Interface the outer pair. Baste the roll shape in a toile before final sewing.";
    C.seamAllowanceCm = 1.0f;
    C.sewing = block::shirtCollar(cp);
    C.inMuslin = false;  // a collar finishes the garment; it does not carry the fit
    common::finish(C);

    // Underarm gusset, same 7cm-square construction as the reference coats.
    Piece G; G.code = "G"; G.name = "Underarm gusset"; G.cutQty = "CUT 2 / SHELL";
    G.note = "Four 7 cm sewing edges; corners are seam-stop points."; G.seamAllowanceCm = 1.0f;
    G.sewing = { {0,0}, {7,0}, {7,7}, {0,7} };
    common::finish(G);

    // A coat with a center seam opens, so it needs a closure; one cut on
    // the fold has no opening to close.
    if (!fold) {
        double firstY = tp.armholeDepth + 4.0;
        double lastY = std::max(firstY + 10.0, coatLength - 16.0);
        // Buttons sit on true center front, which the extension leaves
        // `buttonExt` clear of the cut edge.
        common::addButtonColumn(F, 0.0, firstY, lastY, 4, 22.0, "button (left front)");
        common::addButtonholeColumn(F, 0.0, firstY, lastY, 4, 22.0, "buttonhole (right front)");
    }

    out.pieces = { F, B, SL, C, G };
    common::prepareSetInSleeve(out.checks, out.pieces, out.seams, tp.armholeDepth, tp.shoulder,
                               block::torsoNeckHalfWidth(front));

    common::addSanityChecks(out.checks, out.pieces);
    common::expectClosuresOnCloth(out.checks, out.pieces);
    common::expectSeamsSewable(out.checks, out.pieces, out.seams);
    common::expectSeamFractionsMatch(out.checks, out.pieces, out.seams);
    // The sleeve cap is sewn into the front and back armholes together, so
    // it must measure the same, plus a little ease to shape the cap.
    out.checks.expectNear("Sleeve cap vs armhole (with ease)",
                          block::sleeveCapLength(sp), armholeTotal * block::kSleeveCapEase, armholeTotal * 0.075);
    out.armholeY = tp.armholeDepth;
    out.shoulderX = block::torsoShoulderX(tp);

    // Front and back are sewn down the side seam, so their outer edge must
    // agree at every height below the waist.
    common::expectSideSeamsMatch(out.checks, "Front/back side seams match",
                                  F.sewing, B.sewing, tp.backWaistLength, coatLength, 0.05);
    double collarEdge = 0;
    for (auto& p : C.sewing) collarEdge = std::max(collarEdge, (double)p.x);
    out.checks.expectNear("Collar neck edge vs drafted neckline", collarEdge, halfNeckline + cp.spread, 0.05);

    out.cuttingList = common::defaultCuttingList(out.pieces);
    out.whatChanged = {
        std::string("This ") + kNames[silhouette] + " coat is drafted fresh from the body block (front/back, "
        "long sleeve, collar and underarm gusset) rather than remixed from the Facet/Prism source PDFs "
        "already in this repo -- that PDF-extraction path is not implemented yet (see the project README). " +
        silhouetteNote + (fold
            ? " This version has no front opening -- it's cut on the fold at center front."
            : " It opens at center front for a closure.") +
        " Development draft -- sew a toile before cutting final cloth.",
        "Silhouette, front, length, hem flare, sleeve length, cuff width, collar depth and overall ease "
        "are all adjustable in the Style panel.",
    };
    out.constructionSteps = {
        "1 / Join front to back at the shoulder seams and side seams, leaving the gusset openings "
        "(7 cm on the body side seam, 7 cm on the sleeve underarm seam) unsewn.",
        "2 / Set each sleeve, ending the seam runs at the gusset stop points, then insert the gusset "
        "into the opening: four separate 7 cm seams between the corner dots.",
        "3 / Baste the collar's roll shape over a ham before final sewing, then attach it to the neckline.",
        std::string("4 / Turn and stitch a hem at the finished length") +
            (fold ? "." : "; add the closure of your choice at center front."),
    };
    return out;
}

} // namespace pf
