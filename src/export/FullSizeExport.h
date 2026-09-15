#pragma once
#include "PdfWriter.h"
#include "../garments/GarmentModule.h"

namespace pf { namespace exportx {

// One board (page) per piece, at full size -- for a print shop or a
// plotter. Returns the number of boards written.
int writeFullSize(pdfx::PdfWriter& writer, const DesignResult& design);

} } // namespace pf::exportx
