#pragma once
#include "ofMain.h"
#include <vector>

// Perimetric design core: every pattern piece is nothing but a closed
// perimeter curve (a "Ring") in centimeters. Everything else -- seam
// allowances, hem flare, sleeve taper, matching-seam checks -- is an
// operation performed ON that perimeter: densify it, offset it outward
// or inward, or boolean it against another piece's perimeter. This file
// is the only place that touches Clipper (vendored in thirdparty/clipper,
// Boost-licensed) so the rest of the app only ever sees plain point lists.
//
// A Ring is an open polyline (first point is NOT repeated at the end)
// describing a simple, closed polygon, wound in either direction.

namespace pf {

using Pt   = glm::vec2;          // a point, centimeters
using Ring = std::vector<Pt>;    // a closed perimeter, centimeters

namespace geo {

// Signed area (shoelace). Positive for counter-clockwise winding.
double area(const Ring& r);

// Length of the closed perimeter (sums the wrap-around edge too).
double perimeterLength(const Ring& r);

// Resample the perimeter so no edge is longer than stepCm. Mirrors the
// Python pipeline's dense(): later transforms (widen, taper) are applied
// per-sample rather than per-vertex, so they read as smooth curves.
Ring densify(const Ring& r, double stepCm = 0.15);

// Offset the whole perimeter outward (deltaCm > 0) or inward (< 0) with
// mitered corners -- the seam-allowance "buffer" from the Python pipeline.
// Returns the single largest-area result ring (the common case for a
// simple, non-self-intersecting piece).
Ring offset(const Ring& r, double deltaCm);

// Same as offset(), but returns every resulting ring (an inward offset of
// a concave shape can split into more than one piece).
std::vector<Ring> offsetAll(const Ring& r, double deltaCm);

std::vector<Ring> unionRings(const std::vector<Ring>& rs);
std::vector<Ring> intersectRings(const Ring& a, const Ring& b);

// Area of the symmetric difference between two rings -- "how much do
// these two outlines disagree" -- used to check that two pieces meant to
// tile exactly (e.g. a front panel split in two) really do.
double symmetricDifferenceArea(const Ring& a, const Ring& b);

// Clip `r` to a half-plane / box, used to carve hem facings etc. out of a
// body piece (keeps only points with y in [yLo, yHi], x in [xLo, xHi]).
std::vector<Ring> clipToBox(const Ring& r, double xLo, double yLo, double xHi, double yHi);

Pt  centroid(const Ring& r);
void bounds(const Ring& r, Pt& lo, Pt& hi);

// Horizontal ray scan at height y: the largest/smallest x among every
// point where the ring's boundary crosses that height. For a half-panel
// with its fold at x=0 (a torso front/back), maxXAtY is exactly that
// height's half-width -- what Pattern3D revolves into a body tube.
double maxXAtY(const Ring& r, double y);
double minXAtY(const Ring& r, double y);

// The vertical twin: a ray scan at x, giving the outline's top (min y) and
// bottom (max y) edge there. A torso panel's neckline is its top edge, so
// this is how a facing works out how deep the neckline runs across it.
double minYAtX(const Ring& r, double x);
double maxYAtX(const Ring& r, double x);

// True when the point lies inside the outline. Used to check that a
// closure -- a button, a zipper run -- actually sits on the piece: a
// button marked off the edge of the cloth cannot be sewn.
bool contains(const Ring& r, Pt p);

// True when the outline crosses itself -- a shape that cannot be cut or
// sewn. Worth checking on any hand-edited outline.
bool selfIntersects(const Ring& r);

// Shift so the bounding box's minimum corner sits at the origin.
Ring normalizeToOrigin(const Ring& r);
Ring translate(const Ring& r, Pt by);

// Chaikin corner-cutting on an OPEN polyline: softens interior corners
// into a curve while leaving the first and last points exactly fixed --
// used to turn a hand-placed waist/hip/hem side-seam chain into a smooth
// line without disturbing the seams it joins at either end.
std::vector<Pt> chaikinOpen(const std::vector<Pt>& pts, int iterations = 2);

} // namespace geo
} // namespace pf
