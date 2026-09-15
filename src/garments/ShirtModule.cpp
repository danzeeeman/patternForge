#include "ShirtModule.h"
#include "GarmentCommon.h"
#include "BlockMath.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace pf {

namespace {
enum Fit { kSlim = 0, kClassic, kRelaxed, kOversized };
enum Front { kButtonFront = 0, kPullover };
}

std::vector<SizePreset> ShirtModule::sizePresets() const {
    SizePreset base;
    base.name = "US4";
    base.cm = { {"bust",86.4},{"waist",68.6},{"hip",94.0},{"backWaistLength",40.0},{"hipDepth",20.0},{"shoulder",12.5},{"neck",36.0},{"bicep",28.0},{"wrist",18.0} };
    return common::gradeFrom(base);
}

std::vector<StyleParamSpec> ShirtModule::styleParamSpecs() const {
    std::vector<StyleParamSpec> v = {
        { "fit", "Fit", 0.f, 3.f, 1.f, { "Slim", "Classic", "Relaxed", "Oversized" } },
        { "front", "Front", 0.f, 1.f, 0.f, { "Button-front (placket)", "Pullover (cut on fold)" } },
        { "shirtLength",   "Length (nape to hem, cm)", 60.f, 270.f, 72.f },
        { "armholeDepthCm", "Armhole depth below the shoulder (cm)", 16.f, 40.f, 22.f },
        { "shoulderWidthCm", "Shoulder width (cm)", 8.f, 34.f, 12.5f },
        { "armholeScoop", "Armhole scoop (1 = standard)", 0.5f, 1.6f, 1.f },
        { "sleeveLengthCm","Sleeve length (cm)", 20.f, 198.f, 58.f },
        { "sleeveHemCm", "Sleeve hem circumference (cm)", 10.f, 180.f, 24.f },
        { "sleeveFlareFrom", "Sleeve flare starts at (1 = straight taper)", 0.3f, 1.f, 1.f },
        { "neckDepthCm", "Front neckline depth (cm)", 4.f, 48.f, 7.5f },
        { "neckWidthCm", "Neckline width, front and back (cm)", 8.f, 24.f, 12.f },
        { "backNeckDepthCm", "Back neckline depth (cm)", 2.f, 48.f, 2.2f },
        { "collarDepthCm", "Collar band depth (cm)", 2.5f, 18.f, 3.5f },
        { "easeCm",        "Overall ease added to the body (cm)", 6.f, 72.f, 14.f },
        { "hemFlareCm",    "Hem flare below the hip (cm)", 0.f, 36.f, 2.f },
    };
    // The waist controls are the same on every garment that has a
    // torso, so the slider means one thing wherever it is met.
    for (auto& w : common::waistSpecs()) v.push_back(w);
    return v;
}

DesignResult ShirtModule::build(const SizePreset& size, const StyleParams& style) const {
    DesignResult out;
    out.garmentKey = key();
    out.stem = "Shirt-Pattern-Package";
    out.size = size;
    out.style = style;

    int fit = std::clamp((int)std::lround(style.get("fit", 1.f)), 0, 3);
    int front_ = std::clamp((int)std::lround(style.get("front", 0.f)), 0, 1);
    static const char* kFitNames[] = { "Slim", "Classic", "Relaxed", "Oversized" };
    static const double kFitEaseMul[] = { 0.55, 1.0, 1.5, 2.4 };
    bool pullover = (front_ == kPullover);
    out.title = std::string("Studio ") + kFitNames[fit] + (pullover ? " Pullover Shirt" : " Button-Front Shirt");

    double bust = size.get("bust", 86.4), hip = size.get("hip", 94.0);
    double ease = style.get("easeCm", 14.f) * kFitEaseMul[fit];
    double shirtLength = style.get("shirtLength", 72.f);
    double placket = pullover ? 0.0 : 2.2; // button-overlap width added at center front, cm

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
    tp.easeBust = ease; tp.easeWaist = ease * 0.9; tp.easeHip = ease;
    common::applyWaist(tp, style);

    double hipHalf = hip / 4.0 + tp.easeHip / 4.0;
    double hemHalfWidth = hipHalf + style.get("hemFlareCm", 2.f) / 4.0;

    double neckLimit = std::max(4.0, shirtLength - 4.0); // a neckline may never reach the hem
    block::TorsoParams front = tp;
    block::TorsoParams back  = tp;
    front.neckDrop = std::min((double)style.get("neckDepthCm", 7.5f), neckLimit);
    back.neckDrop  = std::min((double)style.get("backNeckDepthCm", 2.2f), neckLimit);
    // ONE neck width for both panels. The neck point -- where the
    // neckline meets the shoulder seam -- has to land in the same place on
    // the front and the back, or the two shoulder seams come out different
    // lengths and cannot be sewn to each other. The DEPTHS stay
    // independent, which is how a deeper front neckline is cut.
    front.neckWidth = back.neckWidth = style.get("neckWidthCm", 12.f);

    // The armhole the sleeve cap must fit: front plus back.
    double armholeTotal = block::torsoArmholeLength(front) + block::torsoArmholeLength(back);

    Piece F; F.code = "F"; F.name = "Front"; F.seamAllowanceCm = 1.0f;
    if (pullover) {
        F.cutQty = "CUT 1 ON FOLD / SHELL";
        F.note = "Cut on the fold at center front. No front opening -- ease over the head, or add a keyhole placket at the neck.";
        F.foldAtCF = true;
    } else {
        F.cutQty = "CUT 2 MIRRORED / SHELL";
        F.note = "Center front edge includes the button placket overlap.";
    }
    F.sewing = block::torsoHalf(front, shirtLength, hemHalfWidth);
    for (auto& pt : F.sewing) pt.x += placket; // shift the whole panel out from true CF
    common::finish(F);

    Piece B; B.code = "B"; B.name = "Back"; B.cutQty = "CUT 1 ON FOLD / SHELL"; B.foldAtCF = true;
    B.note = "Cut on the fold at center back; optional back yoke omitted in this draft.";
    B.seamAllowanceCm = 1.0f;
    B.sewing = block::torsoHalf(back, shirtLength, hemHalfWidth);
    common::finish(B);

    double sleeveLen = style.get("sleeveLengthCm", 58.f);
    block::SleeveParams sp;
    sp.armholeLen = armholeTotal;
    sp.sleeveLength = sleeveLen;
    sp.bicep = size.get("bicep", 27.0) + ease * 0.6;
    sp.wrist = style.get("sleeveHemCm", 24.f); // gathered into the cuff
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
    SL.note = "Ease cap into armhole; gather hem fullness into the cuff."; SL.seamAllowanceCm = 1.0f;
    SL.sewing = block::sleeve(sp);
    common::finish(SL);

    Piece CUFF; CUFF.code = "CUFF"; CUFF.name = "Cuff"; CUFF.cutQty = "CUT 4 / SHELL (2 PER CUFF)";
    CUFF.note = "Interface one layer per cuff."; CUFF.seamAllowanceCm = 1.0f;
    { double cuffLen = size.get("wrist", 18.0) + 3.0; CUFF.sewing = { {0,0}, {(float)cuffLen,0}, {(float)cuffLen,6.f}, {0,6.f} }; }
    CUFF.inMuslin = false;  // a cuff finishes the garment; it does not carry the fit
    common::finish(CUFF);

    block::CollarParams cp;
    // Half the neckline as actually drafted (center front, over one
    // shoulder, to center back) -- so the collar follows the neckline
    // sliders instead of the raw neck measurement.
    double halfNeckline = block::torsoNeckLength(front) + block::torsoNeckLength(back);
    cp.neckLen = halfNeckline;
    cp.depth = style.get("collarDepthCm", 3.5f);
    cp.spread = placket * 0.6;
    Piece COLLAR; COLLAR.code = "C"; COLLAR.name = "Collar & stand"; COLLAR.cutQty = "CUT 2 MIRRORED (OUTER + UNDER) / SHELL";
    COLLAR.note = "Interface the outer layer. CB at x=0, CF opening at the far end."; COLLAR.seamAllowanceCm = 1.0f;
    COLLAR.sewing = block::shirtCollar(cp);
    COLLAR.inMuslin = false;  // a collar finishes the garment; it does not carry the fit
    common::finish(COLLAR);

    // A button-front shirt runs its buttons down the center front, which
    // on this panel sits at the placket's inner edge; a pullover has none.
    // On a shirt with an applied placket the buttons go on the PLACKET,
    // not on the front panel -- that strip is the cloth they are sewn
    // through, which is what it is for. They are placed below, once the
    // placket piece exists.
    common::addButtonColumn(CUFF, 2.0, 2.0, 2.0, 1, 11.0, "cuff button");

    out.pieces = { F, B, SL, CUFF, COLLAR };
    // The shirt front is shifted out by the placket, so its landmarks sit
    // that much further along than the block coordinates.
    common::prepareSetInSleeve(out.checks, out.pieces, out.seams, tp.armholeDepth, tp.shoulder,
                               block::torsoNeckHalfWidth(front), "", placket);

    if (!pullover) {
        Piece PLACKET; PLACKET.code = "PL"; PLACKET.name = "Button placket"; PLACKET.cutQty = "CUT 2 / SHELL + 2 / INTERFACING";
        PLACKET.note = "Finished width " + std::to_string((int)placket) + " cm."; PLACKET.seamAllowanceCm = 1.0f;
        PLACKET.sewing = { {0,0}, {(float)placket,0}, {(float)placket,(float)shirtLength}, {0,(float)shirtLength} };
        PLACKET.inMuslin = false;  // a placket finishes the garment; it does not carry the fit
        {
            double firstY = front.neckDrop + 2.0;
            double lastY = std::max(firstY + 10.0, shirtLength - 6.0);
            common::addButtonColumn(PLACKET, placket / 2.0, firstY, lastY, 7, 11.0,
                                    "button (left placket)");
            common::addButtonholeColumn(PLACKET, placket / 2.0, firstY, lastY, 7, 11.0,
                                        "buttonhole (right placket)");
        }
        common::finish(PLACKET);
        out.pieces.push_back(PLACKET);
    }

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
    // agree at every height below the waist. The front is shifted out by
    // its placket, so that offset comes back off first.
    common::expectSideSeamsMatch(out.checks, "Front/back side seams match",
                                  geo::translate(F.sewing, Pt((float)-placket, 0)), B.sewing,
                                  tp.backWaistLength, shirtLength, 0.05);
    // The collar's neck edge is sewn to the neckline, so the drawn piece
    // must measure the drafted neckline plus the placket overlap.
    double collarEdge = 0;
    for (auto& p : COLLAR.sewing) collarEdge = std::max(collarEdge, (double)p.x);
    out.checks.expectNear("Collar neck edge vs drafted neckline", collarEdge, halfNeckline + cp.spread, 0.05);
    // The sleeve hem is gathered into the cuff, so the hem must be wider
    // than the cuff but not so much wider that it cannot be eased in.
    double cuffLength = 0;
    for (auto& p : CUFF.sewing) cuffLength = std::max(cuffLength, (double)p.x);
    double fullness = cuffLength > 0.01 ? sp.wrist / cuffLength : 0.0;
    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << fullness << "x the cuff (want 1.0-2.5x)";
        out.checks.add("Sleeve hem gathers into the cuff", ss.str(), fullness >= 1.0 && fullness <= 2.5);
    }

    out.cuttingList = common::defaultCuttingList(out.pieces);
    out.whatChanged = {
        std::string("A ") + kFitNames[fit] + "-fit woven shirt block: front and back share the same "
        "dart-free bodice as the dress module, finished with a set-in sleeve, gathered cuff and banded "
        "collar. " + (pullover
            ? "This version has no front opening -- it's cut on the fold at center front, pullover-style."
            : "It opens at center front with a button placket.") +
        " Development draft -- sew a toile before cutting final cloth.",
        "Fit, front (button vs. pullover), length, sleeve length, collar depth and overall ease are "
        "all adjustable in the Style panel.",
    };
    out.constructionSteps = {};
    if (!pullover) out.constructionSteps.push_back("1 / Apply the placket to each front edge, then join front to back at the shoulders and sides.");
    else out.constructionSteps.push_back("1 / Join front to back at the shoulders and sides.");
    out.constructionSteps.push_back(
        "2 / Set the collar: join outer and under collar, understitch, turn, then attach to the neckline "
        "matching center back" + std::string(pullover ? "." : " and the placket edges."));
    out.constructionSteps.push_back("3 / Ease each sleeve cap into its armhole, then sew the underarm/side seam in one pass.");
    out.constructionSteps.push_back("4 / Gather the sleeve hem to fit the cuff; attach the cuff and finish with a placket opening.");
    out.constructionSteps.push_back("5 / Turn and stitch a narrow hem at the finished length.");
    return out;
}

} // namespace pf
