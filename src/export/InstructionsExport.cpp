#include "InstructionsExport.h"
#include <sstream>
#include <iomanip>

namespace pf { namespace exportx {

namespace {

// A minimal text-flow over PdfWriter: word-wraps paragraphs to a column
// width and starts a new page automatically when content would overflow
// the bottom margin. Stands in for ReportLab's Paragraph/Table flowables,
// which have no equivalent in this stack.
class Flow {
public:
    Flow(pdfx::PdfWriter& w, double pageWCm, double pageHCm, double marginCm)
        : w_(w), pageW_(pageWCm), pageH_(pageHCm), margin_(marginCm) { newPage(); }

    void heading(const std::string& text) {
        ensureSpace(1.3);
        w_.text(text, margin_, y_, 20, true);
        y_ += 1.0;
    }
    void subheading(const std::string& text) {
        ensureSpace(0.9);
        w_.text(text, margin_, y_, 13, true);
        y_ += 0.65;
    }
    void paragraph(const std::string& text) {
        auto lines = wrap(text, 10, pageW_ - 2 * margin_, false);
        for (auto& line : lines) {
            ensureSpace(0.45);
            w_.text(line, margin_, y_, 10, false);
            y_ += 0.42;
        }
        y_ += 0.22;
    }
    // rows[0] is treated as the header (bold, ruled underneath).
    void table(const std::vector<std::vector<std::string>>& rows, const std::vector<double>& colWidthsCm) {
        for (size_t ri = 0; ri < rows.size(); ++ri) {
            ensureSpace(0.5);
            double x = margin_;
            for (size_t ci = 0; ci < rows[ri].size() && ci < colWidthsCm.size(); ++ci) {
                w_.text(rows[ri][ci], x, y_, 9.5, ri == 0);
                x += colWidthsCm[ci];
            }
            y_ += 0.42;
            if (ri == 0) {
                w_.setGray(0.0);
                w_.setLineWidthCm(0.02);
                w_.line(margin_, y_ - 0.12, pageW_ - margin_, y_ - 0.12);
                y_ += 0.12;
            }
        }
        y_ += 0.25;
    }
    void pageBreak() { newPage(); }
    int pageCount() const { return pageCount_; }

private:
    void newPage() {
        w_.beginPage(pageW_, pageH_);
        ++pageCount_;
        y_ = margin_ + 0.6;
    }
    void ensureSpace(double neededCm) {
        if (y_ + neededCm > pageH_ - margin_) newPage();
    }
    std::vector<std::string> wrap(const std::string& text, double sizePt, double maxWidthCm, bool bold) {
        std::vector<std::string> lines;
        std::istringstream iss(text);
        std::string word, line;
        while (iss >> word) {
            std::string candidate = line.empty() ? word : line + " " + word;
            if (w_.textWidthCm(candidate, sizePt, bold) > maxWidthCm && !line.empty()) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
        }
        if (!line.empty()) lines.push_back(line);
        if (lines.empty()) lines.push_back("");
        return lines;
    }

    pdfx::PdfWriter& w_;
    double pageW_, pageH_, margin_, y_ = 0;
    int pageCount_ = 0;
};

std::string fmt(double v, int prec = 1) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(prec) << v;
    return ss.str();
}

} // namespace

int writeInstructions(pdfx::PdfWriter& writer, const DesignResult& design, int boardCount, TiledCounts tiled) {
    const double pageW = 21.0, pageH = 29.7, margin = 2.0;
    Flow flow(writer, pageW, pageH, margin);

    flow.heading(design.title);
    flow.paragraph(design.garmentKey + " / size preset " + design.size.name +
                    " / generative development draft -- not a fitted commercial pattern.");

    flow.subheading("What this design does");
    for (auto& p : design.whatChanged) flow.paragraph(p);

    flow.subheading("Measurements & style");
    {
        std::vector<std::vector<std::string>> rows = { { "Measurement", "Value (cm)" } };
        for (auto& kv : design.size.cm) rows.push_back({ kv.first, fmt(kv.second) });
        for (auto& kv : design.style.values) rows.push_back({ "style: " + kv.first, fmt(kv.second) });
        flow.table(rows, { 9.0, 7.0 });
    }

    flow.pageBreak();
    flow.subheading("Print and select pieces");
    flow.paragraph("Use this package as a complete set. Choose Full-Size for a print shop or plotter, or "
                    "A4 / US-Letter for home printing. Print at 100% -- check the 10cm calibration square "
                    "on the first tiled page before assembling.");
    flow.paragraph("Full-size: " + std::to_string(boardCount) + " boards. A4: " + std::to_string(tiled.a4Pages) +
                    " pages. US Letter: " + std::to_string(tiled.letterPages) +
                    " pages. Tile frames are 18 x 24 cm; align edge to edge with no overlap.");

    flow.subheading("Cutting list");
    {
        std::vector<std::vector<std::string>> rows = { { "Code", "Cut / material; piece" } };
        for (auto& row : design.cuttingList) rows.push_back({ row.first, row.second });
        flow.table(rows, { 3.0, 14.0 });
    }

    flow.pageBreak();
    flow.subheading("Construction");
    for (auto& step : design.constructionSteps) flow.paragraph(step);

    flow.subheading("Checks before final cloth");
    flow.paragraph("These checks verify flat sewing relationships only -- not body fit, fabric drape, "
                    "seam bulk or comfort. Make a full toile before using final cloth.");
    {
        std::vector<std::vector<std::string>> rows = { { "Check", "Result" } };
        for (auto& c : design.checks.results()) rows.push_back({ c.name, (c.pass ? "PASS -- " : "FAIL -- ") + c.detail });
        flow.table(rows, { 8.5, 8.5 });
    }

    flow.subheading("Limits and provenance");
    flow.paragraph("This is a generative development draft produced by an openFrameworks pattern-design "
                    "tool from a body-measurement block, not a digitized commercial pattern. It is not "
                    "graded, fully lined, physically fitted or cloth-simulated. Sew a toile in a fabric "
                    "of comparable weight before cutting final cloth.");

    return flow.pageCount();
}

} } // namespace pf::exportx
