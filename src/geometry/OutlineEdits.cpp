#include "OutlineEdits.h"
#include <algorithm>
#include <cmath>

namespace pf {

namespace {
// Distance between two positions around a closed outline.
float circularDistance(float a, float b) {
    float d = std::fabs(a - b);
    return std::min(d, 1.f - d);
}
const float kMatchTolerance = 0.06f; // how far an edit may drift and still match its point
} // namespace

bool OutlineEdits::anyEdit() const {
    if (!removedT.empty()) return true;
    for (auto& p : points) {
        if (p.added || p.handlesSet) return true;
        if (glm::length(glm::vec2(p.posOffset)) > 1e-4f) return true;
    }
    return false;
}

EditablePath OutlineEdits::applyTo(const EditablePath& base) {
    struct Item { float t; Anchor anchor; PointEdit edit; };
    std::vector<Item> items;
    auto baseT = base.anchorParameters();
    std::vector<bool> used(points.size(), false);

    // Every generated point, unless the designer deleted it, carrying its
    // edit if it has one.
    for (size_t i = 0; i < base.anchors.size(); ++i) {
        float t = baseT[i];
        bool removed = false;
        for (float rt : removedT) if (circularDistance(rt, t) < kMatchTolerance) { removed = true; break; }
        if (removed) continue;

        int best = -1;
        float bestDist = kMatchTolerance;
        for (size_t k = 0; k < points.size(); ++k) {
            if (used[k] || points[k].added) continue;
            float d = circularDistance(points[k].t, t);
            if (d < bestDist) { bestDist = d; best = (int)k; }
        }
        PointEdit e;
        e.t = t;
        if (best >= 0) { e = points[best]; e.t = t; used[best] = true; }
        items.push_back({ t, base.anchors[i], e });
    }

    // Points the designer added sit at their own place along the outline.
    for (size_t k = 0; k < points.size(); ++k) {
        if (!points[k].added) continue;
        items.push_back({ points[k].t, base.anchorAtParameter(points[k].t), points[k] });
    }

    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.t < b.t; });

    EditablePath live;
    std::vector<PointEdit> realigned;
    live.anchors.reserve(items.size());
    realigned.reserve(items.size());
    for (auto& item : items) {
        Anchor a = item.anchor;
        a.pos += item.edit.posOffset;
        if (item.edit.handlesSet) {
            a.inH = a.pos + item.edit.inRel;
            a.outH = a.pos + item.edit.outRel;
        } else {
            a.inH += item.edit.posOffset;   // handles travel with the point
            a.outH += item.edit.posOffset;
        }
        live.anchors.push_back(a);
        realigned.push_back(item.edit);
    }
    points = realigned;
    return live;
}

ofJson OutlineEdits::toJson() const {
    ofJson j;
    ofJson pts = ofJson::array();
    for (auto& p : points) {
        pts.push_back({ {"t", p.t},
                        {"dx", p.posOffset.x}, {"dy", p.posOffset.y},
                        {"handles", p.handlesSet},
                        {"ix", p.inRel.x}, {"iy", p.inRel.y},
                        {"ox", p.outRel.x}, {"oy", p.outRel.y},
                        {"added", p.added} });
    }
    j["points"] = pts;
    j["removed"] = removedT;
    return j;
}

OutlineEdits OutlineEdits::fromJson(const ofJson& j) {
    OutlineEdits e;
    if (!j.is_object()) return e;
    if (j.contains("points") && j["points"].is_array()) {
        for (auto& p : j["points"]) {
            PointEdit pe;
            pe.t = p.value("t", 0.f);
            pe.posOffset = Pt(p.value("dx", 0.f), p.value("dy", 0.f));
            pe.handlesSet = p.value("handles", false);
            pe.inRel = Pt(p.value("ix", 0.f), p.value("iy", 0.f));
            pe.outRel = Pt(p.value("ox", 0.f), p.value("oy", 0.f));
            pe.added = p.value("added", false);
            e.points.push_back(pe);
        }
    }
    if (j.contains("removed") && j["removed"].is_array())
        for (auto& t : j["removed"]) e.removedT.push_back(t.get<float>());
    return e;
}

} // namespace pf
