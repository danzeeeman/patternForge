#pragma once
#include "GarmentModule.h"

namespace pf {

// A-line coat: the same dart-free torso block as the Dress module, run
// longer and with more ease, plus a collar, long sleeve and underarm
// gusset. This drafts a NEW coat from the body block (StyleParams below);
// it does not remix the two example PDF-sourced coats already in this
// repo (Facet / Prism) -- that "extract an existing pattern's outline and
// reshape it" path is SourceExtractor's job and is not implemented yet
// (see the project README).
class CoatModule : public GarmentModule {
public:
    std::string key() const override { return "Coat"; }
    std::string displayName() const override { return "Coat"; }
    std::vector<SizePreset> sizePresets() const override;
    std::vector<StyleParamSpec> styleParamSpecs() const override;
    DesignResult build(const SizePreset& size, const StyleParams& style) const override;
};

} // namespace pf
