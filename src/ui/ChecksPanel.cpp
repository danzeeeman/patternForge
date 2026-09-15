#include "ChecksPanel.h"
#include "imgui.h"

namespace pf { namespace ui {

void drawChecksPanel(const Checks& checks) {
    int passCount = 0;
    for (auto& c : checks.results()) if (c.pass) ++passCount;
    ImGui::Text("%d / %d checks pass", passCount, (int)checks.results().size());
    ImGui::Separator();
    for (auto& c : checks.results()) {
        ImVec4 color = c.pass ? ImVec4(0.35f, 0.75f, 0.35f, 1.f) : ImVec4(0.85f, 0.35f, 0.30f, 1.f);
        ImGui::TextColored(color, "%s", c.pass ? "PASS" : "FAIL");
        ImGui::SameLine();
        ImGui::TextWrapped("%s -- %s", c.name.c_str(), c.detail.c_str());
    }
}

} } // namespace pf::ui
