#include "GarmentCommon.h"
#include "BlockMath.h"
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace pf { namespace common {

std::vector<SizePreset> standardSizes() {
    // bust, waist, hip drive the grade; everything else follows more
    // gently. Figures are a conventional US women's run.
    struct Row {
        const char* name;
        double bust, waist, hip, backWaistLength, hipDepth, shoulder, neck, bicep, wrist;
        double rise, inseam, thigh, ankle;
    };
    static const Row rows[] = {
        { "US4",  86.4, 68.6,  94.0, 40.0, 20.0, 12.5, 36.0, 28.0, 22.0, 27.0, 74.0, 55.0, 36.0 },
        { "US8",  91.4, 73.7,  99.1, 40.8, 20.4, 12.9, 37.0, 29.5, 22.8, 27.8, 75.0, 58.0, 37.5 },
        { "US12", 96.5, 78.7, 104.1, 41.6, 20.8, 13.3, 38.0, 31.0, 23.6, 28.6, 76.0, 61.0, 39.0 },
        { "US16", 104.1, 86.4, 111.8, 42.6, 21.4, 13.8, 39.4, 33.2, 24.6, 29.6, 77.0, 65.0, 41.0 },
        { "US20", 111.8, 94.0, 119.4, 43.6, 22.0, 14.3, 40.8, 35.4, 25.6, 30.6, 78.0, 69.0, 43.0 },
    };
    std::vector<SizePreset> out;
    for (auto& r : rows) {
        SizePreset p;
        p.name = r.name;
        p.cm = {
            { "bust", r.bust }, { "waist", r.waist }, { "hip", r.hip },
            { "backWaistLength", r.backWaistLength }, { "hipDepth", r.hipDepth },
            { "shoulder", r.shoulder }, { "neck", r.neck },
            { "bicep", r.bicep }, { "wrist", r.wrist },
            { "rise", r.rise }, { "inseam", r.inseam },
            { "thigh", r.thigh }, { "ankle", r.ankle },
        };
        out.push_back(p);
    }
    return out;
}

std::vector<SizePreset> gradeFrom(const SizePreset& base) {
    // Per size step: girths grow most, lengths barely. Nobody is
    // proportionally taller for being wider, and grading lengths at the
    // same rate as girths is how a size run stops fitting anyone.
    struct Step { const char* name; double girth, length, limb; };
    static const Step steps[] = {
        { "US4",  0.0, 0.0, 0.0 },
        { "US8",  5.1, 0.8, 1.5 },
        { "US12", 10.2, 1.6, 3.0 },
        { "US16", 17.8, 2.6, 5.2 },
        { "US20", 25.4, 3.6, 7.4 },
    };
    std::vector<SizePreset> out;
    for (auto& s : steps) {
        SizePreset p = base;
        p.name = s.name;
        auto bump = [&](const char* key, double by) {
            auto it = p.cm.find(key);
            if (it != p.cm.end()) it->second += by;
        };
        bump("bust", s.girth);
        bump("waist", s.girth);
        bump("hip", s.girth);
        bump("thigh", s.girth * 0.55);
        bump("neck", s.girth * 0.14);
        bump("bicep", s.limb);
        bump("wrist", s.limb * 0.45);
        bump("ankle", s.limb * 0.5);
        bump("backWaistLength", s.length);
        bump("hipDepth", s.length * 0.4);
        bump("shoulder", s.length * 0.5);
        bump("rise", s.length * 0.7);
        bump("inseam", s.length * 0.9);
        out.push_back(p);
    }
    return out;
}

void addSanityChecks(Checks& checks, const std::vector<Piece>& pieces) {
    for (auto& piece : pieces) {
        double a = std::fabs(geo::area(piece.sewing));
        checks.add(piece.code + " outline is non-degenerate",
                   "area " + std::to_string(a) + " cm2", a > 1.0);

        // "ON FOLD" on the cutting list means the piece stored here is HALF
        // the panel. If the label and the geometry disagree, the piece gets
        // cut at twice or half its intended size -- and CAD exports, which
        // mirror fold pieces, get it wrong too.
        bool labelSaysFold = piece.cutQty.find("ON FOLD") != std::string::npos;
        checks.add(piece.code + ": cutting label matches the fold flag",
                   labelSaysFold ? (piece.foldAtCF ? "on fold, stored as half" : "labelled ON FOLD but stored whole")
                                 : (piece.foldAtCF ? "stored as half but not labelled ON FOLD" : "whole piece"),
                   labelSaysFold == piece.foldAtCF);
    }
}

std::vector<std::pair<std::string, std::string>> defaultCuttingList(const std::vector<Piece>& pieces) {
    std::vector<std::pair<std::string, std::string>> rows;
    rows.reserve(pieces.size());
    for (auto& piece : pieces) {
        rows.push_back({ piece.code, piece.cutQty + "; " + piece.name });
    }
    return rows;
}

// "CUT 4 / SHELL: TWO MIRRORED PAIRS" -> 4 panels, in mirrored pairs.
static void parseCutQuantity(Piece& piece) {
    const std::string& s = piece.cutQty;
    piece.cutMirrored = s.find("MIRRORED") != std::string::npos;
    piece.cutCount = 1;
    size_t at = s.find("CUT ");
    if (at != std::string::npos) {
        int n = std::atoi(s.c_str() + at + 4);
        if (n >= 1 && n <= 64) piece.cutCount = n;
    }
    // A fold piece's two halves are the same panel, not two panels.
    if (piece.foldAtCF) piece.cutCount = std::max(1, piece.cutCount);
}

std::vector<StyleParamSpec> waistSpecs() {
    return {
        // 1 is what every torso garment used to be fixed at, so an
        // existing design reloads unchanged.
        { "waistShaping", "Waist shaping (0 = straight, 1 = fitted)", 0.f, 1.6f, 1.f },
        { "waistRiseCm",  "Waist height (cm above the body's waist)", -20.f, 20.f, 0.f },
    };
}

void applyWaist(block::TorsoParams& tp, const StyleParams& style) {
    tp.waistShaping = style.get("waistShaping", 1.f);
    tp.waistRise    = style.get("waistRiseCm", 0.f);
}

void finish(Piece& piece) {
    piece.resolveCutting();
    parseCutQuantity(piece);

    // A piece cut on the fold gets its fold line here rather than in each
    // module, so none of them can ship a fold-cut piece with nothing
    // saying which edge is the fold. That edge is the one mistake on a
    // pattern that cannot be recovered from.
    if (piece.foldAtCF && !piece.sewing.empty()) {
        bool already = false;
        for (auto& f : piece.lines) if (f.kind == MarkedLine::CutOnFold) already = true;
        if (!already) {
            Pt lo, hi;
            geo::bounds(piece.sewing, lo, hi);
            MarkedLine f;
            f.label = "CUT ON FOLD -- DO NOT CUT THIS EDGE";
            f.a = Pt(0.f, lo.y);
            f.b = Pt(0.f, hi.y);
            f.kind = MarkedLine::CutOnFold;
            piece.lines.push_back(f);
        }
    }
}

Ring facingBand(const Ring& panel, double marginCm) {
    // The neckline is deepest at the center front/back fold, so the band is
    // measured down from there -- a deeper neckline pushes the facing down
    // with it instead of the facing cutting across it.
    double neckY = geo::minYAtX(panel, 0.0);
    auto clipped = geo::clipToBox(panel, -1e6, -1e6, 1e6, neckY + marginCm);
    return clipped.empty() ? panel : clipped[0];
}

void expectFacingCoversNeckline(Checks& checks, const std::string& name,
                                const Ring& panel, const Ring& facing, double minClearCm) {
    Pt lo, hi;
    geo::bounds(facing, lo, hi);
    double bandBottom = hi.y;

    // Only the NECKLINE is measured, not the facing's whole width. The
    // panel's top edge is the neckline from the fold out to the neck point,
    // and the shoulder and armhole after that -- where the band tapers away
    // to nothing, which is how a clipped band is supposed to end, not a
    // fault. The neck point is where the top edge stops descending.
    const int kSamples = 48;
    double step = std::max(0.05, (double)(hi.x - lo.x) / kSamples);
    double neckEndX = 0.0, lowest = geo::minYAtX(panel, 0.0);
    for (double x = step; x <= (double)hi.x; x += step) {
        double top = geo::minYAtX(panel, x);
        if (top > lowest + 1e-6) break;   // climbing again: past the neck point
        lowest = top;
        neckEndX = x;
    }

    double worst = 1e9, atX = 0.0;
    for (int i = 0; i <= kSamples; ++i) {
        double x = neckEndX * ((double)i / kSamples);
        double clear = bandBottom - geo::minYAtX(panel, x);
        if (clear < worst) { worst = clear; atX = x; }
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << "narrowest " << worst << " cm at x=" << atX
       << " along the neckline (x=0 to " << neckEndX << "), needs " << minClearCm;
    checks.add(name, ss.str(), worst >= minClearCm);
}

namespace {

int nearestVertex(const Ring& r, Pt p) {
    int best = 0;
    double bestD = 1e18;
    for (size_t i = 0; i < r.size(); ++i) {
        double d = glm::distance(r[i], p);
        if (d < bestD) { bestD = d; best = (int)i; }
    }
    return best;
}

// Which way round the ring from `start` heads toward `toward`. Probing a
// few vertices each way beats looking at the immediate neighbours, which
// on a finely sampled curve are both a fraction of a millimetre away and
// tell you nothing about where the edge is actually going.
int walkDirection(const Ring& r, int start, Pt toward) {
    int n = (int)r.size();
    const int kProbe = 3;
    double fwd = glm::distance(r[(start + kProbe) % n], toward);
    double back = glm::distance(r[((start - kProbe) % n + n) % n], toward);
    return fwd <= back ? 1 : -1;
}

} // namespace

Pt pointAlongOutline(const Ring& r, Pt from, Pt toward, double distanceCm) {
    if (r.size() < 2) return from;
    int n = (int)r.size();
    int i = nearestVertex(r, from);
    int dir = walkDirection(r, i, toward);

    double travelled = 0.0;
    for (int step = 0; step < n; ++step) {
        int j = ((i + dir) % n + n) % n;
        double seg = glm::distance(r[i], r[j]);
        if (travelled + seg >= distanceCm) {
            double t = seg < 1e-9 ? 0.0 : (distanceCm - travelled) / seg;
            return r[i] + (r[j] - r[i]) * (float)t;
        }
        travelled += seg;
        i = j;
    }
    return r[i]; // the seam ran out before the distance did
}

double arcDistanceAlongOutline(const Ring& r, Pt from, Pt toward, Pt at) {
    if (r.size() < 2) return 0.0;
    int n = (int)r.size();
    int i = nearestVertex(r, from);
    int dir = walkDirection(r, i, toward);

    // Walk until the segment that actually CONTAINS the point, rather than
    // to the vertex nearest it. On a coarsely sampled edge -- a sleeve cap
    // is ten points across -- the nearest vertex is routinely the one past
    // the notch, and stopping there then adding the gap overshoots by most
    // of a segment: enough to fail a 2 mm tolerance on a correct notch.
    double travelled = 0.0;
    for (int step = 0; step < n; ++step) {
        int j = ((i + dir) % n + n) % n;
        glm::vec2 a(r[i].x, r[i].y), b(r[j].x, r[j].y), p(at.x, at.y);
        glm::vec2 ab = b - a;
        double len2 = glm::dot(ab, ab);
        if (len2 > 1e-12) {
            double t = glm::dot(p - a, ab) / len2;
            double tc = std::max(0.0, std::min(1.0, t));
            if (glm::distance(a + ab * (float)tc, p) < 0.05)
                return travelled + std::sqrt(len2) * tc;
        }
        travelled += glm::distance(a, b);
        i = j;
    }
    return travelled; // never found it: walked the whole way round
}

Pt addSeamNotch(Piece& piece, const std::string& label,
                Pt from, Pt toward, double distanceCm) {
    Pt at = pointAlongOutline(piece.sewing, from, toward, distanceCm);
    Mark m;
    m.label = label;
    m.pos = at;
    m.kind = Mark::Notch;
    piece.marks.push_back(m);
    return at;
}

void expectNotchesMeet(Checks& checks, const std::string& name,
                       const Ring& ringA, Pt fromA, Pt towardA, Pt notchA,
                       const Ring& ringB, Pt fromB, Pt towardB, Pt notchB,
                       double tolCm) {
    double a = arcDistanceAlongOutline(ringA, fromA, towardA, notchA);
    double b = arcDistanceAlongOutline(ringB, fromB, towardB, notchB);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2)
       << a << " cm vs " << b << " cm along the two seams (tol " << tolCm << ")";
    checks.add(name, ss.str(), std::fabs(a - b) <= tolCm);
}

void addSetInSleeveNotches(Checks& checks, Piece& front, Piece& back, Piece* sleeve,
                           double armholeY, double shoulderX,
                           double sideSeamDropCm, double armholeRunCm) {
    Pt fLo, fHi, bLo, bHi;
    geo::bounds(front.sewing, fLo, fHi);
    geo::bounds(back.sewing, bLo, bHi);

    // The underarm is the outer end of the armhole; the side seam runs
    // from there down to the hem, and the armhole up to the shoulder.
    Pt fUnder((float)geo::maxXAtY(front.sewing, armholeY), (float)armholeY);
    Pt bUnder((float)geo::maxXAtY(back.sewing, armholeY), (float)armholeY);
    Pt fHem((float)geo::maxXAtY(front.sewing, fHi.y), (float)fHi.y);
    Pt bHem((float)geo::maxXAtY(back.sewing, bHi.y), (float)bHi.y);
    Pt shoulder((float)shoulderX, (float)block::kShoulderDropCm);

    // Side seam: front sews to back, so one notch on each at the same
    // distance below the underarm.
    Pt fSide = addSeamNotch(front, "side seam balance", fUnder, fHem, sideSeamDropCm);
    Pt bSide = addSeamNotch(back, "side seam balance", bUnder, bHem, sideSeamDropCm);
    expectNotchesMeet(checks, front.code + "/" + back.code + " side-seam notches meet",
                      front.sewing, fUnder, fHem, fSide,
                      back.sewing, bUnder, bHem, bSide, 0.2);

    Pt fArm = addSeamNotch(front, "front armhole", fUnder, shoulder, armholeRunCm);
    Pt bArm = addSeamNotch(back, "back armhole", bUnder, shoulder, armholeRunCm);

    if (!sleeve || sleeve->sewing.size() < 3) return;

    // The sleeve's two underarm corners are its widest points -- where the
    // cap ends and the arm tube begins -- and the crown is its highest.
    const Ring& sl = sleeve->sewing;
    int right = 0, left = 0, crown = 0;
    for (size_t i = 0; i < sl.size(); ++i) {
        if (sl[i].x > sl[right].x) right = (int)i;
        if (sl[i].x < sl[left].x) left = (int)i;
        if (sl[i].y < sl[crown].y) crown = (int)i;
    }
    Pt slRight = addSeamNotch(*sleeve, "front armhole", sl[right], sl[crown], armholeRunCm);
    Pt slLeft  = addSeamNotch(*sleeve, "back armhole", sl[left], sl[crown], armholeRunCm);

    // Each cap notch has to sit the same distance up the cap as the
    // armhole notch it is sewn to sits up the armhole.
    expectNotchesMeet(checks, front.code + "/" + sleeve->code + " front cap notches meet",
                      front.sewing, fUnder, shoulder, fArm,
                      sleeve->sewing, sl[right], sl[crown], slRight, 0.2);
    expectNotchesMeet(checks, back.code + "/" + sleeve->code + " back cap notches meet",
                      back.sewing, bUnder, shoulder, bArm,
                      sleeve->sewing, sl[left], sl[crown], slLeft, 0.2);
}

void addTrouserNotches(Checks& checks, Piece& front, Piece& back,
                       double kneeFromCrotchCm, double outseamFromHemCm) {
    auto landmarks = [](const Ring& r, Pt& crotch, Pt& hemIn, Pt& hemOut) {
        Pt lo, hi;
        geo::bounds(r, lo, hi);
        // +x is the crotch/center-seam side, so the crotch point is the
        // outermost point on that side and the outseam runs down -x.
        int c = 0;
        for (size_t i = 0; i < r.size(); ++i) if (r[i].x > r[c].x) c = (int)i;
        crotch = r[c];
        hemIn  = Pt((float)geo::maxXAtY(r, (double)hi.y), (float)hi.y);
        hemOut = Pt((float)geo::minXAtY(r, (double)hi.y), (float)hi.y);
    };
    Pt fCrotch, fHemIn, fHemOut, bCrotch, bHemIn, bHemOut;
    landmarks(front.sewing, fCrotch, fHemIn, fHemOut);
    landmarks(back.sewing, bCrotch, bHemIn, bHemOut);

    // Inseam: crotch point down to the hem, on both panels.
    Pt fIn = addSeamNotch(front, "inseam balance", fCrotch, fHemIn, kneeFromCrotchCm);
    Pt bIn = addSeamNotch(back, "inseam balance", bCrotch, bHemIn, kneeFromCrotchCm);
    expectNotchesMeet(checks, front.code + "/" + back.code + " inseam notches meet",
                      front.sewing, fCrotch, fHemIn, fIn,
                      back.sewing, bCrotch, bHemIn, bIn, 0.3);

    // Outseam: measured up from the hem, the landmark both panels share at
    // that edge.
    Pt fOut = addSeamNotch(front, "outseam balance", fHemOut, Pt(fHemOut.x, 0.f), outseamFromHemCm);
    Pt bOut = addSeamNotch(back, "outseam balance", bHemOut, Pt(bHemOut.x, 0.f), outseamFromHemCm);
    expectNotchesMeet(checks, front.code + "/" + back.code + " outseam notches meet",
                      front.sewing, fHemOut, Pt(fHemOut.x, 0.f), fOut,
                      back.sewing, bHemOut, Pt(bHemOut.x, 0.f), bOut, 0.3);
}

namespace {
Piece* byCode(std::vector<Piece>& pieces, const std::string& code) {
    for (auto& p : pieces) if (p.code == code) return &p;
    return nullptr;
}
} // namespace

void notchSetInSleeve(Checks& checks, std::vector<Piece>& pieces,
                      double armholeY, double shoulderX, const std::string& prefix) {
    Piece* F = byCode(pieces, prefix + "F");
    Piece* B = byCode(pieces, prefix + "B");
    if (!F || !B) return;
    addSetInSleeveNotches(checks, *F, *B, byCode(pieces, prefix + "SL"), armholeY, shoulderX);
}

void notchTrousers(Checks& checks, std::vector<Piece>& pieces, const std::string& prefix) {
    Piece* F = byCode(pieces, prefix + "F");
    Piece* B = byCode(pieces, prefix + "B");
    if (!F || !B) return;
    addTrouserNotches(checks, *F, *B);
}

void addLandmark(Piece& piece, const std::string& label, Pt at) {
    // Snap to the outline: a landmark names a corner OF the edge, and a
    // seam span measured from a point floating beside it would start in
    // the wrong place.
    int i = nearestVertex(piece.sewing, at);
    Mark m;
    m.label = label;
    m.pos = piece.sewing.empty() ? at : piece.sewing[i];
    m.kind = Mark::Landmark;
    piece.marks.push_back(m);
}

bool findLandmark(const Piece& piece, const std::string& label, Pt& out) {
    for (auto& m : piece.marks)
        if (m.kind == Mark::Landmark && m.label == label) { out = m.pos; return true; }
    return false;
}

double seamEndLength(const Piece& piece, const std::string& from, const std::string& to) {
    Pt a, b;
    if (!findLandmark(piece, from, a) || !findLandmark(piece, to, b)) return -1.0;
    // Walk both ways and keep the shorter: an edge between two corners is
    // the near side of the outline, never the long way round the piece.
    double fwd = arcDistanceAlongOutline(piece.sewing, a, b, b);
    double total = 0.0;
    for (size_t i = 0; i < piece.sewing.size(); ++i)
        total += glm::distance(piece.sewing[i], piece.sewing[(i + 1) % piece.sewing.size()]);
    return std::min(fwd, total - fwd);
}

void expectSeamsSewable(Checks& checks, const std::vector<Piece>& pieces,
                        const std::vector<Seam>& seams, double tolCm) {
    auto find = [&](const std::string& code) -> const Piece* {
        for (auto& p : pieces) if (p.code == code) return &p;
        return nullptr;
    };
    for (auto& seam : seams) {
        const Piece* pa = find(seam.a.piece);
        const Piece* pb = find(seam.b.piece);
        if (!pa || !pb) {
            checks.add("Seam \"" + seam.name + "\" names real pieces",
                       seam.a.piece + " + " + seam.b.piece, false);
            continue;
        }
        double la = seamEndLength(*pa, seam.a.from, seam.a.to);
        double lb = seamEndLength(*pb, seam.b.from, seam.b.to);
        if (la < 0 || lb < 0) {
            checks.add("Seam \"" + seam.name + "\" has both edges landmarked",
                       seam.a.piece + "(" + seam.a.from + "->" + seam.a.to + ") + " +
                       seam.b.piece + "(" + seam.b.from + "->" + seam.b.to + ")", false);
            continue;
        }
        if (seam.toMirrorOfSelf) {
            // Edge b IS edge a, reflected, so the lengths match by
            // construction; what is worth confirming is that the edge
            // resolves at all and has real length to sew.
            std::ostringstream ms;
            ms << std::fixed << std::setprecision(2) << la << " cm, joined to its mirror";
            checks.add("Seam \"" + seam.name + "\" edges sew together", ms.str(), la > 0.5);
            continue;
        }
        double expected = la * seam.easeFactor;
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2)
           << seam.a.piece << " " << la << " cm vs " << seam.b.piece << " " << lb << " cm";
        if (seam.easeFactor != 1.0)
            ss << " (" << seam.easeFactor << "x ease designed in, so " << expected << " expected)";
        ss << " (tol " << tolCm << ")";
        checks.add("Seam \"" + seam.name + "\" edges sew together",
                   ss.str(), std::fabs(expected - lb) <= tolCm);
    }
}

void addTorsoLandmarks(Piece& panel, double armholeY, double shoulderX, double neckHalfWidth,
                       double bodyOffsetX) {
    Pt lo, hi;
    geo::bounds(panel.sewing, lo, hi);
    double cfX = lo.x;  // the fold or front edge, whichever this panel has
    addLandmark(panel, "neck CF", Pt((float)cfX, (float)geo::minYAtX(panel.sewing, cfX + 0.01)));
    double neckX = bodyOffsetX + neckHalfWidth;
    addLandmark(panel, "neck shoulder", Pt((float)neckX, (float)geo::minYAtX(panel.sewing, neckX)));
    addLandmark(panel, "shoulder tip", Pt((float)(bodyOffsetX + shoulderX), (float)block::kShoulderDropCm));
    addLandmark(panel, "underarm", Pt((float)geo::maxXAtY(panel.sewing, armholeY), (float)armholeY));
    addLandmark(panel, "hem side", Pt((float)geo::maxXAtY(panel.sewing, hi.y), (float)hi.y));
    addLandmark(panel, "hem CF", Pt((float)cfX, (float)hi.y));
}

void addSleeveLandmarks(Piece& sleeve) {
    const Ring& sl = sleeve.sewing;
    if (sl.size() < 3) return;
    int right = 0, left = 0, crown = 0;
    for (size_t i = 0; i < sl.size(); ++i) {
        if (sl[i].x > sl[right].x) right = (int)i;
        if (sl[i].x < sl[left].x) left = (int)i;
        if (sl[i].y < sl[crown].y) crown = (int)i;
    }
    Pt lo, hi;
    geo::bounds(sl, lo, hi);
    addLandmark(sleeve, "cap crown", sl[crown]);
    addLandmark(sleeve, "underarm front", sl[right]);
    addLandmark(sleeve, "underarm back", sl[left]);
    addLandmark(sleeve, "hem front", Pt((float)geo::maxXAtY(sl, hi.y), (float)hi.y));
    addLandmark(sleeve, "hem back", Pt((float)geo::minXAtY(sl, hi.y), (float)hi.y));
}

void addTrouserLandmarks(Piece& leg) {
    const Ring& r = leg.sewing;
    if (r.size() < 3) return;
    Pt lo, hi;
    geo::bounds(r, lo, hi);
    int crotch = 0;
    for (size_t i = 0; i < r.size(); ++i) if (r[i].x > r[crotch].x) crotch = (int)i;
    // +x is the crotch / centre-seam side and the outseam runs down -x --
    // the same convention the crotch point just above uses, and the one
    // the trouser block is drafted in. These two were the other way round,
    // so "waist CF" named the OUTSEAM corner and "waist side" named the
    // rise. Every seam built from them then sewed the wrong edge: the
    // outseam ran down the rise, and the rise ran across the waistline.
    addLandmark(leg, "waist CF", Pt((float)geo::maxXAtY(r, lo.y), (float)lo.y));
    addLandmark(leg, "waist side", Pt((float)geo::minXAtY(r, lo.y), (float)lo.y));
    addLandmark(leg, "crotch", r[crotch]);
    addLandmark(leg, "hem inseam", Pt((float)geo::maxXAtY(r, hi.y), (float)hi.y));
    addLandmark(leg, "hem outseam", Pt((float)geo::minXAtY(r, hi.y), (float)hi.y));
}

void prepareSetInSleeve(Checks& checks, std::vector<Piece>& pieces, std::vector<Seam>& seams,
                        double armholeY, double shoulderX, double neckHalfWidth,
                        const std::string& prefix, double frontOffsetX) {
    Piece* F = byCode(pieces, prefix + "F");
    Piece* B = byCode(pieces, prefix + "B");
    if (!F || !B) return;
    Piece* SL = byCode(pieces, prefix + "SL");

    notchSetInSleeve(checks, pieces, armholeY, shoulderX, prefix);
    addTorsoLandmarks(*F, armholeY, shoulderX, neckHalfWidth, frontOffsetX);
    addTorsoLandmarks(*B, armholeY, shoulderX, neckHalfWidth);
    if (SL) addSleeveLandmarks(*SL);

    // The neck point is where the shoulder seam starts on both panels, so
    // it must be the same distance from the center line on each. If it is
    // not, the shoulder seams are different lengths and the garment simply
    // does not close there.
    {
        Pt fNeck, bNeck;
        bool haveF = findLandmark(*F, "neck shoulder", fNeck);
        bool haveB = findLandmark(*B, "neck shoulder", bNeck);
        if (haveF && haveB) {
            // Measured from each panel's own center front/back. A shirt
            // front is translated out by its placket, so that shift comes
            // off before comparing -- the placket overlaps when worn, and
            // the neck points do meet.
            double fx = fNeck.x - frontOffsetX;
            double d = std::fabs(fx - bNeck.x);
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2)
               << "front " << fx << " cm vs back " << bNeck.x << " cm from the center line";
            checks.add("Front and back neck points meet at the shoulder", ss.str(), d <= 0.05);
        }
    }

    const std::string f = F->code, b = B->code;

    // A panel cut as a MIRRORED PAIR has a seam down its own center: the
    // two halves are joined at center front or center back. Without it the
    // garment is not closed -- the halves hang free, and anything sewn to
    // both of them gets pulled apart between them.
    if (!B->foldAtCF)
        seams.push_back({ "center back seam", { b, "neck CF", "hem CF" }, { b, "neck CF", "hem CF" },
                          "The two back halves, joined down the center back.", 1.0, true });
    if (!F->foldAtCF && F->closures.empty())
        seams.push_back({ "center front seam", { f, "neck CF", "hem CF" }, { f, "neck CF", "hem CF" },
                          "The two front halves, joined down the center front.", 1.0, true });

    seams.push_back({ "shoulder seam",
                      { f, "neck shoulder", "shoulder tip" },
                      { b, "neck shoulder", "shoulder tip" },
                      "Front to back across the top of the shoulder." });
    seams.push_back({ "side seam",
                      { f, "underarm", "hem side" },
                      { b, "underarm", "hem side" },
                      "Front to back down the side, matching the balance notches." });
    if (SL) {
        seams.push_back({ "armhole (front)",
                          { f, "underarm", "shoulder tip" },
                          { SL->code, "underarm front", "cap crown" },
                          "Front armhole to the front half of the sleeve cap, easing the cap in.",
                          block::kSleeveCapEase });
        seams.push_back({ "armhole (back)",
                          { b, "underarm", "shoulder tip" },
                          { SL->code, "underarm back", "cap crown" },
                          "Back armhole to the back half of the sleeve cap, easing the cap in.",
                          block::kSleeveCapEase });
        seams.push_back({ "sleeve underarm seam",
                          { SL->code, "underarm front", "hem front" },
                          { SL->code, "underarm back", "hem back" },
                          "The sleeve closes on itself into a tube." });
    }
}

void prepareTrousers(Checks& checks, std::vector<Piece>& pieces, std::vector<Seam>& seams,
                     const std::string& prefix) {
    Piece* F = byCode(pieces, prefix + "F");
    Piece* B = byCode(pieces, prefix + "B");
    if (!F || !B) return;

    notchTrousers(checks, pieces, prefix);
    addTrouserLandmarks(*F);
    addTrouserLandmarks(*B);

    const std::string f = F->code, b = B->code;
    seams.push_back({ "outseam",
                      { f, "waist side", "hem outseam" },
                      { b, "waist side", "hem outseam" },
                      "Front to back down the outside of the leg." });
    seams.push_back({ "inseam",
                      { f, "crotch", "hem inseam" },
                      { b, "crotch", "hem inseam" },
                      "Front to back down the inside of the leg." });
    // The rise does NOT join front to back: each leg's front rise sews to
    // the OTHER leg's front rise, and back to back. (Front and back rises
    // are different lengths by design -- the back is longer, to go round
    // the seat -- so joining them to each other could never close.)
    seams.push_back({ "front rise", { f, "waist CF", "crotch" }, { f, "waist CF", "crotch" },
                      "Left front rise to right front rise.", 1.0, true });
    seams.push_back({ "back rise", { b, "waist CF", "crotch" }, { b, "waist CF", "crotch" },
                      "Left back rise to right back rise, over the seat.", 1.0, true });
}

void expectLapelStandsOff(Checks& checks, const std::string& name,
                          const Ring& front, Pt breakPt, Pt neckPt, double lapelWidthCm) {
    glm::vec2 axis(neckPt.x - breakPt.x, neckPt.y - breakPt.y);
    double rollLen = glm::length(axis);
    if (rollLen < 1e-3 || front.size() < 3) {
        checks.add(name, "no roll line to measure from", false);
        return;
    }
    glm::vec2 dir = axis / (float)rollLen;
    glm::vec2 perp(-dir.y, dir.x);
    if (perp.x < 0.f) perp = -perp;   // toward the body

    // How far the outline stands off the roll line on the lapel side,
    // looking only at the stretch of it the lapel spans.
    double widest = 0.0;
    for (auto& p : front) {
        glm::vec2 rel(p.x - breakPt.x, p.y - breakPt.y);
        double u = glm::dot(rel, dir) / rollLen;
        if (u < -0.05 || u > 1.05) continue;
        widest = std::max(widest, (double)-glm::dot(rel, perp));
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << "stands off the roll line by " << widest
       << " cm (lapel width " << lapelWidthCm << ")";
    checks.add(name, ss.str(), widest >= lapelWidthCm * 0.75);
}

namespace {
void addColumn(Piece& piece, Closure::Kind kind, double x, double fromY, double toY,
               int count, double diameterMm, const std::string& label) {
    if (count < 1) return;
    for (int i = 0; i < count; ++i) {
        // Spread across the run, both ends included, so the top button
        // sits at the break and the bottom one where the run ends.
        double t = count == 1 ? 0.0 : (double)i / (count - 1);
        Closure c;
        c.kind = kind;
        c.a = c.b = Pt((float)x, (float)(fromY + (toY - fromY) * t));
        c.sizeMm = diameterMm;
        c.label = label;
        piece.closures.push_back(c);
    }
}
} // namespace

void addButtonColumn(Piece& piece, double x, double fromY, double toY, int count,
                     double diameterMm, const std::string& label) {
    addColumn(piece, Closure::Button, x, fromY, toY, count, diameterMm, label);
}

void addButtonholeColumn(Piece& piece, double x, double fromY, double toY, int count,
                         double diameterMm, const std::string& label) {
    addColumn(piece, Closure::Buttonhole, x, fromY, toY, count, diameterMm, label);
}

void addZipper(Piece& piece, Pt from, Pt to, const std::string& label) {
    Closure c;
    c.kind = Closure::Zipper;
    c.a = from; c.b = to;
    c.sizeMm = 0.0;
    c.label = label;
    piece.closures.push_back(c);
}

void addSeamOpening(Piece& piece, Pt hemEnd, Pt topEnd, const std::string& label) {
    MarkedLine ln;
    ln.label = label;
    ln.a = hemEnd;
    ln.b = topEnd;
    ln.kind = MarkedLine::SeamOpen;
    piece.lines.push_back(ln);

    // The top of the opening is a stitching stop, which is exactly what a
    // notch is for. Without it the line says where the opening is but
    // nothing says where to stop sewing.
    Mark m;
    m.label = "stop stitching";
    m.pos = topEnd;
    m.kind = Mark::Notch;
    piece.marks.push_back(m);
}

void expectPullsOnOverBody(Checks& checks, const std::string& name,
                           const Ring& front, const Ring& back,
                           double yFrom, double yTo, double mustClearCm) {
    const int kSamples = 32;
    double tightest = 1e9, atY = yFrom;
    for (int i = 0; i < kSamples; ++i) {
        double y = yFrom + (yTo - yFrom) * ((double)i / (kSamples - 1));
        double circ = 2.0 * (geo::maxXAtY(front, y) + geo::maxXAtY(back, y));
        if (circ > 1.0 && circ < tightest) { tightest = circ; atY = y; }
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << "narrowest " << tightest << " cm at y=" << atY
       << ", must clear " << mustClearCm << " cm";
    checks.add(name, ss.str(), tightest >= mustClearCm);
}

void addBuckle(Piece& piece, Pt at, double widthCm, const std::string& label) {
    Closure c;
    c.kind = Closure::Buckle;
    c.a = c.b = at;
    c.sizeMm = widthCm * 10.0;
    c.label = label;
    piece.closures.push_back(c);
}

void expectClosuresOnCloth(Checks& checks, const std::vector<Piece>& pieces,
                           double buttonClearanceCm) {
    auto distToRing = [](const Ring& r, Pt p) {
        double best = 1e18;
        for (size_t i = 0; i < r.size(); ++i) {
            glm::vec2 a(r[i].x, r[i].y), b(r[(i + 1) % r.size()].x, r[(i + 1) % r.size()].y);
            glm::vec2 ab = b - a, ap = glm::vec2(p.x, p.y) - a;
            double len2 = glm::dot(ab, ab);
            double t = len2 < 1e-12 ? 0.0 : std::max(0.0, std::min(1.0, (double)glm::dot(ap, ab) / len2));
            best = std::min(best, (double)glm::distance(a + ab * (float)t, glm::vec2(p.x, p.y)));
        }
        return best;
    };
    auto onAFold = [](const Piece& piece, Pt p) {
        for (auto& f : piece.lines) {
            // Only a real FOLD disqualifies a zip. A SeamOpen line is the
            // opening the zip is set into, so lying on one is not merely
            // allowed -- it is the whole point.
            if (f.kind != MarkedLine::CutOnFold && f.kind != MarkedLine::Fold) continue;
            glm::vec2 a(f.a.x, f.a.y), b(f.b.x, f.b.y), q(p.x, p.y);
            glm::vec2 ab = b - a;
            double len2 = glm::dot(ab, ab);
            if (len2 < 1e-12) continue;
            double t = std::max(0.0, std::min(1.0, (double)glm::dot(q - a, ab) / len2));
            if (glm::distance(a + ab * (float)t, q) < 0.3) return true;
        }
        return false;
    };

    for (auto& piece : pieces) {
        if (piece.closures.empty()) continue;
        int off = 0;
        std::string worst;
        for (auto& c : piece.closures) {
            bool ok;
            if (c.isRun()) {
                // In a seam is right; on a fold is not.
                ok = (geo::contains(piece.sewing, c.a) || distToRing(piece.sewing, c.a) < 0.3) &&
                     (geo::contains(piece.sewing, c.b) || distToRing(piece.sewing, c.b) < 0.3) &&
                     !onAFold(piece, c.a) && !onAFold(piece, c.b);
                if (!ok && onAFold(piece, c.a)) {
                    ++off;
                    if (worst.empty()) worst = c.label + " (lies on a fold, which has no seam to open)";
                    continue;
                }
            } else {
                ok = geo::contains(piece.sewing, c.a) &&
                     distToRing(piece.sewing, c.a) >= buttonClearanceCm;
            }
            if (!ok) { ++off; if (worst.empty()) worst = c.label; }
        }
        checks.add(piece.code + ": closures sit on the cloth",
                   off == 0 ? std::to_string(piece.closures.size()) + " placed"
                            : std::to_string(off) + " off the piece, first: " + worst,
                   off == 0);
    }
}

void expectSeamFractionsMatch(Checks& checks, const std::vector<Piece>& pieces,
                              const std::vector<Seam>& seams, double tolFraction) {
    auto find = [&](const std::string& code) -> const Piece* {
        for (auto& p : pieces) if (p.code == code) return &p;
        return nullptr;
    };
    // Where along this edge each notch sits, as a fraction of its length.
    auto fractionsOn = [](const Piece& piece, const std::string& from, const std::string& to) {
        std::vector<double> fr;
        Pt a, b;
        if (!findLandmark(piece, from, a) || !findLandmark(piece, to, b)) return fr;
        double total = seamEndLength(piece, from, to);
        if (total <= 0.01) return fr;
        for (auto& m : piece.marks) {
            if (m.kind != Mark::Notch) continue;
            // Only notches actually ON this edge: measured from `a` toward
            // `b`, a notch beyond the far end belongs to another seam.
            double d = arcDistanceAlongOutline(piece.sewing, a, b, m.pos);
            if (d < 0.01 || d > total - 0.01) continue;
            fr.push_back(d / total);
        }
        std::sort(fr.begin(), fr.end());
        return fr;
    };

    for (auto& seam : seams) {
        if (seam.toMirrorOfSelf) continue;      // an edge against its own reflection
        const Piece* pa = find(seam.a.piece);
        const Piece* pb = find(seam.b.piece);
        if (!pa || !pb) continue;
        auto fa = fractionsOn(*pa, seam.a.from, seam.a.to);
        auto fb = fractionsOn(*pb, seam.b.from, seam.b.to);
        if (fa.empty() && fb.empty()) continue;  // no notches: nothing to line up

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(3);
        bool ok = (fa.size() == fb.size());
        if (!ok) {
            ss << seam.a.piece << " has " << fa.size() << " notch(es), "
               << seam.b.piece << " has " << fb.size();
        } else {
            double worst = 0.0;
            for (size_t i = 0; i < fa.size(); ++i)
                worst = std::max(worst, std::fabs(fa[i] - fb[i]));
            ok = worst <= tolFraction;
            ss << "worst notch off by " << worst << " of the seam (tol " << tolFraction << ")";
        }
        checks.add("Seam \"" + seam.name + "\" notches divide both edges alike", ss.str(), ok);
    }
}

Dart cutDart(Piece& piece, Pt from, Pt toward, double distanceCm,
             double widthCm, double depthCm, const std::string& label) {
    Dart d;
    Ring& r = piece.sewing;
    if (r.size() < 3 || widthCm < 0.2 || depthCm < 0.5) return d;

    // The two legs sit half the dart's width either side of the point.
    Pt centre = pointAlongOutline(r, from, toward, distanceCm);
    d.legA = pointAlongOutline(r, from, toward, distanceCm - widthCm / 2.0);
    d.legB = pointAlongOutline(r, from, toward, distanceCm + widthCm / 2.0);

    // Inward normal: perpendicular to the edge, pointing into the panel.
    glm::vec2 along(d.legB.x - d.legA.x, d.legB.y - d.legA.y);
    double len = glm::length(along);
    if (len < 1e-4) return d;
    along /= (float)len;
    glm::vec2 n(-along.y, along.x);
    Pt probe((float)(centre.x + n.x * 0.4), (float)(centre.y + n.y * 0.4));
    if (!geo::contains(r, probe)) n = -n;
    d.apex = Pt((float)(centre.x + n.x * depthCm), (float)(centre.y + n.y * depthCm));
    if (!geo::contains(r, d.apex)) return d;   // too deep for the panel

    // Replace the run of outline between the legs with the V.
    //
    // The walk from `from` toward `toward` may run either way around the
    // ring, so the dart's span is whichever way between the two legs is
    // SHORTER. Assuming it always runs forward made the span look like
    // almost the whole outline and the dart was silently rejected.
    int ia = nearestVertex(r, d.legA), ib = nearestVertex(r, d.legB);
    int n_ = (int)r.size();
    if (ia == ib) return d;
    int fwd = ((ib - ia) % n_ + n_) % n_;
    int bwd = n_ - fwd;
    int s, e;
    Pt legAtS, legAtE;
    if (fwd <= bwd) { s = ia; e = ib; legAtS = d.legA; legAtE = d.legB; }
    else            { s = ib; e = ia; legAtS = d.legB; legAtE = d.legA; }
    if (std::min(fwd, bwd) > n_ / 2) return d;

    // Keep the outline the LONG way round, from e back to s, then close
    // through the dart: leg, apex, leg.
    Ring out;
    for (int k = 0; k < n_; ++k) {
        int idx = (e + k) % n_;
        out.push_back(r[idx]);
        if (idx == s) break;
    }
    out.push_back(legAtS);
    out.push_back(d.apex);
    out.push_back(legAtE);
    if (out.size() < 4 || geo::selfIntersects(out)) return d;
    r = out;

    piece.marks.push_back({ label + " leg", d.legA, Mark::Notch });
    piece.marks.push_back({ label + " leg", d.legB, Mark::Notch });
    piece.marks.push_back({ label + " point", d.apex, Mark::Drill });
    d.ok = true;
    return d;
}

void expectDartWasCut(Checks& checks, const std::string& name, const Dart& d) {
    checks.add(name, d.ok ? "cut" : "asked for but could not be cut -- panel reverts to no dart",
               d.ok);
}

void expectDartsValid(Checks& checks, const std::vector<Piece>& pieces) {
    for (auto& piece : pieces) {
        int drills = 0;
        for (auto& m : piece.marks) if (m.kind == Mark::Drill) ++drills;
        if (drills == 0) continue;
        bool clean = !geo::selfIntersects(piece.sewing);
        int inside = 0;
        for (auto& m : piece.marks)
            if (m.kind == Mark::Drill && geo::contains(piece.sewing, m.pos)) ++inside;
        checks.add(piece.code + ": darts sit inside the panel",
                   std::to_string(inside) + " of " + std::to_string(drills) +
                   " dart points inside" + (clean ? "" : ", outline crosses itself"),
                   inside == drills && clean);
    }
}

void expectSideSeamsMatch(Checks& checks, const std::string& name,
                          const Ring& front, const Ring& back,
                          double yFrom, double yTo, double tolCm,
                          double frontOffsetX) {
    const int kSamples = 24;
    double worst = 0.0, atY = yFrom;
    for (int i = 0; i < kSamples; ++i) {
        double y = yFrom + (yTo - yFrom) * ((double)i / (kSamples - 1));
        double d = std::fabs((geo::maxXAtY(front, y) - frontOffsetX) - geo::maxXAtY(back, y));
        if (d > worst) { worst = d; atY = y; }
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << "worst " << worst << " cm at y=" << atY
       << " (tol " << tolCm << ")";
    checks.add(name, ss.str(), worst <= tolCm);
}

} } // namespace pf::common
