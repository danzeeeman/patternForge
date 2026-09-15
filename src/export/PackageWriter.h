#pragma once
#include "../garments/GarmentModule.h"
#include "ofJson.h"
#include <string>

// Assembles one design into a folder shaped like the two reference
// packages in this repo (Facet-Coat-US4-Pattern-Package,
// 03-Prism-Cape-Coat-US4-Pattern-Package-2): a full-size print PDF, tiled
// A4/US-Letter PDFs, an instructions manual PDF, a geometry+checks JSON,
// a README, a source/ folder (here: the design's own project JSON, since
// it's drafted rather than remixed from a source PDF), and a zip of the
// whole thing. File names drop the reference packages' "V2-" prefix --
// that names a specific *revision* of an existing pattern, which doesn't
// apply to a design generated fresh by this tool.

namespace pf { namespace exportx {

struct PackageResult {
    bool ok = false;
    std::string error;
    std::string stem;        // the stamped folder name, without the path
    std::string folderPath;
    std::string zipPath;
    int fullSizeBoards = 0;
    int a4Pages = 0, letterPages = 0;
    int instructionPages = 0;
    int dxfEntities = 0;
    int dxfPanels = 0;
    // The fitting muslin, written alongside the pattern it tests.
    int muslinPieces = 0;
    int muslinBoards = 0;
    int muslinA4Pages = 0, muslinLetterPages = 0;
};

// `outlineEdits` carries any hand-edited outlines so the exported
// project can be reopened with those edits intact.
//
// `stamp` is appended to the folder and zip name -- "20260913-0512" --
// so an export never lands on top of an earlier one. Exporting is how you
// keep a version: overwriting silently would throw away the draft you were
// comparing against, and there is no undo for a folder. Pass an empty
// stamp only when you deliberately want the bare name.
PackageResult writePackage(const DesignResult& design, const std::string& outputRootDir,
                           const ofJson& outlineEdits = ofJson::object(),
                           const std::string& stamp = "");

} } // namespace pf::exportx
