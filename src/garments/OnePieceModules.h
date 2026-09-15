#pragma once
#include "GarmentModule.h"

// Jumpsuit and bodysuit: the two garments that join a bodice to what is
// below it instead of ending at a hem.
//
// A JUMPSUIT is a bodice and a trouser sewn together at the waist. That
// seam is the whole difficulty: the bodice hem and the trouser waist have
// to be the same length or the two halves cannot be joined, and they are
// drafted from different blocks, so nothing makes them agree by accident.
//
// A BODYSUIT has no waist seam at all -- the panel runs from the shoulder
// straight through the crotch, with the legs cut away at the sides. It is
// also the only garment here meant for KNIT cloth: it is held on by being
// smaller than the body, so it is drafted with negative ease, and drafting
// it with the woven ease every other block uses would give a bag.

namespace pf {

class JumpsuitModule : public GarmentModule {
public:
    std::string key() const override { return "Jumpsuit"; }
    std::string displayName() const override { return "Jumpsuit"; }
    std::vector<SizePreset> sizePresets() const override;
    std::vector<StyleParamSpec> styleParamSpecs() const override;
    DesignResult build(const SizePreset& size, const StyleParams& style) const override;
};

class BodysuitModule : public GarmentModule {
public:
    std::string key() const override { return "Bodysuit"; }
    std::string displayName() const override { return "Bodysuit"; }
    std::vector<SizePreset> sizePresets() const override;
    std::vector<StyleParamSpec> styleParamSpecs() const override;
    DesignResult build(const SizePreset& size, const StyleParams& style) const override;
};

} // namespace pf
