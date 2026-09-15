#pragma once
#include "ofMain.h"
#include "../garments/GarmentModule.h"

// Assembles a DesignResult's flat pieces in 3D as a two-layer flat lay:
// every piece keeps its exact pattern outline (no revolving, bulging or
// resampling) and is only moved or rotated as a rigid card. Front pieces
// form the front layer, back pieces the back layer, and sleeves are
// folded along their grainline and joined at the armhole the way a
// garment lies flattened. NOT a cloth simulation: no drape, no body.

namespace pf { namespace render3d {

struct Garment3D {
    ofMesh mesh;              // filled panels, per-vertex colors (front/back/sleeve tones)
    ofMesh outlines;          // each piece's cut outline as line segments
    float worldYTop = 0.f;    // mesh extent, for framing the camera
    float worldYBottom = 0.f;
    float worldXHalf = 0.f;   // largest |x|, so wide pieces (sleeves) stay in frame
};

Garment3D buildGarment3D(const DesignResult& design);

} } // namespace pf::render3d
