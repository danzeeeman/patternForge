#include "DxfExport.h"
#include "ofMain.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace pf { namespace exportx {

namespace {

const double kCmToMm = 10.0;

// AAMA/ASTM DXF puts each kind of line on a numbered layer, and pattern
// CAD (CLO3D, Browzwear, Optitex, Gerber) reads those numbers rather than
// any name. The ones this writes:
const char* kLayerBoundary = "1";   // piece boundary -- the cut line
const char* kLayerTurn     = "2";   // turn points
const char* kLayerCurve    = "3";   // curve points
const char* kLayerNotch    = "4";   // notches
const char* kLayerMirror   = "6";   // mirror (fold) line
const char* kLayerGrain    = "7";   // grain line
const char* kLayerInternal = "8";   // internal lines
const char* kLayerDrill    = "11";  // drill holes
const char* kLayerText     = "13";  // annotation
const char* kLayerSew      = "14";  // sew line

struct Writer {
    std::ostringstream out;
    int entities = 0;

    void pair(int code, const std::string& value) { out << code << "\n" << value << "\n"; }
    void pair(int code, double value) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4) << value;
        out << code << "\n" << ss.str() << "\n";
    }
    void pair(int code, int value) { out << code << "\n" << value << "\n"; }

    // Pattern y runs down the piece; CAD y runs up. Flip on the way out.
    double X(double xCm) const { return xCm * kCmToMm; }
    double Y(double yCm) const { return -yCm * kCmToMm; }

    void polyline(const Ring& ring, const char* layer) {
        if (ring.size() < 2) return;
        pair(0, std::string("POLYLINE"));
        pair(8, std::string(layer));
        pair(66, 1);   // vertices follow
        pair(70, 1);   // closed
        pair(10, 0.0); pair(20, 0.0); pair(30, 0.0);
        for (auto& p : ring) {
            pair(0, std::string("VERTEX"));
            pair(8, std::string(layer));
            pair(10, X(p.x));
            pair(20, Y(p.y));
            pair(30, 0.0);
        }
        pair(0, std::string("SEQEND"));
        pair(8, std::string(layer));
        ++entities;
    }

    void line(Pt a, Pt b, const char* layer) {
        pair(0, std::string("LINE"));
        pair(8, std::string(layer));
        pair(10, X(a.x)); pair(20, Y(a.y)); pair(30, 0.0);
        pair(11, X(b.x)); pair(21, Y(b.y)); pair(31, 0.0);
        ++entities;
    }

    void point(Pt p, const char* layer) {
        pair(0, std::string("POINT"));
        pair(8, std::string(layer));
        pair(10, X(p.x)); pair(20, Y(p.y)); pair(30, 0.0);
        ++entities;
    }

    void text(const std::string& s, Pt at, double heightCm, const char* layer) {
        pair(0, std::string("TEXT"));
        pair(8, std::string(layer));
        pair(10, X(at.x)); pair(20, Y(at.y)); pair(30, 0.0);
        pair(40, heightCm * kCmToMm);
        pair(1, s);
        ++entities;
    }
};

// The whole panel for a piece cut on the fold: its half united with its
// mirror image, since that is the shape actually cut -- and what a CAD
// tool expects to receive as the piece boundary.
Ring wholePanel(const Ring& half, bool foldAtCF) {
    if (!foldAtCF || half.size() < 3) return half;
    Ring mirrored = half;
    for (auto& p : mirrored) p.x = -p.x;
    auto joined = geo::unionRings({ half, mirrored });
    if (joined.size() == 1 && joined[0].size() >= 3) return joined[0];
    return half; // union failed: fall back to the half rather than invent a shape
}

Ring mirroredRing(const Ring& r) {
    Ring m = r;
    for (auto& p : m) p.x = -p.x;
    return m;
}

std::string blockName(const DesignResult& design, const Piece& piece) {
    std::string name = design.garmentKey + "_" + piece.code;
    for (auto& c : name) if (!isalnum((unsigned char)c)) c = '_';
    return name;
}

} // namespace

DxfResult writeDxf(const DesignResult& design, const std::string& path) {
    DxfResult result;
    Writer w;

    w.pair(0, std::string("SECTION"));
    w.pair(2, std::string("HEADER"));
    w.pair(9, std::string("$ACADVER"));
    w.pair(1, std::string("AC1009"));
    w.pair(9, std::string("$INSUNITS"));
    w.pair(70, 4); // millimetres
    w.pair(0, std::string("ENDSEC"));

    // Layer table: the numbered AAMA/ASTM layers used below.
    const char* layers[] = { kLayerBoundary, kLayerTurn, kLayerCurve, kLayerNotch,
                             kLayerMirror, kLayerGrain, kLayerInternal, kLayerDrill,
                             kLayerText, kLayerSew };
    w.pair(0, std::string("SECTION"));
    w.pair(2, std::string("TABLES"));
    // Every layer below names CONTINUOUS, and a strict R12 reader expects
    // a linetype it can actually look up.
    w.pair(0, std::string("TABLE"));
    w.pair(2, std::string("LTYPE"));
    w.pair(70, 1);
    w.pair(0, std::string("LTYPE"));
    w.pair(2, std::string("CONTINUOUS"));
    w.pair(70, 0);
    w.pair(3, std::string("Solid line"));
    w.pair(72, 65);
    w.pair(73, 0);
    w.pair(40, 0.0);
    w.pair(0, std::string("ENDTAB"));
    w.pair(0, std::string("TABLE"));
    w.pair(2, std::string("LAYER"));
    w.pair(70, (int)(sizeof(layers) / sizeof(layers[0])));
    int colour = 1;
    for (auto* l : layers) {
        w.pair(0, std::string("LAYER"));
        w.pair(2, std::string(l));
        w.pair(70, 0);
        w.pair(62, colour++);
        w.pair(6, std::string("CONTINUOUS"));
    }
    w.pair(0, std::string("ENDTAB"));
    w.pair(0, std::string("ENDSEC"));

    // Each piece is its own BLOCK, which is how AAMA/ASTM DXF carries a
    // pattern piece and how CLO3D and friends find them.
    w.pair(0, std::string("SECTION"));
    w.pair(2, std::string("BLOCKS"));
    for (auto& piece : design.pieces) {
        std::string name = blockName(design, piece);
        Ring cut = wholePanel(piece.cutting, piece.foldAtCF);
        Ring sew = wholePanel(piece.sewing, piece.foldAtCF);
        Pt lo, hi;
        geo::bounds(cut, lo, hi);

        w.pair(0, std::string("BLOCK"));
        w.pair(8, std::string(kLayerBoundary));
        w.pair(2, name);
        w.pair(70, 0);
        w.pair(10, 0.0); w.pair(20, 0.0); w.pair(30, 0.0);
        w.pair(3, name);
        w.pair(1, std::string(""));

        w.polyline(cut, kLayerBoundary);
        if (piece.hasSeamAllowance()) w.polyline(sew, kLayerSew);
        // Fold lines. The fabric fold a piece is cut on is the MIRROR
        // line in AAMA terms -- it is what tells CAD the panel continues
        // across it -- while a fold made in construction is an internal
        // line. Neither may go on the boundary layer, which is the cut.
        for (auto& f : piece.lines) {
            // A fold piece is exported as the whole mirrored panel, so its
            // cut-on-fold line runs down the middle of what was written.
            // The fabric fold is the MIRROR line in AAMA terms -- it tells
            // CAD the panel continues across it. Everything else here is
            // interior to the piece, never on the boundary layer.
            w.line(f.a, f.b, f.kind == MarkedLine::CutOnFold ? kLayerMirror : kLayerInternal);
            // (a slit is internal too: it is inside the boundary, not part of it)
        }

        Pt centre = geo::centroid(sew);
        double span = std::min(5.0, std::max(1.0, (double)(hi.y - lo.y) / 4.0));
        w.line(Pt(centre.x, (float)(centre.y - span)), Pt(centre.x, (float)(centre.y + span)), kLayerGrain);

        // Each mark on the layer its kind calls for. A roll line is not
        // a notch: put it on 4 and CLO3D would try to match a seam to a
        // point in the middle of the cloth.
        for (auto& m : piece.marks) {
            const char* layer = nullptr;
            switch (m.kind) {
                case Mark::Notch:    layer = kLayerNotch; break;
                case Mark::Internal: layer = kLayerInternal; break;
                case Mark::Drill:    layer = kLayerDrill; break;
                // A landmark names a corner so the seam list can point at
                // it. Nothing is cut or drawn there, so it belongs in no
                // layer -- exporting one would put a phantom notch on the
                // piece for CLO3D to try to match.
                case Mark::Landmark: layer = nullptr; break;
            }
            if (layer) w.point(m.pos, layer);
        }

        // Closures. Pattern CAD places a button as a DRILL HOLE, which is
        // layer 11; a zipper run is an internal line.
        for (auto& c : piece.closures) {
            if (c.kind == Closure::Zipper) w.line(c.a, c.b, kLayerInternal);
            else w.point(c.a, kLayerDrill);
        }

        std::string label = piece.code + " " + piece.name;
        w.text(label, Pt((float)lo.x, (float)(lo.y - 1.5)), 1.0, kLayerText);
        w.text(piece.cutQty + (piece.handEdited ? " / hand-edited" : ""),
               Pt((float)lo.x, (float)(lo.y - 0.2)), 0.7, kLayerText);

        w.pair(0, std::string("ENDBLK"));
        w.pair(8, std::string(kLayerBoundary));
        ++result.pieces;

        // A piece cut as a mirrored pair is TWO physical panels, and the
        // second is not the same shape -- it is the reflection. Its own
        // block carries genuinely mirrored geometry rather than relying on
        // a negative INSERT scale, which not every importer honours.
        if (piece.cutMirrored && piece.cutCount > 1) {
            w.pair(0, std::string("BLOCK"));
            w.pair(8, std::string(kLayerBoundary));
            w.pair(2, name + "_M");
            w.pair(70, 0);
            w.pair(10, 0.0); w.pair(20, 0.0); w.pair(30, 0.0);
            w.pair(3, name + "_M");
            w.pair(1, std::string(""));

            w.polyline(mirroredRing(cut), kLayerBoundary);
            if (piece.hasSeamAllowance()) w.polyline(mirroredRing(sew), kLayerSew);
            for (auto& f : piece.lines)
                w.line(Pt(-f.a.x, f.a.y), Pt(-f.b.x, f.b.y),
                       f.kind == MarkedLine::CutOnFold ? kLayerMirror : kLayerInternal);
            Pt mc = geo::centroid(mirroredRing(sew));
            w.line(Pt(mc.x, (float)(mc.y - span)), Pt(mc.x, (float)(mc.y + span)), kLayerGrain);
            for (auto& m : piece.marks) {
                const char* layer = nullptr;
                switch (m.kind) {
                    case Mark::Notch:    layer = kLayerNotch; break;
                    case Mark::Internal: layer = kLayerInternal; break;
                    case Mark::Drill:    layer = kLayerDrill; break;
                    case Mark::Landmark: layer = nullptr; break;
                }
                if (layer) w.point(Pt(-m.pos.x, m.pos.y), layer);
            }
            for (auto& c : piece.closures) {
                if (c.kind == Closure::Zipper) w.line(Pt(-c.a.x, c.a.y), Pt(-c.b.x, c.b.y), kLayerInternal);
                else w.point(Pt(-c.a.x, c.a.y), kLayerDrill);
            }
            Pt mlo, mhi; geo::bounds(mirroredRing(cut), mlo, mhi);
            w.text(piece.code + " " + piece.name + " (mirrored)",
                   Pt((float)mlo.x, (float)(mlo.y - 1.5)), 1.0, kLayerText);
            w.pair(0, std::string("ENDBLK"));
            w.pair(8, std::string(kLayerBoundary));
        }
    }
    w.pair(0, std::string("ENDSEC"));

    // Pieces are placed left to right, each clear of the last.
    w.pair(0, std::string("SECTION"));
    w.pair(2, std::string("ENTITIES"));
    double trackX = 0.0;
    const double gapCm = 3.0;
    for (auto& piece : design.pieces) {
        Ring cut = wholePanel(piece.cutting, piece.foldAtCF);
        Pt lo, hi;
        geo::bounds(cut, lo, hi);

        // One INSERT per PHYSICAL panel. A file carrying one of a mirrored
        // pair, or one of two sleeves, builds half a garment: the importer
        // has no way to know it was meant to duplicate them.
        int count = std::max(1, piece.cutCount);
        int mirroredCount = piece.cutMirrored ? count / 2 : 0;
        for (int i = 0; i < count; ++i) {
            bool mirrored = (i >= count - mirroredCount);
            Ring placed = mirrored ? mirroredRing(cut) : cut;
            Pt plo, phi;
            geo::bounds(placed, plo, phi);
            double offset = trackX - plo.x;

            w.pair(0, std::string("INSERT"));
            w.pair(8, std::string(kLayerBoundary));
            w.pair(2, blockName(design, piece) + (mirrored ? "_M" : ""));
            w.pair(10, offset * kCmToMm);
            w.pair(20, 0.0);
            w.pair(30, 0.0);
            ++w.entities;
            ++result.panels;

            trackX += (phi.x - plo.x) + gapCm;
        }
    }
    w.pair(0, std::string("ENDSEC"));
    w.pair(0, std::string("EOF"));

    std::ofstream file(path.c_str(), std::ios::binary);
    if (!file) { result.error = "could not open " + path + " for writing"; return result; }
    file << w.out.str();
    file.close();

    result.ok = true;
    result.entities = w.entities;
    return result;
}

} } // namespace pf::exportx
