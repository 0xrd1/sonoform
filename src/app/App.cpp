#include "App.h"
#include "raymath.h"
#include "rlgl.h"
#include "DemoTrack.h"
#include "GlCompat.h"
#include "EngineUi.h"

#include "SpectrumRingVisualizer.h"
#include "GalaxyVisualizer.h"
#include "FireworksVisualizer.h"
#include "TunnelVisualizer.h"
#include "NeonFogVisualizer.h"

#include <filesystem>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace fs = std::filesystem;

namespace {
bool HasSupportedAudioExt(const fs::path& p) {
    std::string ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".flac";
}

// Temporarily off: work is focused entirely on the Neon Fog particle/
// shape-morph pipeline (performance + legibility) for now, and audio
// playback was producing pops/clicks that were a distraction during that
// work, independent of whatever is causing them. Flip back on (and
// restore the other Add() calls below) once that's done. With this off,
// App::musicLoaded_ stays false, so Run() never calls UpdateMusicStream/
// AudioAnalyzer::Update -- NeonFogVisualizer's audio.* calls all read
// harmless zero-initialized defaults (see AudioAnalyzer.h), so lighting
// just stays at its resting color/intensity instead of erroring.
constexpr bool kAudioEnabled = false;
}

bool App::Init(const std::string& audioPathArg) {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1920, 1080, "Particle Audio Visualizer");
    SetTargetFPS(60);

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

    if (kAudioEnabled) InitAudioDevice();

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

    if (kAudioEnabled) LoadAudio(audioPathArg);

    // Only Neon Fog while its particle/shape-morph pipeline is the sole
    // focus -- see kAudioEnabled. Restore these once that work lands.
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
    // settings/default.ini doesn't exist yet (see ui::LoadDefaultSettings).
    // Must come after visualizers_.Add() above, since it reads/writes the
    // current visualizer's settings via VisitSettings.
    {
        ui::PanelState startupState;
        startupState.visualizers = &visualizers_;
        startupState.camera = &cameraSettings_;
        startupState.post = &postSettings_;
        ui::LoadDefaultSettings(startupState);
    }

    return true;
}

bool App::LoadAudio(const std::string& audioPathArg) {
    std::string pathToLoad;

    if (!audioPathArg.empty() && FileExists(audioPathArg.c_str())) {
        pathToLoad = audioPathArg;
    } else {
        fs::path audioDir = fs::path("assets") / "audio";
        if (fs::exists(audioDir) && fs::is_directory(audioDir)) {
            for (const auto& entry : fs::directory_iterator(audioDir)) {
                if (entry.is_regular_file() && HasSupportedAudioExt(entry.path())) {
                    pathToLoad = entry.path().string();
                    break;
                }
            }
        }
    }

    if (!pathToLoad.empty()) {
        music_ = LoadMusicStream(pathToLoad.c_str());
        trackLabel_ = fs::path(pathToLoad).filename().string();
        usingProceduralTrack_ = false;
    } else {
        proceduralTrackData_ = GenerateDemoTrackWav(90.0f, 44100);
        music_ = LoadMusicStreamFromMemory(".wav", proceduralTrackData_.data(),
                                            static_cast<int>(proceduralTrackData_.size()));
        trackLabel_ = "Procedural Demo Track (drop an mp3/wav/ogg into assets/audio to use your own)";
        usingProceduralTrack_ = true;
    }

    if (!IsMusicValid(music_)) {
        musicLoaded_ = false;
        return false;
    }

    music_.looping = true;
    SetMusicVolume(music_, 0.6f);
    PlayMusicStream(music_);
    analyzer_.AttachTo(music_);
    musicLoaded_ = true;
    return true;
}

void App::ApplyPerformanceSettings() {
    if (perfSettings_.vsync != lastVsync_) {
        // raylib forwards FLAG_VSYNC_HINT's SetWindowState/ClearWindowState
        // to glfwSwapInterval(1)/(0) at runtime on the desktop/GLFW backend
        // this project targets -- no window recreation needed.
        if (perfSettings_.vsync) SetWindowState(FLAG_VSYNC_HINT);
        else ClearWindowState(FLAG_VSYNC_HINT);
        lastVsync_ = perfSettings_.vsync;
    }
    if (perfSettings_.targetFps != lastTargetFps_) {
        SetTargetFPS(perfSettings_.targetFps); // raylib treats 0 as uncapped
        lastTargetFps_ = perfSettings_.targetFps;
    }
}

void App::Run() {
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        ApplyPerformanceSettings();
        if (musicLoaded_) UpdateMusicStream(music_);

        if (IsWindowResized()) {
            postProcess_->Resize(GetScreenWidth(), GetScreenHeight());
        }

        HandleInput(dt);
        UpdateCameraOrbit(dt);

        if (!paused_) {
            elapsedTime_ += dt;
            if (musicLoaded_) analyzer_.Update();
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

    if (IsKeyPressed(KEY_SPACE) && musicLoaded_) {
        paused_ = !paused_;
        if (paused_) PauseMusicStream(music_);
        else ResumeMusicStream(music_);
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

    // Drawn last, outside PostProcess::BeginScene/EndScene: bloom only ever
    // reads sceneTarget_, which received everything drawn between those two
    // calls above, so the panel is neither bright-pass extracted nor
    // blurred -- crisp text/widgets over a bloomed scene. After DrawHUD()
    // so panels sit on top of the plain-text HUD rather than under it.
    DrawUi();

    EndDrawing();
}

void App::DrawHUD() const {
    const int pad = 12;
    int y = pad;

    DrawText(TextFormat("FPS: %d", GetFPS()), pad, y, 18, GREEN); y += 22;
    DrawText(TextFormat("Visualizer [%d/%d]: %s", visualizers_.CurrentIndex() + 1, visualizers_.Count(), visualizers_.CurrentName()),
              pad, y, 18, RAYWHITE); y += 22;
    DrawText(TextFormat("Particles: %d", hudParticleCount_), pad, y, 18, RAYWHITE); y += 22;
    DrawText(TextFormat("Intensity: %.2f", postSettings_.reactivityIntensity), pad, y, 18, RAYWHITE); y += 22;

    const char* extra = visualizers_.CurrentExtraStatusLine();
    if (extra != nullptr) { DrawText(extra, pad, y, 16, SKYBLUE); y += 20; }

    DrawText(TextFormat("Track: %s", trackLabel_.c_str()), pad, y, 16, GRAY); y += 20;
    if (paused_) { DrawText("PAUSED", pad, y, 18, YELLOW); y += 22; }

    const char* controls =
        "1-5: switch visualizer   Tab/Right/Left: cycle   Space: pause   S: cycle shape   M: toggle auto-cycle (Neon Fog)\n"
        "Right-drag: orbit camera   Wheel: zoom   C: toggle auto-rotate   R: reset camera\n"
        "[ / ]: light intensity   - / =: morph force (Neon Fog)   F: fullscreen   H: toggle HUD   F1: settings panel   Esc: quit";
    DrawText(controls, pad, GetScreenHeight() - 70, 16, Fade(RAYWHITE, 0.75f));
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
        state.music = &music_;
        state.musicLoaded = musicLoaded_;
        state.particleCount = hudParticleCount_;
        state.trackLabel = trackLabel_.c_str();

        ui::DrawDebugPanel(state);
        ui::DrawDebugWindow(state);
    }

    ui::EndFrame();
}

void App::Shutdown() {
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
    CloseAudioDevice();
    CloseWindow();
}
