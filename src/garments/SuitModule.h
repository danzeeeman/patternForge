#pragma once
#include "GarmentModule.h"

namespace pf {

// A pant suit: a jacket and a pair of trousers drafted to one body and
// exported as a single package.
//
// The point of it being one garment rather than two is that the two
// halves share a size, an ease and a cloth, and are cut together. The
// jacket is the same draft the Suit Jacket module produces (both call
// suit::draftJacket), so "the same jacket" stays true; the trousers carry
// a "P" code prefix so their pieces never collide with the jacket's.
class SuitModule : public GarmentModule {
public:
    std::string key() const override { return "Suit"; }
    std::string displayName() const override { return "Pant Suit"; }
    std::vector<SizePreset> sizePresets() const override;
    std::vector<StyleParamSpec> styleParamSpecs() const override;
    DesignResult build(const SizePreset& size, const StyleParams& style) const override;
};

} // namespace pf
