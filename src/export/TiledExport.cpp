#include "TiledExport.h"
#include "BoardRenderer.h"
#include <cmath>

namespace pf { namespace exportx {

const TiledPageSize kA4       = { 21.0, 29.7 };
const TiledPageSize kUSLetter = { 21.59, 27.94 };

static const double kTileW = 18.0, kTileH = 24.0;

int writeTiled(pdfx::PdfWriter& writer, const DesignResult& design, TiledPageSize pageSize, const std::string& sheetLabel) {
    const std::string footer = "Print at 100%. Sew a toile. Generative development pattern -- not a fitted commercial pattern.";
    int pageCount = 0;

    // Calibration page.
    writer.beginPage(pageSize.wCm, pageSize.hCm);
    ++pageCount;
    writer.setGray(0.0);
    writer.text(sheetLabel + " / PRINT AT 100%", 1.0, 1.6, 14, true);
    writer.text("Print this page first. Disable Fit, Shrink and borderless expansion.", 1.0, 2.3, 9);
    writer.text("Measure the square below: it must be exactly 10 x 10 cm. Assemble each board separately.", 1.0, 2.8, 9);
    writer.setLineWidthCm(0.03);
    writer.rectStroke(1.2, 3.5, 10.0, 10.0);
    writer.text("10 cm x 10 cm", 1.5, 9.0, 11);
    writer.text("Each tile frame is 18 x 24 cm. Trim to the frame and butt the edges.", 1.0, 15.5, 9);
    writer.text("Use board / row / column labels printed above each frame. No overlap is built in.", 1.0, 16.0, 9);
    writer.text("Solid = cutting; dashed = sewing.", 1.0, 16.5, 9);

    double frameX = (pageSize.wCm - kTileW) / 2.0;
    double frameY = 2.2;

    for (size_t bi = 0; bi < design.pieces.size(); ++bi) {
        auto& piece = design.pieces[bi];
        auto layout = computeBoardLayout(piece);
        int nx = (int)std::ceil(layout.pageWCm / kTileW);
        int ny = (int)std::ceil(layout.pageHCm / kTileH);
        for (int row = 0; row < ny; ++row) {
            for (int col = 0; col < nx; ++col) {
                writer.beginPage(pageSize.wCm, pageSize.hCm);
                ++pageCount;
                writer.setGray(0.0);
                writer.text(sheetLabel + " | Board " + std::to_string(bi + 1) + " (" + piece.code + ") | Row "
                                + std::to_string(row + 1) + "/" + std::to_string(ny) + " | Column "
                                + std::to_string(col + 1) + "/" + std::to_string(nx),
                            frameX, frameY - 0.4, 9);

                writer.save();
                writer.clipRectCm(frameX, frameY, kTileW, kTileH);
                writer.translateCm(frameX - col * kTileW, frameY - row * kTileH);
                drawBoard(writer, piece, layout, footer);
                writer.restore();

                writer.setGray(0.5);
                writer.setLineWidthCm(0.02);
                writer.rectStroke(frameX, frameY, kTileW, kTileH);
                writer.text("100% / frame 18 x 24 cm / no overlap", frameX, frameY + kTileH + 0.5, 8);
            }
        }
    }
    return pageCount;
}

} } // namespace pf::exportx
