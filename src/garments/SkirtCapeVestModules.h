#pragma once
#include "GarmentModule.h"

// Three garments that each break one assumption the rest of this app
// shares, which is why they are worth having:
//
//   SKIRT   Hangs from the waist, not the shoulder. Its constraint is the
//           hip, not the bust: a skirt narrower than the hip cannot be got
//           on however well it fits the waist.
//   CAPE    Cut as a piece of a CIRCLE rather than as a body panel. It has
//           no side seams and no armholes -- it hangs from the neck and
//           falls, and the fall comes out of the radius.
//   VEST    A jacket with the sleeves and the ease taken out. Fitted close
//           over a shirt, cut to a point below the waist, and the last
//           piece of a three-piece suit.

namespace pf {

class SkirtModule : public GarmentModule {
public:
    std::string key() const override { return "Skirt"; }
    std::string displayName() const override { return "Skirt"; }
    std::vector<SizePreset> sizePresets() const override;
    std::vector<StyleParamSpec> styleParamSpecs() const override;
    DesignResult build(const SizePreset& size, const StyleParams& style) const override;
};

class CapeModule : public GarmentModule {
public:
    std::string key() const override { return "Cape"; }
    std::string displayName() const override { return "Cape"; }
    std::vector<SizePreset> sizePresets() const override;
    std::vector<StyleParamSpec> styleParamSpecs() const override;
    DesignResult build(const SizePreset& size, const StyleParams& style) const override;
};

class VestModule : public GarmentModule {
public:
    std::string key() const override { return "Vest"; }
    std::string displayName() const override { return "Vest / Waistcoat"; }
    std::vector<SizePreset> sizePresets() const override;
    std::vector<StyleParamSpec> styleParamSpecs() const override;
    DesignResult build(const SizePreset& size, const StyleParams& style) const override;
};

} // namespace pf
