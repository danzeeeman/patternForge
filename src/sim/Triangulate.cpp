#include "Triangulate.h"
#include <algorithm>
#include <cmath>

namespace pf { namespace sim {

namespace {

double ringPerimeter(const Ring& r) {
    double len = 0.0;
    for (size_t i = 0; i < r.size(); ++i)
        len += glm::distance(r[i], r[(i + 1) % r.size()]);
    return len;
}

// The point a given arc length around the outline.
Pt pointAt(const Ring& r, double s) {
    double travelled = 0.0;
    for (size_t i = 0; i < r.size(); ++i) {
        const Pt& a = r[i];
        const Pt& b = r[(i + 1) % r.size()];
        double seg = glm::distance(a, b);
        if (travelled + seg >= s) {
            double t = seg < 1e-9 ? 0.0 : (s - travelled) / seg;
            return a + (b - a) * (float)t;
        }
        travelled += seg;
    }
    return r.back();
}

// --- Bowyer-Watson Delaunay ------------------------------------------
//
// Delaunay rather than ear-clipping because cloth wants triangles of an
// even shape: ear-clipping a long thin panel produces slivers, and a
// sliver behaves like a stiff rod in a cloth solver -- the panel creases
// along it and will not drape.

struct Tri { int a, b, c; };

bool inCircumcircle(const Pt& p, const Pt& a, const Pt& b, const Pt& c) {
    double ax = a.x - p.x, ay = a.y - p.y;
    double bx = b.x - p.x, by = b.y - p.y;
    double cx = c.x - p.x, cy = c.y - p.y;
    double det = (ax * ax + ay * ay) * (bx * cy - by * cx)
               - (bx * bx + by * by) * (ax * cy - ay * cx)
               + (cx * cx + cy * cy) * (ax * by - ay * bx);
    // Positive only for a counter-clockwise triangle, so normalise by the
    // triangle's own orientation.
    double orient = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    return orient > 0 ? det > 1e-12 : det < -1e-12;
}

std::vector<Tri> delaunay(const std::vector<Pt>& pts) {
    std::vector<Tri> tris;
    if (pts.size() < 3) return tris;

    // A super-triangle large enough to contain every point.
    Pt lo = pts[0], hi = pts[0];
    for (auto& p : pts) {
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
    }
    float dx = hi.x - lo.x, dy = hi.y - lo.y;
    float d = std::max(dx, dy) * 10.f + 10.f;
    Pt mid((lo.x + hi.x) / 2.f, (lo.y + hi.y) / 2.f);

    std::vector<Pt> work = pts;
    int s0 = (int)work.size(); work.push_back(Pt(mid.x - d, mid.y - d));
    int s1 = (int)work.size(); work.push_back(Pt(mid.x + d, mid.y - d));
    int s2 = (int)work.size(); work.push_back(Pt(mid.x, mid.y + d));
    tris.push_back({ s0, s1, s2 });

    std::vector<std::pair<int, int>> edges;
    for (int i = 0; i < (int)pts.size(); ++i) {
        edges.clear();
        for (size_t t = 0; t < tris.size();) {
            const Tri& tr = tris[t];
            if (inCircumcircle(work[i], work[tr.a], work[tr.b], work[tr.c])) {
                edges.push_back({ tr.a, tr.b });
                edges.push_back({ tr.b, tr.c });
                edges.push_back({ tr.c, tr.a });
                tris[t] = tris.back();
                tris.pop_back();
            } else {
                ++t;
            }
        }
        // Only edges appearing once bound the hole left behind.
        for (size_t e = 0; e < edges.size(); ++e) {
            bool shared = false;
            for (size_t f = 0; f < edges.size(); ++f) {
                if (e == f) continue;
                if ((edges[e].first == edges[f].first && edges[e].second == edges[f].second) ||
                    (edges[e].first == edges[f].second && edges[e].second == edges[f].first)) {
                    shared = true; break;
                }
            }
            if (!shared) tris.push_back({ edges[e].first, edges[e].second, i });
        }
    }

    // Drop anything still touching the super-triangle.
    std::vector<Tri> out;
    for (auto& tr : tris)
        if (tr.a < s0 && tr.b < s0 && tr.c < s0) out.push_back(tr);
    return out;
}

} // namespace

Mesh2D triangulate(const Ring& outline, double resolutionCm) {
    Mesh2D mesh;
    if (outline.size() < 3) return mesh;
    double res = std::max(0.25, resolutionCm);
    double perim = ringPerimeter(outline);
    if (perim < res * 3) return mesh;

    // --- boundary, at even arc-length steps ---------------------------
    int nb = std::max(8, (int)std::llround(perim / res));
    for (int i = 0; i < nb; ++i) {
        double s = perim * (double)i / nb;
        mesh.boundary.push_back((int)mesh.verts.size());
        mesh.boundaryT.push_back((float)((double)i / nb));
        mesh.verts.push_back(pointAt(outline, s));
    }

    // --- interior, on a staggered grid --------------------------------
    // Staggered (every other row offset by half a step) so the Delaunay
    // comes out close to equilateral rather than made of right triangles,
    // which stretch differently along the two diagonals.
    Pt lo, hi;
    geo::bounds(outline, lo, hi);
    double rowH = res * 0.866;   // equilateral row spacing
    int row = 0;
    for (double y = lo.y + rowH * 0.5; y < hi.y; y += rowH, ++row) {
        double xoff = (row % 2) ? res * 0.5 : 0.0;
        for (double x = lo.x + xoff + res * 0.5; x < hi.x; x += res) {
            Pt p((float)x, (float)y);
            if (!geo::contains(outline, p)) continue;
            // Keep clear of the boundary ring, or the two sets of points
            // produce slivers where they nearly coincide.
            bool tooClose = false;
            for (int bi : mesh.boundary) {
                if (glm::distance(mesh.verts[bi], p) < res * 0.62) { tooClose = true; break; }
            }
            if (!tooClose) mesh.verts.push_back(p);
        }
    }

    // --- triangulate and keep what is inside --------------------------
    auto tris = delaunay(mesh.verts);
    for (auto& t : tris) {
        Pt c((mesh.verts[t.a].x + mesh.verts[t.b].x + mesh.verts[t.c].x) / 3.f,
             (mesh.verts[t.a].y + mesh.verts[t.b].y + mesh.verts[t.c].y) / 3.f);
        // Delaunay fills the convex hull; a concave panel (an armhole, a
        // neckline) needs the triangles outside its own outline removed.
        if (!geo::contains(outline, c)) continue;
        mesh.tris.push_back(t.a);
        mesh.tris.push_back(t.b);
        mesh.tris.push_back(t.c);
    }
    return mesh;
}

} } // namespace pf::sim
