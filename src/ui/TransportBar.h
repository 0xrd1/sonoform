#pragma once
#include <functional>
#include "raylib.h"

// The application's own always-visible playback transport -- prev/play-
// pause/next, a scrubbable timeline, and a volume slider -- drawn directly
// with raylib primitives, not ImGui (see EngineUi.h's containment comment:
// imgui.h stays confined to EngineUi.cpp/ImGuiPanel.cpp, and this bar is
// meant to read as part of the application, not the dev panel). This is
// also the project's first mouse-driven UI outside ImGui: before this,
// nothing in src/ read GetMousePosition or hit-tested a Rectangle (only
// right-drag orbit and wheel zoom existed) -- the minimal hit-testing this
// needs lives in TransportBar.cpp, deliberately local rather than a
// general widget framework.
namespace ui {

// Mirrors PanelState's own discipline (see EngineUi.h): no Music* crosses
// into this layer. Every side effect that would touch the Music object is
// a callback into App, which alone knows the AudioThread mutex discipline
// (see AudioThread.h). App builds a fresh TransportState each frame, same
// pattern DrawUi() already uses for PanelState.
struct TransportState {
    bool visible = true;
    bool enabled = false;      // audio on AND a stream loaded -- gates every control
    bool paused = false;
    float timePlayed = 0.0f;   // seconds; App's single per-frame cached read
    float duration = 0.0f;     // seconds; 0 while unknown (e.g. right after a track switch)
    const char* trackName = "";
    int trackIndex = 0;        // 0-based
    int trackCount = 1;
    float volume = 0.6f;

    std::function<void()> onPrev;
    std::function<void()> onNext;
    std::function<void(bool)> onPausedChanged;
    std::function<void(float)> onSeek;          // seconds, absolute, clamped to [0, duration]
    std::function<void(float)> onVolumeChanged; // [0,1]
};

// Bottom-anchored, GetScreenWidth()-relative so it survives resize. Skips
// all input handling (but still draws, dimmed) while ui::WantsMouse() is
// true, so it never fights an ImGui panel drawn on top of it -- and never
// conflicts with the camera either: orbit is right-drag/wheel, this bar is
// left-click only. No-op if !state.visible.
void DrawTransportBar(TransportState& state);

} // namespace ui
