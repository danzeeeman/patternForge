#pragma once
#include "PdfWriter.h"
#include "../garments/GarmentModule.h"

// A multi-page instructions manual: title + summary, what this design
// does, a measurements/style table, the cutting list, construction steps,
// the checks table, and a limits/provenance note -- the same section
// structure as the reference packages' V2-Instructions.pdf (built with
// ReportLab there; built with a small hand-rolled text-flow over
// PdfWriter/cairo here, since there's no flowable-text library on this
// stack). `boardCount`/`tiledCounts` are folded into the "print and
// select pieces" section the way the reference manual reports them.
namespace pf { namespace exportx {

struct TiledCounts { int a4Pages, letterPages; };

int writeInstructions(pdfx::PdfWriter& writer, const DesignResult& design, int boardCount, TiledCounts tiled);

} } // namespace pf::exportx
