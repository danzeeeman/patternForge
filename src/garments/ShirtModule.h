#pragma once
#include "GarmentModule.h"

namespace pf {

// Classic button-front shirt: front (cut 2, mirrored, opens at center
// front with a button placket), back (cut 1 on the fold, straight hem),
// long sleeve with a cuff, and a banded collar.
class ShirtModule : public GarmentModule {
public:
    std::string key() const override { return "Shirt"; }
    std::string displayName() const override { return "Shirt"; }
    std::vector<SizePreset> sizePresets() const override;
    std::vector<StyleParamSpec> styleParamSpecs() const override;
    DesignResult build(const SizePreset& size, const StyleParams& style) const override;
};

} // namespace pf
