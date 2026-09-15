#pragma once
#include "../garments/GarmentModule.h"

// Draws the garment/size pickers and a slider per StyleParamSpec, built
// generically from whichever module is selected -- no per-garment GUI
// code needed when a new GarmentModule is added.
namespace pf { namespace ui {

struct ParamPanelState {
    int moduleIndex = 0;
    int sizePresetIndex = 0;
    StyleParams style;
    bool initialized = false;
};

// Returns true if the selection or any slider changed this frame (the
// caller should rebuild the DesignResult).
bool drawParamPanel(const std::vector<GarmentModulePtr>& modules, ParamPanelState& state);

// Resets `state` to `module`'s default size preset (index 0) and default
// style values, with no GUI involved -- used by drawParamPanel itself on
// a garment switch, and by the headless autoexport smoke test.
void setDefaultsForModule(const GarmentModulePtr& module, ParamPanelState& state);

} } // namespace pf::ui
