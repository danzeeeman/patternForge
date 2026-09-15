#pragma once
#include <string>

// The CLO3D build script shipped inside every exported package.
//
// A DXF gets flat pieces into CLO and nothing joined -- CLO cannot infer
// which edge sews to which. This script reads the package's own seam list
// and does the assembly: one pattern per piece, mirrored where the cutting
// list says so, arranged around the avatar, sewn edge to edge, simulated.
//
// It lives in the package rather than in this repo so the folder (and the
// zip) stays self-contained: hand someone the package and they have the
// pattern, the CAD file and the thing that builds it.

namespace pf { namespace exportx {

// The script's source text, written to <package>/clo3d_build.py.
const std::string& clo3dBuildScript();

} } // namespace pf::exportx
