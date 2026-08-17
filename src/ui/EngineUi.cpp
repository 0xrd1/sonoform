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

// Repo-root, not "settings/" -- presets are tuned looks worth keeping and
// sharing, not machine-local runtime state (contrast PerformanceSettings/
// AudioSettings just above, which stay out of the round-trip entirely for
// the opposite reason).
//
// Resolved from GetApplicationDirectory() (the executable's own directory)
// rather than a CWD-relative literal: the old "settings/presets" path had
// exactly this problem (documented in the pre-existing settings/ .gitignore
// comment) -- CWD is build/ when the built exe is launched directly, but
// the repo root under the VS debugger (VS_DEBUGGER_WORKING_DIRECTORY, see
// CMakeLists.txt), so a CWD-relative path silently split into two
// divergent preset directories depending on how you ran it. The build
// output always sits one level under the repo root (CMakeLists.txt's
// add_executable target lands at <root>/build/<exe>), so
// GetApplicationDirectory() + "../presets" reaches the one repo-tracked
// presets/ directory regardless of launch method. Cached in a static local
// -- ListPresets() re-scans this path every frame the panel is open (see
// its own call site below), so this avoids a GetApplicationDirectory() call
// on every one of those frames too.
const std::string& PresetsDir() {
    static const std::string dir = std::string(GetApplicationDirectory()) + "../presets";
    return dir;
}
const std::string& DefaultPresetPath() {
    static const std::string path = PresetsDir() + "/default.ini";
    return path;
}

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

// A full textual snapshot of everything VisitAll covers *except Camera* --
// literally what Save As would write to disk (minus that one group), via
// the same SettingsWriter the actual save path uses (see SettingsIO.h),
// just kept in memory instead. Comparing this string against what was
// cached at the last successful Load/Save is the "active preset" badge's
// whole mechanism (see s_loadedSnapshot below): cheap and correct without
// any new hook on IParamVisitor -- if a value differs from what's on disk,
// the round-trip text differs too.
//
// Camera is deliberately excluded here (though it's still fully part of
// VisitAll/the real save-load round-trip -- Save As still captures it):
// CameraSettings::yaw advances every single frame while Auto-Rotate is on
// (the default), so including it would flip this badge to "Custom" within
// one frame of loading *any* preset, regardless of whether the user
// touched anything -- camera position is live view state that happens to
// be saved as a starting point, not part of what makes a preset "the
// same look".
std::string SnapshotOf(PanelState& state) {
    SettingsWriter writer;
    writer.BeginGroup("Post");
    state.post->Visit(writer);
    writer.EndGroup();
    writer.BeginGroup(state.visualizers->CurrentName());
    state.visualizers->VisitCurrentSettings(writer);
    writer.EndGroup();
    return writer.Text();
}

// "Which preset (if any) matches every currently-visited value" -- module-
// level statics, same pattern kPresetsDir/nameBuf already use in this file
// (PanelState is rebuilt fresh every frame by App::DrawUi, so this can't
// live there). Updated by MarkLoaded() below on every successful Load/
// Load Default/Save As/Save As Default; DrawDebugPanel seeds a baseline
// lazily on its first call if nothing else has by then (a totally fresh
// run with no default.ini yet).
std::string s_loadedPresetName;
std::string s_loadedSnapshot;
bool s_presetStateInitialized = false;

void MarkLoaded(PanelState& state, const std::string& name) {
    s_loadedPresetName = name;
    s_loadedSnapshot = SnapshotOf(state);
    s_presetStateInitialized = true;
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

    // ImGuiPanelVisitor's widgets (see ImGuiPanel.cpp) check
    // IsItemHovered(ImGuiHoveredFlags_DelayNormal) on their label text
    // specifically, not the slider/checkbox/etc. control itself -- this is
    // the delay that flag waits out before returning true. Set once, here,
    // rather than left at ImGui's own default (~0.4s): a full second is
    // what actually reads as "hovering to read a tooltip", not "hovering
    // in passing on the way to the next field."
    ImGui::GetStyle().HoverDelayNormal = 1.0f;
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

    // "Which preset is this" -- see SnapshotOf()/s_loadedSnapshot's
    // comment. Recomputed every frame (a full ~150-field VisitAll pass,
    // already declared "free next to 320k particles" elsewhere in this
    // codebase); lazily seeds a baseline on the very first call so the
    // compiled-in hardcoded defaults don't show as "Custom" for no reason
    // on a totally fresh run with no default.ini (LoadDefaultSettings, if
    // it succeeded, already seeded this before the first DrawDebugPanel
    // call -- see App::Init).
    std::string currentSnapshot = SnapshotOf(state);
    if (!s_presetStateInitialized) {
        s_loadedPresetName = "Default";
        s_loadedSnapshot = currentSnapshot;
        s_presetStateInitialized = true;
    }
    ImGui::Text("Preset: %s", currentSnapshot == s_loadedSnapshot ? s_loadedPresetName.c_str() : "Custom");

    ImGui::TextDisabled("Ctrl+Click or double-click any slider to type an exact value");

    // App-wide, not visualizer-specific (a machine characteristic, not
    // part of "the look" -- see PerformanceSettings' own comment on why
    // it's excluded from VisitAll/the preset round-trip below), so it's
    // drawn up here alongside Camera/Post's conceptual tier rather than
    // nested after the current visualizer's own settings.
    if (state.performance != nullptr) {
        ImGui::Separator();
        ImGuiPanelVisitor perfPanel;
        perfPanel.BeginGroup("Performance");
        state.performance->Visit(perfPanel);
        perfPanel.EndGroup();
    }

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
                "(lighting, turbulence, kick impulses). On by default -- this is a music "
                "visualizer.");
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
        std::string path = PresetsDir() + "/" + nameBuf + ".ini";
        if (SaveSettings(path, [&](IParamVisitor& v) { VisitAll(state, v); })) MarkLoaded(state, nameBuf);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As Default")) {
        if (SaveSettings(DefaultPresetPath(), [&](IParamVisitor& v) { VisitAll(state, v); })) MarkLoaded(state, "Default");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Default")) {
        if (LoadSettings(DefaultPresetPath(), [&](IParamVisitor& v) { VisitAll(state, v); })) MarkLoaded(state, "Default");
    }

    for (const std::string& name : ListPresets(PresetsDir())) {
        ImGui::PushID(name.c_str());
        ImGui::BulletText("%s", name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Load")) {
            if (LoadSettings(PresetsDir() + "/" + name + ".ini", [&](IParamVisitor& v) { VisitAll(state, v); })) MarkLoaded(state, name);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            DeletePreset(PresetsDir(), name);
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
    bool loaded = LoadSettings(DefaultPresetPath(), [&](IParamVisitor& v) { VisitAll(state, v); });
    // Seeds the "active preset" badge (see MarkLoaded's comment) before
    // DrawDebugPanel's first call, so a successful startup load shows
    // "Default" immediately instead of one frame of "Custom".
    if (loaded) MarkLoaded(state, "Default");
    return loaded;
}

} // namespace ui
