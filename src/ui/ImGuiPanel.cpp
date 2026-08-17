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

// Draws `meta.label` as its own item (not baked into the preceding
// control's widget, the way e.g. ImGui::SliderFloat(label, ...) would) and
// shows the tooltip only when hovering *this* text specifically, after
// ImGuiHoveredFlags_DelayNormal's configured delay (1s -- see ui::Setup())
// -- so scrubbing a slider, or passing the mouse over it on the way
// somewhere else, never triggers a tooltip; only pausing to read the label
// does. Every widget function below calls this immediately after drawing
// its control with a hidden ("##...") label, wrapped in the same
// PushRebuildTint/PopRebuildTint scope as the control so a NeedsRebuild
// field's label still shows orange.
void DrawLabelWithTooltip(const ParamMeta& meta) {
    ImGui::TextUnformatted(meta.label);
    if (meta.tooltip != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("%s", meta.tooltip);
    }
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
    ImGui::SliderFloat("##ctrl", &value, min, max, "%.3f");
    ImGui::SameLine();
    DrawLabelWithTooltip(meta);
    PopRebuildTint(meta);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) value = defaultValue;
    ImGui::PopID();
}

void ImGuiPanelVisitor::Int(int& value, int defaultValue, int min, int max, const ParamMeta& meta) {
    if (hiddenDepth_ > 0) return;
    ImGui::PushID(&value);
    PushRebuildTint(meta);
    ImGui::SliderInt("##ctrl", &value, min, max);
    ImGui::SameLine();
    DrawLabelWithTooltip(meta);
    PopRebuildTint(meta);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) value = defaultValue;
    ImGui::PopID();
}

void ImGuiPanelVisitor::Bool(bool& value, bool defaultValue, const ParamMeta& meta) {
    if (hiddenDepth_ > 0) return;
    ImGui::PushID(&value);
    PushRebuildTint(meta);
    ImGui::Checkbox("##ctrl", &value);
    ImGui::SameLine();
    DrawLabelWithTooltip(meta);
    PopRebuildTint(meta);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) value = defaultValue;
    ImGui::PopID();
}

void ImGuiPanelVisitor::Vec3(Vector3& value, Vector3 defaultValue, float min, float max, const ParamMeta& meta) {
    if (hiddenDepth_ > 0) return;
    ImGui::PushID(&value);
    PushRebuildTint(meta);
    ImGui::SliderFloat3("##ctrl", &value.x, min, max, "%.3f");
    ImGui::SameLine();
    DrawLabelWithTooltip(meta);
    PopRebuildTint(meta);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) value = defaultValue;
    ImGui::PopID();
}

void ImGuiPanelVisitor::ColorField(Color& value, Color defaultValue, const ParamMeta& meta) {
    if (hiddenDepth_ > 0) return;
    ImGui::PushID(&value);
    float c[4] = { value.r / 255.0f, value.g / 255.0f, value.b / 255.0f, value.a / 255.0f };
    PushRebuildTint(meta);
    // `changed` gates the write-back below: ColorEdit4 returns true only
    // on an actual edit this frame (drag/click/typed value), not every
    // frame it's merely drawn. Writing back unconditionally (the previous
    // behavior) re-round-trips value -> [0,1] float -> byte through
    // "* 255 + 0.5" every single frame regardless of interaction, and
    // that round-trip can truncate an unlucky value +/-1 off from what it
    // started as (e.g. 51 -> 0.2 -> 50.999999 -> 50) -- a real bug in its
    // own right, and one that also broke the "active preset" badge (see
    // EngineUi.cpp's SnapshotOf): a color drifting by 1 on some frame made
    // every preset with a Color field look permanently "Custom" the
    // instant it was drawn, whether or not anything was actually edited.
    bool changed = ImGui::ColorEdit4("##ctrl", c);
    ImGui::SameLine();
    DrawLabelWithTooltip(meta);
    PopRebuildTint(meta);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) {
        value = defaultValue;
    } else if (changed) {
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
    ImGui::Combo("##ctrl", &value, names, count);
    ImGui::SameLine();
    DrawLabelWithTooltip(meta);
    PopRebuildTint(meta);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) value = defaultValue;
    ImGui::PopID();
}

} // namespace ui
