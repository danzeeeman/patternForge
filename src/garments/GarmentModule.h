#pragma once
#include "../geometry/Piece.h"
#include "../geometry/Checks.h"
#include <map>
#include <string>
#include <vector>
#include <memory>

// Shared vocabulary every garment module (Dress/Shirt/Pants/Coat) is built
// from. A design is: pick a SizePreset (a named, fixed body-measurement
// set, cm -- e.g. "US4"), pick StyleParams (the sliders: hem flare, sleeve
// width, collar depth, rise...), and a GarmentModule turns that pair into
// a DesignResult -- the perimeter of every piece plus the checks and copy
// needed to fill out an exported package.

namespace pf {

// A fixed body-measurement set. Matches the existing coats' fixed US4
// assumption rather than free-form measurement entry.
struct SizePreset {
    std::string name;                          // e.g. "US4"
    std::map<std::string, double> cm;          // "bust","waist","hip","shoulder","neck",
                                                // "backWaistLength","armholeDepth","sleeveLength",
                                                // "bicep","wrist","height","inseam","rise","hipDepth", ...
    double get(const std::string& key, double fallback) const {
        auto it = cm.find(key);
        return it == cm.end() ? fallback : it->second;
    }
};

// One adjustable style control. GUI code builds its panel generically
// from a module's styleParamSpecs() rather than hard-coding widgets per
// garment: a plain float slider (minV..maxV) by default, or -- when
// `choices` is non-empty -- a combo box, with the stored float holding
// the chosen index (0..choices.size()-1). A "Silhouette" choice is how a
// module like Dress offers several distinct dress lines (A-line, sheath,
// fit & flare, ...) from one slider panel.
struct StyleParamSpec {
    std::string key;
    std::string label;
    float minV, maxV, defaultV;
    std::vector<std::string> choices; // non-empty => render as a combo, value = chosen index
};

struct StyleParams {
    std::map<std::string, float> values;
    float get(const std::string& key, float fallback) const {
        auto it = values.find(key);
        return it == values.end() ? fallback : it->second;
    }
    void set(const std::string& key, float v) { values[key] = v; }
};

// One edge of one piece: the span of its outline between two named
// landmarks, walked the short way round.
struct SeamEnd {
    std::string piece;      // piece code
    std::string from, to;   // landmark labels on that piece
};

// Two edges sewn to each other. This is the part of a pattern that lives
// only in the instructions until you write it down: the outlines say what
// the pieces ARE, and the seam list says how they become a garment. A CAD
// tool can import outlines without it, but it cannot assemble them.
struct Seam {
    std::string name;       // "side seam", "armhole", ...
    SeamEnd a, b;
    std::string note;

    // Edge `b` is this many times edge `a`. Usually 1 -- two edges sewn
    // flat must be the same length -- but some seams are designed with
    // ease: a sleeve cap is cut longer than its armhole and eased in, a
    // gathered skirt much longer than the bodice it hangs from. Without
    // this a correct sleeve looks like a mismatched seam.
    double easeFactor = 1.0;

    // True when edge `b` is the SAME edge on this piece's mirrored copy,
    // not a different piece. A trouser's front rise sews to the other
    // leg's front rise; a center-back seam joins the two backs. Modelling
    // those as front-to-back joins would be simply wrong -- and would
    // compare two edges that were never meant to match.
    bool toMirrorOfSelf = false;
};

// Everything a completed design needs to hand to the export pipeline.
struct DesignResult {
    std::string title;                          // "Aria Shift Dress"
    std::string garmentKey;                     // "Dress" | "Coat" | "Shirt" | "Pants"
    std::string stem;                           // filename-safe slug
    SizePreset size;
    StyleParams style;
    std::vector<Piece> pieces;
    Checks checks;
    std::vector<std::pair<std::string, std::string>> cuttingList; // code -> "qty / material"
    std::vector<Seam> seams;                     // how the pieces join into a garment
    std::vector<std::string> whatChanged;        // "what this design does" paragraphs for the manual
    std::vector<std::string> constructionSteps;  // ordered assembly paragraphs for the manual

    // Resolved torso landmarks in cm below/across the nape datum, or 0 for
    // a garment with no torso. The 3D preview needs the same armhole and
    // shoulder the pattern was drafted with, rather than re-deriving them.
    double armholeY = 0.0;
    double shoulderX = 0.0;
};

class GarmentModule {
public:
    virtual ~GarmentModule() = default;
    virtual std::string key() const = 0;              // stable id, matches DesignResult::garmentKey
    virtual std::string displayName() const = 0;       // "Dress"
    virtual std::vector<SizePreset> sizePresets() const = 0;
    virtual std::vector<StyleParamSpec> styleParamSpecs() const = 0;
    virtual DesignResult build(const SizePreset& size, const StyleParams& style) const = 0;
};

using GarmentModulePtr = std::shared_ptr<GarmentModule>;

} // namespace pf
