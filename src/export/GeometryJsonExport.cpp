#include "GeometryJsonExport.h"

namespace pf { namespace exportx {

static ofJson ringToJson(const Ring& r) {
    ofJson arr = ofJson::array();
    for (auto& p : r) arr.push_back({ p.x, p.y });
    return arr;
}

ofJson buildGeometryJson(const DesignResult& design) {
    ofJson j;
    j["units"] = "cm";
    j["revision"] = 1;

    ofJson designJ;
    designJ["key"] = design.garmentKey;
    designJ["title"] = design.title;
    designJ["sizePreset"] = design.size.name;
    ofJson measurements;
    for (auto& kv : design.size.cm) measurements[kv.first] = kv.second;
    designJ["measurementsCm"] = measurements;
    ofJson styleJ;
    for (auto& kv : design.style.values) styleJ[kv.first] = kv.second;
    designJ["style"] = styleJ;
    j["design"] = designJ;

    ofJson checksJ;
    for (auto& c : design.checks.results()) {
        checksJ[c.name] = { {"detail", c.detail}, {"pass", c.pass} };
    }
    j["checks"] = checksJ;
    j["allChecksPass"] = design.checks.allPass();

    ofJson pieces = ofJson::array();
    for (auto& piece : design.pieces) {
        ofJson pj;
        pj["code"] = piece.code;
        pj["name"] = piece.name;
        pj["sewing_cm"] = ringToJson(piece.sewing);
        pj["cutting_cm"] = ringToJson(piece.cutting);
        pj["cut"] = piece.cutQty;
        pj["note"] = piece.note;
        pj["handEdited"] = piece.handEdited;
        // Without this, the coordinates above are ambiguous: a piece cut on
        // the fold stores only half the panel, so anything measuring it has
        // to know to mirror before it compares areas or widths.
        pj["foldAtCF"] = piece.foldAtCF;
        // How many physical panels this one pattern piece becomes.
        pj["cutCount"] = piece.cutCount;
        pj["cutMirrored"] = piece.cutMirrored;
        // Notches are how a piece is matched to the piece it is sewn to,
        // so they belong in the machine-readable record alongside the
        // outline, not only in the CAD file.
        ofJson marks = ofJson::array();
        for (auto& m : piece.marks) {
            const char* kind = m.kind == Mark::Notch ? "notch"
                             : m.kind == Mark::Internal ? "internal"
                             : m.kind == Mark::Drill ? "drill" : "landmark";
            marks.push_back({ {"label", m.label}, {"kind", kind},
                              {"x", m.pos.x}, {"y", m.pos.y} });
        }
        pj["marks"] = marks;
        ofJson folds = ofJson::array();
        for (auto& f : piece.lines) {
            const char* lk = f.kind == MarkedLine::CutOnFold ? "cutOnFold"
                           : f.kind == MarkedLine::Cut ? "cut"
                           : f.kind == MarkedLine::SeamOpen ? "seamOpen" : "fold";
            folds.push_back({ {"label", f.label}, {"kind", lk},
                              {"ax", f.a.x}, {"ay", f.a.y}, {"bx", f.b.x}, {"by", f.b.y} });
        }
        pj["folds"] = folds;
        ofJson closures = ofJson::array();
        for (auto& c : piece.closures) {
            const char* kind = c.kind == Closure::Button ? "button"
                             : c.kind == Closure::Buttonhole ? "buttonhole"
                             : c.kind == Closure::Zipper ? "zipper"
                             : c.kind == Closure::Buckle ? "buckle" : "snap";
            closures.push_back({ {"kind", kind}, {"label", c.label}, {"sizeMm", c.sizeMm},
                                 {"ax", c.a.x}, {"ay", c.a.y}, {"bx", c.b.x}, {"by", c.b.y} });
        }
        pj["closures"] = closures;
        pieces.push_back(pj);
    }
    j["new_pieces"] = pieces;

    // The seam graph: which edge of which piece is sewn to which. Without
    // it an importer has outlines and nothing to assemble them with.
    ofJson seams = ofJson::array();
    for (auto& s : design.seams) {
        seams.push_back({
            {"name", s.name},
            {"a", {{"piece", s.a.piece}, {"from", s.a.from}, {"to", s.a.to}}},
            {"b", {{"piece", s.b.piece}, {"from", s.b.from}, {"to", s.b.to}}},
            {"note", s.note},
            // Without these two a reader would compare a sleeve cap to its
            // armhole as if they should be equal (they should not -- the
            // cap is eased in), and would join a rise seam to the wrong
            // copy of its piece.
            {"easeFactor", s.easeFactor},
            {"toMirrorOfSelf", s.toMirrorOfSelf},
        });
    }
    j["seams"] = seams;
    return j;
}

} } // namespace pf::exportx
