#pragma once
#include "../garments/GarmentModule.h"
#include "ofJson.h"

// Builds the {units, revision, design, checks, new_pieces} document that
// matches the field names of the two reference packages'
// V2-Geometry-and-Checks.json (sewing_cm/cutting_cm/cut/note per piece),
// extended with a pass/fail flag per check instead of a plain string,
// since this app surfaces checks live instead of hard-asserting them.

namespace pf { namespace exportx {

ofJson buildGeometryJson(const DesignResult& design);

} } // namespace pf::exportx
