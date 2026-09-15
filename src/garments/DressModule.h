#pragma once
#include "GarmentModule.h"

namespace pf {

// A-line/shift dress: front and back bodice blend straight into the skirt
// (no separate waist seam), with hem length, hem flare and neckline depth
// as the main style sliders, plus an optional set-in sleeve.
class DressModule : public GarmentModule {
public:
    std::string key() const override { return "Dress"; }
    std::string displayName() const override { return "Dress"; }
    std::vector<SizePreset> sizePresets() const override;
    std::vector<StyleParamSpec> styleParamSpecs() const override;
    DesignResult build(const SizePreset& size, const StyleParams& style) const override;
};

} // namespace pf
