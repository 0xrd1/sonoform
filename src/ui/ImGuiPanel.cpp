#include "ImGuiPanel.h"
#include "imgui.h"

namespace ui {

namespace {
// Tints a NeedsRebuild field's label orange so it visually stands out from
// live-tunable fields -- see ParamFlags::NeedsRebuild's comment on why the
// value is still editable rather than disabled.
void PushRebuildTint(const ParamMeta& meta) {
    if (HasFlag(meta.flags, ParamFlags::NeedsRebuild)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.65f, 0.2f, 1.0f));
    }
}
void PopRebuildTint(const ParamMeta& meta) {
    if (HasFlag(meta.flags, ParamFlags::NeedsRebuild)) ImGui::PopStyleColor();
}
} // namespace

void ImGuiPanelVisitor::BeginGroup(const char* name) {
    if (hiddenDepth_ > 0) {
        hideStack_.push_back(true);
        hiddenDepth_++;
        return;
    }
    bool open = ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen);
    if (open) {
        hideStack_.push_back(false);
        ImGui::Indent();
    } else {
        hideStack_.push_back(true);
        hiddenDepth_++;
    }
}

void ImGuiPanelVisitor::EndGroup() {
    if (hideStack_.empty()) return; // unbalanced Begin/EndGroup in a Visit() -- ignore rather than crash
    bool wasHidden = hideStack_.back();
    hideStack_.pop_back();
    if (wasHidden) hiddenDepth_--;
    else ImGui::Unindent();
}

void ImGuiPanelVisitor::Float(float& value, float defaultValue, float min, float max, const ParamMeta& meta) {
    if (hiddenDepth_ > 0) return;
    ImGui::PushID(&value);
    PushRebuildTint(meta);
    ImGui::SliderFloat(meta.label, &value, min, max, "%.3f");
    PopRebuildTint(meta);
    if (meta.tooltip != nullptr && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", meta.tooltip);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) value = defaultValue;
    ImGui::PopID();
}

void ImGuiPanelVisitor::Int(int& value, int defaultValue, int min, int max, const ParamMeta& meta) {
    if (hiddenDepth_ > 0) return;
    ImGui::PushID(&value);
    PushRebuildTint(meta);
    ImGui::SliderInt(meta.label, &value, min, max);
    PopRebuildTint(meta);
    if (meta.tooltip != nullptr && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", meta.tooltip);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) value = defaultValue;
    ImGui::PopID();
}

void ImGuiPanelVisitor::Bool(bool& value, bool defaultValue, const ParamMeta& meta) {
    if (hiddenDepth_ > 0) return;
    ImGui::PushID(&value);
    PushRebuildTint(meta);
    ImGui::Checkbox(meta.label, &value);
    PopRebuildTint(meta);
    if (meta.tooltip != nullptr && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", meta.tooltip);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) value = defaultValue;
    ImGui::PopID();
}

void ImGuiPanelVisitor::Vec3(Vector3& value, Vector3 defaultValue, float min, float max, const ParamMeta& meta) {
    if (hiddenDepth_ > 0) return;
    ImGui::PushID(&value);
    PushRebuildTint(meta);
    ImGui::SliderFloat3(meta.label, &value.x, min, max, "%.3f");
    PopRebuildTint(meta);
    if (meta.tooltip != nullptr && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", meta.tooltip);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) value = defaultValue;
    ImGui::PopID();
}

void ImGuiPanelVisitor::ColorField(Color& value, Color defaultValue, const ParamMeta& meta) {
    if (hiddenDepth_ > 0) return;
    ImGui::PushID(&value);
    float c[4] = { value.r / 255.0f, value.g / 255.0f, value.b / 255.0f, value.a / 255.0f };
    PushRebuildTint(meta);
    ImGui::ColorEdit4(meta.label, c);
    PopRebuildTint(meta);
    if (meta.tooltip != nullptr && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", meta.tooltip);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) {
        value = defaultValue;
    } else {
        value = Color{
            static_cast<unsigned char>(c[0] * 255.0f + 0.5f),
            static_cast<unsigned char>(c[1] * 255.0f + 0.5f),
            static_cast<unsigned char>(c[2] * 255.0f + 0.5f),
            static_cast<unsigned char>(c[3] * 255.0f + 0.5f),
        };
    }
    ImGui::PopID();
}

void ImGuiPanelVisitor::Enum(int& value, int defaultValue, const char* const* names, int count, const ParamMeta& meta) {
    if (hiddenDepth_ > 0) return;
    ImGui::PushID(&value);
    PushRebuildTint(meta);
    ImGui::Combo(meta.label, &value, names, count);
    PopRebuildTint(meta);
    if (meta.tooltip != nullptr && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", meta.tooltip);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) value = defaultValue;
    ImGui::PopID();
}

} // namespace ui
