#pragma once
#include "../garments/GarmentModule.h"
#include <string>

// Writes the pattern as a DXF for CAD, plotters and cutting tables.
//
// Written to the AAMA/ASTM DXF convention that pattern CAD expects --
// CLO3D, Browzwear, Optitex, Gerber -- so each piece is its own BLOCK
// placed by an INSERT, and every line sits on the numbered layer its kind
// belongs to (1 boundary, 4 notches, 6 mirror, 7 grain, 13 annotation,
// 14 sew line). R12 (AC1009) entities, the most widely readable flavour.
// Coordinates are in MILLIMETRES (the pattern works in cm, so everything
// is scaled by 10) and Y is flipped, because pattern space runs downward
// and CAD space runs up.
//
// Written to the documented layer/block convention, but NOT verified
// against CLO3D itself here -- that needs CLO3D in front of it.
//
// A piece cut on the fold is exported as the WHOLE panel (its half united
// with its mirror image), since that is the shape actually cut, with the
// fold line marked on its own layer.

namespace pf { namespace exportx {

struct DxfResult {
    bool ok = false;
    std::string error;
    int pieces = 0;   // pattern pieces (block definitions)
    int panels = 0;   // physical panels placed -- what actually gets cut
    int entities = 0;
};

DxfResult writeDxf(const DesignResult& design, const std::string& path);

} } // namespace pf::exportx
