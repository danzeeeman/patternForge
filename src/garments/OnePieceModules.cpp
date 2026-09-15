#include "OnePieceModules.h"
#include "GarmentCommon.h"
#include "BlockMath.h"
#include <algorithm>
#include <cmath>

namespace pf {

static SizePreset baselineSize() {
    SizePreset base;
    base.name = "US4";
    base.cm = { {"bust",86.4},{"waist",68.6},{"hip",94.0},{"backWaistLength",40.0},{"hipDepth",20.0},{"shoulder",12.5},{"neck",36.0},{"bicep",28.0},{"wrist",22.0},{"rise",27.0},{"inseam",74.0},{"thigh",55.0},{"ankle",36.0} };
    return base;
}


namespace {

SizePreset onePieceSize() {
    SizePreset us4;
    us4.name = "US4";
    us4.cm = {
        {"bust", 86.4}, {"waist", 68.6}, {"hip", 94.0},
        {"backWaistLength", 40.0}, {"hipDepth", 20.0},
        {"shoulder", 12.5}, {"neck", 36.0},
        {"bicep", 28.0}, {"wrist", 22.0},
        {"rise", 27.0}, {"inseam", 74.0}, {"thigh", 55.0}, {"ankle", 36.0},
    };
    return us4;
}

// The waist edge of a panel: its width along its top (trouser) or bottom
// (bodice) row. A jumpsuit is joined along these, so they must agree.
double edgeWidthAtY(const Ring& r, double y) {
    return geo::maxXAtY(r, y) - geo::minXAtY(r, y);
}

} // namespace

// --- Jumpsuit --------------------------------------------------------

std::vector<SizePreset> JumpsuitModule::sizePresets() const { return common::gradeFrom(baselineSize()); }

std::vector<StyleParamSpec> JumpsuitModule::styleParamSpecs() const {
    std::vector<StyleParamSpec> v = {
        { "legStyle", "Leg", 0.f, 3.f, 0.f, { "Straight", "Tapered", "Wide", "Flared" } },
        { "sleeveLengthCm", "Sleeve length (cm, 0 = sleeveless)", 0.f, 198.f, 0.f },
        { "neckDepthCm",    "Front neckline depth (cm)", 4.f, 48.f, 10.f },
        { "neckWidthCm",    "Front neckline width (cm)", 8.f, 26.f, 13.f },
        { "backNeckDepthCm","Back neckline depth (cm)", 1.f, 48.f, 2.5f },
        { "armholeDepthCm", "Armhole depth below the shoulder (cm)", 16.f, 40.f, 21.f },
        { "shoulderWidthCm","Shoulder width (cm)", 8.f, 34.f, 12.5f },
        { "inseamCm",       "Inseam length (cm)", 40.f, 130.f, 76.f },
        { "riseCm",         "Rise, waist to crotch (cm)", 22.f, 48.f, 28.f },
        { "legWidthCm",     "Finished hem circumference (cm)", 26.f, 150.f, 40.f },
        { "bodiceEaseCm",   "Bodice ease (cm)", 2.f, 40.f, 8.f },
        { "hipEaseCm",      "Hip ease (cm)", 0.f, 36.f, 5.f },
    };
    // The waist controls are the same on every garment that has a
    // torso, so the slider means one thing wherever it is met.
    for (auto& w : common::waistSpecs()) v.push_back(w);
    return v;
}

DesignResult JumpsuitModule::build(const SizePreset& size, const StyleParams& style) const {
    DesignResult out;
    out.garmentKey = key();
    out.stem = "Jumpsuit-Pattern-Package";
    out.size = size;
    out.style = style;

    int legStyle = std::clamp((int)std::lround(style.get("legStyle", 0.f)), 0, 3);
    static const char* kLegNames[] = { "Straight", "Tapered", "Wide", "Flared" };
    out.title = std::string("Studio ") + kLegNames[legStyle] + "-Leg Jumpsuit";

    double bodiceEase = style.get("bodiceEaseCm", 8.f);
    double hipEase = style.get("hipEaseCm", 5.f);

    block::TorsoParams tp;
    tp.bust = size.get("bust", 86.4);
    tp.waist = size.get("waist", 68.6);
    tp.hip = size.get("hip", 94.0);
    tp.backWaistLength = size.get("backWaistLength", 40.0);
    tp.hipDepth = size.get("hipDepth", 20.0);
    tp.shoulder = style.get("shoulderWidthCm", 12.5f);
    tp.neck = size.get("neck", 36.0);
    tp.armholeDepth = style.get("armholeDepthCm", 21.f);
    tp.easeBust = bodiceEase;
    tp.easeWaist = bodiceEase * 0.75;
    tp.easeHip = hipEase;
    common::applyWaist(tp, style);

    block::TorsoParams front = tp, back = tp;
    front.neckDrop = style.get("neckDepthCm", 10.f);
    back.neckDrop = style.get("backNeckDepthCm", 2.5f);
    front.neckWidth = back.neckWidth = style.get("neckWidthCm", 13.f);

    // The bodice stops at the waist, and its hem IS the waist seam.
    double waistY = tp.backWaistLength;
    double bodiceWaistHalf = tp.waist / 4.0 + tp.easeWaist / 4.0;

    Piece F; F.code = "F"; F.name = "Bodice front"; F.cutQty = "CUT 1 ON FOLD / SHELL";
    F.note = "Cut on the fold at center front. Joins the trouser front at the waist.";
    F.seamAllowanceCm = 1.0f; F.foldAtCF = true;
    F.sewing = block::torsoHalf(front, waistY, bodiceWaistHalf);
    common::finish(F);

    Piece B; B.code = "B"; B.name = "Bodice back"; B.cutQty = "CUT 1 ON FOLD / SHELL";
    B.note = "Cut on the fold at center back. Joins the trouser back at the waist.";
    B.seamAllowanceCm = 1.0f; B.foldAtCF = true;
    B.sewing = block::torsoHalf(back, waistY, bodiceWaistHalf);
    common::finish(B);

    block::TrouserParams tf;
    tf.waist = tp.waist; tf.hip = tp.hip;
    tf.rise = style.get("riseCm", 28.f);
    tf.inseam = style.get("inseamCm", 76.f);
    tf.thigh = size.get("thigh", 55.0);
    tf.ankleWidth = style.get("legWidthCm", 40.f);
    // The trouser must be drafted to the SAME waist ease as the bodice, or
    // the waist seam the two are joined along cannot close.
    tf.easeWaist = tp.easeWaist;
    tf.easeHip = hipEase;
    tf.isFront = true;

    double ankle = tf.ankleWidth;
    double thighWidth = tf.hip / 2.0 + tf.easeHip / 2.0;
    std::string legNote;
    switch (legStyle) {
        case 1: tf.kneeWidth = ankle * 1.15; legNote = "Tapered to a narrower hem."; break;
        case 2: tf.kneeWidth = std::max(ankle, thighWidth * 0.9);
                tf.ankleWidth = std::max(ankle, tf.kneeWidth);
                legNote = "Wide from the hip down."; break;
        case 3: tf.kneeWidth = ankle * 0.78; legNote = "Held at the knee, flaring to the hem."; break;
        default: tf.kneeWidth = ankle; legNote = "Straight from the knee down."; break;
    }
    block::TrouserParams tb = tf;
    tb.isFront = false;

    Piece PF; PF.code = "PF"; PF.name = "Trouser front"; PF.cutQty = "CUT 2 MIRRORED / SHELL";
    PF.note = "Joins the bodice front at the waist."; PF.seamAllowanceCm = 1.0f;
    PF.sewing = block::trouserHalf(tf);
    common::finish(PF);

    Piece PB; PB.code = "PB"; PB.name = "Trouser back"; PB.cutQty = "CUT 2 MIRRORED / SHELL";
    PB.note = "Joins the bodice back at the waist."; PB.seamAllowanceCm = 1.0f;
    PB.sewing = block::trouserHalf(tb);
    common::finish(PB);

    out.pieces = { F, B, PF, PB };

    double sleeveLen = style.get("sleeveLengthCm", 0.f);
    double armholeTotal = block::torsoArmholeLength(front) + block::torsoArmholeLength(back);
    if (sleeveLen > 0.5) {
        block::SleeveParams sp;
        sp.armholeLen = armholeTotal;
        sp.sleeveLength = sleeveLen;
        sp.bicep = size.get("bicep", 28.0) + bodiceEase * 0.6;
        sp.wrist = 24.0;
        double capTarget = armholeTotal * block::kSleeveCapEase;
        double armFloor = sp.bicep;
        sp.capHeight = armholeTotal / 3.2;
        double solvedBicep = block::solveSleeveBicep(sp, capTarget);
        if (solvedBicep >= armFloor) sp.bicep = solvedBicep;
        else sp.capHeight = block::solveSleeveCapHeight(sp, capTarget);
        Piece SL; SL.code = "SL"; SL.name = "Sleeve"; SL.cutQty = "CUT 2 / SHELL";
        SL.note = "Ease the cap into the armhole."; SL.seamAllowanceCm = 1.0f;
        SL.sewing = block::sleeve(sp);
        common::finish(SL);
        out.pieces.push_back(SL);
        out.checks.expectNear("Sleeve cap vs armhole (with ease)",
                              block::sleeveCapLength(sp), capTarget, armholeTotal * 0.075);
    }

    // A jumpsuit closes nowhere else, so it needs a long zip to get into.
    for (auto& piece : out.pieces) {
        if (piece.code != "B") continue;
        common::addZipper(piece,
                          Pt((float)geo::maxXAtY(piece.sewing, tp.armholeDepth + 1.0),
                             (float)(tp.armholeDepth + 1.0)),
                          Pt((float)geo::maxXAtY(piece.sewing, waistY - 1.0), (float)(waistY - 1.0)),
                          "invisible zip, left side seam");
    }

    common::prepareSetInSleeve(out.checks, out.pieces, out.seams,
                               tp.armholeDepth, tp.shoulder, block::torsoNeckHalfWidth(front));
    common::prepareTrousers(out.checks, out.pieces, out.seams, "P");
    out.armholeY = tp.armholeDepth;
    out.shoulderX = block::torsoShoulderX(tp);

    // The waist seam: the join that makes this one garment instead of two.
    // The bodice is cut on the fold (half a panel) and the trouser is cut
    // as a pair, so compare what each contributes to one quarter of the
    // body and then to the whole waist.
    Pt pflo, pfhi; geo::bounds(PF.sewing, pflo, pfhi);
    Pt pblo, pbhi; geo::bounds(PB.sewing, pblo, pbhi);
    double bodiceWaist = 2.0 * (edgeWidthAtY(F.sewing, waistY) + edgeWidthAtY(B.sewing, waistY));
    double trouserWaist = 2.0 * (edgeWidthAtY(PF.sewing, pflo.y) + edgeWidthAtY(PB.sewing, pblo.y));
    out.checks.expectNear("Bodice waist and trouser waist match (the seam that joins them)",
                          bodiceWaist, trouserWaist, 1.5);

    common::addSanityChecks(out.checks, out.pieces);
    common::expectSeamsSewable(out.checks, out.pieces, out.seams);
    common::expectSeamFractionsMatch(out.checks, out.pieces, out.seams);
    common::expectClosuresOnCloth(out.checks, out.pieces);
    common::expectSideSeamsMatch(out.checks, "Bodice front/back side seams match",
                                 F.sewing, B.sewing, tp.armholeDepth + 2.5, waistY, 0.05);

    out.cuttingList = common::defaultCuttingList(out.pieces);
    out.whatChanged = {
        std::string("A jumpsuit: the dress bodice and the trouser block, joined at the waist. ") +
        kLegNames[legStyle] + " leg -- " + legNote +
        " The two halves are drafted to the same waist measurement and the same waist ease, which "
        "is what lets the seam between them close; they come from different blocks, so nothing "
        "would make them agree by accident.",
        "A jumpsuit has no other opening, so it takes a long side-seam zip -- without one it "
        "cannot be got into. Leg style, inseam, rise, hem width, the neckline, the sleeve and both "
        "eases are adjustable in the Style panel.",
        "Development draft -- dart-free blocks, sew a toile. Check the rise on the body before "
        "cutting: a jumpsuit that is short in the body pulls at the shoulders and nothing below "
        "the waist can fix it.",
    };
    out.constructionSteps = {
        "1 / Join bodice front to bodice back at the shoulders.",
        "2 / Join each trouser front to its back at the outseam and inseam, then close the rises.",
        "3 / Sew the bodice to the trouser around the waist, matching center fronts, center backs "
        "and side seams. This is the seam the whole garment hangs from -- baste it first.",
        "4 / Sew the side seams, leaving the zip opening. Set the zip.",
        "5 / Set the sleeves if the design has them; otherwise bind or face the armholes.",
        "6 / Face the neckline, and hem each leg to the finished length.",
    };
    return out;
}

// --- Bodysuit --------------------------------------------------------

std::vector<SizePreset> BodysuitModule::sizePresets() const { return common::gradeFrom(baselineSize()); }

std::vector<StyleParamSpec> BodysuitModule::styleParamSpecs() const {
    std::vector<StyleParamSpec> v = {
        { "sleeveLengthCm", "Sleeve length (cm, 0 = sleeveless)", 0.f, 70.f, 0.f },
        { "neckDepthCm",    "Front neckline depth (cm)", 4.f, 40.f, 12.f },
        { "neckWidthCm",    "Front neckline width (cm)", 8.f, 26.f, 14.f },
        { "backNeckDepthCm","Back neckline depth (cm)", 1.f, 40.f, 4.f },
        { "armholeDepthCm", "Armhole depth below the shoulder (cm)", 14.f, 34.f, 20.f },
        { "shoulderWidthCm","Shoulder width (cm)", 4.f, 28.f, 10.f },
        { "riseYCm",        "Crotch depth below the nape (cm)", 52.f, 96.f, 68.f },
        { "legOpeningYCm",  "Leg cut height at the side (cm below the nape)", 44.f, 90.f, 58.f },
        { "crotchWidthCm",  "Crotch width (cm)", 4.f, 20.f, 9.f },
        // Negative by default: a bodysuit is knit and is held on by being
        // smaller than the body. Positive values here make a woven one.
        { "stretchEaseCm",  "Ease (cm -- negative for knit)", -20.f, 12.f, -6.f },
    };
    // The waist controls are the same on every garment that has a
    // torso, so the slider means one thing wherever it is met.
    for (auto& w : common::waistSpecs()) v.push_back(w);
    return v;
}

DesignResult BodysuitModule::build(const SizePreset& size, const StyleParams& style) const {
    DesignResult out;
    out.garmentKey = key();
    out.stem = "Bodysuit-Pattern-Package";
    out.size = size;
    out.style = style;
    out.title = "Studio Bodysuit";

    double ease = style.get("stretchEaseCm", -6.f);

    block::TorsoParams tp;
    tp.bust = size.get("bust", 86.4);
    tp.waist = size.get("waist", 68.6);
    tp.hip = size.get("hip", 94.0);
    tp.backWaistLength = size.get("backWaistLength", 40.0);
    tp.hipDepth = size.get("hipDepth", 20.0);
    tp.shoulder = style.get("shoulderWidthCm", 10.f);
    tp.neck = size.get("neck", 36.0);
    tp.armholeDepth = style.get("armholeDepthCm", 20.f);
    tp.easeBust = ease; tp.easeWaist = ease; tp.easeHip = ease;
    common::applyWaist(tp, style);

    block::BodysuitParams bf;
    bf.torso = tp;
    bf.torso.neckDrop = style.get("neckDepthCm", 12.f);
    bf.torso.neckWidth = style.get("neckWidthCm", 14.f);
    bf.riseY = style.get("riseYCm", 68.f);
    bf.legOpeningY = std::min((double)style.get("legOpeningYCm", 58.f), bf.riseY - 6.0);
    bf.crotchHalf = style.get("crotchWidthCm", 9.f) / 2.0;
    bf.isFront = true;

    block::BodysuitParams bb = bf;
    bb.torso.neckDrop = style.get("backNeckDepthCm", 4.f);
    bb.isFront = false;

    Piece F; F.code = "F"; F.name = "Front"; F.cutQty = "CUT 1 ON FOLD / KNIT";
    F.note = "Cut on the fold at center front. The panel runs unbroken from shoulder to crotch.";
    F.seamAllowanceCm = 1.0f; F.foldAtCF = true;
    F.sewing = block::bodysuitHalf(bf);
    common::finish(F);

    Piece B; B.code = "B"; B.name = "Back"; B.cutQty = "CUT 1 ON FOLD / KNIT";
    B.note = "Cut on the fold at center back. Cut fuller than the front through the seat.";
    B.seamAllowanceCm = 1.0f; B.foldAtCF = true;
    B.sewing = block::bodysuitHalf(bb);
    common::finish(B);

    Piece G; G.code = "G"; G.name = "Crotch gusset"; G.cutQty = "CUT 1 / KNIT + 1 / LINING";
    G.note = "Lined, and left openable: a bodysuit has to come undone at the crotch to be worn.";
    G.seamAllowanceCm = 1.0f;
    {
        double w = bf.crotchHalf * 2.0;
        G.sewing = { Pt(0, 0), Pt((float)w, 0), Pt((float)w, 12.f), Pt(0, 12.f) };
        common::addButtonColumn(G, w / 2.0, 9.0, 9.0, 1, 12.0, "snap fastener");
    }
    common::finish(G);

    out.pieces = { F, B, G };

    double armholeTotal = block::torsoArmholeLength(bf.torso) + block::torsoArmholeLength(bb.torso);
    double sleeveLen = style.get("sleeveLengthCm", 0.f);
    if (sleeveLen > 0.5) {
        block::SleeveParams sp;
        sp.armholeLen = armholeTotal;
        sp.sleeveLength = sleeveLen;
        sp.bicep = size.get("bicep", 28.0) + ease * 0.5;
        sp.wrist = 20.0;
        double capTarget = armholeTotal * block::kSleeveCapEase;
        double armFloor = sp.bicep;
        sp.capHeight = armholeTotal / 3.2;
        double solvedBicep = block::solveSleeveBicep(sp, capTarget);
        if (solvedBicep >= armFloor) sp.bicep = solvedBicep;
        else sp.capHeight = block::solveSleeveCapHeight(sp, capTarget);
        Piece SL; SL.code = "SL"; SL.name = "Sleeve"; SL.cutQty = "CUT 2 / KNIT";
        SL.note = "Ease the cap into the armhole."; SL.seamAllowanceCm = 1.0f;
        SL.sewing = block::sleeve(sp);
        common::finish(SL);
        out.pieces.push_back(SL);
        out.checks.expectNear("Sleeve cap vs armhole (with ease)",
                              block::sleeveCapLength(sp), capTarget, armholeTotal * 0.075);
    }

    common::prepareSetInSleeve(out.checks, out.pieces, out.seams,
                               tp.armholeDepth, tp.shoulder, block::torsoNeckHalfWidth(bf.torso));

    // A bodysuit's side seam ends at the LEG OPENING, not at a hem -- the
    // panel carries on below that to the crotch, and the front and back
    // are cut differently down there. The shared landmarks call the
    // lowest point "hem side", which would measure the side seam right
    // around the leg curve and make two seams that do meet look as if
    // they could not.
    for (auto& piece : out.pieces) {
        if (piece.code != "F" && piece.code != "B") continue;
        common::addLandmark(piece, "leg opening",
                            Pt((float)geo::maxXAtY(piece.sewing, bf.legOpeningY),
                               (float)bf.legOpeningY));
    }
    for (auto& seam : out.seams) {
        if (seam.name != "side seam") continue;
        seam.a.to = seam.b.to = "leg opening";
        seam.note = "Front to back down the side, ending where the leg is cut away.";
    }
    // The gusset's two seams are not in the graph: it is a plain rectangle
    // with no landmarks naming its edges, so there is nothing for a seam
    // to point at yet. The construction steps carry it instead.

    out.armholeY = tp.armholeDepth;
    out.shoulderX = block::torsoShoulderX(tp);

    common::addSanityChecks(out.checks, out.pieces);
    common::expectSeamsSewable(out.checks, out.pieces, out.seams);
    common::expectSeamFractionsMatch(out.checks, out.pieces, out.seams);
    common::expectClosuresOnCloth(out.checks, out.pieces);

    // The crotch has to be wide enough to sew a gusset to, and the front
    // and back have to meet there -- the leg curve sweeps in toward it, so
    // a leg cut too low or a crotch too narrow closes the panel off.
    out.checks.add("Crotch is wide enough to take the gusset",
                   ofToString(bf.crotchHalf * 2.0, 1) + " cm across",
                   bf.crotchHalf * 2.0 >= 3.0);
    out.checks.add("Front outline does not cross itself",
                   "leg opening stays clear of the side seam", !geo::selfIntersects(F.sewing));
    out.checks.add("Back outline does not cross itself",
                   "leg opening stays clear of the side seam", !geo::selfIntersects(B.sewing));
    // Negative ease is the point of the garment, so it is worth stating
    // rather than leaving the reader to work out from the numbers.
    out.checks.add("Drafted for knit cloth",
                   ease < 0 ? ofToString(-ease, 1) + " cm NEGATIVE ease -- knit only"
                            : ofToString(ease, 1) + " cm positive ease -- woven, needs an opening",
                   true);

    out.cuttingList = common::defaultCuttingList(out.pieces);
    out.whatChanged = {
        "A bodysuit: front and back running unbroken from the shoulder through the crotch, with "
        "the legs cut away at the sides and a gusset at the crotch. There is no waist seam and no "
        "hem -- that is what distinguishes it from a top.",
        ease < 0
            ? "Drafted with NEGATIVE ease, so the flat pattern is smaller than the body. It is held "
              "on by stretch, and must be cut in a knit with good recovery. Cut this in a woven and "
              "it will not go on."
            : "Drafted with positive ease, so it will not rely on stretch -- but a woven bodysuit "
              "needs an opening long enough to get into, which this draft does not yet include.",
        "The leg height, crotch depth and crotch width are the three numbers that decide whether it "
        "is comfortable. Sew the muslin in a similar knit and check the rise sitting down, not just "
        "standing up -- a bodysuit that fits standing can be unwearable seated.",
    };
    out.constructionSteps = {
        "1 / Join front to back at the shoulders and side seams, using a stretch stitch.",
        "2 / Sew the gusset to the front and back crotch edges, leaving the back edge openable.",
        "3 / Set the sleeves if the design has them; otherwise bind the armholes with elastic.",
        "4 / Bind the neckline and both leg openings with elastic, stretching the elastic slightly "
        "as you sew so the edges hold to the body.",
        "5 / Add the snaps at the gusset.",
    };
    return out;
}

} // namespace pf
