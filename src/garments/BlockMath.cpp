#include "BlockMath.h"
#include <cmath>

namespace pf { namespace block {

static constexpr double kPi = 3.14159265358979323846;
static double rad(double deg) { return deg * kPi / 180.0; }

std::vector<Pt> arc(Pt center, double rx, double ry, double fromDeg, double toDeg, int steps) {
    std::vector<Pt> pts;
    pts.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        double t = fromDeg + (toDeg - fromDeg) * ((double)i / (double)steps);
        pts.push_back(Pt((float)(center.x + rx * std::cos(rad(t))), (float)(center.y + ry * std::sin(rad(t)))));
    }
    return pts;
}

static std::vector<Pt> chaikinOpenLocal(const std::vector<Pt>& pts) {
    return geo::chaikinOpen(pts, 2);
}

double torsoShoulderX(const TorsoParams& p) {
    // The shoulder goes where it is asked to go.
    //
    // This used to stop at the bust line, on the reasoning that past there
    // the armhole turns inside out. It does reverse -- the shoulder point
    // ends up outboard of the underarm and the armhole bows the other way
    // -- but that is not a broken panel, it is a DROPPED shoulder, and it
    // is how an extended-shoulder dress or a kimono-cut sleeve is drafted.
    // The armhole's two ends stay exactly where they belong either way.
    //
    // So no clamp. A shoulder wide enough to genuinely fold the outline
    // back on itself is caught by the outline sanity check, which says so
    // out loud rather than quietly moving the slider somewhere else.
    return std::max(1.0, p.shoulder);
}

double torsoNeckHalfWidth(const TorsoParams& p) {
    double half = p.neckWidth > 0.0 ? p.neckWidth / 2.0 : p.neck / 6.0;
    // The neckline stops short of the shoulder point; past it there would
    // be no shoulder seam left to sew.
    return std::max(1.0, std::min(half, torsoShoulderX(p) - 1.5));
}

double torsoNeckLength(const TorsoParams& p) {
    // Arc length of the neckline quarter ellipse, sampled.
    auto pts = arc(Pt(0, 0), torsoNeckHalfWidth(p), p.neckDrop, 90.0, 0.0, 48);
    double len = 0.0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) len += glm::distance(pts[i], pts[i + 1]);
    return len;
}

std::vector<Pt> torsoArmholeCurve(const TorsoParams& p) {
    double bustHalf = p.bust / 4.0 + p.easeBust / 4.0;
    Pt shoulderPt((float)torsoShoulderX(p), (float)kShoulderDropCm);
    Pt underarmPt((float)bustHalf, (float)p.armholeDepth);
    // A quarter ellipse from the shoulder point (angle 180) down and out to
    // the underarm (angle 90), scooped toward the panel. `armholeScoop`
    // blends between the straight chord (0) and that curve (1), and past 1
    // cuts deeper still.
    auto curve = arc(Pt((float)bustHalf, (float)kShoulderDropCm),
                     bustHalf - torsoShoulderX(p), p.armholeDepth - kShoulderDropCm, 180.0, 90.0, 16);
    std::vector<Pt> out;
    out.reserve(curve.size());
    for (size_t i = 0; i < curve.size(); ++i) {
        float t = (float)i / (float)(curve.size() - 1);
        Pt chord = shoulderPt + (underarmPt - shoulderPt) * t;
        out.push_back(chord + (curve[i] - chord) * (float)p.armholeScoop);
    }
    return out;
}

double torsoArmholeLength(const TorsoParams& p) {
    auto pts = torsoArmholeCurve(p);
    double len = 0.0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) len += glm::distance(pts[i], pts[i + 1]);
    return len;
}

double sleeveCapLength(const SleeveParams& p) {
    double bicepHalf = p.bicep / 2.0;
    Pt capCenter(0, (float)p.capHeight);
    double len = 0.0;
    for (double from : { 270.0, 180.0 }) {
        auto pts = arc(capCenter, bicepHalf, p.capHeight, from, from + 90.0, 48);
        for (size_t i = 0; i + 1 < pts.size(); ++i) len += glm::distance(pts[i], pts[i + 1]);
    }
    return len;
}

double solveSleeveCapHeight(const SleeveParams& p, double targetLength) {
    // The cap seam grows with cap height, so bisect on it.
    SleeveParams t = p;
    double lo = 0.5, hi = std::max(6.0, p.sleeveLength * 0.9);
    for (int i = 0; i < 48; ++i) {
        double mid = 0.5 * (lo + hi);
        t.capHeight = mid;
        if (sleeveCapLength(t) < targetLength) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

double solveSleeveBicep(const SleeveParams& p, double targetLength) {
    // The cap seam grows with bicep width, so bisect on it.
    SleeveParams t = p;
    double lo = 5.0, hi = 200.0;
    for (int i = 0; i < 48; ++i) {
        double mid = 0.5 * (lo + hi);
        t.bicep = mid;
        if (sleeveCapLength(t) < targetLength) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

Ring torsoHalf(const TorsoParams& p, double hemY, double hemHalfWidthCm, const std::vector<Pt>& extraSideSeamPoints) {
    // The plain block's upper edge IS its neckline: a quarter ellipse from
    // the center-front neck point up to the neck/shoulder point.
    auto neckline = arc(Pt(0, 0), torsoNeckHalfWidth(p), p.neckDrop, 90.0, 0.0, 6);
    return torsoHalfShaped(p, hemY, hemHalfWidthCm, extraSideSeamPoints, neckline, 0.0);
}

Ring torsoHalfShaped(const TorsoParams& p, double hemY, double hemHalfWidthCm,
                     const std::vector<Pt>& extraSideSeamPoints,
                     const std::vector<Pt>& upperEdge, double cfX) {
    double armholeY   = p.armholeDepth;
    double bustHalf   = p.bust / 4.0 + p.easeBust / 4.0;
    double hipHalf    = p.hip / 4.0 + p.easeHip / 4.0;
    double hipY       = p.backWaistLength + p.hipDepth;

    // The waist, as a level and as a width.
    //
    // It can be moved up or down the body, but not past the landmarks
    // either side of it: a waist above the underarm or below the hip is
    // not a raised or dropped waist, it is a side seam doubling back on
    // itself.
    double waistY = std::min(hipY - 2.0,
                             std::max(armholeY + 2.0, p.backWaistLength - p.waistRise));
    double waistHalf = p.waist / 4.0 + p.easeWaist / 4.0;
    // Unshaped, the side seam runs straight from the underarm to the hip
    // and the drafted waist pulls it in from there, so interpolating
    // between the two is exactly "how fitted is this". It also keeps a
    // shaping of 0 from collapsing anything: a straight side seam is a
    // real garment -- a shift, a box jacket -- and not the same thing as a
    // fitted block with its waist ease wound up.
    {
        double t = (waistY - armholeY) / std::max(1.0, hipY - armholeY);
        t = std::min(1.0, std::max(0.0, t));
        double straight = bustHalf + (hipHalf - bustHalf) * t;
        waistHalf = straight + (waistHalf - straight) * p.waistShaping;
    }

    // The shoulder point sits a fixed slope below the high-shoulder datum,
    // independent of the neckline: deepening the neckline must not drop
    // the shoulder and armhole with it (it used to, via neckDrop + 4).
    Pt shoulderPt((float)torsoShoulderX(p), (float)kShoulderDropCm);
    Pt underarmPt((float)bustHalf, (float)armholeY);
    Pt waistPt((float)waistHalf, (float)waistY);
    Pt hipPt((float)hipHalf, (float)hipY);
    Pt hemPt((float)hemHalfWidthCm, (float)hemY);
    Pt cfHem((float)cfX, (float)hemY);

    Ring r;
    // The upper edge, running from the front edge to the neck/shoulder
    // point -- a neckline on a plain block, a lapel on a jacket front --
    // then a short straight shoulder seam out to the arm point.
    for (auto& pt : upperEdge) r.push_back(pt);
    r.push_back(shoulderPt);
    // Armhole, from the shoulder point down and out to the underarm.
    auto armhole = torsoArmholeCurve(p);
    for (size_t i = 1; i < armhole.size(); ++i) r.push_back(armhole[i]); // [0] is the shoulder point, already added
    // Side seam: underarm -> waist -> hip -> (extra shaping points) -> hem,
    // dart-free. Chaikin-smoothed so control points read as a curve
    // instead of sharp corners, while the underarm and hem ends stay put.
    // A cropped piece (hem above the natural waist/hip, e.g. an empire
    // bodice) simply omits whichever of those points it never reaches.
    std::vector<Pt> chain = { r.back() };
    if (hemY > waistY + 0.01) chain.push_back(waistPt);
    if (hemY > hipY + 0.01) chain.push_back(hipPt);
    chain.insert(chain.end(), extraSideSeamPoints.begin(), extraSideSeamPoints.end());
    chain.push_back(hemPt);
    std::vector<Pt> sideSeam = geo::chaikinOpen(chain, 2);
    for (size_t i = 1; i < sideSeam.size(); ++i) r.push_back(sideSeam[i]);
    // Hem, then straight back up the center front/back edge to where the
    // upper edge started.
    r.push_back(cfHem);
    if (!upperEdge.empty()) r.push_back(upperEdge.front());
    return r;
}

void lapelRollLine(const TorsoParams& p, const LapelParams& lp, Pt& breakPt, Pt& neckPt) {
    breakPt = Pt((float)-lp.wrap, (float)lp.breakY);
    neckPt  = Pt((float)torsoNeckHalfWidth(p), 0.f);
}

std::vector<Pt> lapelEdge(const TorsoParams& p, const LapelParams& lp) {
    Pt breakPt, neckPt;
    lapelRollLine(p, lp, breakPt, neckPt);
    glm::vec2 axis(neckPt.x - breakPt.x, neckPt.y - breakPt.y);
    double rollLen = glm::length(axis);
    if (rollLen < 1e-3) return { breakPt, neckPt };
    glm::vec2 dir = axis / (float)rollLen;
    // Square off the roll line, toward the body (+x): the side the lapel
    // lies on when it is worn. The pattern carries it mirrored to -perp.
    glm::vec2 perp(-dir.y, dir.x);
    if (perp.x < 0.f) perp = -perp;

    // (u, v): u runs 0..1 along the roll line, v is width away from it on
    // the body side. Reflecting is just taking -v.
    auto at = [&](double u, double v) {
        glm::vec2 q = glm::vec2(breakPt.x, breakPt.y)
                    + dir * (float)(u * rollLen) - perp * (float)v;
        return Pt(q.x, q.y);
    };

    double w = lp.width;
    std::vector<Pt> e;
    e.push_back(breakPt);
    switch (lp.style) {
        case LapelStyle::Peak:
            // The lapel point sweeps UP past the notch rather than being
            // cut square across it -- that upward peak is the whole point
            // of the style, so it reaches past the lapel's nominal width.
            e.push_back(at(0.55, w * 0.75));
            e.push_back(at(0.80, w * 1.15));   // the peak
            e.push_back(at(0.74, w * 0.50));   // cut back in toward the roll line
            e.push_back(at(0.92, w * 0.95));   // collar point
            break;
        case LapelStyle::Shawl:
            // No notch at all: one unbroken curve from break to collar,
            // which is why a shawl collar has no separate collar point.
            e.push_back(at(0.30, w * 0.80));
            e.push_back(at(0.60, w * 0.95));
            e.push_back(at(0.85, w * 0.85));
            break;
        case LapelStyle::Notched:
        default:
            e.push_back(at(0.50, w * 0.70));
            e.push_back(at(0.68, w * 1.00));   // lapel point
            e.push_back(at(0.76, w * 0.55));   // the notch, cut in toward the roll line
            e.push_back(at(0.90, w * 0.90));   // collar point
            break;
    }
    e.push_back(neckPt);
    return e;
}

Ring jacketFacing(const TorsoParams& p, const LapelParams& lp,
                  double hemY, double facingWidthCm) {
    Pt breakPt, neckPt;
    lapelRollLine(p, lp, breakPt, neckPt);
    glm::vec2 axis(neckPt.x - breakPt.x, neckPt.y - breakPt.y);
    double rollLen = glm::length(axis);
    glm::vec2 dir = rollLen > 1e-3 ? axis / (float)rollLen : glm::vec2(0.f, -1.f);
    glm::vec2 perp(-dir.y, dir.x);
    if (perp.x < 0.f) perp = -perp;
    auto at = [&](double u, double v) {
        glm::vec2 q = glm::vec2(breakPt.x, breakPt.y)
                    + dir * (float)(u * rollLen) + perp * (float)v;
        return Pt(q.x, q.y);
    };

    Ring r;
    // Same lapel as the front -- the two are sewn edge to edge and turned,
    // so any difference between them would show along the lapel's edge.
    for (auto& pt : lapelEdge(p, lp)) r.push_back(pt);
    // Inner edge, coming back down. It stays outboard of where the folded
    // lapel lands (hence the lapel width, on the body side of the roll
    // line) so the lapel never reaches past the facing onto raw cloth.
    r.push_back(at(0.62, lp.width * 0.9));
    r.push_back(at(0.15, lp.width * 0.55));
    r.push_back(Pt((float)(-lp.wrap + facingWidthCm), (float)hemY));
    r.push_back(Pt((float)-lp.wrap, (float)hemY));
    return r;
}

#include <algorithm>

Ring bodysuitHalf(const BodysuitParams& p) {
    const TorsoParams& t = p.torso;
    double neckHalf  = torsoNeckHalfWidth(t);
    double bustHalf  = t.bust / 4.0 + t.easeBust / 4.0;
    double waistHalf = t.waist / 4.0 + t.easeWaist / 4.0;
    double hipHalf   = t.hip / 4.0 + t.easeHip / 4.0;
    double hipY      = t.backWaistLength + t.hipDepth;

    // The back is cut wider at the CROTCH than the front, which is what
    // stops a bodysuit riding up over the seat. The leg opening starts at
    // the same height on both, though: front and back side seams are sewn
    // to each other, so ending them at different points would leave two
    // seams of different lengths that cannot be joined.
    double crotchHalf = p.isFront ? p.crotchHalf : p.crotchHalf * 1.45;
    double legY = p.legOpeningY;

    Ring r;
    for (auto& pt : arc(Pt(0, 0), neckHalf, t.neckDrop, 90.0, 0.0, 6)) r.push_back(pt);
    r.push_back(Pt((float)t.shoulder, (float)kShoulderDropCm));
    auto armhole = torsoArmholeCurve(t);
    for (size_t i = 1; i < armhole.size(); ++i) r.push_back(armhole[i]);

    // Side seam: underarm through the waist and hip to where the leg is
    // cut. A landmark BELOW the leg cut is skipped -- a high-cut leg comes
    // above the hip, and running the seam down to the hip and back up to
    // the cut folds the edge over itself.
    std::vector<Pt> side = { r.back() };
    if (legY > t.backWaistLength + 1.0) side.push_back(Pt((float)waistHalf, (float)t.backWaistLength));
    if (legY > hipY + 1.0) side.push_back(Pt((float)hipHalf, (float)hipY));
    // Width at the cut: the hip width where the leg is cut below it, and
    // an interpolation between waist and hip where it is cut above.
    double sideHalf = legY > hipY + 1.0
        ? hipHalf * 0.97
        : waistHalf + (hipHalf - waistHalf) *
              std::min(1.0, std::max(0.0, (legY - t.backWaistLength) /
                                          std::max(1.0, hipY - t.backWaistLength)));
    side.push_back(Pt((float)sideHalf, (float)legY));
    auto sideSeam = chaikinOpenLocal(side);
    for (size_t i = 1; i < sideSeam.size(); ++i) r.push_back(sideSeam[i]);

    // The leg opening: a curve from the side in to the crotch point. It
    // sweeps DOWN and IN, so the highest part of the cut is at the side.
    std::vector<Pt> leg = {
        r.back(),
        Pt((float)(sideHalf * 0.62), (float)(legY + (p.riseY - legY) * 0.55)),
        Pt((float)crotchHalf, (float)p.riseY),
    };
    auto legCurve = chaikinOpenLocal(leg);
    for (size_t i = 1; i < legCurve.size(); ++i) r.push_back(legCurve[i]);

    r.push_back(Pt(0.f, (float)p.riseY));
    r.push_back(Pt(0, (float)t.neckDrop));
    return r;
}

Ring skirtHalf(const SkirtParams& p) {
    double waistHalf = p.waist / 4.0 + p.easeWaist / 4.0;
    double hipHalf   = p.hip / 4.0 + p.easeHip / 4.0;
    // The hem is never allowed inside the hip: a skirt that narrows past
    // the hip cannot be pulled on, whatever the hem measurement asks for.
    double hemHalf   = std::max(p.hemHalfWidth, p.length > p.hipDepth ? hipHalf * 0.92 : waistHalf);

    Ring r;
    r.push_back(Pt(0, 0));
    r.push_back(Pt((float)waistHalf, 0));
    std::vector<Pt> side = { Pt((float)waistHalf, 0) };
    if (p.length > p.hipDepth + 0.5) side.push_back(Pt((float)hipHalf, (float)p.hipDepth));
    side.push_back(Pt((float)hemHalf, (float)p.length));
    auto seam = geo::chaikinOpen(side, 2);
    for (size_t i = 1; i < seam.size(); ++i) r.push_back(seam[i]);
    r.push_back(Pt(0, (float)p.length));
    return r;
}

Ring annulusSector(double innerCircumference, double widthCm, double sweepDeg) {
    // r = C / 2pi: the radius whose full circle measures the body edge.
    double r0 = std::max(1.0, innerCircumference / (2.0 * kPi));
    double r1 = r0 + std::max(1.0, widthCm);
    double sweep = std::max(5.0, std::min(sweepDeg, 350.0));

    int steps = std::max(8, (int)(sweep / 6.0));
    Ring r;
    for (auto& pt : arc(Pt(0, 0), r0, r0, 0.0, sweep, steps)) r.push_back(pt);
    auto outer = arc(Pt(0, 0), r1, r1, sweep, 0.0, steps);
    for (auto& pt : outer) r.push_back(pt);
    return r;
}

Ring sleeve(const SleeveParams& p) {
    double bicepHalf = p.bicep / 2.0;
    double wristHalf = p.wrist / 2.0;
    // A bell sleeve follows the arm to the flare point, then widens to the
    // hem. Below the bicep the arm narrows, so the flare point sits at a
    // fraction of the bicep; the elbow point is only inserted when the hem
    // is actually wider than that (otherwise it's a plain taper).
    double flareY = p.sleeveLength * std::min(1.0, std::max(0.0, p.flareFrom));
    double flareHalf = bicepHalf * 0.85;
    bool bell = p.flareFrom < 0.98 && wristHalf > flareHalf && flareY > p.capHeight;

    Pt capCenter(0, (float)p.capHeight);
    Ring r;
    // Cap crown: in y-down space angle 270 is straight UP from the center,
    // so 270->360 runs from the crown (0,0) down the right side to the
    // bicep point (bicepHalf, capHeight). (The old 90->5 range swept the
    // cap downward from (0, 2*capHeight), giving a concave cap plus a
    // spike back to (0,0).)
    for (auto& pt : arc(capCenter, bicepHalf, p.capHeight, 270.0, 360.0, 10)) r.push_back(pt);
    if (bell) r.push_back(Pt((float)flareHalf, (float)flareY));
    r.push_back(Pt((float)wristHalf, (float)p.sleeveLength));
    r.push_back(Pt((float)-wristHalf, (float)p.sleeveLength));
    if (bell) r.push_back(Pt((float)-flareHalf, (float)flareY));
    // Left bicep point back up to the crown; skip the last point, which
    // would duplicate the crown the ring already starts with.
    std::vector<Pt> left = arc(capCenter, bicepHalf, p.capHeight, 180.0, 270.0, 10);
    for (size_t i = 0; i + 1 < left.size(); ++i) r.push_back(left[i]);
    return r;
}

Ring shirtCollar(const CollarParams& p) {
    double cf = p.neckLen + p.spread;
    Ring r = {
        Pt(0, 0),
        Pt((float)cf, (float)(-p.depth * 0.15)),
        Pt((float)cf, (float)(p.depth - p.depth * 0.15)),
        Pt(0, (float)p.depth),
    };
    return r;
}

namespace {

struct TrouserPoints {
    Pt waist, cfHip, crotch, kneeIn, hemIn, hemOut, kneeOut, outHip, outWaist;
    double crotchExt, hipY;
};

TrouserPoints trouserPoints(const TrouserParams& p) {
    TrouserPoints t;
    double waistHalf = p.waist / 4.0 + p.easeWaist / 4.0;
    double hipHalf   = p.hip / 4.0 + p.easeHip / 4.0;
    t.crotchExt      = p.isFront ? (p.hip * 0.05 + 1.0) : (p.hip * 0.11 + 2.0);
    t.hipY           = p.rise * 0.42;
    double hemY      = p.rise + p.inseam;
    double kneeY     = p.rise + p.inseam * 0.45;
    double crease    = (-hipHalf + t.crotchExt) / 2.0;
    double kneeHalf  = p.kneeWidth / 4.0;  // each panel carries half the circumference
    double ankleHalf = p.ankleWidth / 4.0;

    t.waist    = Pt(0, 0);
    t.cfHip    = Pt(0, (float)t.hipY);
    t.crotch   = Pt((float)t.crotchExt, (float)p.rise);
    t.kneeIn   = Pt((float)(crease + kneeHalf), (float)kneeY);
    t.hemIn    = Pt((float)(crease + ankleHalf), (float)hemY);
    t.hemOut   = Pt((float)(crease - ankleHalf), (float)hemY);
    t.kneeOut  = Pt((float)(crease - kneeHalf), (float)kneeY);
    t.outHip   = Pt((float)-hipHalf, (float)t.hipY);
    t.outWaist = Pt((float)-waistHalf, 0);
    return t;
}

double polylineLength(const std::vector<Pt>& pts) {
    double len = 0.0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) len += glm::distance(pts[i], pts[i + 1]);
    return len;
}

std::vector<Pt> inseamLine(const TrouserPoints& t)  { return geo::chaikinOpen({ t.crotch, t.kneeIn, t.hemIn }, 2); }
std::vector<Pt> outseamLine(const TrouserPoints& t) { return geo::chaikinOpen({ t.hemOut, t.kneeOut, t.outHip }, 2); }

} // namespace

Ring trouserHalf(const TrouserParams& p) {
    TrouserPoints t = trouserPoints(p);
    Ring r;
    r.push_back(t.waist);
    r.push_back(t.cfHip);
    // Crotch curve: a quarter ellipse centered at (crotchExt, hipY), from
    // the CF line at hip level (angle 180) scooping down and out to the
    // crotch point (angle 90). The first point is cfHip, already added.
    auto curve = arc(Pt((float)t.crotchExt, (float)t.hipY), t.crotchExt, p.rise - t.hipY, 180.0, 90.0, 8);
    for (size_t i = 1; i < curve.size(); ++i) r.push_back(curve[i]);
    auto in = inseamLine(t);
    for (size_t i = 1; i < in.size(); ++i) r.push_back(in[i]);   // in[0] is the crotch point
    auto out = outseamLine(t);
    for (auto& pt : out) r.push_back(pt);                          // starts at the outer hem
    r.push_back(t.outWaist);
    return r;
}

double trouserInseamLength(const TrouserParams& p)  { return polylineLength(inseamLine(trouserPoints(p))); }

double trouserOutseamLength(const TrouserParams& p) {
    TrouserPoints t = trouserPoints(p);
    auto out = outseamLine(t);
    out.push_back(t.outWaist);
    return polylineLength(out);
}

} } // namespace pf::block
