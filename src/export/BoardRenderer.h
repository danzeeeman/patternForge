#pragma once
#include "PdfWriter.h"
#include "../geometry/Piece.h"

// Draws one piece's printable "board": title, cut/allowance notes, the
// cutting line (solid) and sewing line (dashed, if there's a seam
// allowance), a grainline with arrow, registration-mark circles, and a
// footer. This is the one place that draws a board's content, in board-
// local centimeters starting at (0,0) -- FullSizeExport calls it once per
// page; TiledExport calls it once per print frame, wrapped in a translate
// + clip, so a "tile" really is a window onto the same drawing rather
// than a re-render, matching how a print shop actually tiles a page.

namespace pf { namespace exportx {

struct BoardLayout {
    double pageWCm, pageHCm;
    double offsetXCm, offsetYCm; // board-local shift so the piece sits inside the page margins
};

BoardLayout computeBoardLayout(const Piece& piece);
void drawBoard(pdfx::PdfWriter& w, const Piece& piece, const BoardLayout& layout, const std::string& footerNote);

} } // namespace pf::exportx
