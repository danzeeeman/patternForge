#include "Pattern3D.h"
#include "../garments/BlockMath.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace pf { namespace render3d {

namespace {

const ofFloatColor kFrontTone(0.90f, 0.88f, 0.84f);
const ofFloatColor kBackTone(0.70f, 0.68f, 0.64f);
const ofFloatColor kSleeveTone(0.84f, 0.82f, 0.77f);

const int kRows = 72;      // horizontal samples down a panel
const int kCols = 26;      // samples across a panel's own width
const double kPi = 3.14159265358979323846;

const Piece* findPiece(const DesignResult& d, const std::string& code) {
    for (auto& p : d.pieces) if (p.code == code) return &p;
    return nullptr;
}

double minXOf(const Ring& r) {
    double m = 1e18;
    for (auto& p : r) m = std::min(m, (double)p.x);
    return m;
}

// The cross-section a panel row is bent around: a flattened quarter
// profile (straight across the front, a rounded corner, then straight in
// to the seam) whose ARC LENGTH from the center line to the seam is
// exactly `L` -- the panel's own fabric width at that row. So bending
// preserves every width measurement instead of stretching the piece, and
// the profile always ends at (xs, 0), the shared seam point where the
// front and back panels meet.
struct CrossSection {
    double L = 0, xs = 0, zDepth = 0, r = 0, flat = 0, zEnd = 0;

    // `zEnd` is the z the seam edge lands on: 0 where the front and back
    // panels meet, and non-zero across the armhole, where they separate to
    // leave an opening for the arm.
    // `cornerFactor` is the side corner's radius as a fraction of xs, and
    // it sets how flat the panel reads: a small corner spends almost none
    // of the panel's width turning the corner, so nearly all of it lies
    // flat across and the panel has little depth. A large one rounds the
    // section out. The seam still lands on (xs, zEnd) either way.
    static CrossSection make(double L, double xs, double zEnd = 0.0, double cornerFactor = 0.15) {
        CrossSection c;
        c.L = std::max(L, 1e-4);
        double k = std::max(0.02, std::min(cornerFactor, 0.5));
        // xs must leave enough length for the corner and the run in to the
        // seam, i.e. L >= xs * (1 + 0.5708 k) (see r/zDepth below).
        c.xs = std::max(1e-4, std::min(xs, c.L / (1.0 + 0.5708 * k)));
        c.r = k * c.xs;
        c.flat = c.xs - c.r;
        c.zEnd = zEnd;
        c.zDepth = c.L - c.xs + c.r * (2.0 - kPi / 2.0) + zEnd;
        return c;
    }

    // (x, z) at arc length s from the center line, x >= 0.
    glm::vec2 at(double s) const {
        s = std::max(0.0, std::min(s, L));
        if (s <= flat) return glm::vec2((float)s, (float)zDepth);
        double corner = kPi * r / 2.0;
        if (s <= flat + corner) {
            double t = (s - flat) / r;
            return glm::vec2((float)(flat + r * std::sin(t)), (float)(zDepth - r + r * std::cos(t)));
        }
        return glm::vec2((float)xs, (float)(zDepth - r - (s - flat - corner)));
    }
};

// Lofts between two closed rings, each given as a function of normalized
// position s in [0, 1) around the loop. Used to sew the sleeve head from
// the armhole seam to the sleeve's bicep ring.
void addRingLoft(Garment3D& g, const std::function<glm::vec3(double)>& ringA,
                 const std::function<glm::vec3(double)>& ringB,
                 int rows, int cols, const ofFloatColor& color, bool outlineA) {
    std::vector<std::vector<int>> idx(rows, std::vector<int>(cols));
    for (int i = 0; i < rows; ++i) {
        double u = (double)i / (rows - 1);
        double e = u * u * (3.0 - 2.0 * u); // ease so the head leaves the armhole smoothly
        for (int j = 0; j < cols; ++j) {
            double s = (double)j / cols;
            idx[i][j] = (int)g.mesh.getNumVertices();
            g.mesh.addVertex(glm::mix(ringA(s), ringB(s), (float)e));
            g.mesh.addColor(color);
        }
    }
    for (int i = 0; i + 1 < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            int j2 = (j + 1) % cols;
            g.mesh.addTriangle(idx[i][j], idx[i + 1][j], idx[i][j2]);
            g.mesh.addTriangle(idx[i][j2], idx[i + 1][j], idx[i + 1][j2]);
        }
    }
    if (outlineA) {
        for (int j = 0; j < cols; ++j) {
            g.outlines.addVertex(g.mesh.getVertex(idx[0][j]));
            g.outlines.addVertex(g.mesh.getVertex(idx[0][(j + 1) % cols]));
        }
    }
}

// Samples a polyline and returns a lookup by normalized arc length.
std::function<glm::vec3(double)> arcLengthLookup(const std::vector<glm::vec3>& pts) {
    auto cum = std::make_shared<std::vector<float>>();
    auto pp = std::make_shared<std::vector<glm::vec3>>(pts);
    cum->push_back(0.f);
    for (size_t i = 1; i < pp->size(); ++i)
        cum->push_back(cum->back() + glm::distance((*pp)[i - 1], (*pp)[i]));
    float total = std::max(1e-4f, cum->back());
    return [pp, cum, total](double s) {
        float target = (float)(s - std::floor(s)) * total;
        for (size_t i = 1; i < pp->size(); ++i) {
            if ((*cum)[i] >= target) {
                float seg = std::max(1e-6f, (*cum)[i] - (*cum)[i - 1]);
                float t = (target - (*cum)[i - 1]) / seg;
                return glm::mix((*pp)[i - 1], (*pp)[i], t);
            }
        }
        return pp->back();
    };
}

struct RowSpan { double xMin = 0, xMax = 0; bool valid = false; };

RowSpan spanAt(const Ring& ring, double y) {
    RowSpan s;
    s.xMin = geo::minXAtY(ring, y);
    s.xMax = geo::maxXAtY(ring, y);
    s.valid = (s.xMax - s.xMin) > 1e-4;
    return s;
}

struct PanelOpts {
    ofFloatColor color;
    bool mirrorToo = false;      // also emit the panel flipped across its center line
    bool drawCenterLine = false; // outline the center line (a seam) rather than leaving it (a fold)
    bool outlineTopEdge = true;  // false where the top row continues into another piece (no seam there)
};

// Bends one panel: `spanOf(y)` gives the fabric interval at row y,
// `place(x, y)` maps a point of the flat piece into world space. Emits a
// quad grid plus the panel's boundary as outlines.
void addBentPanel(Garment3D& g, double yFrom, double yTo,
                  const std::function<RowSpan(double)>& spanOf,
                  const std::function<glm::vec3(double, double)>& place,
                  const PanelOpts& opts) {
    struct Row { std::vector<int> idx; RowSpan span; double y = 0; bool valid = false; };
    std::vector<Row> rows(kRows);
    auto emit = [&](bool flip) {
        for (int i = 0; i < kRows; ++i) {
            Row& row = rows[i];
            row.y = yFrom + (yTo - yFrom) * ((double)i / (kRows - 1));
            row.span = spanOf(row.y);
            row.valid = row.span.valid;
            row.idx.assign(kCols, -1);
            if (!row.valid) continue;
            for (int j = 0; j < kCols; ++j) {
                double x = row.span.xMin + (row.span.xMax - row.span.xMin) * ((double)j / (kCols - 1));
                glm::vec3 p = place(flip ? -x : x, row.y);
                row.idx[j] = (int)g.mesh.getNumVertices();
                g.mesh.addVertex(p);
                g.mesh.addColor(opts.color);
            }
        }
        for (int i = 0; i + 1 < kRows; ++i) {
            if (!rows[i].valid || !rows[i + 1].valid) continue;
            for (int j = 0; j + 1 < kCols; ++j) {
                int a = rows[i].idx[j], b = rows[i].idx[j + 1];
                int c = rows[i + 1].idx[j], d = rows[i + 1].idx[j + 1];
                g.mesh.addTriangle(a, c, b);
                g.mesh.addTriangle(b, c, d);
            }
        }
        // Outlines: the panel's two side boundaries down the rows, plus its
        // top and bottom edges. A center line at x = 0 is a fold unless the
        // caller says it's a seam.
        auto lineBetween = [&](glm::vec3 p, glm::vec3 q) {
            g.outlines.addVertex(p);
            g.outlines.addVertex(q);
        };
        for (int i = 0; i + 1 < kRows; ++i) {
            if (!rows[i].valid || !rows[i + 1].valid) continue;
            bool innerIsFold = rows[i].span.xMin < 0.01 && rows[i + 1].span.xMin < 0.01;
            if (opts.drawCenterLine || !innerIsFold) {
                lineBetween(g.mesh.getVertex(rows[i].idx[0]), g.mesh.getVertex(rows[i + 1].idx[0]));
            }
            lineBetween(g.mesh.getVertex(rows[i].idx[kCols - 1]), g.mesh.getVertex(rows[i + 1].idx[kCols - 1]));
        }
        for (int i : { 0, kRows - 1 }) {
            if (!rows[i].valid) continue;
            if (i == 0 && !opts.outlineTopEdge) continue;
            for (int j = 0; j + 1 < kCols; ++j)
                lineBetween(g.mesh.getVertex(rows[i].idx[j]), g.mesh.getVertex(rows[i].idx[j + 1]));
        }
    };
    emit(false);
    if (opts.mirrorToo) emit(true);
}

// A torso half with its center line on x = 0, so its mirrored copy meets
// it there (the shirt front is stored shifted out by its placket width).
Ring centered(const Ring& r) { return geo::translate(r, Pt((float)-minXOf(r), 0.f)); }

// Both legs of a trouser, built as two tubes.
//
// `yOffset` is how far below the pattern's own datum the trousers hang:
// zero for a standalone pair, which measures from its own waist, and the
// back waist length for the trousers of a suit, where the jacket owns the
// datum and the trousers start at the waist below it.
void addTrouserLegs(Garment3D& out, const Ring& front, const Ring& back,
                    double rise, double yOffset) {
    // Legs stay round -- a leg is not flat.
    const double kLegSeamFactor = 0.70, kLegCorner = 0.5;
    Pt lo, hi; geo::bounds(front, lo, hi);

    // Each leg is a tube: the front panel wraps the front of the leg and
    // the back panel the back, meeting at the inseam and the outseam. Each
    // panel is bent around its own crease line, so half its width runs to
    // each seam.
    auto legSection = [&](double y, bool isFront, double& creaseOut) {
        RowSpan sf = spanAt(front, y), sb = spanAt(back, y);
        double wf = sf.xMax - sf.xMin, wb = sb.xMax - sb.xMin;
        double xs = kLegSeamFactor * std::min(wf, wb) / 2.0;
        RowSpan s = isFront ? sf : sb;
        creaseOut = (s.xMin + s.xMax) / 2.0;
        return CrossSection::make((isFront ? wf : wb) / 2.0, xs, 0.0, kLegCorner);
    };
    // Each leg's axis sits its own half-width away from the body's center
    // line above the crotch, so the two legs' center front/back edges meet
    // on x = 0 and the rise seam closes; below the crotch the offset holds
    // and the narrowing legs separate.
    auto legOffset = [&](double y) {
        double crease = 0;
        return legSection(std::min(y, rise), true, crease).xs;
    };

    for (int side : { -1, 1 }) {
        for (bool isFront : { true, false }) {
            const Ring& ring = isFront ? front : back;
            addBentPanel(out, lo.y, hi.y,
                [&](double y) { return spanAt(ring, y); },
                [&](double x, double y) {
                    double crease = 0;
                    CrossSection cs = legSection(y, isFront, crease);
                    double s = x - crease;
                    glm::vec2 p = cs.at(std::fabs(s));
                    double px = (s < 0 ? -p.x : p.x);
                    // +x in the pattern is the crotch/center-seam edge,
                    // which must face the body's center.
                    return glm::vec3((float)(side * (legOffset(y) - px)),
                                     (float)-(y + yOffset),
                                     isFront ? p.y : -p.y);
                },
                { isFront ? kFrontTone : kBackTone, false, true });
        }
    }
}

void measure(Garment3D& g) {
    float top = -1e9f, bottom = 1e9f, xHalf = 0.f;
    for (auto& v : g.mesh.getVertices()) {
        top = std::max(top, v.y);
        bottom = std::min(bottom, v.y);
        xHalf = std::max(xHalf, std::fabs(v.x));
    }
    if (g.mesh.getNumVertices() == 0) { top = 0.f; bottom = -1.f; }
    g.worldYTop = top;
    g.worldYBottom = bottom;
    g.worldXHalf = xHalf;
}

} // namespace

Garment3D buildGarment3D(const DesignResult& design) {
    Garment3D out;
    out.mesh.setMode(OF_PRIMITIVE_TRIANGLES);
    out.outlines.setMode(OF_PRIMITIVE_LINES);
    // How much of each panel's width runs straight across before it turns
    // the corner to the side seam. A wide seam share plus a small corner
    // means a flat panel; the back is flattest, since a back has no bust
    // to round out.
    const double kSeamFactor = 0.90;
    const double kFrontCorner = 0.15, kBackCorner = 0.08;

    if (design.garmentKey == "Pants") {
        const Piece* F = findPiece(design, "F");
        const Piece* B = findPiece(design, "B");
        if (F) {
            addTrouserLegs(out, F->sewing, B ? B->sewing : F->sewing,
                           design.style.get("riseCm", 27.f), 0.0);
        }
    } else {
        const Piece* F = findPiece(design, "F");
        const Piece* B = findPiece(design, "B");
        if (F) {
            Ring front = centered(F->sewing);
            Ring back = B ? centered(B->sewing) : front;
            Pt fLo, fHi; geo::bounds(front, fLo, fHi);
            Pt bLo, bHi; geo::bounds(back, bLo, bHi);

            // The front and back panels share the side seam at (xs, 0), so
            // the shell closes. xs comes from whichever panel is narrower
            // at that row; any extra width on the other panel becomes
            // depth, never stretch.
            // The armhole is a real opening: between the shoulder point and
            // the underarm, the front and back panels' seam edges separate
            // in z instead of meeting, and close again at both ends (the
            // shoulder seam above, the side seam below).
            const Piece* SLp = findPiece(design, "SL");
            double shoulderY = block::kShoulderDropCm;
            // The armhole the pattern was actually drafted with (it is
            // adjustable), not a re-derived guess.
            double armholeY = design.armholeY > 0 ? design.armholeY : 22.0;
            double armholeSpan = std::max(1e-3, armholeY - shoulderY);
            double armGap = 4.0;
            if (SLp) {
                double widest = 0;
                for (int i = 0; i < kRows; ++i) {
                    Pt a, b; geo::bounds(SLp->sewing, a, b);
                    double y = a.y + (b.y - a.y) * ((double)i / (kRows - 1));
                    RowSpan s = spanAt(SLp->sewing, y);
                    if (s.valid) widest = std::max(widest, s.xMax - s.xMin);
                }
                if (widest > 0) armGap = widest / (2.0 * kPi); // the arm's own radius
            }
            armGap = std::min(armGap, armholeSpan * 0.35);
            auto armholeZ = [&](double y) {
                if (y <= shoulderY || y >= armholeY) return 0.0;
                return armGap * std::sin(kPi * (y - shoulderY) / armholeSpan);
            };
            auto seamX = [&](double y) {
                double lf = geo::maxXAtY(front, y), lb = geo::maxXAtY(back, y);
                // Shared by both panels, so it has to stay inside what each
                // one's own width and corner allow.
                return std::min({ kSeamFactor * std::min(lf, lb),
                                  lf / (1.0 + 0.5708 * kFrontCorner),
                                  lb / (1.0 + 0.5708 * kBackCorner) });
            };
            auto torsoSection = [&](double y, bool isFront) {
                double lf = geo::maxXAtY(front, y), lb = geo::maxXAtY(back, y);
                return CrossSection::make(isFront ? lf : lb, seamX(y), armholeZ(y),
                                          isFront ? kFrontCorner : kBackCorner);
            };
            auto placeTorso = [&](bool isFront, double yOffsetWorld) {
                return [&, isFront, yOffsetWorld](double x, double y) {
                    CrossSection cs = torsoSection(y, isFront);
                    glm::vec2 p = cs.at(std::fabs(x));
                    return glm::vec3((float)(x < 0 ? -p.x : p.x),
                                     (float)(yOffsetWorld - y),
                                     isFront ? p.y : -p.y);
                };
            };

            addBentPanel(out, fLo.y, fHi.y, [&](double y) { return spanAt(front, y); },
                         placeTorso(true, 0.0), { kFrontTone, true, !F->foldAtCF });
            if (B) addBentPanel(out, bLo.y, bHi.y, [&](double y) { return spanAt(back, y); },
                                placeTorso(false, 0.0), { kBackTone, true, !B->foldAtCF });

            // Empire: gathered skirt panels hang from the bodice hem.
            const Piece* SK = findPiece(design, "SK");
            const Piece* SKB = findPiece(design, "SKB");
            if (SK) {
                const Ring& skF = SK->sewing;
                const Ring& skB = SKB ? SKB->sewing : SK->sewing;
                Pt sLo, sHi; geo::bounds(skF, sLo, sHi);
                // The skirt is cut wider than the bodice hem because it is
                // GATHERED into that seam, so its flat width is not its
                // width in space: at the seam it is compressed to the
                // bodice hem, and the gathers release over the next stretch
                // down. This is the one place the preview deliberately does
                // not take a piece's flat width literally.
                double bodiceHemF = geo::maxXAtY(front, fHi.y);
                double bodiceHemB = geo::maxXAtY(back, bHi.y);
                double skirtTopF = std::max(1e-3, geo::maxXAtY(skF, sLo.y));
                double skirtTopB = std::max(1e-3, geo::maxXAtY(skB, sLo.y));
                double release = std::max(1e-3, (sHi.y - sLo.y) * 0.45);
                auto gatherFactor = [&](double y, bool isFront) {
                    double t = std::min(1.0, std::max(0.0, (y - sLo.y) / release));
                    double atSeam = (isFront ? bodiceHemF / skirtTopF : bodiceHemB / skirtTopB);
                    return atSeam + (1.0 - atSeam) * t;
                };
                auto skirtSection = [&](double y, bool isFront) {
                    double lf = geo::maxXAtY(skF, y) * gatherFactor(y, true);
                    double lb = geo::maxXAtY(skB, y) * gatherFactor(y, false);
                    double xs = std::min({ kSeamFactor * std::min(lf, lb),
                                           lf / (1.0 + 0.5708 * kFrontCorner),
                                           lb / (1.0 + 0.5708 * kBackCorner) });
                    return CrossSection::make(isFront ? lf : lb, xs, 0.0,
                                              isFront ? kFrontCorner : kBackCorner);
                };
                double hemWorldY = -fHi.y;
                for (bool isFront : { true, false }) {
                    const Ring& ring = isFront ? skF : skB;
                    addBentPanel(out, sLo.y, sHi.y, [&](double y) { return spanAt(ring, y); },
                        [&, isFront, hemWorldY](double x, double y) {
                            CrossSection cs = skirtSection(y, isFront);
                            // Gathers compress the fabric evenly across the
                            // row, so the arc-length position scales too.
                            glm::vec2 p = cs.at(std::fabs(x) * gatherFactor(y, isFront));
                            return glm::vec3((float)(x < 0 ? -p.x : p.x),
                                             (float)(hemWorldY - (y - sLo.y)),
                                             isFront ? p.y : -p.y);
                        },
                        { isFront ? kFrontTone : kBackTone, true, false });
                }
            }

            // Sleeves: the piece is folded along its grainline (x = 0) and
            // its two long edges sew to each other, so it bends into a tube
            // whose circumference is the piece's own width at that row.
            // The tube hangs from the shoulder point along the arm.
            const Piece* SL = SLp;
            if (SL) {
                const Ring& sl = SL->sewing;
                Pt slLo, slHi; geo::bounds(sl, slLo, slHi);
                // The bicep is the sleeve's widest row -- where the cap ends
                // and the arm tube begins.
                double biceps = slLo.y, widest = 0;
                for (int i = 0; i < kRows; ++i) {
                    double y = slLo.y + (slHi.y - slLo.y) * ((double)i / (kRows - 1));
                    RowSpan s = spanAt(sl, y);
                    if (s.valid && (s.xMax - s.xMin) > widest) { widest = s.xMax - s.xMin; biceps = y; }
                }
                double capHeight = std::max(1.0, biceps - slLo.y);
                double tilt = glm::radians(38.0); // arm angle away from vertical

                for (int side : { -1, 1 }) {
                    // The arm's frame. `across` points out along the
                    // shoulder, so -across is the underarm side: the
                    // sleeve's own underarm seam lands there, and its
                    // grainline (the piece's center) sits on top.
                    glm::vec3 dir((float)(side * std::sin(tilt)), (float)-std::cos(tilt), 0.f);
                    glm::vec3 across((float)(side * std::cos(tilt)), (float)std::sin(tilt), 0.f);
                    glm::vec3 depth(0.f, 0.f, 1.f);
                    glm::vec3 armholeCenter((float)(side * seamX((shoulderY + armholeY) / 2.0)),
                                            (float)-((shoulderY + armholeY) / 2.0), 0.f);
                    // The cap wraps over the shoulder, so it covers less
                    // distance along the arm than its flat height.
                    glm::vec3 bicepCenter = armholeCenter + dir * (float)(capHeight * 0.6);

                    auto ringAt = [&](glm::vec3 center, double radius, double s) {
                        return center - across * (float)(radius * std::cos(2.0 * kPi * s))
                                      + depth * (float)(radius * std::sin(2.0 * kPi * s));
                    };

                    // The armhole seam, walked from the underarm up the
                    // front to the shoulder and back down the back, so
                    // s = 0 is the underarm and s = 0.5 the shoulder --
                    // the same convention as the sleeve's own rings.
                    std::vector<glm::vec3> rim;
                    const int kRim = 40;
                    for (int i = 0; i <= kRim / 2; ++i) {
                        double y = armholeY + (shoulderY - armholeY) * ((double)i / (kRim / 2));
                        rim.push_back(glm::vec3((float)(side * seamX(y)), (float)-y, (float)armholeZ(y)));
                    }
                    for (int i = 1; i <= kRim / 2; ++i) {
                        double y = shoulderY + (armholeY - shoulderY) * ((double)i / (kRim / 2));
                        rim.push_back(glm::vec3((float)(side * seamX(y)), (float)-y, (float)-armholeZ(y)));
                    }
                    auto rimAt = arcLengthLookup(rim);

                    // Sleeve head: sewn to the armhole seam at one end and
                    // to the sleeve's bicep ring at the other. The cap's
                    // fabric is eased into the armhole, so this is a smooth
                    // head rather than the cap piece matched point by point.
                    double bicepRadius = widest / (2.0 * kPi);
                    addRingLoft(out, rimAt,
                                [&](double s) { return ringAt(bicepCenter, bicepRadius, s); },
                                10, 40, kSleeveTone, true);

                    // Arm tube: bicep down to the hem, each row's
                    // circumference the sleeve piece's own width there.
                    addBentPanel(out, biceps, slHi.y, [&](double y) { return spanAt(sl, y); },
                        [&](double x, double y) {
                            RowSpan s = spanAt(sl, y);
                            double circumference = std::max(1e-3, s.xMax - s.xMin);
                            double radius = circumference / (2.0 * kPi);
                            double along = y - biceps; // remaining sleeve length, along the arm
                            double t = (x - s.xMin) / circumference;                        // 0 at the underarm seam
                            return ringAt(bicepCenter + dir * (float)along, radius, t);
                        },
                        // No outline at the bicep: the sleeve head continues
                        // into the tube there, it isn't a seam.
                        { kSleeveTone, false, false, false });
                }
            }
        }
    }

    // Any garment carrying P-prefixed trouser panels -- a pant suit, a
    // jumpsuit -- is a torso AND legs, so previewing only the torso would
    // show half of it. They hang from the body's waist, which is where the
    // torso block's own datum puts it.
    {
        const Piece* PF = findPiece(design, "PF");
        const Piece* PB = findPiece(design, "PB");
        if (PF) {
            addTrouserLegs(out, PF->sewing, PB ? PB->sewing : PF->sewing,
                           design.style.get("riseCm", 28.f),
                           design.size.get("backWaistLength", 40.0));
        }
    }

    measure(out);
    return out;
}

} } // namespace pf::render3d
