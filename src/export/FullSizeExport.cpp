#include "FullSizeExport.h"
#include "BoardRenderer.h"

namespace pf { namespace exportx {

int writeFullSize(pdfx::PdfWriter& writer, const DesignResult& design) {
    const std::string footer = "Print at 100%. Sew a toile. Generative development pattern -- not a fitted commercial pattern.";
    int count = 0;
    for (auto& piece : design.pieces) {
        auto layout = computeBoardLayout(piece);
        writer.beginPage(layout.pageWCm, layout.pageHCm);
        drawBoard(writer, piece, layout, footer);
        ++count;
    }
    return count;
}

} } // namespace pf::exportx
