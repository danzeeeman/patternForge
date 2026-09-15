#pragma once
#include "GarmentModule.h"

// Overcoat, peacoat and trench coat. Three garments in the picker because
// that is how anyone asks for them, but one draft underneath: see
// OuterwearDraft.h for what actually distinguishes them.
//
// Each module supplies the same sliders plus whatever its own kind adds,
// and starts from that kind's defaults -- so picking "Peacoat" already
// gives you a peacoat, not an overcoat you have to shorten by hand.

namespace pf {

class OvercoatModule : public GarmentModule {
public:
    std::string key() const override { return "Overcoat"; }
    std::string displayName() const override { return "Overcoat"; }
    std::vector<SizePreset> sizePresets() const override;
    std::vector<StyleParamSpec> styleParamSpecs() const override;
    DesignResult build(const SizePreset& size, const StyleParams& style) const override;
};

class PeacoatModule : public GarmentModule {
public:
    std::string key() const override { return "Peacoat"; }
    std::string displayName() const override { return "Peacoat"; }
    std::vector<SizePreset> sizePresets() const override;
    std::vector<StyleParamSpec> styleParamSpecs() const override;
    DesignResult build(const SizePreset& size, const StyleParams& style) const override;
};

class TrenchCoatModule : public GarmentModule {
public:
    std::string key() const override { return "Trench"; }
    std::string displayName() const override { return "Trench Coat"; }
    std::vector<SizePreset> sizePresets() const override;
    std::vector<StyleParamSpec> styleParamSpecs() const override;
    DesignResult build(const SizePreset& size, const StyleParams& style) const override;
};

} // namespace pf
