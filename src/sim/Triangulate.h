#pragma once
#include "../geometry/Perimeter.h"
#include <vector>

// Turning a flat pattern piece into a triangle mesh fit to simulate.
//
// A pattern piece is a closed outline; cloth simulation needs a mesh of
// reasonably even triangles across its interior, with vertices sitting at
// a known spacing along the boundary so that two pieces sewn together can
// have their boundary vertices matched up one to one.
//
// The boundary spacing is the part that matters most. Seams are made by
// welding boundary vertices of one panel to another's, so both sides have
// to be sampled at the same resolution or there is nothing to weld to.

namespace pf { namespace sim {

struct Mesh2D {
    std::vector<Pt> verts;
    std::vector<int> tris;        // 3 indices per triangle
    std::vector<int> boundary;    // indices into `verts`, in order around the outline
    // Arc-length position of each boundary vertex, 0..1 around the
    // outline. This is what lets a seam find "the vertex 40% along this
    // edge" on both pieces it joins.
    std::vector<float> boundaryT;

    size_t triCount() const { return tris.size() / 3; }
};

// Meshes `outline` with triangles of roughly `resolutionCm` across.
//
// Boundary vertices are placed at even arc-length steps of that size --
// not at the outline's own points, which are spaced however the drafting
// happened to leave them.
Mesh2D triangulate(const Ring& outline, double resolutionCm);

} } // namespace pf::sim
