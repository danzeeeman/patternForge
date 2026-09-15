#include "BoardRenderer.h"
#include <algorithm>
#include <string>

namespace pf { namespace exportx {

BoardLayout computeBoardLayout(const Piece& piece) {
    Pt lo, hi;
    geo::bounds(piece.cutting, lo, hi);
    double w = hi.x - lo.x, h = hi.y - lo.y;
    BoardLayout layout;
    layout.pageWCm = std::max(28.0, w + 8.0);
    layout.pageHCm = std::max(27.0, h + 16.0);
    layout.offsetXCm = 4.0 - lo.x;
    layout.offsetYCm = 7.0 - lo.y;
    return layout;
}

void drawBoard(pdfx::PdfWriter& w, const Piece& piece, const BoardLayout& layout, const std::string& footerNote) {
    w.setGray(0.0);
    w.text(piece.code + " / " + piece.name, 2.0, 2.6, 14, true);
    w.text("FULL SIZE / cm / generative development draft", 2.0, 3.7, 9);
    w.text(piece.cutQty, 2.0, 4.2, 9);
    w.text(piece.hasSeamAllowance()
               ? "Solid = cut; dashed = sew; dashed + labelled = FOLD, never cut."
               : "Solid = finished cutting edge. NO seam allowance.",
           2.0, 4.7, 9);
    w.text(piece.note, 2.0, 5.2, 9);

    w.save();
    w.translateCm(layout.offsetXCm, layout.offsetYCm);
    w.setLineWidthCm(0.03);
    w.strokePath(piece.cutting, true, false);
    if (piece.hasSeamAllowance()) {
        w.setLineWidthCm(0.02);
        w.strokePath(piece.sewing, true, true);
    }

    // Grainline: a vertical arrow through the piece's center, matching
    // the reference pipeline's placement rule.
    Pt lo, hi;
    geo::bounds(piece.sewing, lo, hi);
    Pt c = geo::centroid(piece.sewing);
    double span = std::min(5.0, std::max(1.0, (double)(hi.y - lo.y) / 4.0));
    w.setGray(0.0);
    w.setLineWidthCm(0.02);
    w.line(c.x, c.y - span, c.x, c.y + span);
    w.strokePath({ Pt(c.x - 0.15f, (float)(c.y - span + 0.4)), Pt(c.x, (float)(c.y - span)), Pt(c.x + 0.15f, (float)(c.y - span + 0.4)) }, false, false);
    w.text("GRAIN", c.x + 0.25, c.y - span + 0.3, 8);

    // Fold lines, before the marks so a notch never hides under one.
    for (auto& f : piece.lines) {
        bool fold = (f.kind == MarkedLine::CutOnFold);
        w.setGray(fold ? 0.0 : 0.35);
        w.setLineWidthCm(fold ? 0.035 : 0.02);
        // A slit is CUT, so it is drawn solid like every other cut line;
        // dashing it would put it in the same visual language as the folds
        // and get it pressed instead of opened.
        w.strokePath({ f.a, f.b }, false, f.kind != MarkedLine::Cut);
        double mx = (f.a.x + f.b.x) / 2.0, my = (f.a.y + f.b.y) / 2.0;
        w.text(f.label, mx + 0.3, my, 8);
    }
    // Closures: a button is a circle with a cross through it, a
    // buttonhole the slot it passes through, a zipper its run.
    for (auto& c : piece.closures) {
        w.setGray(0.0);
        w.setLineWidthCm(0.02);
        double r = std::max(0.25, c.sizeMm / 20.0);
        if (c.kind == Closure::Zipper) {
            w.setLineWidthCm(0.03);
            w.strokePath({ c.a, c.b }, false, true);
            w.line(c.a.x - 0.4, c.a.y, c.a.x + 0.4, c.a.y);
            w.line(c.b.x - 0.4, c.b.y, c.b.x + 0.4, c.b.y);
            w.text(c.label, c.b.x + 0.3, c.b.y, 8);
            continue;
        }
        if (c.kind == Closure::Buttonhole) {
            // A slot, drawn on the axis it is cut on.
            w.line(c.a.x - r, c.a.y, c.a.x + r, c.a.y);
            w.line(c.a.x - r, c.a.y - 0.12, c.a.x + r, c.a.y - 0.12);
            continue;
        }
        w.circleStroke(c.a.x, c.a.y, r);
        w.line(c.a.x - r * 0.5, c.a.y, c.a.x + r * 0.5, c.a.y);
        w.line(c.a.x, c.a.y - r * 0.5, c.a.x, c.a.y + r * 0.5);
    }
    // Name the closures once rather than beside every one of them.
    if (!piece.closures.empty()) {
        int buttons = 0, holes = 0;
        for (auto& c : piece.closures) {
            if (c.kind == Closure::Button) ++buttons;
            if (c.kind == Closure::Buttonhole) ++holes;
        }
        if (buttons || holes) {
            Pt clo, chi; geo::bounds(piece.sewing, clo, chi);
            w.text(std::to_string(buttons) + " buttons / " + std::to_string(holes) +
                   " buttonholes marked", clo.x, chi.y + 1.2, 8);
        }
    }

    w.setGray(0.0);
    w.setLineWidthCm(0.02);

    for (auto& m : piece.marks) {
        // Landmarks name corners so the seam list can point at them; they
        // are not markings to transfer to cloth, and drawing all six per
        // panel would bury the notches that are.
        if (m.kind == Mark::Landmark) continue;
        w.circleStroke(m.pos.x, m.pos.y, 0.11);
        w.text(m.label, m.pos.x + 0.2, m.pos.y - 0.2, 8);
    }
    w.restore();

    w.setGray(0.35);
    w.text(footerNote, 2.0, layout.pageHCm - 1.4, 8);
}

} } // namespace pf::exportx
