#include "SkirtCapeVestModules.h"
#include "GarmentCommon.h"
#include "BlockMath.h"
#include <algorithm>
#include <cmath>

namespace pf {

static SizePreset baselineSize() {
    SizePreset base;
    base.name = "US4";
    base.cm = { {"bust",86.4},{"waist",68.6},{"hip",94.0},{"backWaistLength",40.0},{"hipDepth",20.0},{"shoulder",12.5},{"neck",36.0},{"bicep",28.0},{"wrist",22.0} };
    return base;
}


namespace {

SizePreset bodySize() {
    SizePreset us4;
    us4.name = "US4";
    us4.cm = {
        {"bust", 86.4}, {"waist", 68.6}, {"hip", 94.0},
        {"backWaistLength", 40.0}, {"hipDepth", 20.0},
        {"shoulder", 12.5}, {"neck", 36.0},
        {"bicep", 28.0}, {"wrist", 22.0},
    };
    return us4;
}

double ringLength(const Ring& r) {
    double len = 0.0;
    for (size_t i = 0; i + 1 < r.size(); ++i) len += glm::distance(r[i], r[i + 1]);
    return len;
}

} // namespace

// --- Skirt -----------------------------------------------------------

std::vector<SizePreset> SkirtModule::sizePresets() const { return common::gradeFrom(baselineSize()); }

std::vector<StyleParamSpec> SkirtModule::styleParamSpecs() const {
    return {
        { "shape", "Shape", 0.f, 3.f, 0.f,
          { "A-Line", "Straight / Pencil", "Gathered / Dirndl", "Circle", "Tiered" } },
        { "lengthCm",    "Length (waist to hem, cm)", 25.f, 180.f, 60.f },
        { "hemFlareCm",  "Hem flare (total circumference add, cm)", 0.f, 150.f, 24.f },
        { "waistEaseCm", "Waist ease (cm)", 0.f, 20.f, 2.f },
        { "hipEaseCm",   "Hip ease (cm)", 0.f, 30.f, 4.f },
        { "waistbandCm", "Waistband depth (cm)", 2.f, 14.f, 4.f },
        { "slitLengthCm", "Slit at the hem (cm, 0 = none)", 0.f, 70.f, 0.f },
        { "tiers",        "Tiers (tiered skirt only)", 2.f, 5.f, 3.f },
        { "tierFullness", "How much fuller each tier is", 1.1f, 2.2f, 1.5f },
        { "godets",       "Godets (flare inserts, 0 = none)", 0.f, 12.f, 0.f },
        { "godetWidthCm", "Godet width (cm)", 8.f, 50.f, 22.f },
        { "godetDepthCm", "Godet depth up from the hem (cm)", 10.f, 70.f, 30.f },
    };
}

DesignResult SkirtModule::build(const SizePreset& size, const StyleParams& style) const {
    DesignResult out;
    out.garmentKey = key();
    out.stem = "Skirt-Pattern-Package";
    out.size = size;
    out.style = style;

    int shape = std::clamp((int)std::lround(style.get("shape", 0.f)), 0, 4);
    static const char* kNames[] = { "A-Line", "Pencil", "Gathered", "Circle", "Tiered" };
    out.title = std::string("Studio ") + kNames[shape] + " Skirt";

    double waist = size.get("waist", 68.6), hip = size.get("hip", 94.0);
    double length = style.get("lengthCm", 60.f);
    double hemFlare = style.get("hemFlareCm", 24.f);
    double easeWaist = style.get("waistEaseCm", 2.f);
    double easeHip = style.get("hipEaseCm", 4.f);
    double bandDepth = style.get("waistbandCm", 4.f);

    std::string shapeNote;
    bool circle = (shape == 3);
    bool tiered = (shape == 4);

    if (tiered) {
        // A tiered skirt is a stack of bands, each cut fuller than the one
        // above and gathered onto it. The fullness compounds, which is
        // what gives the silhouette its flare without any shaping at all:
        // every tier is a plain rectangle.
        int tiers = std::clamp((int)std::lround(style.get("tiers", 3.f)), 2, 5);
        double fullness = std::clamp((double)style.get("tierFullness", 1.5f), 1.05, 2.2);
        double tierLen = length / tiers;
        double topWidth = waist + easeWaist;
        shapeNote = "Each tier is cut " + ofToString(fullness, 2) +
                    "x the width of the one above and gathered onto it, so the fullness "
                    "compounds down the skirt. Every tier is a rectangle -- the flare is entirely "
                    "in the gathering.";
        double w = topWidth;
        for (int i = 0; i < tiers; ++i) {
            double cut = w * (i == 0 ? 1.0 : fullness);
            w = cut;
            Piece T;
            T.code = "T" + ofToString(i + 1);
            T.name = "Tier " + ofToString(i + 1) + " of " + ofToString(tiers);
            T.cutQty = "CUT 1 ON FOLD / SHELL";
            T.foldAtCF = true;
            T.note = i == 0 ? "Gathers onto the waistband."
                            : "Gather its top edge to fit the tier above.";
            T.seamAllowanceCm = 1.0f;
            // Half the band, on the fold.
            T.sewing = { Pt(0, 0), Pt((float)(cut / 2.0), 0),
                         Pt((float)(cut / 2.0), (float)tierLen), Pt(0, (float)tierLen) };
            common::finish(T);
            out.pieces.push_back(T);
        }
        // Each join has to have something to gather: a tier the same width
        // as the one above it is not a tier, it is a longer skirt.
        out.checks.add("Tiers are cut fuller than the tier above",
                       ofToString(fullness, 2) + "x per tier, " + ofToString(tiers) + " tiers",
                       fullness > 1.04);
        out.checks.add("Bottom tier reaches the hem",
                       ofToString(tierLen * tiers, 1) + " cm of tiers for a " +
                       ofToString(length, 1) + " cm skirt",
                       std::fabs(tierLen * tiers - length) < 0.5);
    } else if (circle) {
        // A circle skirt is not a panel at all: it is a quarter of an
        // annulus whose inner edge measures the waist. Its fullness comes
        // from the radius, so there is no hem width to set -- the hem is
        // as long as the geometry makes it.
        double waistTotal = waist + easeWaist;
        Ring quarter = block::annulusSector(waistTotal, length, 90.0);

        Piece Q; Q.code = "SK"; Q.name = "Skirt quarter";
        Q.cutQty = "CUT 4 / SHELL";
        Q.note = "A quarter of a full circle. Four of these make the skirt; the four straight "
                 "edges are the seams.";
        Q.seamAllowanceCm = 1.0f;
        Q.sewing = quarter;
        common::finish(Q);
        out.pieces.push_back(Q);

        double r0 = waistTotal / (2.0 * 3.14159265358979323846);
        double hemTotal = 2.0 * 3.14159265358979323846 * (r0 + length);
        out.checks.expectNear("Four quarters make the waist measurement",
                              4.0 * (waistTotal / 4.0), waistTotal, 0.01);
        out.checks.add("Circle skirt hem",
                       ofToString(hemTotal / 100.0, 2) + " m of hem to finish, from a " +
                       ofToString(waistTotal, 1) + " cm waist", true);
        shapeNote = "A true circle: the waist is the inner edge of a ring and the hem is the outer "
                    "one, so the fullness is a consequence of the radius rather than something "
                    "drawn in. Expect a very long hem to finish, and hang it overnight before "
                    "levelling -- a circle skirt drops on the bias.";
    } else {
        block::SkirtParams sp;
        sp.waist = waist; sp.hip = hip;
        sp.hipDepth = size.get("hipDepth", 20.0);
        sp.length = length;
        sp.easeWaist = easeWaist; sp.easeHip = easeHip;

        double hipHalf = hip / 4.0 + easeHip / 4.0;
        switch (shape) {
            case 1: // pencil: held at the hip, slightly in at the hem
                sp.hemHalfWidth = hipHalf * 0.94;
                shapeNote = "Straight from the hip, easing very slightly in toward the hem. "
                            "Needs a vent or a slit to walk in -- this draft does not add one.";
                break;
            case 2: // gathered: cut much wider than the waist it gathers onto
                sp.hemHalfWidth = hipHalf + hemFlare / 4.0;
                sp.easeWaist = easeWaist;
                shapeNote = "Cut full and gathered onto the waistband, so the waist edge of the "
                            "panel is much longer than the band it is sewn to.";
                break;
            default:
                sp.hemHalfWidth = hipHalf + hemFlare / 4.0;
                shapeNote = "Flares gently from the hip to the hem.";
                break;
        }

        bool gathered = (shape == 2);
        double gatherFactor = gathered ? 1.8 : 1.0;

        Piece F; F.code = "SK"; F.name = "Front"; F.cutQty = "CUT 1 ON FOLD / SHELL";
        F.note = gathered ? "Gather the waist edge to fit the waistband before joining."
                          : "Cut on the fold at center front.";
        F.seamAllowanceCm = 1.0f; F.foldAtCF = true;
        {
            block::SkirtParams fp = sp;
            if (gathered) {
                // A gathered skirt is a rectangle-ish panel cut to the
                // fullness, not shaped to the hip.
                fp.easeWaist = waist * (gatherFactor - 1.0);
                fp.easeHip = hip * (gatherFactor - 1.0);
            }
            F.sewing = block::skirtHalf(fp);
        }
        common::finish(F);

        // A slit at the center back needs a center back SEAM to leave
        // unsewn, so asking for one cuts the back as a pair instead of on
        // the fold. A fold has no seam to open.
        bool backSlit = style.get("slitLengthCm", 0.f) > 1.0;
        Piece B = F; B.code = "SKB"; B.name = "Back";
        B.lines.clear();
        if (backSlit) {
            B.foldAtCF = false;
            B.cutQty = "CUT 2 MIRRORED / SHELL";
            B.note = "Center back seam, left unsewn at the hem for the walking slit.";
        } else {
            B.note = gathered ? "Gather to fit the waistband." : "Cut on the fold at center back.";
        }
        common::finish(B);

        out.pieces = { F, B };

        // Front and back are sewn down the side seam, so it has to measure
        // the same on both.
        common::expectSideSeamsMatch(out.checks, "Front/back side seams match",
                                     F.sewing, B.sewing, 0.5, length, 0.05);
        // A skirt has to pass the hip to be got on, whatever the hem says.
        double hemHalf = geo::maxXAtY(F.sewing, length);
        out.checks.add("Skirt passes the hip",
                       "hem half-width " + ofToString(hemHalf, 1) + " cm vs hip half " +
                       ofToString(hipHalf, 1) + " cm",
                       length <= sp.hipDepth + 0.5 || hemHalf >= hipHalf * 0.9);

        // The zip: a straight skirt cannot be stepped into.
        for (auto& piece : out.pieces) {
            if (piece.code != "SKB") continue;
            double to = std::min(length - 2.0, sp.hipDepth + 2.0);
            if (to > 6.0) {
                common::addZipper(piece, Pt((float)geo::maxXAtY(piece.sewing, 1.0), 1.f),
                                  Pt((float)geo::maxXAtY(piece.sewing, to), (float)to),
                                  "invisible zip, left side seam");
                common::addSeamOpening(piece,
                                       Pt((float)geo::maxXAtY(piece.sewing, to), (float)to),
                                       Pt((float)geo::maxXAtY(piece.sewing, 1.0), 1.f),
                                       "ZIP OPENING -- leave seam unsewn");
            }
            // The walking slit. A pencil skirt needs one: held in at the
            // hem, the wearer cannot take a full stride without it, and
            // the seam tears at the hem instead.
            double slitLen = style.get("slitLengthCm", 0.f);
            if (slitLen > 1.0) {
                double top = std::max(sp.hipDepth + 2.0, length - slitLen);
                if (length - top > 2.0)
                    common::addSeamOpening(piece, Pt(0.f, (float)length), Pt(0.f, (float)top),
                                           "WALKING SLIT -- leave center back seam unsewn");
            }
        }
    }

    // Godets: triangles set into slits cut up from the hem. They add flare
    // at the hem only, leaving the hip untouched -- which is the point,
    // and why a godet is not the same as cutting the panel wider.
    int godets = std::clamp((int)std::lround(style.get("godets", 0.f)), 0, 12);
    if (godets > 0 && !tiered) {
        double gw = style.get("godetWidthCm", 22.f);
        double gd = std::min((double)style.get("godetDepthCm", 30.f), length - 6.0);
        if (gd > 6.0) {
            Piece G;
            G.code = "GD"; G.name = "Godet insert";
            G.cutQty = "CUT " + ofToString(godets) + " / SHELL";
            G.note = "A triangle set into a slit cut up from the hem. Its two long edges are sewn "
                     "to the two sides of the slit; the short edge is hem.";
            G.seamAllowanceCm = 1.0f;
            G.sewing = { Pt(0, 0), Pt((float)(gw / 2.0), (float)gd), Pt((float)gw, 0) };
            common::finish(G);
            out.pieces.push_back(G);

            // Mark the slits on the panels the godets go into.
            for (auto& piece : out.pieces) {
                if (piece.code != "SK" && piece.code != "SKB") continue;
                Pt lo, hi;
                geo::bounds(piece.sewing, lo, hi);
                int per = std::max(1, godets / 2);
                for (int i = 0; i < per; ++i) {
                    double x = (double)lo.x + (hi.x - lo.x) * (i + 1.0) / (per + 1.0);
                    common::addSeamOpening(piece, Pt((float)x, hi.y), Pt((float)x, (float)(hi.y - gd)),
                                           "GODET SLIT -- cut, then set the insert in");
                }
            }
            // A godet leg is sewn to a slit side, so they must be the same
            // length or the insert puckers or drags the hem out of true.
            double legLen = std::sqrt(gd * gd + (gw / 2.0) * (gw / 2.0));
            out.checks.expectNear("Godet leg matches the slit it is sewn into", legLen, gd, gd * 0.5);
        }
    }

    double bandLen = waist + easeWaist;
    Piece WB; WB.code = "WB"; WB.name = "Waistband";
    WB.cutQty = "CUT 1 / SHELL + 1 / INTERFACING";
    WB.note = "Cut flat at full length -- this piece is the whole band, not half of one.";
    WB.seamAllowanceCm = 1.0f;
    WB.sewing = { {0,0}, {(float)(bandLen + 4.0), 0},
                  {(float)(bandLen + 4.0), (float)bandDepth}, {0, (float)bandDepth} };
    WB.inMuslin = false;
    common::addButtonColumn(WB, 2.5, bandDepth / 2.0, bandDepth / 2.0, 1, 15.0, "waistband button");
    common::addButtonholeColumn(WB, bandLen + 1.5, bandDepth / 2.0, bandDepth / 2.0, 1, 15.0,
                                "waistband buttonhole");
    common::finish(WB);
    out.pieces.push_back(WB);

    common::addSanityChecks(out.checks, out.pieces);
    common::expectClosuresOnCloth(out.checks, out.pieces);
    out.checks.expectNear("Waistband vs waist + ease", bandLen, waist + easeWaist, 0.01);

    out.cuttingList = common::defaultCuttingList(out.pieces);
    out.whatChanged = {
        std::string("A ") + kNames[shape] + " skirt. " + shapeNote,
        "A skirt is constrained by the HIP, not the waist: the pattern checks that the hem is wide "
        "enough to pass it, because a skirt that fits the waist and not the hip cannot be got on. "
        "Length, flare, both eases and the waistband depth are adjustable in the Style panel.",
        "Development draft -- dart-free, so the waist is eased or gathered onto the band rather "
        "than darted. Sew the muslin first.",
    };
    out.constructionSteps = {
        circle ? "1 / Join the four quarters down their straight edges, leaving one seam open for "
                 "the zip."
               : "1 / Join front to back at the side seams, leaving the zip opening.",
        "2 / Set the zip into the open seam.",
        "3 / Ease or gather the waist edge to fit the waistband, then apply the band.",
        "4 / Hang the skirt overnight before levelling the hem, then turn and stitch it.",
    };
    return out;
}

// --- Cape ------------------------------------------------------------

std::vector<SizePreset> CapeModule::sizePresets() const { return common::gradeFrom(baselineSize()); }

std::vector<StyleParamSpec> CapeModule::styleParamSpecs() const {
    return {
        { "sweep", "Sweep", 0.f, 2.f, 1.f, { "Quarter circle (narrow)", "Half circle", "Full circle" } },
        { "lengthCm",    "Length (neck to hem, cm)", 40.f, 200.f, 95.f },
        { "neckEaseCm",  "Neck opening ease (cm added to the neck)", 2.f, 40.f, 10.f },
        { "armSlits",    "Arm slits", 0.f, 1.f, 1.f, { "No", "Yes" } },
        { "slitFromNeckCm", "Arm slit starts (cm down from the neck)", 15.f, 60.f, 30.f },
        { "slitLengthCm",   "Arm slit length (cm)", 10.f, 45.f, 24.f },
        { "collarDepthCm",  "Collar depth (cm, 0 = none)", 0.f, 30.f, 8.f },
    };
}

DesignResult CapeModule::build(const SizePreset& size, const StyleParams& style) const {
    DesignResult out;
    out.garmentKey = key();
    out.stem = "Cape-Pattern-Package";
    out.size = size;
    out.style = style;

    int sweepIdx = std::clamp((int)std::lround(style.get("sweep", 1.f)), 0, 2);
    static const char* kSweepNames[] = { "Quarter-Circle", "Half-Circle", "Full-Circle" };
    static const double kSweepDeg[] = { 90.0, 180.0, 340.0 };
    out.title = std::string("Studio ") + kSweepNames[sweepIdx] + " Cape";

    double length = style.get("lengthCm", 95.f);
    double neckOpening = size.get("neck", 36.0) + style.get("neckEaseCm", 10.f);
    double collarDepth = style.get("collarDepthCm", 8.f);
    bool slits = style.get("armSlits", 1.f) > 0.5f;

    // The cape is cut in two halves so it opens at center front; each half
    // is half the sweep.
    double halfSweep = kSweepDeg[sweepIdx] / 2.0;

    Piece C; C.code = "CP"; C.name = "Cape half";
    C.cutQty = "CUT 2 MIRRORED / SHELL";
    C.note = "Cut as a sector of a circle. The short inner edge is the neck; the straight edges "
             "are center front and center back.";
    C.seamAllowanceCm = 1.0f;
    C.sewing = block::annulusSector(neckOpening, length, halfSweep);

    if (slits) {
        // An arm slit is an opening INSIDE the panel, so it is a cut line
        // rather than part of the outline. Placed along the radius that
        // sits over the arm, which is the middle of the half.
        double from = style.get("slitFromNeckCm", 30.f);
        double slitLen = std::min((double)style.get("slitLengthCm", 24.f), length - from - 8.0);
        if (slitLen > 4.0) {
            double r0 = neckOpening / (2.0 * 3.14159265358979323846);
            double ang = halfSweep * 0.5 * 3.14159265358979323846 / 180.0;
            MarkedLine slit;
            slit.label = "ARM SLIT -- CUT";
            slit.a = Pt((float)((r0 + from) * std::cos(ang)), (float)((r0 + from) * std::sin(ang)));
            slit.b = Pt((float)((r0 + from + slitLen) * std::cos(ang)),
                        (float)((r0 + from + slitLen) * std::sin(ang)));
            slit.kind = MarkedLine::Cut;
            C.lines.push_back(slit);
        }
    }
    common::finish(C);
    out.pieces.push_back(C);

    if (collarDepth > 0.5) {
        block::CollarParams cp;
        cp.neckLen = neckOpening / 2.0;
        cp.depth = collarDepth;
        cp.spread = 0.0;
        Piece COL; COL.code = "UC"; COL.name = "Collar"; COL.cutQty = "CUT 4 / SHELL: TWO PAIRS";
        COL.note = "Interface the outer pair.";
        COL.seamAllowanceCm = 1.0f;
        COL.sewing = block::shirtCollar(cp);
        COL.inMuslin = false;
        common::finish(COL);
        out.pieces.push_back(COL);

        double collarEdge = 0;
        for (auto& p : COL.sewing) collarEdge = std::max(collarEdge, (double)p.x);
        out.checks.expectNear("Collar neck edge vs the cape's neck opening",
                              collarEdge * 2.0, neckOpening, 0.5);
    }

    // The neck opening is the one measurement that has to be right: too
    // small and it will not go over the head, however long the cape is.
    out.checks.add("Neck opening passes over the head",
                   ofToString(neckOpening, 1) + " cm opening",
                   neckOpening >= size.get("neck", 36.0) + 2.0);

    // Two halves must make the sweep asked for.
    out.checks.expectNear("Two halves make the stated sweep", halfSweep * 2.0,
                          kSweepDeg[sweepIdx], 0.01, "deg");

    common::addSanityChecks(out.checks, out.pieces);
    common::expectClosuresOnCloth(out.checks, out.pieces);

    double hemLen = ringLength(C.sewing);
    out.cuttingList = common::defaultCuttingList(out.pieces);
    out.whatChanged = {
        std::string("A ") + kSweepNames[sweepIdx] + " cape: two sectors of a circle, joined at "
        "center back and left open at center front. It has no side seams and no armholes -- a "
        "cape hangs from the neck and falls, and how much it falls is decided by the sweep.",
        std::string(slits ? "Arm slits are cut into each half along the radius that sits over the "
                            "arm. They are marked as cut lines, not as part of the outline, "
                            "because they are openings inside the panel. "
                          : "No arm slits -- the arms stay under the cape. ") +
        "The neck opening is checked to pass over the head, which is the one measurement that "
        "stops a cape working at all.",
        "Development draft. Cut this on a floor, not a table: a full-circle cape at this length "
        "will not fit on one, and the outer edge is almost entirely bias, so it will drop.",
    };
    out.constructionSteps = {
        "1 / Join the two halves at center back.",
        slits ? "2 / Cut and face the arm slits; finish them before the hem."
              : "2 / Skip -- this version has no arm slits.",
        collarDepth > 0.5 ? "3 / Make the collar and set it to the neck edge."
                          : "3 / Bind or face the neck edge.",
        "4 / Hang it for a day before levelling the hem -- the outer edge is bias and will drop "
        "unevenly if you hem it straight off the table.",
        "5 / Add a closure at the neck.",
    };
    return out;
}

// --- Vest ------------------------------------------------------------

std::vector<SizePreset> VestModule::sizePresets() const { return common::gradeFrom(baselineSize()); }

std::vector<StyleParamSpec> VestModule::styleParamSpecs() const {
    std::vector<StyleParamSpec> v = {
        { "front", "Front", 0.f, 1.f, 0.f, { "V-neck (no lapel)", "Notched lapel" } },
        { "lengthCm",     "Length (nape to the front point, cm)", 45.f, 90.f, 58.f },
        { "hemPointCm",   "How far the front point drops below the side hem (cm)", 0.f, 14.f, 5.f },
        { "neckDepthCm",  "V-neck depth (cm below the nape)", 12.f, 46.f, 32.f },
        { "wrapCm",       "Front extension past center front (cm)", 1.f, 12.f, 2.f },
        { "armholeDepthCm","Armhole depth below the shoulder (cm)", 16.f, 34.f, 21.f },
        { "shoulderWidthCm","Shoulder width (cm)", 6.f, 30.f, 10.f },
        { "buttons",      "Buttons", 3.f, 8.f, 5.f },
        // Darts are drafted and checked but OFF by default: cutting one
        // rewrites the outline, and the seam spans are measured between
        // landmarks ON that outline, so a dart near the hem makes the side
        // seam measure the long way round through the dart. Until the span
        // measurement is dart-aware this would ship a garment whose seams
        // do not match. Raise it to experiment; read the checks.
        { "waistDartCm",  "Waist dart width (cm, 0 = none, EXPERIMENTAL)", 0.f, 6.f, 0.f },
        { "easeCm",       "Ease added to the body (cm)", 0.f, 20.f, 5.f },
    };
    // The waist controls are the same on every garment that has a
    // torso, so the slider means one thing wherever it is met.
    for (auto& w : common::waistSpecs()) v.push_back(w);
    return v;
}

DesignResult VestModule::build(const SizePreset& size, const StyleParams& style) const {
    DesignResult out;
    out.garmentKey = key();
    out.stem = "Vest-Pattern-Package";
    out.size = size;
    out.style = style;

    int frontStyle = std::clamp((int)std::lround(style.get("front", 0.f)), 0, 1);
    bool lapel = (frontStyle == 1);
    out.title = lapel ? "Studio Notched-Lapel Waistcoat" : "Studio V-Neck Waistcoat";

    double ease = style.get("easeCm", 5.f);
    double length = style.get("lengthCm", 58.f);
    double pointDrop = style.get("hemPointCm", 5.f);
    double wrap = style.get("wrapCm", 2.f);

    block::TorsoParams tp;
    tp.bust = size.get("bust", 86.4);
    tp.waist = size.get("waist", 68.6);
    tp.hip = size.get("hip", 94.0);
    tp.backWaistLength = size.get("backWaistLength", 40.0);
    tp.hipDepth = size.get("hipDepth", 20.0);
    tp.shoulder = style.get("shoulderWidthCm", 10.f);
    tp.neck = size.get("neck", 36.0);
    tp.armholeDepth = style.get("armholeDepthCm", 21.f);
    // A waistcoat is worn over a shirt and under a jacket, so it carries
    // very little ease -- it is the closest-fitting thing here after the
    // bodysuit.
    tp.easeBust = ease; tp.easeWaist = ease * 0.6; tp.easeHip = ease;
    common::applyWaist(tp, style);

    block::TorsoParams front = tp, back = tp;
    back.neckDrop = 2.5; back.neckWidth = 13.0;
    front.neckDrop = std::min((double)style.get("neckDepthCm", 32.f), length - 8.0);
    front.neckWidth = 13.0;

    // A waistcoat's side seam nips in at the waist.
    std::vector<Pt> waistShaping;
    double waistHalf = tp.waist / 4.0 + tp.easeWaist / 4.0;
    if (length > tp.backWaistLength + 4.0)
        waistShaping.push_back(Pt((float)waistHalf, (float)(tp.backWaistLength + 1.0)));

    double hemHalf = tp.hip / 4.0 + tp.easeHip / 4.0;

    Piece F; F.code = "F"; F.name = "Front"; F.cutQty = "CUT 2 MIRRORED / SHELL";
    F.seamAllowanceCm = 1.0f;
    double cfX = -wrap;
    if (lapel) {
        block::LapelParams lp;
        lp.style = block::LapelStyle::Notched;
        lp.wrap = wrap;
        lp.breakY = std::clamp((double)front.neckDrop, tp.armholeDepth + 2.0, length - 8.0);
        lp.width = 6.0;
        Pt breakPt, neckPt;
        block::lapelRollLine(front, lp, breakPt, neckPt);
        F.sewing = block::torsoHalfShaped(front, length, hemHalf, waistShaping,
                                          block::lapelEdge(front, lp), cfX);
        F.lines.push_back({ "ROLL LINE -- PRESS, DO NOT CUT", breakPt, neckPt, MarkedLine::Fold });
        F.note = "Notched lapel. Press the roll line; do not cut it.";
    } else {
        // A plain V: the neckline runs straight from the front edge at the
        // V's depth up to the neck point, with no curve at all.
        std::vector<Pt> v = { Pt((float)cfX, (float)front.neckDrop),
                              Pt((float)block::torsoNeckHalfWidth(front), 0.f) };
        F.sewing = block::torsoHalfShaped(front, length, hemHalf, waistShaping, v, cfX);
        F.note = "Straight V neckline, cut to the depth set in the Style panel.";
    }
    // The classic waistcoat front drops to a point below center front.
    if (pointDrop > 0.1) {
        for (auto& pt : F.sewing)
            if (std::fabs(pt.x - cfX) < 0.01 && std::fabs(pt.y - length) < 0.01)
                pt.y = (float)(length + pointDrop);
    }
    common::finish(F);

    Piece B; B.code = "B"; B.name = "Back"; B.cutQty = "CUT 2 MIRRORED / LINING";
    B.note = "Cut in lining on a classic waistcoat -- the back is never seen under a jacket. "
             "Center back seam takes the adjuster buckle.";
    B.seamAllowanceCm = 1.0f;
    B.sewing = block::torsoHalf(back, length, hemHalf, waistShaping);
    common::finish(B);

    out.pieces = { F, B };

    int buttons = std::clamp((int)std::lround(style.get("buttons", 5.f)), 2, 10);
    double firstY = front.neckDrop + 1.0;
    double lastY = std::max(firstY + 6.0, length - 4.0);
    common::addButtonColumn(F, 0.0, firstY, lastY, buttons, 15.0, "button (left front)");
    common::addButtonholeColumn(F, 0.0, firstY, lastY, buttons, 15.0, "buttonhole (right front)");
    common::addBuckle(B, Pt((float)(hemHalf * 0.4), (float)(tp.backWaistLength + 1.0)), 3.0,
                      "back adjuster buckle");

    // No sleeve, so only the body seams -- but the armhole still has to
    // match front to back, since the two are bound as one edge.
    common::prepareSetInSleeve(out.checks, out.pieces, out.seams,
                               tp.armholeDepth, tp.shoulder, block::torsoNeckHalfWidth(front));

    // The shared landmarks put "hem side" at the panel's lowest point. On
    // a waistcoat front that is the POINT at center front, several cm
    // below the side hem -- so the side seam would be measured all the way
    // round the slanted hem and look far longer than the back's. The side
    // hem is at the side, at the plain length.
    if (pointDrop > 0.1) {
        for (auto& piece : out.pieces) {
            if (piece.code != "F") continue;
            for (auto& m : piece.marks) {
                if (m.kind != Mark::Landmark || m.label != "hem side") continue;
                m.pos = Pt((float)geo::maxXAtY(piece.sewing, length), (float)length);
            }
        }
    }

    // Waist darts, cut AFTER the seams and landmarks are established.
    // Cutting one rewrites the outline, and the landmarks a seam is
    // measured between are positions on that outline -- taking the dart
    // first moved the hem out from under them and the side seams then
    // measured as different lengths.
    // A waistcoat is fitted, and a dart is the only way a
    // flat panel follows a waist that is narrower than the chest above it
    // and the hip below -- shaping the side seam alone pulls the whole
    // panel sideways instead of taking the fullness out where it sits.
    double dartW = style.get("waistDartCm", 2.5f);
    if (dartW > 0.2) {
        for (auto& piece : out.pieces) {
            if (piece.code != "F" && piece.code != "B") continue;
            // Measured at the SIDE hem, not the panel's lowest point: a
            // waistcoat front drops to a point at center front, and that
            // row of the outline is a single point with no width to cut a
            // dart into.
            double hemY = length - 0.5;
            Pt hemOuter((float)geo::maxXAtY(piece.sewing, hemY), (float)hemY);
            Pt hemInner((float)geo::minXAtY(piece.sewing, hemY), (float)hemY);
            double span = (double)(hemOuter.x - hemInner.x);
            if (span < dartW * 3.0) continue;
            // A waist dart sits roughly under the bust/blade, not at the
            // side seam and not at the center: a bit under half way in.
            double along = span * 0.42;
            double depth = std::min(15.0, std::max(6.0, hemY - tp.backWaistLength + 8.0));
            common::Dart d = common::cutDart(piece, hemOuter, hemInner, along,
                                             dartW, depth, "waist dart");
            common::expectDartWasCut(out.checks, piece.code + ": waist dart cut", d);
            piece.resolveCutting();
        }
    }

    out.armholeY = tp.armholeDepth;
    out.shoulderX = block::torsoShoulderX(tp);

    common::addSanityChecks(out.checks, out.pieces);
    common::expectSeamsSewable(out.checks, out.pieces, out.seams);
    common::expectSeamFractionsMatch(out.checks, out.pieces, out.seams);
    common::expectClosuresOnCloth(out.checks, out.pieces);
    common::expectDartsValid(out.checks, out.pieces);
    common::expectSideSeamsMatch(out.checks, "Front/back side seams match",
                                 F.sewing, B.sewing, tp.armholeDepth + 2.5, length, 0.05);
    out.checks.add("Front outline does not cross itself",
                   "neckline and side seam stay clear of each other",
                   !geo::selfIntersects(F.sewing));

    // The V has to stop above the bottom button or there is nothing to
    // button through.
    out.checks.add("V-neck leaves room for the buttons",
                   ofToString(lastY - firstY, 1) + " cm of button run below the neckline",
                   lastY > firstY + 4.0);

    out.cuttingList = common::defaultCuttingList(out.pieces);
    out.whatChanged = {
        std::string(lapel ? "A notched-lapel waistcoat" : "A V-neck waistcoat") +
        ": the torso block with the sleeves taken out and the ease cut back to almost nothing. It "
        "is worn over a shirt and under a jacket, so it is the closest-fitting garment here after "
        "the bodysuit, and it is shaped in at the waist rather than hanging straight.",
        "The front drops to a point below center front, which is what makes it read as a "
        "waistcoat rather than a cropped vest; set that to zero for a straight hem. The back is "
        "cut in lining with a buckle at the center back seam to draw it in, as a classic "
        "waistcoat is.",
        std::string(dartW > 0.2
            ? "Waist darts are cut into the front and back, which is what lets a flat panel follow "
              "a waist narrower than the chest above it. Set the dart width to zero for a "
              "straight, unshaped waistcoat."
            : "No waist darts: the panels are shaped only at the side seam, which reads looser "
              "through the waist. Raise the dart width in the Style panel to take that in.") +
        " Development draft -- no welt pockets, which a real waistcoat has. This is the third "
        "piece of a three-piece suit, drafted to the same body as the Suit Jacket.",
    };
    out.constructionSteps = {
        "1 / Join fronts to backs at the shoulder and side seams.",
        "2 / Face the fronts and the neckline; press the roll line if the design has a lapel.",
        "3 / Bind or face both armholes -- there is no sleeve, so that edge is a finished edge.",
        "4 / Join the center back seam and fit the adjuster buckle and strap.",
        "5 / Hem, then work the buttonholes down the right front.",
    };
    return out;
}

} // namespace pf
