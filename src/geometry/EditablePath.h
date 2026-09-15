#pragma once
#include "Perimeter.h"
#include "ofJson.h"
#include <vector>

// A piece's outline as an editable closed Bezier path: a ring of anchors,
// each with an incoming and an outgoing handle. A generated outline is
// converted into one of these (simplified to a handful of anchors so it
// can actually be grabbed), and sampling it back into a Ring is what the
// rest of the app -- seam allowance, checks, 3D, exports -- consumes. So
// an edited piece stays a first-class piece rather than a special case.

namespace pf {

struct Anchor {
    Pt pos;
    Pt inH, outH;      // handle positions in the same space as pos (cm)
    bool smooth = true; // keep the two handles collinear when one is dragged
};

class EditablePath {
public:
    std::vector<Anchor> anchors;

    // Simplifies `r` to anchors no further than `toleranceCm` from the
    // original outline, then fits smooth handles through them.
    static EditablePath fromRing(const Ring& r, double toleranceCm = 0.35);

    Ring toRing(int samplesPerSegment = 14) const;

    // Splits segment `seg` at parameter t, keeping the curve's shape.
    void insertPoint(int seg, float t);
    void removePoint(int index);          // no-op below 3 anchors
    void moveAnchor(int index, Pt delta);  // handles travel with the anchor
    void moveHandle(int index, bool incoming, Pt to);

    // Closest point on the path to `p`, for adding a point where clicked.
    void closestSegment(Pt p, int& segOut, float& tOut, float& distOut) const;

    // A piece cut on the fold is half a panel: the other half is this one
    // mirrored across x = 0. So nothing may cross the fold, and a point
    // sitting on it has to stay on it -- with a vertical tangent, or the
    // two halves meet at a corner instead of a continuous line.
    void constrainToFold(double snapCm = 0.2);

    // The outline mirrored across x = 0: the panel's other half.
    Ring mirroredRing(int samplesPerSegment = 14) const;

    // Where each anchor sits along the outline, 0..1 by arc length. This
    // is how an edit is remembered: "a third of the way round", not
    // "anchor 7", so it still lands correctly when the sliders change the
    // shape and the anchors are re-derived.
    std::vector<float> anchorParameters() const;
    // The point at parameter t, with handles along the curve's tangent.
    Anchor anchorAtParameter(float t) const;

    ofJson toJson() const;
    static EditablePath fromJson(const ofJson& j);

private:
    int next(int i) const { return (i + 1) % (int)anchors.size(); }
};

} // namespace pf
