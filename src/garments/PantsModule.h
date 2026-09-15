#pragma once
#include "GarmentModule.h"

namespace pf {

// Straight/tapered trouser: front and back panels from a shared block
// (crotch extension and rise differ between them), a waistband, and a
// leg-width slider that runs from tapered to wide-leg.
class PantsModule : public GarmentModule {
public:
    std::string key() const override { return "Pants"; }
    std::string displayName() const override { return "Pants"; }
    std::vector<SizePreset> sizePresets() const override;
    std::vector<StyleParamSpec> styleParamSpecs() const override;
    DesignResult build(const SizePreset& size, const StyleParams& style) const override;
};

} // namespace pf
