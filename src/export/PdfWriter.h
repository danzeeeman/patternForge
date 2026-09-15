#pragma once
#include "ofMain.h"
#include "../geometry/Perimeter.h"
#include <string>
#include <vector>

// Thin sequential-page PDF writer built on openFrameworks' own
// ofCairoRenderer (a core OF class wrapping libcairo -- already a build
// dependency of every OF app, no addon required). All drawing calls take
// centimeters; CM converts to PDF points, matching the reference Python
// pipeline's own `CM = 72/2.54` convention. Cairo's page space is already
// y-down from the top-left, the same convention used for every Ring in
// this app, so no coordinate flip is needed anywhere in the export code.

namespace pf { namespace pdfx {

constexpr double CM = 72.0 / 2.54;

class PdfWriter {
public:
    ~PdfWriter();
    bool open(const std::string& path);
    // Every page, including the first, goes through beginPage(); pages can
    // be any size (the reference pipeline gives every piece its own board
    // size), matching cairo_pdf_surface_set_size's "before any drawing on
    // the new page" contract.
    void beginPage(double wCm, double hCm);
    void close();

    void setLineWidthCm(double w);
    void setGray(double g);
    void setRGB(double r, double g, double b);
    // A closed or open polyline in the current stroke color/width.
    void strokePath(const std::vector<Pt>& ptsCm, bool closed, bool dashed = false);
    void circleStroke(double xCm, double yCm, double radiusCm);
    void rectFill(double xCm, double yCm, double wCm, double hCm);
    void rectStroke(double xCm, double yCm, double wCm, double hCm);
    void line(double x1Cm, double y1Cm, double x2Cm, double y2Cm);
    void text(const std::string& s, double xCm, double yCm, double sizePt, bool bold = false);
    double textWidthCm(const std::string& s, double sizePt, bool bold = false);

    // Cairo's transform/clip stack, exposed so a single "draw this board"
    // routine can be reused unmodified by both the full-size export (draw
    // once, no clip) and the tiled export (draw once per frame, translated
    // and clipped to that frame -- true print tiling, not a re-layout).
    void save();
    void restore();
    void translateCm(double dxCm, double dyCm);
    void clipRectCm(double xCm, double yCm, double wCm, double hCm);

private:
    ofCairoRenderer renderer_;
    cairo_t* cr_ = nullptr;
    bool firstPage_ = true;
    bool open_ = false;
};

} } // namespace pf::pdfx
