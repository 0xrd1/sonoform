#pragma once
#include <string>
#include <vector>
#include <memory>
#include "raylib.h"
#include "AudioAnalyzer.h"
#include "AudioThread.h"
#include "VisualizerManager.h"
#include "ShaderLibrary.h"
#include "ParticleRenderer.h"
#include "PostProcess.h"
#include "EngineSettings.h"

class App {
public:
    // audioPathArg: optional path from the command line; empty means
    // "auto-detect" (scan assets/audio, else fall back to the procedural
    // demo track). Only takes effect once audio is actually enabled (see
    // ui::AudioSettings::enabled) -- Init() no longer touches the audio
    // device or loads anything up front.
    bool Init(const std::string& audioPathArg);
    void Run();
    void Shutdown();

private:
    void HandleInput(float dt);
    void Update(float dt);
    void Draw();
    void DrawHUD() const;
    void DrawUi(); // the ImGui settings panel -- see src/ui/EngineUi.h; non-const, unlike DrawHUD
    void UpdateCameraOrbit(float dt);

    // Populates trackPaths_ by scanning assets/audio (or, if audioPathArg_
    // names an existing file, that single file instead) -- called once,
    // lazily, the first time audio is enabled (see ApplyAudioSettings).
    void ScanTrackList();

    // Every entry in the playlist as a display name (a track's filename,
    // or "Procedural Demo Track" for the zero-files fallback) -- TrackCount()
    // is always >= 1 even with an empty assets/audio, so callers (the
    // transport UI, Next/Prev's wraparound) never need to special-case
    // "no tracks."
    int TrackCount() const { return trackPaths_.empty() ? 1 : static_cast<int>(trackPaths_.size()); }
    std::string TrackDisplayName(int index) const;

    // Loads and plays track `index` (clamped into [0, TrackCount()-1]):
    // detaches the analyzer from whatever's currently loaded, unloads it,
    // loads the requested track (or the procedural demo track, if
    // trackPaths_ is empty), re-attaches, plays. Every touch of music_ is
    // made under audioThread_.MusicMutex() -- see AudioThread's class
    // comment -- since the audio thread may already be running and
    // ticking UpdateMusicStream(music_) concurrently.
    void PlayTrack(int index);
    void NextTrack();
    void PrevTrack();

    // Applies audioSettings_ to actual device/playback state, but only on
    // an actual change (mirrors ApplyPerformanceSettings' own comment on
    // why): lazily InitAudioDevice()s and loads the track list the first
    // time `enabled` flips true, plays if nothing's loaded yet or
    // ResumeMusicStreams otherwise; PauseMusicStreams on false. Applies
    // `volume` via SetMusicVolume whenever it changes.
    void ApplyAudioSettings();

    // Applies perfSettings_ to the actual window/frame-pacing state, but
    // only on an actual change (compares against lastVsync_/lastTargetFps_)
    // -- SetWindowState/SetTargetFPS every frame regardless would be wasted
    // driver calls for two values that are almost always unchanged.
    void ApplyPerformanceSettings();

    Camera3D camera_{};
    ui::CameraSettings cameraSettings_;
    // Latches the right-drag orbit gesture on mouse-down rather than
    // testing IsMouseButtonDown every frame, so dragging *over* an ImGui
    // panel mid-orbit doesn't stall the camera -- see UpdateCameraOrbit's
    // comment. ImGui's WantCaptureMouse only gates starting a new drag.
    bool orbiting_ = false;

    Music music_{};
    bool musicLoaded_ = false;
    bool usingProceduralTrack_ = false;
    std::vector<unsigned char> proceduralTrackData_; // must outlive `music_`
    std::string trackLabel_;
    std::string audioPathArg_;             // command-line override, consumed lazily by ScanTrackList
    std::vector<std::string> trackPaths_;  // full paths, sorted by filename; empty -> procedural-only fallback (see TrackCount)
    std::vector<std::string> trackDisplayNames_; // parallel to trackPaths_ (or a single "Procedural Demo Track" entry) -- what the transport UI actually shows, see TrackDisplayName
    int currentTrackIndex_ = 0;
    bool trackListScanned_ = false;

    AudioAnalyzer analyzer_;
    AudioThread audioThread_;
    ui::AudioSettings audioSettings_;
    bool lastAudioEnabled_ = false;         // edge-triggered apply -- see ApplyAudioSettings
    float lastAudioVolume_ = audioSettings_.volume;
    bool audioDeviceInitialized_ = false;   // InitAudioDevice() called at most once per run -- see ApplyAudioSettings

    // GPU particle infrastructure, shared by every visualizer: ShaderLibrary
    // loads/caches compute + graphics shaders, ParticleRenderer owns the
    // one empty VAO + render program every GpuParticleSystem draws through.
    // ParticleRenderer's constructor issues real GL calls (compiles
    // shaders, allocates a VAO), so it cannot be a plain member with a
    // default initializer -- App's constructor runs in `main()` before
    // InitWindow(), when there is no GL context yet. It is constructed
    // inside Init() instead, once the context (and gl_compat::Init) is
    // confirmed ready. ShaderLibrary has no GL dependency at construction
    // (just an empty cache), so it's safe as a plain member.
    ShaderLibrary shaderLibrary_;
    std::unique_ptr<ParticleRenderer> particleRenderer_;
    std::unique_ptr<PostProcess> postProcess_; // same deferred-construction reasoning as particleRenderer_
    VisualizerManager visualizers_;

    // Small procedural soft-glow texture for the handful of non-GPU-particle
    // accent billboards a few visualizers still draw directly (see
    // RenderContext::accentTexture).
    Texture2D accentTexture_{};

    ui::PostSettings postSettings_;
    ui::PerformanceSettings perfSettings_;
    ui::DebugSettings debugSettings_;
    // Last-applied values, so ApplyPerformanceSettings only calls into
    // raylib/GLFW when perfSettings_ actually changed since last frame.
    bool lastVsync_ = perfSettings_.vsync;
    int lastTargetFps_ = perfSettings_.targetFps;
    bool paused_ = false;
    float elapsedTime_ = 0.0f;
    bool showHud_ = true;
    bool showSettingsPanel_ = true; // ImGui panel visibility, toggled by F1 -- independent of showHud_ ('H')

    // Throttled HUD particle-count readback (see GpuParticleSystem::AliveCountApprox).
    int hudParticleCount_ = 0;
    float hudReadbackTimer_ = 0.0f;
};
