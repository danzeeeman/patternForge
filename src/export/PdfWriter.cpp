#include "PdfWriter.h"
#include <cairo/cairo-pdf.h>

namespace pf { namespace pdfx {

PdfWriter::~PdfWriter() {
    if (open_) close();
}

bool PdfWriter::open(const std::string& path) {
    // A placeholder page size; the first real beginPage() call resizes it
    // before anything is drawn, which cairo_pdf_surface_set_size allows.
    renderer_.setup(path, ofCairoRenderer::PDF, true, false, ofRectangle(0, 0, 100, 100));
    cr_ = renderer_.getCairoContext();
    firstPage_ = true;
    open_ = true;
    return cr_ != nullptr;
}

void PdfWriter::beginPage(double wCm, double hCm) {
    if (!firstPage_) cairo_show_page(cr_);
    cairo_pdf_surface_set_size(cairo_get_target(cr_), wCm * CM, hCm * CM);
    // ofCairoRenderer::setup() clips the context to whatever placeholder
    // size open() was called with, and that clip outlives a raw
    // cairo_pdf_surface_set_size (they're independent cairo concepts) --
    // without this reset, every page after the first would render only
    // inside that original tiny rectangle.
    cairo_reset_clip(cr_);
    firstPage_ = false;
}

void PdfWriter::close() {
    renderer_.close();
    open_ = false;
    cr_ = nullptr;
}

void PdfWriter::setLineWidthCm(double w) { cairo_set_line_width(cr_, w * CM); }
void PdfWriter::setGray(double g) { cairo_set_source_rgb(cr_, g, g, g); }
void PdfWriter::setRGB(double r, double g, double b) { cairo_set_source_rgb(cr_, r, g, b); }

void PdfWriter::strokePath(const std::vector<Pt>& ptsCm, bool closed, bool dashed) {
    if (ptsCm.empty()) return;
    if (dashed) {
        double dashes[2] = { 4.0, 3.0 };
        cairo_set_dash(cr_, dashes, 2, 0.0);
    } else {
        cairo_set_dash(cr_, nullptr, 0, 0.0);
    }
    cairo_move_to(cr_, ptsCm[0].x * CM, ptsCm[0].y * CM);
    for (size_t i = 1; i < ptsCm.size(); ++i) cairo_line_to(cr_, ptsCm[i].x * CM, ptsCm[i].y * CM);
    if (closed) cairo_close_path(cr_);
    cairo_stroke(cr_);
    cairo_set_dash(cr_, nullptr, 0, 0.0);
}

void PdfWriter::circleStroke(double xCm, double yCm, double radiusCm) {
    cairo_new_sub_path(cr_);
    cairo_arc(cr_, xCm * CM, yCm * CM, radiusCm * CM, 0, 2 * 3.14159265358979323846);
    cairo_stroke(cr_);
}

void PdfWriter::rectFill(double xCm, double yCm, double wCm, double hCm) {
    cairo_rectangle(cr_, xCm * CM, yCm * CM, wCm * CM, hCm * CM);
    cairo_fill(cr_);
}

void PdfWriter::rectStroke(double xCm, double yCm, double wCm, double hCm) {
    cairo_rectangle(cr_, xCm * CM, yCm * CM, wCm * CM, hCm * CM);
    cairo_stroke(cr_);
}

void PdfWriter::line(double x1Cm, double y1Cm, double x2Cm, double y2Cm) {
    cairo_set_dash(cr_, nullptr, 0, 0.0);
    cairo_move_to(cr_, x1Cm * CM, y1Cm * CM);
    cairo_line_to(cr_, x2Cm * CM, y2Cm * CM);
    cairo_stroke(cr_);
}

void PdfWriter::text(const std::string& s, double xCm, double yCm, double sizePt, bool bold) {
    cairo_select_font_face(cr_, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                            bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr_, sizePt);
    cairo_move_to(cr_, xCm * CM, yCm * CM);
    cairo_show_text(cr_, s.c_str());
}

void PdfWriter::save() { cairo_save(cr_); }
void PdfWriter::restore() { cairo_restore(cr_); }
void PdfWriter::translateCm(double dxCm, double dyCm) { cairo_translate(cr_, dxCm * CM, dyCm * CM); }
void PdfWriter::clipRectCm(double xCm, double yCm, double wCm, double hCm) {
    cairo_rectangle(cr_, xCm * CM, yCm * CM, wCm * CM, hCm * CM);
    cairo_clip(cr_);
}

double PdfWriter::textWidthCm(const std::string& s, double sizePt, bool bold) {
    cairo_select_font_face(cr_, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                            bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr_, sizePt);
    cairo_text_extents_t extents;
    cairo_text_extents(cr_, s.c_str(), &extents);
    return extents.x_advance / CM;
}

} } // namespace pf::pdfx
