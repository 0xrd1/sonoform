#pragma once
#include "raylib.h"

// Zero-dependency parameter-visiting interface: the single seam between
// "what a setting is" (declared once per struct, in EngineSettings.h) and
// "what happens to it" (draw a slider + tooltip, write it to a preset file,
// read it back, or reset it to its compiled-in default). This is a runtime
// interface, not a template, specifically so imgui.h stays confined to
// src/ui/EngineUi.cpp and ImGuiPanel.cpp -- no visualizer, particle, or gfx
// translation unit ever needs to see it. Virtual dispatch over ~150 params
// once per frame is free next to the 320k particles those params tune.
//
// Every settings struct exposes its fields exactly once, via a Visit()
// method that calls these; every consumer (panel, writer, reader, reset-to-
// default) is a visitor over that single declaration -- add a field to a
// struct's Visit() and the panel, the preset round-trip, and reset-to-
// default all pick it up together. The classic failure of a hand-rolled
// ImGui panel is adding a slider and forgetting the save path; routing
// everything through one Visit() makes that mistake structurally
// impossible instead of something to remember.
namespace ui {

enum class ParamFlags : unsigned {
    None = 0,
    // This value is baked into a GPU resource at construction (e.g. an
    // SSBO's allocated capacity, a ShapeField's grid) and can't take effect
    // until the owning system is torn down and re-Init'd. The panel still
    // lets it be edited -- see ImGuiPanelVisitor -- it just marks it and
    // leaves applying it to the app-level "Rebuild Systems" action, so nothing
    // silently desyncs from what's actually running on the GPU.
    NeedsRebuild = 1u << 0,
};

inline ParamFlags operator|(ParamFlags a, ParamFlags b) {
    return static_cast<ParamFlags>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
inline bool HasFlag(ParamFlags flags, ParamFlags bit) {
    return (static_cast<unsigned>(flags) & static_cast<unsigned>(bit)) != 0;
}

struct ParamMeta {
    const char* label;
    const char* tooltip = nullptr;
    ParamFlags flags = ParamFlags::None;
};

class IParamVisitor {
public:
    virtual ~IParamVisitor() = default;

    // Groups params into a section -- an ImGui CollapsingHeader in the
    // panel, a "Group/" key prefix in the settings file. Calls must be
    // balanced; groups may nest (e.g. a visualizer's top-level group
    // containing one sub-group per settings struct it owns).
    virtual void BeginGroup(const char* name) = 0;
    virtual void EndGroup() = 0;

    virtual void Float(float& value, float defaultValue, float min, float max, const ParamMeta& meta) = 0;
    virtual void Int(int& value, int defaultValue, int min, int max, const ParamMeta& meta) = 0;
    virtual void Bool(bool& value, bool defaultValue, const ParamMeta& meta) = 0;

    // Component-wise, one shared [min,max] across x/y/z -- every current
    // use (positions, jitter half-extents, directions) is fine with a
    // single range; split into per-axis ranges if a future field needs it.
    virtual void Vec3(Vector3& value, Vector3 defaultValue, float min, float max, const ParamMeta& meta) = 0;

    virtual void ColorField(Color& value, Color defaultValue, const ParamMeta& meta) = 0;

    // `value` indexes into `names`/`count`. The owning struct stores the
    // enum as a plain int (see e.g. ShapeSettings::shapeType) and converts
    // to/from its real enum type only at its point of use, so this
    // interface doesn't need a template parameter per enum type.
    virtual void Enum(int& value, int defaultValue, const char* const* names, int count, const ParamMeta& meta) = 0;
};

} // namespace ui
