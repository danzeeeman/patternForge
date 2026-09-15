#include "Perimeter.h"
#include "thirdparty/clipper/clipper.hpp"
#include <algorithm>
#include <cmath>

using namespace ClipperLib;

namespace pf { namespace geo {

// Clipper works in int64 coordinates. Garment pieces run to ~2m, so a
// scale of 10000 (1/10000 cm = 100 nanometers/unit) stays far inside
// int64 range while keeping sub-micron rounding error -- effectively
// exact for sewing-pattern purposes.
static constexpr double kScale = 10000.0;

static Path toPath(const Ring& r) {
    Path p;
    p.reserve(r.size());
    for (auto& pt : r) {
        p.push_back(IntPoint((cInt)std::llround(pt.x * kScale), (cInt)std::llround(pt.y * kScale)));
    }
    return p;
}

static Ring fromPath(const Path& p) {
    Ring r;
    r.reserve(p.size());
    for (auto& ip : p) {
        r.push_back(Pt((float)(ip.X / kScale), (float)(ip.Y / kScale)));
    }
    return r;
}

double area(const Ring& r) {
    if (r.size() < 3) return 0.0;
    double a = 0.0;
    for (size_t i = 0; i < r.size(); ++i) {
        auto& p0 = r[i];
        auto& p1 = r[(i + 1) % r.size()];
        a += (double)p0.x * p1.y - (double)p1.x * p0.y;
    }
    return a * 0.5;
}

double perimeterLength(const Ring& r) {
    if (r.size() < 2) return 0.0;
    double total = 0.0;
    for (size_t i = 0; i < r.size(); ++i) {
        total += glm::distance(r[i], r[(i + 1) % r.size()]);
    }
    return total;
}

Ring densify(const Ring& r, double stepCm) {
    Ring out;
    if (r.size() < 2 || stepCm <= 0.0) return r;
    for (size_t i = 0; i < r.size(); ++i) {
        const Pt& a = r[i];
        const Pt& b = r[(i + 1) % r.size()];
        double segLen = glm::distance(a, b);
        int n = std::max(1, (int)std::ceil(segLen / stepCm));
        for (int k = 0; k < n; ++k) {
            float t = (float)k / (float)n;
            out.push_back(a + (b - a) * t);
        }
    }
    return out;
}

std::vector<Ring> offsetAll(const Ring& r, double deltaCm) {
    // ClipperOffset's outward/inward sign convention depends on winding
    // order. Callers build rings in whatever order is convenient, so
    // normalize to positive (shoelace) area first: positive deltaCm then
    // always means "outward" regardless of how the ring was authored.
    Ring oriented = r;
    if (area(r) < 0) std::reverse(oriented.begin(), oriented.end());
    ClipperOffset co;
    co.AddPath(toPath(oriented), jtMiter, etClosedPolygon);
    Paths solution;
    co.Execute(solution, deltaCm * kScale);
    std::vector<Ring> out;
    out.reserve(solution.size());
    for (auto& p : solution) out.push_back(fromPath(p));
    return out;
}

Ring offset(const Ring& r, double deltaCm) {
    auto all = offsetAll(r, deltaCm);
    if (all.empty()) return r;
    // Keep the largest-area ring: an inward offset can spawn slivers,
    // an outward one is normally a single result.
    auto best = std::max_element(all.begin(), all.end(), [](const Ring& a, const Ring& b) {
        return std::fabs(area(a)) < std::fabs(area(b));
    });
    return *best;
}

std::vector<Ring> unionRings(const std::vector<Ring>& rs) {
    Clipper c;
    for (auto& r : rs) c.AddPath(toPath(r), ptSubject, true);
    Paths solution;
    c.Execute(ctUnion, solution, pftNonZero, pftNonZero);
    std::vector<Ring> out;
    for (auto& p : solution) out.push_back(fromPath(p));
    return out;
}

std::vector<Ring> intersectRings(const Ring& a, const Ring& b) {
    Clipper c;
    c.AddPath(toPath(a), ptSubject, true);
    c.AddPath(toPath(b), ptClip, true);
    Paths solution;
    c.Execute(ctIntersection, solution, pftNonZero, pftNonZero);
    std::vector<Ring> out;
    for (auto& p : solution) out.push_back(fromPath(p));
    return out;
}

double symmetricDifferenceArea(const Ring& a, const Ring& b) {
    Clipper c;
    c.AddPath(toPath(a), ptSubject, true);
    c.AddPath(toPath(b), ptClip, true);
    Paths solution;
    c.Execute(ctXor, solution, pftNonZero, pftNonZero);
    double total = 0.0;
    for (auto& p : solution) total += std::fabs(area(fromPath(p)));
    return total;
}

std::vector<Ring> clipToBox(const Ring& r, double xLo, double yLo, double xHi, double yHi) {
    Ring box = { {(float)xLo, (float)yLo}, {(float)xHi, (float)yLo}, {(float)xHi, (float)yHi}, {(float)xLo, (float)yHi} };
    return intersectRings(r, box);
}

Pt centroid(const Ring& r) {
    if (r.empty()) return Pt(0, 0);
    double cx = 0, cy = 0, a = 0;
    for (size_t i = 0; i < r.size(); ++i) {
        auto& p0 = r[i];
        auto& p1 = r[(i + 1) % r.size()];
        double cross = (double)p0.x * p1.y - (double)p1.x * p0.y;
        cx += (p0.x + p1.x) * cross;
        cy += (p0.y + p1.y) * cross;
        a += cross;
    }
    a *= 0.5;
    if (std::fabs(a) < 1e-9) {
        // degenerate ring: fall back to the plain average of its points
        Pt avg(0, 0);
        for (auto& p : r) avg += p;
        return avg / (float)r.size();
    }
    return Pt((float)(cx / (6.0 * a)), (float)(cy / (6.0 * a)));
}

void bounds(const Ring& r, Pt& lo, Pt& hi) {
    lo = Pt(1e9f, 1e9f);
    hi = Pt(-1e9f, -1e9f);
    for (auto& p : r) {
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
    }
}

Ring translate(const Ring& r, Pt by) {
    Ring out;
    out.reserve(r.size());
    for (auto& p : r) out.push_back(p + by);
    return out;
}

std::vector<Pt> chaikinOpen(const std::vector<Pt>& pts, int iterations) {
    if (pts.size() < 3) return pts;
    std::vector<Pt> cur = pts;
    for (int it = 0; it < iterations; ++it) {
        std::vector<Pt> next;
        next.reserve(cur.size() * 2);
        next.push_back(cur.front());
        for (size_t i = 0; i + 1 < cur.size(); ++i) {
            const Pt& a = cur[i];
            const Pt& b = cur[i + 1];
            next.push_back(a + (b - a) * 0.25f);
            next.push_back(a + (b - a) * 0.75f);
        }
        next.push_back(cur.back());
        cur = next;
    }
    return cur;
}

static double scanXAtY(const Ring& r, double y, bool wantMax) {
    double best = wantMax ? -1e18 : 1e18;
    bool found = false;
    for (size_t i = 0; i < r.size(); ++i) {
        const Pt& a = r[i];
        const Pt& b = r[(i + 1) % r.size()];
        double aY = a.y, bY = b.y;
        if ((aY <= y && bY >= y) || (aY >= y && bY <= y)) {
            double x;
            if (std::fabs(bY - aY) < 1e-9) {
                x = wantMax ? std::max(a.x, b.x) : std::min(a.x, b.x);
            } else {
                double t = (y - aY) / (bY - aY);
                x = a.x + (b.x - a.x) * (float)t;
            }
            best = wantMax ? std::max(best, x) : std::min(best, x);
            found = true;
        }
    }
    return found ? best : 0.0;
}

double maxXAtY(const Ring& r, double y) { return scanXAtY(r, y, true); }
double minXAtY(const Ring& r, double y) { return scanXAtY(r, y, false); }

static double scanYAtX(const Ring& r, double x, bool wantMax) {
    double best = wantMax ? -1e18 : 1e18;
    bool found = false;
    for (size_t i = 0; i < r.size(); ++i) {
        const Pt& a = r[i];
        const Pt& b = r[(i + 1) % r.size()];
        double aX = a.x, bX = b.x;
        if ((aX <= x && bX >= x) || (aX >= x && bX <= x)) {
            double y;
            if (std::fabs(bX - aX) < 1e-9) {
                y = wantMax ? std::max(a.y, b.y) : std::min(a.y, b.y);
            } else {
                double t = (x - aX) / (bX - aX);
                y = a.y + (b.y - a.y) * (float)t;
            }
            best = wantMax ? std::max(best, y) : std::min(best, y);
            found = true;
        }
    }
    return found ? best : 0.0;
}

double minYAtX(const Ring& r, double x) { return scanYAtX(r, x, false); }
double maxYAtX(const Ring& r, double x) { return scanYAtX(r, x, true); }

namespace {
bool segmentsCross(Pt a, Pt b, Pt c, Pt d) {
    auto side = [](Pt p, Pt q, Pt r) {
        double v = (double)(q.x - p.x) * (r.y - p.y) - (double)(q.y - p.y) * (r.x - p.x);
        return (v > 1e-9) - (v < -1e-9);
    };
    int d1 = side(a, b, c), d2 = side(a, b, d), d3 = side(c, d, a), d4 = side(c, d, b);
    return d1 * d2 < 0 && d3 * d4 < 0; // proper crossing only; shared endpoints are fine
}
} // namespace

bool contains(const Ring& r, Pt p) {
    // Ray cast along +x: an odd number of crossings means inside.
    bool in = false;
    for (size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
        bool straddles = (r[i].y > p.y) != (r[j].y > p.y);
        if (!straddles) continue;
        double t = (double)(p.y - r[i].y) / (r[j].y - r[i].y);
        if (p.x < r[i].x + t * (r[j].x - r[i].x)) in = !in;
    }
    return in;
}

bool selfIntersects(const Ring& r) {
    int n = (int)r.size();
    if (n < 4) return false;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue; // adjacent across the wrap
            if (segmentsCross(r[i], r[(i + 1) % n], r[j], r[(j + 1) % n])) return true;
        }
    }
    return false;
}

Ring normalizeToOrigin(const Ring& r) {
    Pt lo, hi;
    bounds(r, lo, hi);
    return translate(r, -lo);
}

} } // namespace pf::geo
