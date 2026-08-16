#include "EngineUi.h"
#include "EngineSettings.h"
#include "ImGuiPanel.h"
#include "SettingsIO.h"
#include "VisualizerManager.h"

#include "imgui.h"
#include "rlImGui.h"

#include <string>
#include <vector>
#include <cstring>

namespace ui {

namespace {

constexpr const char* kPresetsDir = "settings/presets";
constexpr const char* kDefaultPath = "settings/default.ini";

// The one place that knows the full shape of "everything persisted":
// Camera, Post/Global, and whatever the current visualizer exposes via
// VisitSettings. Used identically by the panel draw, Save/Load, and the
// startup auto-load, so those three can never drift out of sync with each
// other about what fields exist. The visualizer's own group is keyed by
// its display name (e.g. "Neon Fog") rather than folded flat, so a future
// second wired-up visualizer gets its own namespaced section for free with
// no key collisions against Neon Fog's "Emission"/"Forces"/etc. groups.
void VisitAll(PanelState& state, IParamVisitor& v) {
    v.BeginGroup("Camera");
    state.camera->Visit(v);
    v.EndGroup();

    v.BeginGroup("Post");
    state.post->Visit(v);
    v.EndGroup();

    v.BeginGroup(state.visualizers->CurrentName());
    state.visualizers->VisitCurrentSettings(v);
    v.EndGroup();
}

} // namespace

void Setup() {
    rlImGuiSetup(true);

    ImGuiIO& io = ImGui::GetIO();

    // ImGui writes imgui.ini to the current working directory, which isn't
    // stable for this project: running the built exe directly gives
    // CWD=build/, while the VS debugger launches with CWD set to the repo
    // root (VS_DEBUGGER_WORKING_DIRECTORY in CMakeLists.txt) -- either of
    // which would leave a stray, untracked imgui.ini behind. Persisted
    // layout isn't needed for one floating window, so just disable it
    // rather than pick one CWD to trust.
    io.IniFilename = nullptr;

    // Docking is available (imgui is pinned to the -docking tag in
    // CMakeLists.txt) but not enabled -- one floating panel needs no
    // dockspace. ImGuiConfigFlags_ViewportsEnable must never be set: it
    // needs an ImGui platform backend able to create additional OS windows,
    // and rlImGui implements none (raylib owns the single GLFW window).
}

void Shutdown() { rlImGuiShutdown(); }
void BeginFrame() { rlImGuiBegin(); }
void EndFrame() { rlImGuiEnd(); }

bool WantsKeyboard() {
    return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
}
bool WantsMouse() {
    return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
}

void DrawDebugPanel(PanelState& state) {
    if (state.visualizers == nullptr || state.camera == nullptr || state.post == nullptr) return;

    ImGui::SetNextWindowSize(ImVec2(440, 640), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Engine Settings")) {
        ImGui::End();
        return;
    }

    ImGui::Text("FPS: %d   Particles: %d", GetFPS(), state.particleCount);
    ImGui::Text("Track: %s", state.trackLabel);

    if (state.paused != nullptr) {
        bool wasPaused = *state.paused;
        ImGui::Checkbox("Paused", state.paused);
        if (*state.paused != wasPaused && state.music != nullptr && state.musicLoaded) {
            if (*state.paused) PauseMusicStream(*state.music);
            else ResumeMusicStream(*state.music);
        }
    }
    if (state.showHud != nullptr) {
        ImGui::SameLine();
        ImGui::Checkbox("Text HUD", state.showHud);
    }

    ImGui::Separator();
    ImGui::Text("Visualizer: %s (%d/%d)", state.visualizers->CurrentName(),
                state.visualizers->CurrentIndex() + 1, state.visualizers->Count());
    if (state.visualizers->Count() > 1) {
        if (ImGui::Button("< Prev")) state.visualizers->Prev();
        ImGui::SameLine();
        if (ImGui::Button("Next >")) state.visualizers->Next();
    }

    ImGui::Separator();
    {
        // Fresh each draw -- see ImGuiPanelVisitor's class comment. Holds
        // only the current group-visibility stack, so this is cheap next
        // to the widgets it's about to draw.
        ImGuiPanelVisitor panel;
        VisitAll(state, panel);
    }

    ImGui::Separator();
    ImGui::BeginDisabled(state.shaders == nullptr || state.renderer == nullptr);
    if (ImGui::Button("Rebuild Systems")) {
        state.visualizers->RebuildCurrent(*state.shaders, *state.renderer);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Applies every orange-tinted field above (e.g. Fog Capacity, Shape Grid "
            "Resolution/Extent, Field Center) by tearing down and re-initializing the "
            "current visualizer's GPU systems. Everything else already applies live.");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Presets");

    static char nameBuf[64] = "my-look";
    ImGui::InputText("Name", nameBuf, sizeof(nameBuf));

    if (ImGui::Button("Save As")) {
        std::string path = std::string(kPresetsDir) + "/" + nameBuf + ".ini";
        SaveSettings(path, [&](IParamVisitor& v) { VisitAll(state, v); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As Default")) {
        SaveSettings(kDefaultPath, [&](IParamVisitor& v) { VisitAll(state, v); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Default")) {
        LoadSettings(kDefaultPath, [&](IParamVisitor& v) { VisitAll(state, v); });
    }

    for (const std::string& name : ListPresets(kPresetsDir)) {
        ImGui::PushID(name.c_str());
        ImGui::BulletText("%s", name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Load")) {
            LoadSettings(std::string(kPresetsDir) + "/" + name + ".ini", [&](IParamVisitor& v) { VisitAll(state, v); });
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            DeletePreset(kPresetsDir, name);
        }
        ImGui::PopID();
    }

    ImGui::End();
}

bool LoadDefaultSettings(PanelState& state) {
    if (state.visualizers == nullptr || state.camera == nullptr || state.post == nullptr) return false;
    return LoadSettings(kDefaultPath, [&](IParamVisitor& v) { VisitAll(state, v); });
}

} // namespace ui
