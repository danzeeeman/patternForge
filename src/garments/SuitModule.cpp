#include "SuitModule.h"
#include "SuitDraft.h"
#include "GarmentCommon.h"
#include <algorithm>
#include <cmath>

namespace pf {

std::vector<SizePreset> SuitModule::sizePresets() const {
    SizePreset base;
    base.name = "US4";
    base.cm = { {"bust",86.4},{"waist",68.6},{"hip",94.0},{"backWaistLength",40.0},{"hipDepth",20.0},{"shoulder",13.0},{"neck",36.0},{"bicep",30.0},{"wrist",24.0},{"rise",27.0},{"inseam",74.0},{"thigh",55.0},{"ankle",42.0} };
    return common::gradeFrom(base);
}

std::vector<StyleParamSpec> SuitModule::styleParamSpecs() const {
    std::vector<StyleParamSpec> v = {
        { "lapel", "Lapel", 0.f, 2.f, 0.f, { "Notched", "Peak", "Shawl" } },
        { "legStyle", "Trouser leg", 0.f, 3.f, 0.f, { "Straight", "Tapered", "Wide", "Flared" } },
        { "back", "Jacket back", 0.f, 1.f, 0.f, { "Center back seam", "No center back seam (cut on fold)" } },
        { "jacketLengthCm", "Jacket length (nape to hem, cm)", 55.f, 210.f, 70.f },
        { "lapelWidthCm",   "Lapel width (cm)", 3.f, 24.f, 8.f },
        { "breakYCm",       "Lapel break point (cm below the nape)", 26.f, 120.f, 46.f },
        { "wrapCm",         "Front extension past center front (cm)", 1.f, 24.f, 2.f },
        { "facingWidthCm",  "Front facing width at the hem (cm)", 5.f, 30.f, 9.f },
        { "armholeDepthCm", "Armhole depth below the shoulder (cm)", 16.f, 40.f, 22.f },
        { "shoulderWidthCm","Shoulder width (cm)", 8.f, 34.f, 13.f },
        { "sleeveLengthCm", "Sleeve length (cm)", 45.f, 198.f, 60.f },
        { "cuffWidthCm",    "Finished sleeve-hem circumference (cm)", 20.f, 180.f, 28.f },
        { "easeCm",         "Jacket ease added to the body (cm)", 4.f, 60.f, 10.f },
        { "inseamCm",       "Trouser inseam length (cm)", 50.f, 130.f, 76.f },
        { "riseCm",         "Trouser rise, waist to crotch (cm)", 22.f, 48.f, 28.f },
        { "legWidthCm",     "Trouser hem circumference (cm)", 26.f, 120.f, 44.f },
        { "trouserEaseCm",  "Trouser hip ease (cm)", 0.f, 30.f, 6.f },
    };
    // The waist controls are the same on every garment that has a
    // torso, so the slider means one thing wherever it is met.
    for (auto& w : common::waistSpecs()) v.push_back(w);
    return v;
}

DesignResult SuitModule::build(const SizePreset& size, const StyleParams& style) const {
    DesignResult out;
    out.garmentKey = key();
    out.stem = "Pant-Suit-Pattern-Package";
    out.size = size;
    out.style = style;

    int lapel = std::clamp((int)std::lround(style.get("lapel", 0.f)), 0, 2);
    int legStyle = std::clamp((int)std::lround(style.get("legStyle", 0.f)), 0, 3);
    int back_ = std::clamp((int)std::lround(style.get("back", 0.f)), 0, 1);
    static const char* kLapelNames[] = { "Notched", "Peak", "Shawl" };
    static const char* kLegNames[] = { "Straight", "Tapered", "Wide", "Flared" };
    out.title = std::string("Studio ") + kLapelNames[lapel] + "-Lapel Pant Suit";

    suit::JacketSpec js;
    js.bust = size.get("bust", 86.4);
    js.waist = size.get("waist", 68.6);
    js.hip = size.get("hip", 94.0);
    js.backWaistLength = size.get("backWaistLength", 40.0);
    js.hipDepth = size.get("hipDepth", 20.0);
    js.neck = size.get("neck", 36.0);
    js.bicep = size.get("bicep", 30.0);
    js.shoulder = style.get("shoulderWidthCm", 13.f);
    js.ease = style.get("easeCm", 10.f);
    js.waistShaping = style.get("waistShaping", 1.f);
    js.waistRise    = style.get("waistRiseCm", 0.f);
    js.jacketLength = style.get("jacketLengthCm", 70.f);
    js.lapel = (block::LapelStyle)lapel;
    js.wrap = style.get("wrapCm", 2.f);
    js.breakY = style.get("breakYCm", 46.f);
    js.lapelWidth = style.get("lapelWidthCm", 8.f);
    js.facingWidth = style.get("facingWidthCm", 9.f);
    js.armholeDepth = style.get("armholeDepthCm", 22.f);
    js.sleeveLength = style.get("sleeveLengthCm", 60.f);
    js.cuffWidth = style.get("cuffWidthCm", 28.f);
    js.centerBackSeam = (back_ == 0);

    suit::TrouserSpec ts;
    ts.waist = size.get("waist", 68.6);
    ts.hip = size.get("hip", 94.0);
    ts.thigh = size.get("thigh", 55.0);
    ts.rise = style.get("riseCm", 28.f);
    ts.inseam = style.get("inseamCm", 76.f);
    ts.ankleWidth = style.get("legWidthCm", 44.f);
    ts.easeHip = style.get("trouserEaseCm", 6.f);
    ts.easeWaist = 2.0;

    // The leg styles are the same block with a different knee, exactly as
    // in the standalone Pants module.
    double ankle = ts.ankleWidth;
    double thighWidth = ts.hip / 2.0 + ts.easeHip / 2.0;
    std::string legNote;
    switch (legStyle) {
        case 1: ts.kneeWidth = ankle * 1.15;
                legNote = "Tapered through the knee to a narrower hem."; break;
        case 2: ts.kneeWidth = std::max(ankle, thighWidth * 0.9);
                ts.ankleWidth = std::max(ankle, ts.kneeWidth);
                legNote = "Wide from the hip down, knee and hem at least thigh width."; break;
        case 3: ts.kneeWidth = ankle * 0.78;
                legNote = "Held in at the knee, then flaring out to the hem."; break;
        default: ts.kneeWidth = ankle;
                legNote = "Straight from the knee to the hem."; break;
    }

    // One jacket draft, shared with the Suit Jacket module. The trousers
    // take a "P" prefix so no piece code appears twice in the package.
    suit::Draft jacket = suit::draftJacket(js, "");
    suit::Draft trousers = suit::draftTrousers(ts, "P");

    out.pieces = jacket.pieces;
    out.pieces.insert(out.pieces.end(), trousers.pieces.begin(), trousers.pieces.end());
    out.seams = jacket.seams;
    out.seams.insert(out.seams.end(), trousers.seams.begin(), trousers.seams.end());
    out.checks = jacket.checks;
    for (auto& c : trousers.checks.results()) out.checks.add(c.name, c.detail, c.pass);
    out.armholeY = jacket.armholeY;
    out.shoulderX = jacket.shoulderX;

    // The two halves are cut from one cloth to one body, so the piece
    // codes must be unique across the whole package -- a duplicate would
    // put two different pieces under one label on the cutting list.
    bool unique = true;
    for (size_t i = 0; i < out.pieces.size() && unique; ++i)
        for (size_t j = i + 1; j < out.pieces.size(); ++j)
            if (out.pieces[i].code == out.pieces[j].code) { unique = false; break; }
    out.checks.add("Jacket and trouser piece codes do not collide",
                   std::to_string(out.pieces.size()) + " pieces, all distinct codes", unique);
    // Jacket and trousers meet at the body's waist, so they are drafted to
    // one waist measurement rather than two.
    out.checks.expectNear("Jacket and trousers drafted to one waist", js.waist, ts.waist, 0.01);

    out.cuttingList = common::defaultCuttingList(out.pieces);
    out.whatChanged = {
        std::string("A ") + kLapelNames[lapel] + "-lapel jacket and a " + kLegNames[legStyle] +
        " trouser, drafted to the same body and exported as one package. The jacket is the same "
        "draft the Suit Jacket garment produces -- both call one shared routine, so the two cannot "
        "drift apart. " + legNote,
        "Jacket pieces carry their usual codes (F, B, SL, UC, FF); trouser pieces are prefixed with "
        "P (PF, PB, PWB) so nothing collides on the cutting list. Lapel style, jacket length and "
        "ease, and the trouser leg, rise, inseam and hem are all adjustable in the Style panel.",
        "Development draft -- dart-free blocks with a one-piece sleeve and no chest canvas. Sew a "
        "toile of both halves before cutting cloth.",
    };
    out.constructionSteps = {
        "1 / Cut the jacket and the trousers from one length of cloth, keeping every piece on the "
        "same grain -- a suit reads as a suit because both halves fall the same way.",
        "2 / Jacket: mark and press the roll line, join shoulders and side seams, set the under "
        "collar, then sew and turn the facings so the lapels roll back.",
        "3 / Jacket: set the sleeves, easing the cap into the armhole, and hem.",
        "4 / Trousers: join each front to its back at the outseam, then the inseams, then close "
        "the crotch seam.",
        "5 / Trousers: apply the waistband, add the closure, and hem to the finished length.",
    };
    return out;
}

} // namespace pf
