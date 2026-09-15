#include "PantsModule.h"
#include "GarmentCommon.h"
#include "BlockMath.h"
#include <algorithm>
#include <cmath>

namespace pf {

namespace {
enum LegStyle { kStraight = 0, kSkinny, kBootcut, kWideLeg };
}

std::vector<SizePreset> PantsModule::sizePresets() const {
    SizePreset base;
    base.name = "US4";
    base.cm = { {"waist",68.6},{"hip",94.0},{"rise",27.0},{"inseam",74.0},{"thigh",55.0},{"ankle",34.0} };
    return common::gradeFrom(base);
}

std::vector<StyleParamSpec> PantsModule::styleParamSpecs() const {
    return {
        { "legStyle", "Leg style", 0.f, 3.f, 0.f, { "Straight", "Skinny", "Bootcut", "Wide-Leg / Flare" } },
        // Inseam and rise are body-length measurements, not a stylistic
        // degree of freedom -- tripled like every other slider (to 270cm
        // and 102cm) they produce a leg so long relative to the hip/ankle
        // widths that the crotch region looks like a separate blob stuck
        // on a stick rather than a bigger pair of pants. Capped lower, at
        // real (if generous) proportions, instead.
        { "inseamCm",   "Inseam length (cm)", 50.f, 130.f, 74.f },
        { "riseCm",     "Rise, waist to crotch (cm)", 22.f, 48.f, 27.f },
        { "legWidthCm", "Finished ankle-hem circumference (cm)", 26.f, 180.f, 34.f },
        { "waistEaseCm","Waist ease (cm)", 0.f, 24.f, 2.f },
        { "hipEaseCm",  "Hip ease (cm)", 0.f, 42.f, 4.f },
    };
}

DesignResult PantsModule::build(const SizePreset& size, const StyleParams& style) const {
    DesignResult out;
    out.garmentKey = key();
    out.stem = "Pants-Pattern-Package";
    out.size = size;
    out.style = style;

    int legStyle = std::clamp((int)std::lround(style.get("legStyle", 0.f)), 0, 3);
    static const char* kNames[] = { "Straight-Leg", "Skinny", "Bootcut", "Wide-Leg" };
    out.title = std::string("Studio ") + kNames[legStyle] + " Trouser";

    block::TrouserParams front;
    front.waist = size.get("waist", 68.6);
    front.hip = size.get("hip", 94.0);
    front.rise = style.get("riseCm", 27.f);
    front.inseam = style.get("inseamCm", 74.f);
    front.thigh = size.get("thigh", 55.0);
    front.ankleWidth = style.get("legWidthCm", 34.f);
    front.easeWaist = style.get("waistEaseCm", 2.f);
    front.easeHip = style.get("hipEaseCm", 4.f);
    front.isFront = true;

    // Leg style sets the knee circumference against the hem the slider
    // sets: the same block, a different knee.
    double ankle = front.ankleWidth;
    double thighWidth = front.hip / 2.0 + front.easeHip / 2.0; // front + back panel width at hip level
    std::string legStyleNote;
    switch (legStyle) {
        case kSkinny:
            front.kneeWidth = ankle * 1.15;
            legStyleNote = "Tapers through the knee to the hem; pair it with a narrow hem.";
            break;
        case kBootcut:
            front.kneeWidth = ankle * 0.82;
            legStyleNote = "Fitted through the knee, flaring out below it to the hem.";
            break;
        case kWideLeg:
            front.kneeWidth = std::max(ankle, thighWidth * 0.9);
            front.ankleWidth = std::max(ankle, front.kneeWidth);
            legStyleNote = "Wide from the hip down; the knee and hem are at least thigh width.";
            break;
        default:
            front.kneeWidth = ankle;
            legStyleNote = "Straight from the knee to the hem.";
            break;
    }

    block::TrouserParams back = front;
    back.isFront = false;

    Piece F; F.code = "F"; F.name = "Front leg"; F.cutQty = "CUT 2 MIRRORED / SHELL";
    F.note = "Front rise carries a shallower crotch curve than the back."; F.seamAllowanceCm = 1.0f;
    F.sewing = block::trouserHalf(front);
    common::finish(F);

    Piece B; B.code = "B"; B.name = "Back leg"; B.cutQty = "CUT 2 MIRRORED / SHELL";
    B.note = "Back rise carries extra seat room in the crotch curve."; B.seamAllowanceCm = 1.0f;
    B.sewing = block::trouserHalf(back);
    common::finish(B);

    double waistband = 2.0 * (front.waist / 4.0 + front.easeWaist / 4.0 + back.waist / 4.0 + back.easeWaist / 4.0);
    Piece WB; WB.code = "WB"; WB.name = "Waistband"; WB.cutQty = "CUT 1 / SHELL + 1 / INTERFACING";
    WB.note = "Cut flat at full length -- this piece is the whole band, not half of one. Finished length equals the waist plus ease and closure overlap."; WB.seamAllowanceCm = 1.0f;
    WB.sewing = { {0,0}, {(float)(waistband + 4.0), 0}, {(float)(waistband + 4.0), 5.f}, {0, 5.f} };
    WB.inMuslin = false;  // a waistband finishes the garment; it does not carry the fit
    common::finish(WB);

    // Fly zip down the front rise, and the button that closes the band.
    common::addZipper(F, Pt(0.f, 1.f), Pt(0.f, (float)(front.rise * 0.55)), "fly zipper");
    common::addButtonColumn(WB, 2.5, 2.5, 2.5, 1, 18.0, "waistband button");
    common::addButtonholeColumn(WB, waistband + 1.5, 2.5, 2.5, 1, 18.0, "waistband buttonhole");

    out.pieces = { F, B, WB };
    common::prepareTrousers(out.checks, out.pieces, out.seams);

    common::addSanityChecks(out.checks, out.pieces);
    common::expectClosuresOnCloth(out.checks, out.pieces);
    common::expectSeamsSewable(out.checks, out.pieces, out.seams);
    common::expectSeamFractionsMatch(out.checks, out.pieces, out.seams);
    // Front and back legs are sewn to each other along the inseam and the
    // outseam, so those seam lines must be the same length on both panels.
    out.checks.expectNear("Front/back inseam lengths match",
        block::trouserInseamLength(front), block::trouserInseamLength(back), 1.0);
    out.checks.expectNear("Front/back outseam lengths match",
        block::trouserOutseamLength(front), block::trouserOutseamLength(back), 1.0);
    // front/back share one body waist measurement -- the finished band is
    // just that waist plus its ease, not front+back added together.
    out.checks.expectNear("Waistband vs waist + ease", waistband,
        front.waist + front.easeWaist, 0.01);

    out.cuttingList = common::defaultCuttingList(out.pieces);
    out.whatChanged = {
        std::string("A ") + kNames[legStyle] + " trouser block: front and back legs share the same "
        "waist/hip/inseam measurements and the same knee and hem widths centered on the crease "
        "line, and differ only in crotch-curve extension, so their inseams and outseams match "
        "for sewing. " + legStyleNote + " Development draft -- sew a toile.",
        "Leg style, inseam length, rise, ankle-hem width and both waist/hip ease are adjustable in "
        "the Style panel. Front and back always meet at a real (curved) center seam here -- the "
        "crotch curve can't be cut on a straight fold the way a coat or shirt front can.",
    };
    out.constructionSteps = {
        "1 / Join each front to its back at the outseam, then join the two inseams.",
        "2 / Sew the front crotch curve to the back crotch curve to close the center seam.",
        "3 / Apply the waistband, matching side seams, and finish with the closure of your choice.",
        "4 / Turn and stitch a hem at the finished ankle length.",
    };
    return out;
}

} // namespace pf
