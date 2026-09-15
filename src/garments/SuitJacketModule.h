#pragma once
#include "GarmentModule.h"

namespace pf {

// A tailored suit jacket: the same torso block the Coat module uses, but
// cut to jacket length and finished with a lapel instead of a neckline.
//
// The lapel is what makes this its own garment rather than a short coat.
// It needs a front edge that runs out past center front (the button
// wrap), a roll line for it to fold back along, and a facing cut to match
// so the folded-back side shows cloth -- none of which the plain block
// has. Notched, peak and shawl are the three lapel styles offered.
//
// Like every other module here this is a development draft built from
// simplified block ratios, not a fitted tailoring pattern: a real jacket
// carries darts, a chest canvas and a two-piece sleeve. Sew a toile.
class SuitJacketModule : public GarmentModule {
public:
    std::string key() const override { return "SuitJacket"; }
    std::string displayName() const override { return "Suit Jacket"; }
    std::vector<SizePreset> sizePresets() const override;
    std::vector<StyleParamSpec> styleParamSpecs() const override;
    DesignResult build(const SizePreset& size, const StyleParams& style) const override;
};

} // namespace pf
