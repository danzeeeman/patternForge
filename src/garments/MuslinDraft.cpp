#include "MuslinDraft.h"
#include "GarmentCommon.h"
#include <algorithm>
#include <cmath>

namespace pf { namespace muslin {

namespace {

// The cutting list says what cloth a piece is cut from. A muslin is cut
// from muslin, whatever the finished garment was going to be.
std::string inMuslinCloth(std::string cutQty) {
    for (const char* fabric : { "SHELL", "FACING CLOTH", "INTERFACING", "LINING" }) {
        std::string f(fabric);
        size_t at = cutQty.find(f);
        while (at != std::string::npos) {
            cutQty.replace(at, f.size(), "MUSLIN");
            at = cutQty.find(f, at + 6);
        }
    }
    return cutQty;
}

} // namespace

DesignResult muslinFrom(const DesignResult& design) {
    DesignResult out;
    out.garmentKey = design.garmentKey;
    out.stem = design.stem;
    out.size = design.size;
    out.style = design.style;
    out.title = design.title + " -- Fitting Muslin";
    out.armholeY = design.armholeY;
    out.shoulderX = design.shoulderX;

    for (auto& piece : design.pieces) {
        // A derived piece is a facing, which finishes rather than fits.
        if (piece.isDerived() || !piece.inMuslin) continue;

        Piece m = piece;
        m.seamAllowanceCm = (float)kMuslinSeamAllowanceCm;
        m.cutQty = inMuslinCloth(m.cutQty);
        m.note = piece.note + " Cut with " +
                 std::to_string((int)std::lround(kMuslinSeamAllowanceCm)) +
                 " cm seam allowance so it can be let out at the fitting.";
        // A fold is still not sewn, so resolveCutting keeps it on x = 0.
        m.resolveCutting();
        out.pieces.push_back(m);
    }

    common::addSanityChecks(out.checks, out.pieces);
    // The point of the wider allowance is that it is actually there: if a
    // muslin came out with the pattern's own 1 cm, it could not be let out
    // and the fitting would only ever tell you "too tight", never by how
    // much.
    bool wide = true;
    for (auto& p : out.pieces)
        if (p.seamAllowanceCm < kMuslinSeamAllowanceCm - 0.01) wide = false;
    out.checks.add("Muslin carries let-out seam allowances",
                   std::to_string((int)std::lround(kMuslinSeamAllowanceCm)) +
                   " cm on all " + std::to_string(out.pieces.size()) + " pieces",
                   wide && !out.pieces.empty());

    // Every piece that carries the fit has to be here, or the muslin tests
    // a different garment from the one the pattern makes.
    size_t expected = 0;
    for (auto& p : design.pieces) if (!p.isDerived() && p.inMuslin) ++expected;
    out.checks.add("Muslin keeps every fit-carrying piece",
                   std::to_string(out.pieces.size()) + " of " +
                   std::to_string(design.pieces.size()) + " pattern pieces "
                   "(finishing pieces left out by design)",
                   out.pieces.size() == expected);

    out.cuttingList = common::defaultCuttingList(out.pieces);
    out.whatChanged = {
        "The fitting muslin for " + design.title + ": the same pieces that carry the fit, cut "
        "with " + std::to_string((int)std::lround(kMuslinSeamAllowanceCm)) + " cm seam "
        "allowances instead of the pattern's 1 cm so every seam can be let out on the body.",
        "Collars, cuffs, plackets, waistbands and facings are deliberately absent. They finish a "
        "garment; they do not change how it hangs, and leaving them off makes the muslin faster "
        "to sew and easier to re-cut. Sew it in calico or any cheap cloth of a similar weight to "
        "your final fabric -- a stiff muslin will not tell you the truth about a drapey cloth.",
    };
    out.constructionSteps = {
        "1 / Cut in calico. Mark the center front, center back and waistline with a pen -- on a "
        "muslin you want those lines visible, because they are what you check against the body.",
        "2 / Machine-baste the seams with a long stitch, so they can be unpicked and moved.",
        "3 / Try it on over the underwear you will wear with the finished garment. Check the "
        "center front hangs plumb, the side seams sit vertical, and the armhole and neckline sit "
        "where you want them.",
        "4 / Pin out what is loose and slash-and-spread what is tight, then draw the corrected "
        "seam lines straight onto the cloth.",
        "5 / Unpick, press flat, and measure the difference against this pattern. Carry those "
        "numbers back to the Style panel, or move the outline points by hand in the editor, and "
        "export again.",
    };
    return out;
}

} } // namespace pf::muslin
