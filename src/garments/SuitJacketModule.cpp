#include "SuitJacketModule.h"
#include "SuitDraft.h"
#include "GarmentCommon.h"
#include <algorithm>
#include <cmath>

namespace pf {

std::vector<SizePreset> SuitJacketModule::sizePresets() const {
    SizePreset base;
    base.name = "US4";
    base.cm = { {"bust",86.4},{"waist",68.6},{"hip",94.0},{"backWaistLength",40.0},{"hipDepth",20.0},{"shoulder",13.0},{"neck",36.0},{"bicep",30.0},{"wrist",24.0} };
    return common::gradeFrom(base);
}

std::vector<StyleParamSpec> SuitJacketModule::styleParamSpecs() const {
    std::vector<StyleParamSpec> v = {
        { "lapel", "Lapel", 0.f, 2.f, 0.f, { "Notched", "Peak", "Shawl" } },
        { "back", "Back", 0.f, 1.f, 0.f, { "Center back seam", "No center back seam (cut on fold)" } },
        { "jacketLengthCm", "Length (nape to hem, cm)", 55.f, 210.f, 70.f },
        { "lapelWidthCm",   "Lapel width (cm)", 3.f, 24.f, 8.f },
        { "breakYCm",       "Lapel break point (cm below the nape)", 26.f, 120.f, 46.f },
        { "wrapCm",         "Front extension past center front (cm)", 1.f, 24.f, 2.f },
        { "facingWidthCm",  "Front facing width at the hem (cm)", 5.f, 30.f, 9.f },
        { "armholeDepthCm", "Armhole depth below the shoulder (cm)", 16.f, 40.f, 22.f },
        { "shoulderWidthCm","Shoulder width (cm)", 8.f, 34.f, 13.f },
        { "armholeScoop",   "Armhole scoop (1 = standard)", 0.5f, 1.6f, 1.f },
        { "sleeveLengthCm", "Sleeve length (cm)", 45.f, 198.f, 60.f },
        { "cuffWidthCm",    "Finished sleeve-hem circumference (cm)", 20.f, 180.f, 28.f },
        { "backNeckDepthCm","Back neckline depth (cm)", 1.f, 20.f, 2.5f },
        { "backNeckWidthCm","Neckline width, front and back (cm)", 8.f, 24.f, 12.f },
        { "hemFlareCm",     "Hem flare (total circumference add, cm)", 0.f, 90.f, 0.f },
        { "easeCm",         "Overall ease added to the body (cm)", 4.f, 60.f, 10.f },
    };
    // The waist controls are the same on every garment that has a
    // torso, so the slider means one thing wherever it is met.
    for (auto& w : common::waistSpecs()) v.push_back(w);
    return v;
}

DesignResult SuitJacketModule::build(const SizePreset& size, const StyleParams& style) const {
    DesignResult out;
    out.garmentKey = key();
    out.stem = "Suit-Jacket-Pattern-Package";
    out.size = size;
    out.style = style;

    int lapel = std::clamp((int)std::lround(style.get("lapel", 0.f)), 0, 2);
    int back_ = std::clamp((int)std::lround(style.get("back", 0.f)), 0, 1);
    static const char* kLapelNames[] = { "Notched", "Peak", "Shawl" };
    out.title = std::string("Studio ") + kLapelNames[lapel] + "-Lapel Suit Jacket";

    suit::JacketSpec spec;
    spec.bust = size.get("bust", 86.4);
    spec.waist = size.get("waist", 68.6);
    spec.hip = size.get("hip", 94.0);
    spec.backWaistLength = size.get("backWaistLength", 40.0);
    spec.hipDepth = size.get("hipDepth", 20.0);
    spec.neck = size.get("neck", 36.0);
    spec.bicep = size.get("bicep", 30.0);
    spec.shoulder = style.get("shoulderWidthCm", 13.f);
    spec.ease = style.get("easeCm", 10.f);
    spec.waistShaping = style.get("waistShaping", 1.f);
    spec.waistRise    = style.get("waistRiseCm", 0.f);
    spec.jacketLength = style.get("jacketLengthCm", 70.f);
    spec.hemFlare = style.get("hemFlareCm", 0.f);
    spec.lapel = (block::LapelStyle)lapel;
    spec.wrap = style.get("wrapCm", 2.f);
    spec.breakY = style.get("breakYCm", 46.f);
    spec.lapelWidth = style.get("lapelWidthCm", 8.f);
    spec.facingWidth = style.get("facingWidthCm", 9.f);
    spec.armholeDepth = style.get("armholeDepthCm", 22.f);
    spec.armholeScoop = style.get("armholeScoop", 1.f);
    spec.sleeveLength = style.get("sleeveLengthCm", 60.f);
    spec.cuffWidth = style.get("cuffWidthCm", 28.f);
    spec.backNeckDepth = style.get("backNeckDepthCm", 2.5f);
    spec.backNeckWidth = style.get("backNeckWidthCm", 12.f);
    spec.centerBackSeam = (back_ == 0);

    suit::Draft jacket = suit::draftJacket(spec, "");
    out.pieces = jacket.pieces;
    out.seams = jacket.seams;
    out.checks = jacket.checks;
    out.armholeY = jacket.armholeY;
    out.shoulderX = jacket.shoulderX;

    out.cuttingList = common::defaultCuttingList(out.pieces);

    static const char* kLapelBlurb[] = {
        "A notched lapel: the collar and lapel meet in a cut-out step, which is the ordinary "
        "business-jacket collar.",
        "A peak lapel: the lapel point sweeps up past the notch instead of being cut square "
        "across it, which reads as the more formal of the two.",
        "A shawl collar: no notch at all, one unbroken curve from the break point around the "
        "neck, as on a dinner jacket.",
    };
    out.whatChanged = {
        std::string("A tailored jacket drafted from the same body block as the Coat, cut to jacket "
        "length and finished with a lapel rather than a neckline. ") + kLapelBlurb[lapel] +
        " The front edge runs past center front for the buttons, and the lapel is drafted by "
        "reflecting its worn shape across the roll line, so folding the piece back along that "
        "line lands the lapel on the chest.",
        "The front facing is cut from the same lapel curve as the front, so the two turn cleanly "
        "against each other. Lapel style and width, the break point, the button extension, the "
        "facing width, length, sleeve, armhole and ease are all adjustable in the Style panel.",
        "Development draft -- this is a dart-free block with a one-piece sleeve and no chest "
        "canvas, which a real tailored jacket would all have. Sew a toile before cutting cloth.",
    };
    out.constructionSteps = {
        "1 / Mark the roll line on both fronts and press it, but do not cut it -- it is a fold.",
        "2 / Join front to back at the shoulder and side seams.",
        "3 / Sew the under collar to the neckline, matching the collar point to the notch.",
        "4 / Sew each facing to its front, face to face, around the lapel and down the front edge; "
        "turn and press so the lapel rolls back along the roll line.",
        "5 / Set the sleeves, easing the cap into the armhole.",
        "6 / Turn and stitch the hem, then add buttons and buttonholes on the front extension.",
    };
    return out;
}

} // namespace pf
