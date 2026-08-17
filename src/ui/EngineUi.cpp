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

// Both windows default to the right edge of the screen, stacked downward
// (see the plan's "growing down" ask) -- FirstUseEver only, so this is a
// *default* the user can freely drag/resize away from during the session;
// io.IniFilename == nullptr (see Setup()) means it re-applies fresh every
// launch rather than remembering wherever it was last dragged to.
constexpr float kWindowMargin = 20.0f;
constexpr float kPanelWidth = 480.0f;
constexpr float kPanelHeightEstimate = 760.0f; // only used to place the *next* stacked window
constexpr float kDebugWindowWidth = 340.0f;

// A fixed, modest item width rather than ImGui's default (which sizes to
// whatever's left of the current row) is what actually fixes labels being
// pushed off-window: two levels of BeginGroup/Indent (see
// ImGuiPanelVisitor) eat real width before a slider ever gets to size
// itself, and the default sizing has no idea that happened.
constexpr float kItemWidth = 160.0f;

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

    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, kPanelHeightEstimate), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(GetScreenWidth() - kPanelWidth - kWindowMargin, kWindowMargin), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Engine Settings")) {
        ImGui::End();
        return;
    }
    ImGui::PushItemWidth(kItemWidth);

    ImGui::Text("FPS: %d   Particles: %d", GetFPS(), state.particleCount);
    ImGui::Text("Track: %s", state.trackLabel);
    ImGui::TextDisabled("Ctrl+Click or double-click any slider to type an exact value");

    if (state.paused != nullptr) {
        bool wasPaused = *state.paused;
        ImGui::Checkbox("Paused", state.paused);
        if (*state.paused != wasPaused && state.onPausedChanged) {
            state.onPausedChanged(*state.paused);
        }
    }
    if (state.showHud != nullptr) {
        ImGui::SameLine();
        ImGui::Checkbox("Text HUD", state.showHud);
    }

    if (state.audio != nullptr) {
        ImGui::Separator();
        ImGui::TextUnformatted("Audio");
        ImGui::Checkbox("Enabled", &state.audio->enabled);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Turns on the audio device, track playback, and every audio-reactive visual "
                "(lighting, turbulence, kick impulses). Off by default -- silent, no audio "
                "device initialized.");
        }
        ImGui::SliderFloat("Volume", &state.audio->volume, 0.0f, 1.0f);

        bool canTransport = state.audio->enabled && state.musicLoaded;
        ImGui::BeginDisabled(!canTransport);
        if (ImGui::Button("<< Prev") && state.onPrevTrack) state.onPrevTrack();
        ImGui::SameLine();
        bool isPaused = state.paused != nullptr && *state.paused;
        if (ImGui::Button(isPaused ? "Play" : "Pause") && state.paused != nullptr) {
            *state.paused = !*state.paused;
            if (state.onPausedChanged) state.onPausedChanged(*state.paused);
        }
        ImGui::SameLine();
        if (ImGui::Button("Next >>") && state.onNextTrack) state.onNextTrack();
        ImGui::EndDisabled();

        // A compact scrollable playlist -- same bullet + SmallButton shape
        // as the Presets list further down, just swapped to track names
        // and state.onSelectTrack.
        if (state.trackNames != nullptr && !state.trackNames->empty()) {
            ImGui::BeginChild("TrackList", ImVec2(0.0f, 90.0f), true);
            for (int i = 0; i < static_cast<int>(state.trackNames->size()); i++) {
                ImGui::PushID(i);
                const std::string& name = (*state.trackNames)[static_cast<size_t>(i)];
                if (i == state.currentTrackIndex) ImGui::BulletText("%s (playing)", name.c_str());
                else ImGui::BulletText("%s", name.c_str());
                ImGui::SameLine();
                ImGui::BeginDisabled(!state.audio->enabled);
                if (ImGui::SmallButton("Play") && state.onSelectTrack) state.onSelectTrack(i);
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
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

        // Not part of VisitAll/the preset round-trip -- see
        // PerformanceSettings' own comment on why (a machine
        // characteristic, not part of "the look").
        if (state.performance != nullptr) {
            panel.BeginGroup("Performance");
            state.performance->Visit(panel);
            panel.EndGroup();
        }
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

    ImGui::PopItemWidth();
    ImGui::End();
}

void DrawDebugWindow(PanelState& state) {
    if (state.debug == nullptr) return;
    if (state.visualizers == nullptr) return;

    // Stacked below the settings window's estimated height -- see
    // kPanelHeightEstimate's comment: a default only, not enforced.
    ImGui::SetNextWindowSize(ImVec2(kDebugWindowWidth, 260.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(GetScreenWidth() - kDebugWindowWidth - kWindowMargin, kWindowMargin + kPanelHeightEstimate + kWindowMargin),
        ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Debug View")) {
        ImGui::End();
        return;
    }
    ImGui::PushItemWidth(kItemWidth);

    // Precise numbers a 3D gizmo communicates poorly (grid resolution,
    // voxel size, exact field center, current light color/intensity) --
    // see Visualizer::DebugInfoText's comment. Optional: nullptr for a
    // visualizer that hasn't implemented it.
    const char* info = state.visualizers->CurrentDebugInfoText();
    if (info != nullptr) {
        ImGui::TextUnformatted(info);
        ImGui::Separator();
    }

    ImGui::TextDisabled("In-scene gizmos, drawn by the current visualizer");
    ImGuiPanelVisitor panel;
    state.debug->Visit(panel);

    ImGui::PopItemWidth();
    ImGui::End();
}

bool LoadDefaultSettings(PanelState& state) {
    if (state.visualizers == nullptr || state.camera == nullptr || state.post == nullptr) return false;
    return LoadSettings(kDefaultPath, [&](IParamVisitor& v) { VisitAll(state, v); });
}

} // namespace ui
