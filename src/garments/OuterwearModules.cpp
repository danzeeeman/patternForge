#include "OuterwearModules.h"
#include "OuterwearDraft.h"
#include "GarmentCommon.h"
#include <algorithm>
#include <cmath>

namespace pf {

static SizePreset baselineSize() {
    SizePreset base;
    base.name = "US4";
    base.cm = { {"bust",86.4},{"waist",68.6},{"hip",94.0},{"backWaistLength",40.0},{"hipDepth",20.0},{"shoulder",13.5},{"neck",36.0},{"bicep",30.0},{"wrist",24.0} };
    return base;
}


namespace {

std::vector<SizePreset> outerwearSizes() {
    SizePreset us4;
    us4.name = "US4";
    us4.cm = {
        {"bust", 86.4}, {"waist", 68.6}, {"hip", 94.0},
        {"backWaistLength", 40.0}, {"hipDepth", 20.0},
        {"shoulder", 13.5}, {"neck", 36.0},
        {"bicep", 30.0}, {"wrist", 24.0},
    };
    return { us4 };
}

// The sliders every coat here shares. Ranges are set around the kind's own
// defaults, which are filled in per module below -- so the same slider
// means the same thing on all three, and only its starting value differs.
std::vector<StyleParamSpec> sharedSpecs(const outerwear::Spec& d) {
    std::vector<StyleParamSpec> v = {
        { "lapel", "Lapel", 0.f, 2.f, (float)(int)d.lapel, { "Notched", "Peak", "Shawl" } },
        { "lengthCm",      "Length (nape to hem, cm)", 60.f, 330.f, (float)d.length },
        { "wrapCm",        "Front extension past center front (cm)", 1.f, 30.f, (float)d.wrap },
        { "lapelWidthCm",  "Lapel width (cm)", 3.f, 27.f, (float)d.lapelWidth },
        { "breakYCm",      "Lapel break point (cm below the nape)", 26.f, 120.f, (float)d.breakY },
        { "collarDepthCm", "Collar depth (cm)", 4.f, 36.f, (float)d.collarDepth },
        { "facingWidthCm", "Front facing width at the hem (cm)", 5.f, 36.f, (float)d.facingWidth },
        { "hemFlareCm",    "Hem flare (total circumference add, cm)", 0.f, 120.f, (float)d.hemFlare },
        { "ventLengthCm",  "Back vent length (cm, 0 = no vent)", 0.f, 60.f, (float)d.ventLength },
        { "armholeDepthCm","Armhole depth below the shoulder (cm)", 16.f, 42.f, (float)d.armholeDepth },
        { "shoulderWidthCm","Shoulder width (cm)", 8.f, 38.f, (float)d.shoulder },
        { "sleeveLengthCm","Sleeve length (cm)", 45.f, 198.f, (float)d.sleeveLength },
        { "cuffWidthCm",   "Finished sleeve-hem circumference (cm)", 24.f, 180.f, (float)d.cuffWidth },
        { "easeCm",        "Overall ease added to the body (cm)", 8.f, 78.f, (float)d.ease },
    };
    // The waist controls are the same on every garment that has a
    // torso, so the slider means one thing wherever it is met.
    for (auto& w : common::waistSpecs()) v.push_back(w);
    return v;
}

// Reads the shared sliders back onto a spec already carrying its kind's
// defaults, so anything a module does not expose keeps that default.
void readShared(outerwear::Spec& s, const SizePreset& size, const StyleParams& style) {
    s.bust = size.get("bust", 86.4);
    s.waist = size.get("waist", 68.6);
    s.hip = size.get("hip", 94.0);
    s.backWaistLength = size.get("backWaistLength", 40.0);
    s.hipDepth = size.get("hipDepth", 20.0);
    s.neck = size.get("neck", 36.0);
    s.bicep = size.get("bicep", 30.0);

    s.lapel = (block::LapelStyle)std::clamp((int)std::lround(style.get("lapel", (float)(int)s.lapel)), 0, 2);
    s.length = style.get("lengthCm", (float)s.length);
    s.wrap = style.get("wrapCm", (float)s.wrap);
    s.lapelWidth = style.get("lapelWidthCm", (float)s.lapelWidth);
    s.breakY = style.get("breakYCm", (float)s.breakY);
    s.collarDepth = style.get("collarDepthCm", (float)s.collarDepth);
    s.facingWidth = style.get("facingWidthCm", (float)s.facingWidth);
    s.hemFlare = style.get("hemFlareCm", (float)s.hemFlare);
    s.ventLength = style.get("ventLengthCm", (float)s.ventLength);
    s.armholeDepth = style.get("armholeDepthCm", (float)s.armholeDepth);
    s.shoulder = style.get("shoulderWidthCm", (float)s.shoulder);
    s.sleeveLength = style.get("sleeveLengthCm", (float)s.sleeveLength);
    s.cuffWidth = style.get("cuffWidthCm", (float)s.cuffWidth);
    s.ease = style.get("easeCm", (float)s.ease);
    s.waistShaping = style.get("waistShaping", 1.f);
    s.waistRise    = style.get("waistRiseCm", 0.f);
    // The wrap is what makes a coat double-breasted: past about 6 cm past
    // center front there is room for a second row of buttons, and the
    // fronts cross the body rather than meeting.
    s.doubleBreasted = s.wrap >= 6.0;
}

DesignResult finishDesign(outerwear::Spec& spec, const SizePreset& size, const StyleParams& style,
                          const std::string& key, const std::string& stem, const std::string& title) {
    DesignResult out;
    out.garmentKey = key;
    out.stem = stem;
    out.size = size;
    out.style = style;
    out.title = title;

    common::Draft d = outerwear::draft(spec);
    out.pieces = d.pieces;
    out.seams = d.seams;
    out.checks = d.checks;
    out.armholeY = d.armholeY;
    out.shoulderX = d.shoulderX;
    out.cuttingList = common::defaultCuttingList(out.pieces);
    return out;
}

const char* kLapelNames[] = { "Notched", "Peak", "Shawl" };

} // namespace

// --- Overcoat --------------------------------------------------------

std::vector<SizePreset> OvercoatModule::sizePresets() const { return common::gradeFrom(baselineSize()); }

std::vector<StyleParamSpec> OvercoatModule::styleParamSpecs() const {
    outerwear::Spec d;
    outerwear::applyKindDefaults(d, outerwear::Kind::Overcoat);
    return sharedSpecs(d);
}

DesignResult OvercoatModule::build(const SizePreset& size, const StyleParams& style) const {
    outerwear::Spec spec;
    outerwear::applyKindDefaults(spec, outerwear::Kind::Overcoat);
    readShared(spec, size, style);

    DesignResult out = finishDesign(spec, size, style, key(), "Overcoat-Pattern-Package",
        std::string("Studio ") + kLapelNames[(int)spec.lapel] + "-Lapel Overcoat");
    out.whatChanged = {
        "A full-length overcoat: the lapelled torso block cut long, over enough ease to go on top "
        "of a suit jacket rather than against the body. " +
        std::string(spec.doubleBreasted
            ? "The front extension is wide enough to cross the body, so it buttons double-breasted."
            : "Single-breasted, with a modest button extension past center front.") +
        (spec.ventLength > 0.1
            ? " A vent at the center back keeps it from binding across the seat when you walk."
            : " No back vent -- add one if it pulls when you stride."),
        "Length, ease, lapel style and width, the break point, the vent, the sleeve and the collar "
        "are all adjustable in the Style panel. Seam allowances are 1.5 cm, not the usual 1 cm: "
        "coat cloth is thick and wants the extra to turn.",
        "Development draft -- a dart-free block with a one-piece sleeve and no chest canvas. "
        "Sew the muslin in this package before cutting coat cloth, which is expensive to get wrong.",
    };
    out.constructionSteps = {
        "1 / Mark and press the roll line on both fronts; do not cut it.",
        "2 / Join the center back seam, leaving the vent open below its top mark.",
        "3 / Join fronts to back at the shoulders and side seams, matching the balance notches.",
        "4 / Set the under collar to the neckline, then sew the facings on around the lapels and "
        "down the front edges; turn and press so the lapels roll back.",
        "5 / Set the sleeves, easing the cap into the armhole.",
        "6 / Finish the vent, hem the coat, and work the buttonholes on the front extension.",
    };
    return out;
}

// --- Peacoat ---------------------------------------------------------

std::vector<SizePreset> PeacoatModule::sizePresets() const { return common::gradeFrom(baselineSize()); }

std::vector<StyleParamSpec> PeacoatModule::styleParamSpecs() const {
    outerwear::Spec d;
    outerwear::applyKindDefaults(d, outerwear::Kind::Peacoat);
    return sharedSpecs(d);
}

DesignResult PeacoatModule::build(const SizePreset& size, const StyleParams& style) const {
    outerwear::Spec spec;
    outerwear::applyKindDefaults(spec, outerwear::Kind::Peacoat);
    readShared(spec, size, style);

    DesignResult out = finishDesign(spec, size, style, key(), "Peacoat-Pattern-Package",
        std::string("Studio ") + kLapelNames[(int)spec.lapel] + "-Lapel Peacoat");
    out.whatChanged = {
        "A peacoat: short enough to end at the hip, double-breasted over a wide front extension, "
        "and carrying a deliberately deep collar. That collar is the garment's whole character -- "
        "it turns up against weather, so it is cut with the height to stand rather than lie flat.",
        "Being hip length it needs no back vent, so the vent slider starts at zero. Cut it in "
        "melton or any dense wool; the 1.5 cm seam allowances assume thick cloth.",
        "Development draft -- a dart-free block with a one-piece sleeve and no chest canvas. "
        "Sew the muslin in this package first.",
    };
    out.constructionSteps = {
        "1 / Mark and press the roll line on both fronts.",
        "2 / Join the center back seam, then fronts to back at the shoulders and side seams.",
        "3 / Set the under collar. Press the stand and fall over a ham so it holds its roll turned "
        "up as well as turned down -- a peacoat collar has to work both ways.",
        "4 / Sew the facings on around the lapels and down the front edges; turn and press.",
        "5 / Set the sleeves, easing the cap into the armhole.",
        "6 / Hem, then work both rows of buttonholes on the front extension.",
    };
    return out;
}

// --- Trench ----------------------------------------------------------

std::vector<SizePreset> TrenchCoatModule::sizePresets() const { return common::gradeFrom(baselineSize()); }

std::vector<StyleParamSpec> TrenchCoatModule::styleParamSpecs() const {
    outerwear::Spec d;
    outerwear::applyKindDefaults(d, outerwear::Kind::Trench);
    auto specs = sharedSpecs(d);
    // The pieces that make it a trench rather than a long overcoat, each
    // switchable, because plenty of trenches drop one or another.
    specs.push_back({ "stormFlap", "Storm flap", 0.f, 1.f, 1.f, { "No", "Yes" } });
    specs.push_back({ "belt", "Belt", 0.f, 1.f, 1.f, { "No", "Yes" } });
    specs.push_back({ "epaulettes", "Epaulettes", 0.f, 1.f, 1.f, { "No", "Yes" } });
    specs.push_back({ "beltWidthCm", "Belt width (cm)", 2.f, 15.f, (float)d.beltWidth });
    return specs;
}

DesignResult TrenchCoatModule::build(const SizePreset& size, const StyleParams& style) const {
    outerwear::Spec spec;
    outerwear::applyKindDefaults(spec, outerwear::Kind::Trench);
    readShared(spec, size, style);
    spec.stormFlap = style.get("stormFlap", 1.f) > 0.5f;
    spec.belt = style.get("belt", 1.f) > 0.5f;
    spec.epaulettes = style.get("epaulettes", 1.f) > 0.5f;
    spec.beltWidth = style.get("beltWidthCm", (float)spec.beltWidth);

    DesignResult out = finishDesign(spec, size, style, key(), "Trench-Coat-Pattern-Package",
        std::string("Studio ") + kLapelNames[(int)spec.lapel] + "-Lapel Trench Coat");

    std::string extras;
    if (spec.stormFlap) extras += "a storm flap over the right chest";
    if (spec.belt) extras += extras.empty() ? "a belt" : ", a belt";
    if (spec.epaulettes) extras += extras.empty() ? "epaulettes" : " and epaulettes";
    out.whatChanged = {
        "A trench coat: long, double-breasted and flared through the hem, with " +
        (extras.empty() ? std::string("none of the usual trench detailing switched on") : extras) +
        ". Those pieces are what separate a trench from a long overcoat -- the storm flap sheds "
        "water running off the shoulder clear of the front opening, and the belt lets the coat be "
        "pulled in at the waist without being cut fitted.",
        "Each of the three is switchable in the Style panel, along with length, ease, the lapel, "
        "the vent and the sleeve. Cut it in gabardine or a coated cotton.",
        "Development draft -- a dart-free block with a one-piece sleeve, no chest canvas, and no "
        "raglan option yet (a great many trenches are raglan-sleeved). Sew the muslin first.",
    };
    out.constructionSteps = {
        "1 / Mark and press the roll line on both fronts.",
        "2 / Join the center back seam, leaving the vent open below its top mark.",
        "3 / Join fronts to back at the shoulders and side seams. Catch the storm flap and the "
        "epaulettes in the shoulder and armhole seams as you go -- they are not applied on top.",
        "4 / Set the under collar, then the facings; turn and press the lapels back along the roll.",
        "5 / Set the sleeves, easing the cap into the armhole.",
        "6 / Make the belt, add its buckle and keepers, finish the vent and hem, and work the "
        "buttonholes on the front extension.",
    };
    return out;
}

} // namespace pf
