#include "ParamPanel.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace pf { namespace ui {

void setDefaultsForModule(const GarmentModulePtr& module, ParamPanelState& state) {
    state.style = StyleParams{};
    for (auto& spec : module->styleParamSpecs()) state.style.set(spec.key, spec.defaultV);
    state.sizePresetIndex = 0;
}

bool drawParamPanel(const std::vector<GarmentModulePtr>& modules, ParamPanelState& state) {
    bool changed = false;
    if (!state.initialized && !modules.empty()) {
        setDefaultsForModule(modules[0], state);
        state.initialized = true;
        changed = true;
    }
    if (modules.empty()) { ImGui::Text("No garment modules registered."); return changed; }

    ImGui::Text("Garment");
    if (ImGui::BeginCombo("##garment", modules[state.moduleIndex]->displayName().c_str())) {
        for (int i = 0; i < (int)modules.size(); ++i) {
            bool selected = (i == state.moduleIndex);
            if (ImGui::Selectable(modules[i]->displayName().c_str(), selected)) {
                state.moduleIndex = i;
                setDefaultsForModule(modules[i], state);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    auto& module = modules[state.moduleIndex];
    auto presets = module->sizePresets();
    if (!presets.empty()) {
        ImGui::Text("Size preset");
        if (ImGui::BeginCombo("##sizepreset", presets[state.sizePresetIndex].name.c_str())) {
            for (int i = 0; i < (int)presets.size(); ++i) {
                bool selected = (i == state.sizePresetIndex);
                if (ImGui::Selectable(presets[i].name.c_str(), selected)) { state.sizePresetIndex = i; changed = true; }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Separator();
    ImGui::Text("Style");
    for (auto& spec : module->styleParamSpecs()) {
        float v = state.style.get(spec.key, spec.defaultV);
        if (!spec.choices.empty()) {
            int idx = std::clamp((int)std::lround(v), 0, (int)spec.choices.size() - 1);
            ImGui::Text("%s", spec.label.c_str());
            if (ImGui::BeginCombo(("##" + spec.key).c_str(), spec.choices[idx].c_str())) {
                for (int i = 0; i < (int)spec.choices.size(); ++i) {
                    bool selected = (i == idx);
                    if (ImGui::Selectable(spec.choices[i].c_str(), selected)) {
                        state.style.set(spec.key, (float)i);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        } else if (ImGui::SliderFloat(spec.label.c_str(), &v, spec.minV, spec.maxV)) {
            state.style.set(spec.key, v);
            changed = true;
        }
    }
    return changed;
}

} } // namespace pf::ui
