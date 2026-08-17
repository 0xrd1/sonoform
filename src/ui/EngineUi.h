#pragma once
#include <string>
#include <vector>
#include <functional>
#include "raylib.h"

// Facade over Dear ImGui + rlImGui. Exposes no ImGui type in its own
// interface -- imgui.h/rlImGui.h are included only by EngineUi.cpp and
// ImGuiPanel.cpp, both inside src/ui/ -- mirroring the containment
// src/core/GlCompat.h already applies to raylib's GLAD symbols, so no
// visualizer, particle, or gfx translation unit ever needs to know ImGui
// exists, and swapping the backend later touches only this directory.
class VisualizerManager;
class ShaderLibrary;
class ParticleRenderer;

namespace ui {

struct CameraSettings;
struct PostSettings;
struct PerformanceSettings;
struct DebugSettings;
struct AudioSettings;

// Everything the debug panel needs, as raw pointers into App's own fields
// rather than a copy-in/copy-back struct -- so a slider takes effect the
// same frame it moves, with no separate "apply" step. `shaders`/`renderer`
// are only used for the "Rebuild Systems" action (re-runs the current
// visualizer's Init(), which is what actually applies any NeedsRebuild
// field -- see ParamFlags::NeedsRebuild).
//
// Every side effect that touches a raylib Music object (pause/resume,
// next/prev/select-track) is a callback into App rather than a raw
// `Music*` this file pokes directly -- App's audio thread (see
// AudioThread.h) means every Music touch now needs to go through a
// mutex, and that synchronization concern has no business leaking into
// this UI-drawing code.
struct PanelState {
    VisualizerManager* visualizers = nullptr;
    ShaderLibrary* shaders = nullptr;
    ParticleRenderer* renderer = nullptr;

    CameraSettings* camera = nullptr;
    PostSettings* post = nullptr;
    PerformanceSettings* performance = nullptr; // not part of the preset round-trip -- see its own comment
    DebugSettings* debug = nullptr;             // drawn in its own stacked window -- see DrawDebugWindow
    AudioSettings* audio = nullptr;             // not part of the preset round-trip either -- see its own comment

    bool* showHud = nullptr;
    bool* paused = nullptr;
    // Invoked whenever the Paused checkbox's value actually changes (not
    // every frame) -- App wires this to its own mutex-guarded Pause/
    // ResumeMusicStream side effect.
    std::function<void(bool)> onPausedChanged;
    bool musicLoaded = false;

    // The playlist as display names (see App::TrackDisplayName), and
    // which entry is current -- App::TrackCount()/trackPaths_ own the
    // real data, this is just what the transport UI needs to draw itself.
    const std::vector<std::string>* trackNames = nullptr;
    int currentTrackIndex = -1;
    std::function<void()> onNextTrack;
    std::function<void()> onPrevTrack;
    std::function<void(int)> onSelectTrack;

    int particleCount = 0;
    const char* trackLabel = "";
};

// Creates the ImGui context and uploads its font atlas as a raylib texture,
// so a live GL context is required: call after InitWindow()/gl_compat::Init().
void Setup();

// Unloads the font atlas through raylib, so this must run BEFORE
// CloseWindow() -- the same "everything GPU-owning dies before the window
// does" rule App::Shutdown already applies to every other GPU resource.
// Created last in App::Init, so this is called first in App::Shutdown.
void Shutdown();

void BeginFrame(); // rlImGuiBegin() + ImGui::NewFrame() side effects; call inside BeginDrawing()/EndDrawing()
void EndFrame();   // rlImGuiEnd()

// True when ImGui owns the corresponding input device this frame. Backed
// by ImGuiIO flags computed inside BeginFrame(), which runs later in
// App::Draw() than HandleInput()/UpdateCameraOrbit() do -- so these read
// one frame stale. That's the standard ImGui gating pattern and is benign
// here: WantCaptureMouse goes true the frame the pointer starts *hovering*
// a panel, always at least one frame before a click or scroll on it. See
// App::UpdateCameraOrbit's orbiting_ latch for the one gesture (a drag
// begun exactly as the pointer crosses a panel) this lag could otherwise
// leak through on.
bool WantsKeyboard();
bool WantsMouse();

// Draws the whole settings window: FPS/particle-count strip, Camera and
// Post/Global groups, the current visualizer's own settings (via
// Visualizer::VisitSettings), a Rebuild Systems button for any
// NeedsRebuild field, and Save/Load preset controls under presets/.
void DrawDebugPanel(PanelState& state);

// Draws the separate "Debug View" window (in-scene gizmo toggles -- see
// ui::DebugSettings) stacked below DrawDebugPanel's window, right-anchored
// the same way. A no-op if state.debug is null.
void DrawDebugWindow(PanelState& state);

// Loads presets/default.ini into `state`'s bound Camera/Post/current-
// visualizer settings if that file exists. Called once by App::Init after
// visualizers are registered; a no-op on first run, when no default has
// been saved yet. Only state.visualizers/camera/post need to be set --
// the rest of PanelState is unused by this path.
bool LoadDefaultSettings(PanelState& state);

} // namespace ui
