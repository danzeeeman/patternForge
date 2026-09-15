#include "Avatar.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <map>

namespace pf { namespace sim {

namespace {

// "f 12/3/4 15//7 9" -- take the vertex index, ignore texture and normal.
int faceIndex(const std::string& token) {
    size_t slash = token.find('/');
    std::string num = slash == std::string::npos ? token : token.substr(0, slash);
    if (num.empty()) return 0;
    return std::atoi(num.c_str());
}

// Closest point on a triangle to p, and whether p is behind its face.
glm::vec3 closestOnTriangle(const glm::vec3& p, const glm::vec3& a,
                            const glm::vec3& b, const glm::vec3& c) {
    glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.f && d2 <= 0.f) return a;
    glm::vec3 bp = p - b;
    float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.f && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) return a + ab * (d1 / (d1 - d3));
    glm::vec3 cp = p - c;
    float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.f && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) return a + ac * (d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f)
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float denom = 1.f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

} // namespace

bool Avatar::load(const std::string& objPath, double napeFromTopCm, double simplifyCm) {
    std::ifstream in(objPath.c_str());
    if (!in) return false;

    verts_.clear();
    tris_.clear();
    std::vector<glm::vec3> raw;
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 2) continue;
        if (line[0] == 'v' && line[1] == ' ') {
            std::istringstream ss(line.substr(2));
            float x, y, z;
            if (ss >> x >> y >> z) raw.push_back(glm::vec3(x, y, z));
        } else if (line[0] == 'f' && line[1] == ' ') {
            std::istringstream ss(line.substr(2));
            std::vector<int> idx;
            std::string tok;
            while (ss >> tok) {
                int i = faceIndex(tok);
                if (i < 0) i = (int)raw.size() + i + 1;   // negative indices are relative
                if (i >= 1) idx.push_back(i - 1);
            }
            // Fan-triangulate: these exports carry quads as well as tris.
            for (size_t k = 2; k < idx.size(); ++k) {
                tris_.push_back(idx[0]);
                tris_.push_back(idx[k - 1]);
                tris_.push_back(idx[k]);
            }
        }
    }
    if (raw.empty() || tris_.empty()) return false;

    // Millimetres to centimetres, and Y measured down from the nape rather
    // than up from the floor.
    float top = raw[0].y, bottom = raw[0].y;
    for (auto& v : raw) { top = std::max(top, v.y); bottom = std::min(bottom, v.y); }
    const float kMmToCm = 0.1f;
    float napeY = (top - (float)(napeFromTopCm / kMmToCm));

    verts_.reserve(raw.size());
    for (auto& v : raw)
        verts_.push_back(glm::vec3(v.x * kMmToCm, (v.y - napeY) * kMmToCm, v.z * kMmToCm));

    minY_ = (bottom - napeY) * kMmToCm;
    maxY_ = (top - napeY) * kMmToCm;
    origTris_ = tris_.size() / 3;

    // --- decimate by vertex clustering --------------------------------
    //
    // Every vertex in the same small cube becomes one vertex at their
    // average, and any triangle that collapses to a line or a point goes.
    // Crude next to a quadric-error decimator, but a body has no sharp
    // features to preserve and this keeps the silhouette that a garment
    // actually hangs on.
    if (simplifyCm > 0.05) {
        float cs = (float)simplifyCm;
        glm::vec3 lo = verts_[0];
        for (auto& v : verts_) lo = glm::min(lo, v);
        std::map<long long, int> cellVert;
        std::vector<glm::vec3> sum;
        std::vector<int> count;
        std::vector<int> map(verts_.size());
        for (size_t i = 0; i < verts_.size(); ++i) {
            long long ix = (long long)std::floor((verts_[i].x - lo.x) / cs);
            long long iy = (long long)std::floor((verts_[i].y - lo.y) / cs);
            long long iz = (long long)std::floor((verts_[i].z - lo.z) / cs);
            long long key = (ix * 73856093LL) ^ (iy * 19349663LL) ^ (iz * 83492791LL);
            auto it = cellVert.find(key);
            if (it == cellVert.end()) {
                cellVert[key] = (int)sum.size();
                map[i] = (int)sum.size();
                sum.push_back(verts_[i]);
                count.push_back(1);
            } else {
                map[i] = it->second;
                sum[it->second] += verts_[i];
                ++count[it->second];
            }
        }
        std::vector<glm::vec3> nv(sum.size());
        for (size_t i = 0; i < sum.size(); ++i) nv[i] = sum[i] / (float)count[i];
        std::vector<int> nt;
        nt.reserve(tris_.size());
        for (size_t t = 0; t + 2 < tris_.size(); t += 3) {
            int a = map[tris_[t]], b = map[tris_[t + 1]], c2 = map[tris_[t + 2]];
            if (a == b || b == c2 || a == c2) continue;
            nt.push_back(a); nt.push_back(b); nt.push_back(c2);
        }
        if (nt.size() >= 9) { verts_ = nv; tris_ = nt; }
    }

    mesh_.clear();
    mesh_.setMode(OF_PRIMITIVE_TRIANGLES);
    for (auto& v : verts_) {
        mesh_.addVertex(v);
        mesh_.addColor(ofFloatColor(0.42f, 0.40f, 0.39f));
    }
    for (size_t t = 0; t + 2 < tris_.size(); t += 3)
        mesh_.addTriangle(tris_[t], tris_[t + 1], tris_[t + 2]);
    addNormals();

    size_t slash = objPath.find_last_of('/');
    name_ = slash == std::string::npos ? objPath : objPath.substr(slash + 1);

    buildGrid();
    return true;
}

double Avatar::girthAt(double cmBelowNape, double bandCm) const {
    // Convex hull of the slice: a body section is convex enough that this
    // is the tape-measure reading, and it ignores the dent between the
    // legs that a raw perimeter would follow.
    float y = (float)-cmBelowNape;
    std::vector<glm::vec2> pts;
    for (auto& v : verts_) {
        if (std::fabs(v.y - y) > bandCm) continue;
        if (std::fabs(v.x) > 32.f) continue;        // leave the arms out
        pts.push_back(glm::vec2(v.x, v.z));
    }
    if (pts.size() < 8) return 0.0;
    std::sort(pts.begin(), pts.end(), [](const glm::vec2& a, const glm::vec2& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    auto cross = [](const glm::vec2& o, const glm::vec2& a, const glm::vec2& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    std::vector<glm::vec2> hull;
    for (int pass = 0; pass < 2; ++pass) {
        size_t start = hull.size();
        for (size_t i = 0; i < pts.size(); ++i) {
            const glm::vec2& p = pass ? pts[pts.size() - 1 - i] : pts[i];
            while (hull.size() >= start + 2 &&
                   cross(hull[hull.size() - 2], hull[hull.size() - 1], p) <= 0) hull.pop_back();
            hull.push_back(p);
        }
        hull.pop_back();
    }
    double per = 0.0;
    for (size_t i = 0; i < hull.size(); ++i)
        per += glm::distance(hull[i], hull[(i + 1) % hull.size()]);
    return per;
}

bool Avatar::sectionAt(double cmBelowNape, float& halfWidthCm, float& halfDepthCm,
                       double bandCm) const {
    if (verts_.empty()) return false;
    // Arms have to come out or every level from the shoulder to the wrist
    // measures as wide as the wingspan. The arm junction is where they
    // start, so that is the cut.
    glm::vec3 sh, d;
    float r = 6.f;
    float cut = armAxis(sh, d, r) ? sh.x : 1e9f;

    float y = (float)-cmBelowNape;
    float xMax = 0.f, zLo = 1e9f, zHi = -1e9f;
    int n = 0;
    for (auto& v : verts_) {
        if (std::fabs(v.y - y) > bandCm) continue;
        if (std::fabs(v.x) > cut) continue;
        xMax = std::max(xMax, std::fabs(v.x));
        zLo = std::min(zLo, v.z);
        zHi = std::max(zHi, v.z);
        ++n;
    }
    if (n < 8 || xMax < 1.f || zHi <= zLo) return false;
    halfWidthCm = xMax;
    halfDepthCm = (zHi - zLo) * 0.5f;
    return true;
}

bool Avatar::armAxis(glm::vec3& shoulder, glm::vec3& dir, float& radiusCm) const {
    if (verts_.size() < 100) return false;
    float maxX = 0.f;
    for (auto& v : verts_) maxX = std::max(maxX, v.x);
    if (maxX < 20.f) return false;

    // Where the arm stops being an arm.
    //
    // Scanning inward, the vertical extent of a thin x-slab stays about an
    // arm thick all the way along the limb and then jumps to the height of
    // the whole torso. That jump is the shoulder, and everything else is
    // measured from it -- sampling at fixed fractions of the body's width
    // instead lands on the forearm and the HAND on one avatar and on the
    // upper arm on another, which is how the sleeve ended up following the
    // angle of a forearm.
    const int kSlabs = 256;
    std::vector<float> lo(kSlabs, 1e9f), hi(kSlabs, -1e9f);
    for (auto& v : verts_) {
        int s = (int)(v.x / maxX * (kSlabs - 1));
        if (s < 0 || s >= kSlabs) continue;
        lo[s] = std::min(lo[s], v.y);
        hi[s] = std::max(hi[s], v.y);
    }
    // An arm's thickness, measured over a stretch that is arm on every
    // body: past the shoulder, short of the hand.
    std::vector<float> spans;
    for (int s = (int)(0.45f * kSlabs); s < (int)(0.70f * kSlabs); ++s)
        if (hi[s] > lo[s]) spans.push_back(hi[s] - lo[s]);
    if (spans.empty()) return false;
    std::sort(spans.begin(), spans.end());
    float armSpan = std::max(4.f, spans[spans.size() / 2]);

    float junctionX = 0.30f * maxX;
    for (int s = (int)(0.70f * kSlabs); s >= 0; --s) {
        if (hi[s] <= lo[s]) continue;
        if (hi[s] - lo[s] > 2.5f * armSpan) {
            junctionX = (float)s / (kSlabs - 1) * maxX;
            break;
        }
    }
    float armLen = maxX - junctionX;
    if (armLen < 10.f) return false;

    // Two rings along the arm -- upper arm and forearm, both clear of the
    // hand. The arm is a tube, so a slice of it averages to a point on its
    // axis, and two of those give the axis.
    auto centroid = [&](float a, float b, glm::vec3& out) {
        glm::vec3 sum(0);
        int n = 0;
        for (auto& v : verts_) {
            if (v.x < junctionX + a * armLen || v.x > junctionX + b * armLen) continue;
            sum += v; ++n;
        }
        if (n < 12) return false;
        out = sum / (float)n;
        return true;
    };
    glm::vec3 upper, fore;
    if (!centroid(0.05f, 0.25f, upper)) return false;
    if (!centroid(0.45f, 0.65f, fore)) return false;
    glm::vec3 d = fore - upper;
    if (glm::length(d) < 5.f || d.x < 1.f) return false;
    dir = glm::normalize(d);

    // Arm radius: how far the upper-arm ring's vertices sit off that line.
    // The UPPER arm, because that is the part a sleeve head has to clear.
    double rs = 0.0;
    int rn = 0;
    for (auto& v : verts_) {
        if (v.x < junctionX + 0.05f * armLen || v.x > junctionX + 0.25f * armLen) continue;
        glm::vec3 rel = v - upper;
        rs += glm::length(rel - dir * glm::dot(rel, dir));
        ++rn;
    }
    radiusCm = rn > 0 ? (float)(rs / rn) : 6.f;

    shoulder = upper + dir * ((junctionX - upper.x) / std::max(0.05f, dir.x));
    return true;
}

void Avatar::fitGirthTo(double hipCm, double bustCm) {
    if (verts_.empty()) return;
    double hipNow = girthAt(60.0), bustNow = girthAt(20.0);
    double sHip = (hipNow > 1.0 && hipCm > 1.0) ? hipCm / hipNow : 1.0;
    double sBust = (bustNow > 1.0 && bustCm > 1.0) ? bustCm / bustNow : 1.0;
    // One scale for the whole body: scaling the chest and hip differently
    // would need a real body model, and a uniform girth scale already
    // removes the gross mismatch that was tearing the cloth.
    double s = std::max(0.5, std::min(2.0, (sHip * 2.0 + sBust) / 3.0));
    for (auto& v : verts_) { v.x *= (float)s; v.z *= (float)s; }

    mesh_.clear();
    mesh_.setMode(OF_PRIMITIVE_TRIANGLES);
    for (auto& v : verts_) {
        mesh_.addVertex(v);
        mesh_.addColor(ofFloatColor(0.42f, 0.40f, 0.39f));
    }
    for (size_t t = 0; t + 2 < tris_.size(); t += 3)
        mesh_.addTriangle(tris_[t], tris_[t + 1], tris_[t + 2]);
    addNormals();
    buildGrid();
}

// Smooth vertex normals, so the body is lit as a surface rather than a
// field of flat facets.
void Avatar::addNormals() {
    std::vector<glm::vec3> n(verts_.size(), glm::vec3(0));
    for (size_t t = 0; t + 2 < tris_.size(); t += 3) {
        glm::vec3 f = glm::cross(verts_[tris_[t + 1]] - verts_[tris_[t]],
                                 verts_[tris_[t + 2]] - verts_[tris_[t]]);
        n[tris_[t]] += f; n[tris_[t + 1]] += f; n[tris_[t + 2]] += f;
    }
    mesh_.clearNormals();
    for (auto& q : n) {
        float l = glm::length(q);
        mesh_.addNormal(l > 1e-8f ? q / l : glm::vec3(0, 1, 0));
    }
}

void Avatar::buildGrid() {
    gridMin_ = gridMax_ = verts_[0];
    for (auto& v : verts_) {
        gridMin_ = glm::min(gridMin_, v);
        gridMax_ = glm::max(gridMax_, v);
    }
    gridMin_ -= glm::vec3(1.f);
    gridMax_ += glm::vec3(1.f);

    cell_ = 5.f;   // cm; a few triangles per cell on a body this size
    glm::vec3 span = gridMax_ - gridMin_;
    nx_ = std::max(1, (int)std::ceil(span.x / cell_));
    ny_ = std::max(1, (int)std::ceil(span.y / cell_));
    nz_ = std::max(1, (int)std::ceil(span.z / cell_));
    buckets_.assign((size_t)nx_ * ny_ * nz_, {});

    // A triangle goes in every cell its bounding box touches.
    for (size_t t = 0; t + 2 < tris_.size(); t += 3) {
        glm::vec3 a = verts_[tris_[t]], b = verts_[tris_[t + 1]], c = verts_[tris_[t + 2]];
        glm::vec3 lo = glm::min(a, glm::min(b, c)), hi = glm::max(a, glm::max(b, c));
        int x0 = (int)((lo.x - gridMin_.x) / cell_), x1 = (int)((hi.x - gridMin_.x) / cell_);
        int y0 = (int)((lo.y - gridMin_.y) / cell_), y1 = (int)((hi.y - gridMin_.y) / cell_);
        int z0 = (int)((lo.z - gridMin_.z) / cell_), z1 = (int)((hi.z - gridMin_.z) / cell_);
        for (int z = std::max(0, z0); z <= std::min(nz_ - 1, z1); ++z)
            for (int y = std::max(0, y0); y <= std::min(ny_ - 1, y1); ++y)
                for (int x = std::max(0, x0); x <= std::min(nx_ - 1, x1); ++x)
                    buckets_[((size_t)z * ny_ + y) * nx_ + x].push_back((int)t);
    }
}

int Avatar::cellOf(const glm::vec3& p) const {
    int x = (int)((p.x - gridMin_.x) / cell_);
    int y = (int)((p.y - gridMin_.y) / cell_);
    int z = (int)((p.z - gridMin_.z) / cell_);
    if (x < 0 || y < 0 || z < 0 || x >= nx_ || y >= ny_ || z >= nz_) return -1;
    return ((z * ny_) + y) * nx_ + x;
}

bool Avatar::pushOut(glm::vec3& p, float marginCm) const {
    if (tris_.empty()) return false;

    // Search the cell the point is in and its immediate neighbours, which
    // covers anything within one cell of the surface.
    int cx = (int)((p.x - gridMin_.x) / cell_);
    int cy = (int)((p.y - gridMin_.y) / cell_);
    int cz = (int)((p.z - gridMin_.z) / cell_);
    if (cx < -1 || cy < -1 || cz < -1 || cx > nx_ || cy > ny_ || cz > nz_) return false;

    float bestD2 = 1e18f;
    glm::vec3 bestPoint(0), bestNormal(0, 0, 1);
    bool found = false;

    for (int z = std::max(0, cz - 1); z <= std::min(nz_ - 1, cz + 1); ++z)
    for (int y = std::max(0, cy - 1); y <= std::min(ny_ - 1, cy + 1); ++y)
    for (int x = std::max(0, cx - 1); x <= std::min(nx_ - 1, cx + 1); ++x) {
        for (int t : buckets_[((size_t)z * ny_ + y) * nx_ + x]) {
            const glm::vec3& a = verts_[tris_[t]];
            const glm::vec3& b = verts_[tris_[t + 1]];
            const glm::vec3& c = verts_[tris_[t + 2]];
            glm::vec3 q = closestOnTriangle(p, a, b, c);
            float d2 = glm::dot(p - q, p - q);
            if (d2 >= bestD2) continue;
            bestD2 = d2;
            bestPoint = q;
            glm::vec3 n = glm::cross(b - a, c - a);
            float l = glm::length(n);
            bestNormal = l > 1e-8f ? n / l : glm::vec3(0, 0, 1);
            found = true;
        }
    }
    if (!found) return false;

    glm::vec3 d = p - bestPoint;
    float dist = glm::length(d);
    // Behind the face means inside the body; in front but too close still
    // needs pushing out to leave room for the cloth's thickness.
    float side = glm::dot(d, bestNormal);
    if (side > marginCm) return false;
    p = bestPoint + bestNormal * marginCm;
    (void)dist;
    return true;
}

} } // namespace pf::sim
