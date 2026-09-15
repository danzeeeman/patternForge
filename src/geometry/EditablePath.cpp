#include "EditablePath.h"
#include <algorithm>
#include <cmath>

namespace pf {

namespace {

Pt lerpPt(Pt a, Pt b, float t) { return a + (b - a) * t; }

double pointSegDistance(Pt p, Pt a, Pt b) {
    glm::vec2 ab = b - a;
    double len2 = glm::dot(ab, ab);
    if (len2 < 1e-12) return glm::distance(p, a);
    double t = std::max(0.0, std::min(1.0, (double)glm::dot(p - a, ab) / len2));
    return glm::distance(p, a + ab * (float)t);
}

// Douglas-Peucker on an open polyline: keeps the points that carry the
// shape and drops the ones a straight line already covers.
void simplifyInto(const std::vector<Pt>& pts, int first, int last, double tol, std::vector<int>& keep) {
    if (last <= first + 1) return;
    double worst = 0.0;
    int worstIdx = -1;
    for (int i = first + 1; i < last; ++i) {
        double d = pointSegDistance(pts[i], pts[first], pts[last]);
        if (d > worst) { worst = d; worstIdx = i; }
    }
    if (worst > tol && worstIdx > 0) {
        simplifyInto(pts, first, worstIdx, tol, keep);
        keep.push_back(worstIdx);
        simplifyInto(pts, worstIdx, last, tol, keep);
    }
}

} // namespace

EditablePath EditablePath::fromRing(const Ring& r, double toleranceCm) {
    EditablePath path;
    if (r.size() < 3) return path;

    std::vector<Pt> pts(r.begin(), r.end());
    pts.push_back(r[0]); // close it so the wrap-around segment is simplified too
    std::vector<int> keep;
    keep.push_back(0);
    simplifyInto(pts, 0, (int)pts.size() - 1, toleranceCm, keep);
    std::sort(keep.begin(), keep.end());
    keep.erase(std::unique(keep.begin(), keep.end()), keep.end());

    for (int idx : keep) {
        if (idx >= (int)r.size()) continue;
        Anchor a;
        a.pos = r[idx];
        path.anchors.push_back(a);
    }
    if (path.anchors.size() < 3) { // degenerate: fall back to the raw ring
        path.anchors.clear();
        for (auto& p : r) { Anchor a; a.pos = p; path.anchors.push_back(a); }
    }

    // Catmull-Rom handles: each anchor's handles follow the direction
    // between its neighbours, which reproduces a smooth curve through the
    // simplified points.
    //
    // They start SHORT on purpose. A full-length Catmull-Rom tangent
    // (a sixth of the span between the neighbours) makes every point the
    // far end of a long lever: drag one anchor and the curve sweeps out
    // on both sides of it, so moving a single waist point appears to
    // reshape the whole seam. Short handles keep the curve close to the
    // straight line between anchors, so an edit reads as local -- which is
    // what "move this one point" is supposed to mean. Anyone who wants a
    // sweeping curve can drag the handles out; nobody wants one by
    // surprise.
    const float kHandleFraction = 0.22f;   // of the Catmull-Rom tangent
    // Past this much of a turn, the point is a CORNER and not a smooth
    // point on a curve.
    const float kCornerTurnDeg = 32.f;

    int n = (int)path.anchors.size();
    for (int i = 0; i < n; ++i) {
        Pt prev = path.anchors[(i - 1 + n) % n].pos;
        Pt cur  = path.anchors[i].pos;
        Pt next = path.anchors[(i + 1) % n].pos;

        // A pattern piece is mostly corners: the hem meets the fold at a
        // right angle, the shoulder meets the neckline, the hem meets the
        // side seam. Giving a corner a smooth tangent aims its handles
        // along the average of two edges that are nearly perpendicular,
        // which BOWS both of them -- and a center-front fold edge that
        // bows is simply wrong, since that edge is a straight fold. So a
        // corner gets no handles and stays sharp; only genuinely smooth
        // points get a tangent.
        glm::vec2 in(cur.x - prev.x, cur.y - prev.y);
        glm::vec2 out(next.x - cur.x, next.y - cur.y);
        float li = glm::length(in), lo = glm::length(out);
        bool corner = false;
        if (li > 1e-5f && lo > 1e-5f) {
            float cosTurn = glm::dot(in / li, out / lo);
            cosTurn = std::max(-1.f, std::min(1.f, cosTurn));
            corner = (std::acos(cosTurn) * 180.f / 3.14159265f) > kCornerTurnDeg;
        }

        path.anchors[i].smooth = !corner;
        if (corner) {
            path.anchors[i].inH = cur;
            path.anchors[i].outH = cur;
            continue;
        }
        Pt tangent = (next - prev) / 6.f * kHandleFraction;
        path.anchors[i].outH = cur + tangent;
        path.anchors[i].inH = cur - tangent;
    }
    return path;
}

Ring EditablePath::toRing(int samplesPerSegment) const {
    Ring out;
    int n = (int)anchors.size();
    if (n < 2) return out;
    samplesPerSegment = std::max(2, samplesPerSegment);
    for (int i = 0; i < n; ++i) {
        const Anchor& a = anchors[i];
        const Anchor& b = anchors[next(i)];
        for (int s = 0; s < samplesPerSegment; ++s) {
            float t = (float)s / (float)samplesPerSegment;
            float u = 1.f - t;
            Pt p = a.pos * (u * u * u)
                 + a.outH * (3.f * u * u * t)
                 + b.inH * (3.f * u * t * t)
                 + b.pos * (t * t * t);
            out.push_back(p);
        }
    }
    return out;
}

void EditablePath::insertPoint(int seg, float t) {
    int n = (int)anchors.size();
    if (n < 2 || seg < 0 || seg >= n) return;
    t = std::max(0.01f, std::min(0.99f, t));
    Anchor& a = anchors[seg];
    Anchor& b = anchors[next(seg)];
    // De Casteljau: split the cubic so the curve's shape is unchanged.
    Pt q0 = lerpPt(a.pos, a.outH, t);
    Pt q1 = lerpPt(a.outH, b.inH, t);
    Pt q2 = lerpPt(b.inH, b.pos, t);
    Pt r0 = lerpPt(q0, q1, t);
    Pt r1 = lerpPt(q1, q2, t);
    Pt s = lerpPt(r0, r1, t);

    Anchor mid;
    mid.pos = s; mid.inH = r0; mid.outH = r1; mid.smooth = true;
    a.outH = q0;
    b.inH = q2;
    anchors.insert(anchors.begin() + seg + 1, mid);
}

void EditablePath::removePoint(int index) {
    if ((int)anchors.size() <= 3) return; // a closed path needs three
    if (index < 0 || index >= (int)anchors.size()) return;
    anchors.erase(anchors.begin() + index);
}

void EditablePath::moveAnchor(int index, Pt delta) {
    if (index < 0 || index >= (int)anchors.size()) return;
    Anchor& a = anchors[index];
    a.pos += delta;
    a.inH += delta;
    a.outH += delta;
}

void EditablePath::moveHandle(int index, bool incoming, Pt to) {
    if (index < 0 || index >= (int)anchors.size()) return;
    Anchor& a = anchors[index];
    (incoming ? a.inH : a.outH) = to;
    if (!a.smooth) return;
    // Keep the opposite handle collinear, preserving its own length.
    Pt moved = incoming ? a.inH : a.outH;
    Pt& other = incoming ? a.outH : a.inH;
    glm::vec2 away = a.pos - moved;
    float len = glm::distance(other, a.pos);
    if (glm::length(away) > 1e-5f) other = a.pos + glm::normalize(away) * len;
}

void EditablePath::closestSegment(Pt p, int& segOut, float& tOut, float& distOut) const {
    segOut = -1; tOut = 0.f; distOut = 1e18f;
    int n = (int)anchors.size();
    if (n < 2) return;
    const int kSamples = 24;
    for (int i = 0; i < n; ++i) {
        const Anchor& a = anchors[i];
        const Anchor& b = anchors[next(i)];
        for (int s = 0; s <= kSamples; ++s) {
            float t = (float)s / (float)kSamples;
            float u = 1.f - t;
            Pt q = a.pos * (u * u * u) + a.outH * (3.f * u * u * t)
                 + b.inH * (3.f * u * t * t) + b.pos * (t * t * t);
            float d = glm::distance(q, p);
            if (d < distOut) { distOut = d; segOut = i; tOut = t; }
        }
    }
}

namespace {
// Length of one cubic segment, sampled.
float segmentLength(const Anchor& a, const Anchor& b) {
    const int kS = 16;
    float len = 0.f;
    Pt prev = a.pos;
    for (int s = 1; s <= kS; ++s) {
        float t = (float)s / kS, u = 1.f - t;
        Pt p = a.pos * (u*u*u) + a.outH * (3.f*u*u*t) + b.inH * (3.f*u*t*t) + b.pos * (t*t*t);
        len += glm::distance(prev, p);
        prev = p;
    }
    return len;
}
} // namespace

std::vector<float> EditablePath::anchorParameters() const {
    int n = (int)anchors.size();
    std::vector<float> params(std::max(0, n), 0.f);
    if (n < 2) return params;
    std::vector<float> segLen(n, 0.f);
    float total = 0.f;
    for (int i = 0; i < n; ++i) { segLen[i] = segmentLength(anchors[i], anchors[next(i)]); total += segLen[i]; }
    if (total < 1e-6f) return params;
    float acc = 0.f;
    for (int i = 0; i < n; ++i) { params[i] = acc / total; acc += segLen[i]; }
    return params;
}

Anchor EditablePath::anchorAtParameter(float t) const {
    Anchor out;
    int n = (int)anchors.size();
    if (n == 0) return out;
    if (n == 1) return anchors[0];
    t = t - std::floor(t);
    std::vector<float> segLen(n, 0.f);
    float total = 0.f;
    for (int i = 0; i < n; ++i) { segLen[i] = segmentLength(anchors[i], anchors[next(i)]); total += segLen[i]; }
    float target = t * total, acc = 0.f;
    int seg = n - 1; float local = 1.f;
    for (int i = 0; i < n; ++i) {
        if (target <= acc + segLen[i] || i == n - 1) {
            seg = i;
            local = segLen[i] > 1e-6f ? (target - acc) / segLen[i] : 0.f;
            break;
        }
        acc += segLen[i];
    }
    local = std::max(0.f, std::min(1.f, local));
    const Anchor& a = anchors[seg];
    const Anchor& b = anchors[next(seg)];
    float u = 1.f - local;
    out.pos = a.pos * (u*u*u) + a.outH * (3.f*u*u*local) + b.inH * (3.f*u*local*local) + b.pos * (local*local*local);
    // Tangent of the cubic, scaled to a third of the segment -- the same
    // proportion the generated handles use.
    Pt d = (a.outH - a.pos) * (3.f*u*u) + (b.inH - a.outH) * (6.f*u*local) + (b.pos - b.inH) * (3.f*local*local);
    float dl = glm::length(d);
    Pt dir = dl > 1e-5f ? d / dl : Pt(1.f, 0.f);
    float reach = segLen[seg] / 3.f;
    out.outH = out.pos + dir * reach;
    out.inH = out.pos - dir * reach;
    return out;
}

void EditablePath::constrainToFold(double snapCm) {
    for (auto& a : anchors) {
        if (a.pos.x < snapCm) {
            // On (or pushed past) the fold: pin it to the fold and stand
            // its handles vertical, so the mirrored half continues the
            // line smoothly instead of meeting it at a corner.
            a.pos.x = 0.f;
            a.inH.x = 0.f;
            a.outH.x = 0.f;
        }
        // Nothing else may cross the fold either.
        a.inH.x = std::max(0.f, a.inH.x);
        a.outH.x = std::max(0.f, a.outH.x);
    }
}

Ring EditablePath::mirroredRing(int samplesPerSegment) const {
    Ring r = toRing(samplesPerSegment);
    for (auto& p : r) p.x = -p.x;
    return r;
}

ofJson EditablePath::toJson() const {
    ofJson arr = ofJson::array();
    for (auto& a : anchors) {
        arr.push_back({ {"x", a.pos.x}, {"y", a.pos.y},
                        {"ix", a.inH.x}, {"iy", a.inH.y},
                        {"ox", a.outH.x}, {"oy", a.outH.y},
                        {"smooth", a.smooth} });
    }
    return arr;
}

EditablePath EditablePath::fromJson(const ofJson& j) {
    EditablePath path;
    if (!j.is_array()) return path;
    for (auto& e : j) {
        Anchor a;
        a.pos = Pt(e.value("x", 0.f), e.value("y", 0.f));
        a.inH = Pt(e.value("ix", 0.f), e.value("iy", 0.f));
        a.outH = Pt(e.value("ox", 0.f), e.value("oy", 0.f));
        a.smooth = e.value("smooth", true);
        path.anchors.push_back(a);
    }
    return path;
}

} // namespace pf
