#pragma once
#include "EditablePath.h"
#include <vector>

// A piece's hand edits, stored so the style sliders keep working.
//
// An edit is NOT a replacement outline. Each edited point is remembered by
// WHERE it sits along the generated outline (0..1 by arc length) plus how
// far it was moved from there. So when a slider regenerates the piece, the
// edits are re-applied on top of the new shape: lengthen a dress and a
// point pulled out at the hip is still pulled out at the hip.

namespace pf {

struct PointEdit {
    float t = 0.f;              // where along the generated outline
    Pt posOffset{ 0.f, 0.f };    // how far it was moved from there, cm
    bool handlesSet = false;     // its curve handles were shaped by hand
    Pt inRel{ 0.f, 0.f };        // handle positions relative to the point
    Pt outRel{ 0.f, 0.f };
    bool added = false;          // a point the designer added, not a generated one
};

struct OutlineEdits {
    std::vector<PointEdit> points; // aligned with the live path's anchors
    std::vector<float> removedT;   // generated points the designer deleted

    bool empty() const { return points.empty() && removedT.empty(); }
    bool anyEdit() const;

    // Rebuilds the live path from a freshly generated outline, re-applying
    // every edit. Also re-aligns `points` with the resulting anchors.
    EditablePath applyTo(const EditablePath& base);

    ofJson toJson() const;
    static OutlineEdits fromJson(const ofJson& j);
};

} // namespace pf
