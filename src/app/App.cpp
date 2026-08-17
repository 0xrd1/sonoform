#include "App.h"
#include "raymath.h"
#include "rlgl.h"
#include "DemoTrack.h"
#include "GlCompat.h"
#include "EngineUi.h"
#include "TransportBar.h"

#include "SpectrumRingVisualizer.h"
#include "GalaxyVisualizer.h"
#include "FireworksVisualizer.h"
#include "TunnelVisualizer.h"
#include "NeonFogVisualizer.h"

#include <filesystem>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <mutex>

namespace fs = std::filesystem;

namespace {
// Every format raylib 5.5's LoadMusicStream can actually stream (miniaudio/
// stb_vorbis/dr_mp3/dr_flac for wav/ogg/mp3/flac, jar_xm/jar_mod for
// xm/mod, plus raylib's own qoa) -- not an arbitrary subset, so anything
// droppable into assets/audio that raylib can play gets picked up.
bool HasSupportedAudioExt(const fs::path& p) {
    std::string ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".flac" ||
           ext == ".xm" || ext == ".mod" || ext == ".qoa";
}
}

bool App::Init(const std::string& audioPathArg) {
    // FLAG_VSYNC_HINT here is just an initial window-creation hint --
    // Run()'s very first ApplyPerformanceSettings() call corrects both
    // vsync and target FPS to whatever ui::PerformanceSettings' actual
    // defaults are (vsync off, uncapped) before the first frame draws, so
    // no SetTargetFPS() placeholder is needed here -- see that function's
    // comment.
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1920, 1080, "Particle Audio Visualizer");

    // The requested 1920x1080 is a target aspect ratio, not a guarantee --
    // on a 1080p (or smaller) desktop that would exactly fill or exceed the
    // work area once the title bar/taskbar are accounted for. Clamp to 90%
    // of the current monitor's size (keeping the 16:9 aspect) and center
    // the window so the whole scene is always visible without the user
    // having to manually resize/reposition on first launch.
    {
        int monitor = GetCurrentMonitor();
        int maxW = static_cast<int>(GetMonitorWidth(monitor) * 0.9f);
        int maxH = static_cast<int>(GetMonitorHeight(monitor) * 0.9f);
        int w = 1920, h = 1080;
        if (w > maxW || h > maxH) {
            float scale = std::min(static_cast<float>(maxW) / w, static_cast<float>(maxH) / h);
            w = static_cast<int>(w * scale);
            h = static_cast<int>(h * scale);
        }
        SetWindowSize(w, h);
        SetWindowPosition((GetMonitorWidth(monitor) - w) / 2, (GetMonitorHeight(monitor) - h) / 2);
    }

    // This engine simulates and renders all particles on the GPU via
    // compute shaders and SSBOs; there is no CPU particle fallback. Fail
    // fast with a clear message rather than silently rendering nothing.
    if (!gl_compat::Init()) {
        CloseWindow();
        return false;
    }

    // Audio device/track loading is deferred entirely to ApplyAudioSettings,
    // the first time ui::AudioSettings::enabled actually flips true (see
    // its own comment) -- a cold launch with audio off (the default) stays
    // exactly as fast/silent as a build with no audio support at all.
    audioPathArg_ = audioPathArg;

    // ParticleRenderer's constructor issues real GL calls (compiles the
    // render shader, allocates a VAO), so it can only be constructed here,
    // after InitWindow()/gl_compat::Init() have confirmed a live GL 4.3
    // context exists -- see the comment on App::particleRenderer_.
    particleRenderer_ = std::make_unique<ParticleRenderer>(shaderLibrary_);
    postProcess_ = std::make_unique<PostProcess>(shaderLibrary_, GetScreenWidth(), GetScreenHeight());

    // Small soft-glow sprite for the few non-GPU-particle accent
    // billboards visualizers still draw directly (see RenderContext);
    // generated procedurally so the project ships with zero image assets.
    Image glow = GenImageGradientRadial(64, 64, 0.15f, WHITE, Color{ 255, 255, 255, 0 });
    accentTexture_ = LoadTextureFromImage(glow);
    UnloadImage(glow);

    // Only Neon Fog while its particle/shape-morph pipeline is the sole
    // visual focus -- audio is back (see ApplyAudioSettings), but the
    // other four visualizers stay disabled for now. Restore these Add()
    // calls once that work lands.
    visualizers_.Add(std::make_unique<NeonFogVisualizer>(), shaderLibrary_, *particleRenderer_);
    // visualizers_.Add(std::make_unique<SpectrumRingVisualizer>(), shaderLibrary_, *particleRenderer_);
    // visualizers_.Add(std::make_unique<GalaxyVisualizer>(), shaderLibrary_, *particleRenderer_);
    // visualizers_.Add(std::make_unique<FireworksVisualizer>(), shaderLibrary_, *particleRenderer_);
    // visualizers_.Add(std::make_unique<TunnelVisualizer>(), shaderLibrary_, *particleRenderer_);

    camera_.up = { 0, 1, 0 };
    camera_.fovy = cameraSettings_.fovy;
    camera_.projection = CAMERA_PERSPECTIVE;
    camera_.target = { 0, 2.0f, 0 };
    // cameraSettings_'s in-class defaults (pitch 0.55, distance 20) already
    // hold the intended resting view -- steeper than a typical orbit-camera
    // default because at a shallow pitch the VoidFloor (see gfx/VoidFloor.h)
    // recedes toward the horizon almost immediately and mostly falls below
    // the frame. See ui::CameraSettings for the tunable values themselves.

    // After gl_compat::Init() because rlImGuiSetup uploads the font atlas
    // as a raylib Texture2D and needs a live GL context, and last in Init()
    // so the earlier failure paths above never leave an orphaned ImGui
    // context, and Shutdown() can tear down in exact LIFO order.
    ui::Setup();

    // Restore a previously saved look, if any -- a no-op on first run, when
    // presets/default.ini doesn't exist yet (see ui::LoadDefaultSettings).
    // Must come after visualizers_.Add() above, since it reads/writes the
    // current visualizer's settings via VisitSettings.
    {
        ui::PanelState startupState;
        startupState.visualizers = &visualizers_;
        startupState.camera = &cameraSettings_;
        startupState.post = &postSettings_;
        // If a default.ini existed and was actually loaded, the GPU state
        // built by visualizers_.Add() above (using pre-load hardcoded
        // defaults) may now disagree with the just-loaded settings for any
        // NeedsRebuild field (capacity, grid resolution, ...) -- rebuild
        // once so frame one already matches what the panel displays,
        // instead of silently drifting until the user notices and clicks
        // Rebuild Systems themselves. RebuildCurrent is idempotent/cheap to
        // call speculatively (see its own doc comment); a no-op first run
        // (no default.ini yet) skips this entirely.
        if (ui::LoadDefaultSettings(startupState)) {
            visualizers_.RebuildCurrent(shaderLibrary_, *particleRenderer_);
        }
    }

    return true;
}

void App::ScanTrackList() {
    trackPaths_.clear();

    // A command-line-specified file overrides the directory scan entirely
    // -- a single-entry "playlist" of just that file, same as the old
    // LoadAudio's precedence.
    if (!audioPathArg_.empty() && FileExists(audioPathArg_.c_str())) {
        trackPaths_.push_back(audioPathArg_);
    } else {
        fs::path audioDir = fs::path("assets") / "audio";
        if (fs::exists(audioDir) && fs::is_directory(audioDir)) {
            for (const auto& entry : fs::directory_iterator(audioDir)) {
                if (entry.is_regular_file() && HasSupportedAudioExt(entry.path())) {
                    trackPaths_.push_back(entry.path().string());
                }
            }
        }
        std::sort(trackPaths_.begin(), trackPaths_.end(), [](const std::string& a, const std::string& b) {
            return fs::path(a).filename().string() < fs::path(b).filename().string();
        });
        // Empty is a valid outcome (nothing dropped into assets/audio yet)
        // -- TrackCount()/PlayTrack both fall back to the procedural demo
        // track as a one-entry playlist in that case, not an error.
    }

    trackDisplayNames_.clear();
    if (trackPaths_.empty()) {
        trackDisplayNames_.push_back("Procedural Demo Track");
    } else {
        for (const auto& path : trackPaths_) trackDisplayNames_.push_back(fs::path(path).filename().string());
    }
}

std::string App::TrackDisplayName(int index) const {
    if (index < 0 || index >= static_cast<int>(trackDisplayNames_.size())) return "?";
    return trackDisplayNames_[static_cast<size_t>(index)];
}

void App::PlayTrack(int index) {
    int count = TrackCount();
    if (count <= 0) return; // unreachable -- TrackCount() is always >= 1 -- but no reason to trust that blindly here
    index = ((index % count) + count) % count;
    currentTrackIndex_ = index;

    // Every touch of music_ below -- detach, unload, load, attach, play --
    // happens under the same lock the audio thread takes for its periodic
    // UpdateMusicStream (see AudioThread's class comment). Held for this
    // whole sequence (including file I/O) rather than per-call: the
    // audio thread must never see music_ mid-swap.
    std::lock_guard<std::mutex> lock(audioThread_.MusicMutex());

    // Detach while the OLD stream (if any) is still valid, *before*
    // unloading it -- AttachTo() below would otherwise run its own
    // internal Detach() against whatever music_.stream holds *after*
    // reassignment (the new stream, never actually attached), not the one
    // that really was.
    analyzer_.Detach();
    if (musicLoaded_) {
        StopMusicStream(music_);
        UnloadMusicStream(music_);
        musicLoaded_ = false;
    }

    if (trackPaths_.empty()) {
        proceduralTrackData_ = GenerateDemoTrackWav(90.0f, 44100);
        music_ = LoadMusicStreamFromMemory(".wav", proceduralTrackData_.data(),
                                            static_cast<int>(proceduralTrackData_.size()));
        trackLabel_ = "Procedural Demo Track (drop mp3/wav/ogg/flac/xm/mod/qoa into assets/audio to use your own)";
        usingProceduralTrack_ = true;
    } else {
        const std::string& path = trackPaths_[static_cast<size_t>(index)];
        music_ = LoadMusicStream(path.c_str());
        trackLabel_ = fs::path(path).filename().string();
        usingProceduralTrack_ = false;
    }

    if (!IsMusicValid(music_)) {
        musicLoaded_ = false;
        return;
    }

    // The procedural fallback loops seamlessly in place (there's nothing
    // else to advance to); a real playlist instead lets Run()'s
    // end-of-track check advance to the next entry (see the auto-advance
    // block there), which needs looping off to ever actually fire.
    music_.looping = usingProceduralTrack_;
    SetMusicVolume(music_, audioSettings_.volume);
    PlayMusicStream(music_);
    analyzer_.AttachTo(music_);
    musicLoaded_ = true;
}

void App::NextTrack() { PlayTrack(currentTrackIndex_ + 1); }
void App::PrevTrack() { PlayTrack(currentTrackIndex_ - 1); }

void App::SeekTo(float seconds) {
    if (!musicLoaded_) return;
    float target = Clamp(seconds, 0.0f, cachedDuration_);
    std::lock_guard<std::mutex> lock(audioThread_.MusicMutex());
    SeekMusicStream(music_, target);
}

void App::ApplyAudioSettings() {
    if (audioSettings_.enabled != lastAudioEnabled_) {
        if (audioSettings_.enabled) {
            if (!audioDeviceInitialized_) {
                InitAudioDevice();
                audioDeviceInitialized_ = true;
            }
            if (!trackListScanned_) {
                ScanTrackList();
                trackListScanned_ = true;
            }
            if (!musicLoaded_) {
                PlayTrack(currentTrackIndex_);
                // Hand the now-valid, loaded stream to the audio thread so
                // it keeps ticking (playback + analysis) through a window
                // resize/move -- see AudioThread's class comment. Start()
                // is a no-op on any call after the first, so this is safe
                // to reach every time audio is (re-)enabled, not just once.
                if (musicLoaded_) audioThread_.Start(music_, analyzer_);
            } else if (!paused_) {
                std::lock_guard<std::mutex> lock(audioThread_.MusicMutex());
                ResumeMusicStream(music_);
            }
        } else if (musicLoaded_) {
            std::lock_guard<std::mutex> lock(audioThread_.MusicMutex());
            PauseMusicStream(music_);
        }
        lastAudioEnabled_ = audioSettings_.enabled;
    }

    if (audioSettings_.volume != lastAudioVolume_) {
        if (musicLoaded_) {
            std::lock_guard<std::mutex> lock(audioThread_.MusicMutex());
            SetMusicVolume(music_, audioSettings_.volume);
        }
        lastAudioVolume_ = audioSettings_.volume;
    }
}

void App::ApplyPerformanceSettings() {
    // `|| !perfSettingsApplied_` on both conditions: without it, the very
    // first call here (perfSettings_ still at its just-constructed
    // defaults) sees perfSettings_.vsync == lastVsync_ and
    // perfSettings_.targetFps == lastTargetFps_ -- both member-initialized
    // from perfSettings_'s own defaults -- so neither branch ever fires,
    // and the SetTargetFPS(60)/default-vsync state InitWindow() set up
    // silently never gets corrected to the real (uncapped, no-vsync)
    // defaults until the user manually moves a slider once.
    if (perfSettings_.vsync != lastVsync_ || !perfSettingsApplied_) {
        // raylib forwards FLAG_VSYNC_HINT's SetWindowState/ClearWindowState
        // to glfwSwapInterval(1)/(0) at runtime on the desktop/GLFW backend
        // this project targets -- no window recreation needed.
        if (perfSettings_.vsync) SetWindowState(FLAG_VSYNC_HINT);
        else ClearWindowState(FLAG_VSYNC_HINT);
        lastVsync_ = perfSettings_.vsync;
    }
    if (perfSettings_.targetFps != lastTargetFps_ || !perfSettingsApplied_) {
        SetTargetFPS(perfSettings_.targetFps); // raylib treats 0 as uncapped
        lastTargetFps_ = perfSettings_.targetFps;
    }
    perfSettingsApplied_ = true;
}

void App::Run() {
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        ApplyPerformanceSettings();
        ApplyAudioSettings();

        // UpdateMusicStream/AudioAnalyzer::Update are no longer called
        // here -- they run on audioThread_'s own dedicated cadence (see
        // AudioThread's class comment) precisely so they keep going even
        // while this loop is blocked inside an interactive window resize/
        // move on Windows.

        // Single time-played/duration read per frame -- feeds both the
        // auto-advance check right below and the transport bar (see
        // DrawUi()'s TransportState wiring), replacing what used to be a
        // second, separate read inside the auto-advance block alone.
        if (musicLoaded_) {
            std::lock_guard<std::mutex> lock(audioThread_.MusicMutex());
            cachedTimePlayed_ = GetMusicTimePlayed(music_);
            cachedDuration_ = GetMusicTimeLength(music_);
        } else {
            cachedTimePlayed_ = 0.0f;
            cachedDuration_ = 0.0f;
        }

        // A real (non-procedural) playlist advances on its own once the
        // current track finishes -- see PlayTrack's looping comment for
        // why the procedural fallback doesn't need this (it loops in
        // place).
        if (audioSettings_.enabled && musicLoaded_ && !usingProceduralTrack_ && !paused_) {
            // `played > 1.0f` guards against a real, observed false-
            // positive right after a fresh PlayTrack(): immediately
            // after LoadMusicStream/PlayMusicStream, before the audio
            // thread's next UpdateMusicStream tick has run even once,
            // GetMusicTimeLength/GetMusicTimePlayed can transiently
            // report a length near 0 (decoder duration not fully
            // settled yet) -- without this guard that reads as
            // "already finished" and immediately advances again,
            // which is exactly what was observed: tracks skipping
            // every couple of seconds instead of playing to the end.
            // A real track is always playing for well over a second
            // before it can legitimately end, so this costs nothing
            // for genuine end-of-track detection.
            bool trackEnded = cachedDuration_ > 0.5f && cachedTimePlayed_ > 1.0f
                             && cachedTimePlayed_ >= cachedDuration_ - 0.05f;
            if (trackEnded) NextTrack();
        }

        if (IsWindowResized()) {
            postProcess_->Resize(GetScreenWidth(), GetScreenHeight());
        }

        HandleInput(dt);
        UpdateCameraOrbit(dt);

        if (!paused_) {
            elapsedTime_ += dt;
            FrameContext frame{ dt, elapsedTime_, analyzer_, postSettings_.reactivityIntensity };
            visualizers_.Update(frame);
        }

        // Throttled GPU->CPU readback for the HUD particle counter --
        // see GpuParticleSystem::AliveCountApprox; querying every frame
        // would risk a pipeline stall for no visible benefit.
        hudReadbackTimer_ += dt;
        if (hudReadbackTimer_ >= 0.25f) {
            hudReadbackTimer_ = 0.0f;
            hudParticleCount_ = visualizers_.CurrentParticleCount();
        }

        Draw();
    }
}

void App::HandleInput(float dt) {
    // Deliberately outside the WantsKeyboard() gate below: F1 must still
    // close the panel even while an ImGui field has focus (WantCaptureKeyboard
    // would otherwise be true and swallow it along with everything else).
    if (IsKeyPressed(KEY_F1)) showSettingsPanel_ = !showSettingsPanel_;

    // rlImGuiBegin() only *copies* raylib's input state into ImGuiIO (see
    // EngineUi.cpp) -- it doesn't consume it, so IsKeyPressed keeps firing
    // underneath a focused ImGui widget without this gate. See the plan's
    // "Input arbitration" section for why this has to be a whole-function
    // early-out here but only a partial gate in UpdateCameraOrbit.
    if (ui::WantsKeyboard()) return;

    if (IsKeyPressed(KEY_ONE)) visualizers_.SetIndex(0);
    if (IsKeyPressed(KEY_TWO)) visualizers_.SetIndex(1);
    if (IsKeyPressed(KEY_THREE)) visualizers_.SetIndex(2);
    if (IsKeyPressed(KEY_FOUR)) visualizers_.SetIndex(3);
    if (IsKeyPressed(KEY_FIVE)) visualizers_.SetIndex(4);
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_TAB)) visualizers_.Next();
    if (IsKeyPressed(KEY_LEFT)) visualizers_.Prev();
    if (IsKeyPressed(KEY_S)) visualizers_.SecondaryActionOnCurrent();
    if (IsKeyPressed(KEY_M)) visualizers_.TertiaryActionOnCurrent();
    if (IsKeyDown(KEY_MINUS)) visualizers_.AdjustPrimaryOnCurrent(-dt * 0.6f);
    if (IsKeyDown(KEY_EQUAL)) visualizers_.AdjustPrimaryOnCurrent(dt * 0.6f);

    if (IsKeyPressed(KEY_SPACE)) {
        paused_ = !paused_;
        if (musicLoaded_) {
            std::lock_guard<std::mutex> lock(audioThread_.MusicMutex());
            if (paused_) PauseMusicStream(music_);
            else ResumeMusicStream(music_);
        }
    }

    if (IsKeyPressed(KEY_C)) cameraSettings_.autoRotate = !cameraSettings_.autoRotate;
    if (IsKeyPressed(KEY_R)) {
        ui::CameraSettings d; // resets orientation/distance only -- rates/limits/fov are left as tuned
        cameraSettings_.yaw = d.yaw;
        cameraSettings_.pitch = d.pitch;
        cameraSettings_.distance = d.distance;
    }
    if (IsKeyPressed(KEY_F)) ToggleFullscreen();
    if (IsKeyPressed(KEY_H)) showHud_ = !showHud_;
    // Preset-tuning aid: a quick way to capture a look without an external
    // grabber, saved into the working directory with a millisecond
    // timestamp so repeated presses never collide on one filename.
    if (IsKeyPressed(KEY_F12)) TakeScreenshot(TextFormat("screenshot_%d.png", static_cast<int>(GetTime() * 1000.0)));

    if (IsKeyDown(KEY_LEFT_BRACKET)) postSettings_.reactivityIntensity -= dt * 0.6f;
    if (IsKeyDown(KEY_RIGHT_BRACKET)) postSettings_.reactivityIntensity += dt * 0.6f;
    postSettings_.reactivityIntensity = Clamp(postSettings_.reactivityIntensity, 0.25f, 3.0f);
}

void App::UpdateCameraOrbit(float dt) {
    camera_.fovy = cameraSettings_.fovy;

    if (cameraSettings_.autoRotate) cameraSettings_.yaw += dt * cameraSettings_.autoRotateSpeed;

    // Only the two input-driven branches below are gated on WantsMouse(),
    // never this whole function: an early return here would freeze
    // auto-rotate and skip the camera_.position recompute at the bottom
    // whenever the pointer happens to cross a panel.
    const bool uiOwnsMouse = ui::WantsMouse();

    // The orbit drag is latched on press rather than re-tested every frame
    // (orbiting_, see App.h), so dragging *over* a panel mid-orbit doesn't
    // stall the camera: ImGui reports WantCaptureMouse for anything
    // hovering a window, with no notion that this drag already started.
    if (!uiOwnsMouse && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) orbiting_ = true;
    if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) orbiting_ = false;

    if (orbiting_) {
        Vector2 d = GetMouseDelta();
        cameraSettings_.yaw -= d.x * cameraSettings_.dragSensitivity;
        cameraSettings_.pitch = Clamp(cameraSettings_.pitch + d.y * cameraSettings_.dragSensitivity,
                                       -cameraSettings_.pitchLimit, cameraSettings_.pitchLimit);
    }

    // Wheel zoom is a per-event action with no drag to latch, so a plain
    // gate is correct here -- and necessary, or scrolling an ImGui list
    // would also dolly the camera.
    if (!uiOwnsMouse) cameraSettings_.distance -= GetMouseWheelMove() * cameraSettings_.zoomSpeed;
    cameraSettings_.distance = Clamp(cameraSettings_.distance, cameraSettings_.minDistance, cameraSettings_.maxDistance);

    camera_.position = {
        cameraSettings_.distance * cosf(cameraSettings_.pitch) * sinf(cameraSettings_.yaw),
        cameraSettings_.distance * sinf(cameraSettings_.pitch) + 3.0f,
        cameraSettings_.distance * cosf(cameraSettings_.pitch) * cosf(cameraSettings_.yaw)
    };
}

void App::Draw() {
    // Any off-screen render-to-texture work a visualizer needs on its own
    // framebuffer (e.g. NeonFogVisualizer's top-down particle shadow map)
    // must happen before PostProcess::BeginScene() below -- see
    // Visualizer::PreDraw's comment on why raylib's BeginTextureMode/
    // EndTextureMode can't nest inside it once the main scene target is
    // already bound.
    visualizers_.PreDraw();

    // The 3D scene renders into an offscreen target first (PostProcess::
    // BeginScene/EndScene) so bloom can extract and blur its bright areas
    // before the final composite goes to the backbuffer -- see
    // gfx/PostProcess.h.
    postProcess_->BeginScene();

    BeginMode3D(camera_);

    // The genuine view-projection matrix raylib just set up for this
    // frame's camera, reused by every GPU particle draw instead of each
    // visualizer re-deriving its own (and risking it drifting out of sync
    // with whatever BeginMode3D actually configured).
    Matrix viewProj = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());

    Vector3 forward = Vector3Normalize(Vector3Subtract(camera_.target, camera_.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera_.up));
    Vector3 up = Vector3CrossProduct(right, forward);

    RenderContext ctx{ camera_, viewProj, right, up, accentTexture_, &debugSettings_ };

    // Blend mode is each visualizer's own responsibility now, not a
    // global wrap: additive suits self-luminous sparks/energy (bars,
    // fireworks, orbiting stars, lightning), but a dense fog cloud lit by
    // external lights needs standard alpha blending -- additive would
    // sum every overlapping sprite along a view ray without bound and
    // blow out to solid white well before it reads as "dense fog" (see
    // NeonFogVisualizer::Draw).
    visualizers_.Draw(ctx);

    EndMode3D();

    postProcess_->EndScene();

    BeginDrawing();
    ClearBackground(BLACK);
    postProcess_->Composite(postSettings_.bloomThreshold, postSettings_.bloomIntensity);

    if (showHud_) DrawHUD();

    // The application's own playback transport -- always drawn (not gated
    // behind showHud_/showSettingsPanel_, both of which the user may well
    // have hidden while still wanting to control playback) -- see
    // ui/TransportBar.h. Built fresh each frame, same pattern DrawUi()
    // already uses for ui::PanelState; every side effect that would touch
    // Music is a callback into App, mirroring PanelState's own "no Music*
    // crosses into the UI layer" rule (see EngineUi.h).
    {
        ui::TransportState transport;
        transport.enabled = audioSettings_.enabled && musicLoaded_;
        transport.paused = paused_;
        transport.timePlayed = cachedTimePlayed_;
        transport.duration = cachedDuration_;
        transport.trackName = trackLabel_.c_str();
        transport.trackIndex = currentTrackIndex_;
        transport.trackCount = TrackCount();
        transport.volume = audioSettings_.volume;
        transport.onPrev = [this] { PrevTrack(); };
        transport.onNext = [this] { NextTrack(); };
        transport.onPausedChanged = [this](bool p) {
            paused_ = p;
            if (!musicLoaded_) return;
            std::lock_guard<std::mutex> lock(audioThread_.MusicMutex());
            if (paused_) PauseMusicStream(music_);
            else ResumeMusicStream(music_);
        };
        transport.onSeek = [this](float seconds) { SeekTo(seconds); };
        transport.onVolumeChanged = [this](float v) { audioSettings_.volume = v; };
        ui::DrawTransportBar(transport);
    }

    // Drawn last, outside PostProcess::BeginScene/EndScene: bloom only ever
    // reads sceneTarget_, which received everything drawn between those two
    // calls above, so the panel is neither bright-pass extracted nor
    // blurred -- crisp text/widgets over a bloomed scene. After DrawHUD()
    // so panels sit on top of the plain-text HUD rather than under it.
    DrawUi();

    EndDrawing();
}

// Deliberately minimal -- FPS and Particles are the two numbers worth
// seeing with the ImGui panel closed (F1); everything else this used to
// duplicate (visualizer index, Intensity, Track, PAUSED) now has a better
// home: ImGui's Engine Settings panel or the always-on transport bar (see
// ui::DrawTransportBar) -- see the class comment on why keeping both was
// redundant, and why the old 3-line keybind block had gone stale (it
// advertised switching between five visualizers when only one is
// registered -- see Init()'s comment).
void App::DrawHUD() const {
    const int pad = 12;
    int y = pad;

    DrawText(TextFormat("FPS: %d", GetFPS()), pad, y, 18, GREEN); y += 22;
    DrawText(TextFormat("Particles: %d", hudParticleCount_), pad, y, 18, RAYWHITE); y += 22;
    DrawText("F1: settings panel   H: toggle HUD   F12: screenshot   Esc: quit", pad, y, 14, Fade(RAYWHITE, 0.6f));
}

void App::DrawUi() {
    // BeginFrame()/EndFrame() run every frame regardless of
    // showSettingsPanel_, not just when the window is actually drawn:
    // skipping them while hidden would freeze ui::WantsKeyboard()/
    // WantsMouse() at whatever they last were the instant before F1 hid the
    // panel, instead of correctly settling back to false once nothing is
    // hovered. With no window open, rlImGui simply has nothing to render.
    ui::BeginFrame();

    if (showSettingsPanel_) {
        ui::PanelState state;
        state.visualizers = &visualizers_;
        state.shaders = &shaderLibrary_;
        state.renderer = particleRenderer_.get();
        state.camera = &cameraSettings_;
        state.post = &postSettings_;
        state.performance = &perfSettings_;
        state.debug = &debugSettings_;
        state.showHud = &showHud_;
        state.paused = &paused_;
        state.onPausedChanged = [this](bool paused) {
            if (!musicLoaded_) return;
            std::lock_guard<std::mutex> lock(audioThread_.MusicMutex());
            if (paused) PauseMusicStream(music_);
            else ResumeMusicStream(music_);
        };
        state.musicLoaded = musicLoaded_;
        state.audio = &audioSettings_;
        state.trackNames = &trackDisplayNames_;
        state.currentTrackIndex = currentTrackIndex_;
        state.onNextTrack = [this] { NextTrack(); };
        state.onPrevTrack = [this] { PrevTrack(); };
        state.onSelectTrack = [this](int index) { PlayTrack(index); };
        state.particleCount = hudParticleCount_;
        state.trackLabel = trackLabel_.c_str();

        ui::DrawDebugPanel(state);
        ui::DrawDebugWindow(state);
    }

    ui::EndFrame();
}

void App::Shutdown() {
    // Must stop (and join) before anything it touches is torn down --
    // see AudioThread::Stop's doc comment. No lock needed here: once the
    // thread is joined, nothing else touches music_/analyzer_ concurrently.
    audioThread_.Stop();

    analyzer_.Detach();
    if (musicLoaded_) {
        StopMusicStream(music_);
        UnloadMusicStream(music_);
    }

    // Same "before CloseWindow()" rule the block below documents:
    // rlImGuiShutdown unloads the font atlas Texture2D through raylib,
    // which needs a live GL context. Created last in Init(), destroyed
    // first here.
    ui::Shutdown();

    // Every GPU resource (particle SSBOs, compute programs, the render
    // VAO, cached shaders) must be released while the GL context is still
    // alive. App's own destructor runs *after* Shutdown() returns (it's a
    // stack-local in main(), destructed on scope exit), which would be
    // too late -- so everything GPU-owning is torn down explicitly here,
    // before CloseWindow(), rather than left to implicit destruction order.
    visualizers_ = VisualizerManager{};
    postProcess_.reset();
    particleRenderer_.reset();
    shaderLibrary_.UnloadAll();

    UnloadTexture(accentTexture_);
    if (audioDeviceInitialized_) CloseAudioDevice();
    CloseWindow();
}
