#pragma once
#include <vector>
#include "ParamVisitor.h"

// Declares no ImGui type -- imgui.h is included only by ImGuiPanel.cpp
// (widgets) and EngineUi.cpp (context setup), both inside src/ui/. Nothing
// outside this directory needs to know ImGui exists.
namespace ui {

// IParamVisitor that draws one ImGui widget (+ hover tooltip + a small
// per-field "Reset" button) for each visited value, grouped into collapsing
// headers by BeginGroup/EndGroup. Fields whose ParamMeta carries
// ParamFlags::NeedsRebuild are tinted so it's visually clear the change
// won't apply until "Rebuild Systems" is pressed.
//
// Cheap to construct: holds no state beyond the current group-visibility
// stack, so a fresh instance per panel draw is the intended usage (see
// EngineUi.cpp's DrawDebugPanel).
class ImGuiPanelVisitor : public IParamVisitor {
public:
    void BeginGroup(const char* name) override;
    void EndGroup() override;

    void Float(float& value, float defaultValue, float min, float max, const ParamMeta& meta) override;
    void Int(int& value, int defaultValue, int min, int max, const ParamMeta& meta) override;
    void Bool(bool& value, bool defaultValue, const ParamMeta& meta) override;
    void Vec3(Vector3& value, Vector3 defaultValue, float min, float max, const ParamMeta& meta) override;
    void ColorField(Color& value, Color defaultValue, const ParamMeta& meta) override;
    void Enum(int& value, int defaultValue, const char* const* names, int count, const ParamMeta& meta) override;

private:
    // >0 means the current field is nested inside a collapsed (closed)
    // header somewhere up the stack, so every leaf setter no-ops instead of
    // drawing -- ImGui's CollapsingHeader doesn't hide its body
    // automatically the way a TreeNode's indentation does, so this has to
    // be tracked explicitly.
    int hiddenDepth_ = 0;
    // Per BeginGroup call: whether *that* call is the reason anything is
    // hidden, so EndGroup knows whether to decrement hiddenDepth_ / call
    // Unindent().
    std::vector<bool> hideStack_;
};

} // namespace ui
