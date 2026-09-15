#pragma once
#include "PdfWriter.h"
#include "../garments/GarmentModule.h"

// Home-printer tiling: every full-size board (see FullSizeExport) is cut
// into fixed 18x24cm frames with no overlap, one frame per page, so it
// can be reassembled on a normal printer. A leading calibration page
// carries a 10cm square to check the printer isn't scaling the output.
// This tiles the SAME piece geometry FullSizeExport draws (via
// BoardRenderer::drawBoard, clipped+translated per frame) rather than
// re-importing a rendered PDF, so it needs no PDF-transclusion library.

namespace pf { namespace exportx {

struct TiledPageSize { double wCm, hCm; };
extern const TiledPageSize kA4;
extern const TiledPageSize kUSLetter;

// Returns the number of pages written (including the calibration page).
int writeTiled(pdfx::PdfWriter& writer, const DesignResult& design, TiledPageSize pageSize, const std::string& sheetLabel);

} } // namespace pf::exportx
