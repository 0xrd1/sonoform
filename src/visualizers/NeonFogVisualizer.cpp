#include "NeonFogVisualizer.h"
#include "raymath.h"
#include <algorithm>
#include <array>
#include <cmath>

void NeonFogVisualizer::Init(ShaderLibrary& shaders, ParticleRenderer& renderer) {
    // Re-entrant: this also runs on the settings panel's "Rebuild Systems"
    // action, so every value read below comes from the *current* settings
    // members (which the user may already have tuned), never from a local
    // constant -- re-running Init() must apply live edits, not discard them.
    shapeField_ = std::make_unique<ShapeField>(shaders, shapeSettings_.gridResolution,
                                                shapeSettings_.fieldCenter, shapeSettings_.gridHalfExtent);
    shapeProvider_ = std::make_unique<ProceduralShapeProvider>(static_cast<ProceduralShapeType>(shapeSettings_.shapeType));
    shapeProvider_->BakeInto(*shapeField_, 0.0f);
    // Populates the CPU sample cache immediately -- see Update()'s
    // needsRebake block for why this can't wait for the first live
    // rebake: LightningSystem (and, when shown, the debug gizmos) sample
    // the field via SampleWorld(), which returns a harmless-but-useless
    // default (dist 0) until the cache has been populated at least once.
    shapeField_->RefreshSampleCache();

    fog_.Init(shaders, renderer, *shapeField_);
    // fog_.Update() (called every frame, before Draw() -- see App::Run's
    // Update-then-Draw order) re-applies shading/forces from current
    // settings each frame, including the very first frame after this
    // Init/Rebuild, so there's no separate one-time setup needed here.

    floor_.Init(shaders);

    // Reallocating on every Init() (including "Rebuild Systems") is fine --
    // RenderTarget::Allocate() releases any prior texture first, and this
    // only runs on a rare, deliberate user action, never per-frame.
    shadowTarget_.Allocate(kShadowMapResolution, kShadowMapResolution);

    // See settingsSeeded_'s comment on why this only runs once: Init() is
    // also the "Rebuild Systems" entry point, and re-seeding the floor's
    // look on every rebuild would silently discard any floor tuning the
    // user had already dialed in via the panel.
    if (!settingsSeeded_) {
        settingsSeeded_ = true;
        // Was -2.2 -- only a ~0.7-unit gap below the fog's typical lowest
        // excursion (roughly y=-1.5 around fieldCenter_), which turbulence/
        // shed-particle jitter routinely dipped below, visibly clipping
        // particles through the floor. -6.0 keeps a comfortable ~4.5-unit
        // clear gap while staying close enough that the camera's default
        // pitch (see App::Init) doesn't put it below the frame -- at a
        // shallow viewing angle a ground plane recedes toward the horizon
        // far faster than its raw distance below the subject suggests.
        constexpr float kInitialFloorY = -6.0f;
        floorParams_.center = { shapeSettings_.fieldCenter.x, kInitialFloorY, shapeSettings_.fieldCenter.z };
        floorParams_.shapeSampleY = shapeSettings_.fieldCenter.y;
        floorParams_.lightPoolCenter = floorParams_.center; // pool sits under the overhead light, which is above fieldCenter_
    }
}

void NeonFogVisualizer::Update(const FrameContext& frame) {
    // Shape attraction is driven purely by shapeSettings_.morphForce -- an
    // independent value the user controls directly (see AdjustPrimary or
    // the panel), not derived from audio in any way. Eased toward its
    // target so shape changes still read as organic emergence/dissolution
    // rather than a snap.
    float morphTarget = Clamp(shapeSettings_.morphForce, 0.0f, shapeSettings_.morphMax);
    morphStrength_ += (morphTarget - morphStrength_) * std::min(1.0f, frame.dt * shapeSettings_.morphEaseRate);

    // The shape target re-bakes whenever shapeType, Field Center, or Shape
    // Grid Half-Extent actually change, from *either* the keyboard ('S' ->
    // CycleShapePreset -> shapeSettings_.shapeType) or the settings panel
    // directly -- both funnel through this one check, so there's exactly
    // one place that talks to shapeProvider_/shapeField_. Center/half-extent
    // are compared against shapeField_'s own authoritative values (not a
    // cached copy) since those two are applied live -- see
    // ShapeField::SetCenter/SetHalfExtent's comment -- unlike gridResolution
    // (voxel count, requires a full SSBO reallocation via Rebuild Systems).
    bool needsRebake = shapeSettings_.shapeType != static_cast<int>(shapeProvider_->Type())
        || Vector3Distance(shapeSettings_.fieldCenter, shapeField_->Center()) > 0.0001f
        || fabsf(shapeSettings_.gridHalfExtent - shapeField_->HalfExtent()) > 0.0001f;
    if (needsRebake) {
        shapeProvider_->SetType(static_cast<ProceduralShapeType>(shapeSettings_.shapeType));
        shapeField_->SetCenter(shapeSettings_.fieldCenter);
        shapeField_->SetHalfExtent(shapeSettings_.gridHalfExtent);
        shapeProvider_->BakeInto(*shapeField_, 0.0f);
        // Unconditional now, not gated behind debug-view visibility: real
        // (non-debug) features depend on this too -- LightningSystem
        // samples the field to keep bolts contained inside the shape, and
        // needs it fresh every time the shape changes, not just when
        // Shape Bounds/Force Vectors happen to be shown. See Draw()'s
        // gizmo block, which no longer does its own gated refresh.
        shapeField_->RefreshSampleCache();
    }

    // Auto-advance through shape presets on a fixed timer so the morph is
    // visible without any input; 'S'/'M' (SecondaryAction/TertiaryAction)
    // or the panel still let the user force a change or stop the timer.
    if (shapeSettings_.autoCycle) {
        shapeTimer_ += frame.dt;
        if (shapeTimer_ >= shapeSettings_.cycleSeconds) {
            shapeTimer_ = 0.0f;
            CycleShapePreset();
        }
    }

    if (frame.audio.BeatTriggered()) beatFlash_ = 1.0f;
    beatFlash_ = std::max(0.0f, beatFlash_ - frame.dt * lightingSettings_.beatFlashDecayRate);

    // The core light: the fog's *only* real light source now (see Draw()
    // -- the overhead position no longer contributes to lightBoost), so
    // this is what "glowing from within" actually means. Hue is driven by
    // AudioAnalyzer's normalized *Level() getters (each [0,1] against the
    // track's own recent peak), not the raw Bass()/Mid()/Treble() -- those
    // are un-normalized FFT magnitudes far too small (~0.0-0.15) for a
    // hand-tuned degree scale to move visibly, which is the root cause of
    // the old "hue just rotates" behavior (see hueCycleSpeed's comment in
    // EngineSettings.h). Mid is folded in here via colorSettings_ rather
    // than FogLightingSettings, since the core light's own struct predates
    // the color-reactivity pass -- see FogColorSettings' class comment.
    float coreHue = lightingSettings_.coreHueBase
                   + frame.audio.BassLevel() * lightingSettings_.coreHueBassScale
                   + frame.audio.TrebleLevel() * lightingSettings_.coreHueTrebleScale
                   + frame.audio.MidLevel() * colorSettings_.hueMidScale
                   + frame.time * lightingSettings_.hueCycleSpeed;
    float coreSat = Clamp(lightingSettings_.coreSaturation + frame.audio.EnergyLevel() * colorSettings_.saturationEnergyScale, 0.0f, 1.0f);
    float coreVal = Clamp(lightingSettings_.coreValue + frame.audio.EnergyLevel() * colorSettings_.valueEnergyScale, 0.0f, 1.0f);
    coreLightColor_ = ColorFromHSV(std::fmod(coreHue + 360.0f, 360.0f), coreSat, coreVal);
    coreLightIntensity_ = (lightingSettings_.intensityBase + frame.audio.Energy() * lightingSettings_.intensityEnergyScale +
                            beatFlash_ * lightingSettings_.intensityBeatFlashScale) * frame.intensity;

    // Spectral lights: up to three extra LightSample entries, one per band,
    // positioned near the field center and colored/sized by that band's
    // own level -- see FogColorSettings' class comment on why this is what
    // makes color read as *localized*, not just a single scene-wide hue.
    // Center is shapeField_->Center() (the authoritative, possibly-just-
    // rebaked position), same reasoning as the core light's own position
    // in Draw() below.
    spectralLightCount_ = 0;
    if (colorSettings_.spectralEnabled) {
        Vector3 center = shapeField_->Center();
        float r = colorSettings_.spectralRadius;
        float sat = colorSettings_.spectralSaturation;
        float scale = colorSettings_.spectralIntensity * frame.intensity;
        spectralLights_[0] = LightSample{ center + Vector3{ 0.0f, -r, 0.0f },
            frame.audio.BassLevel() * scale, ColorFromHSV(colorSettings_.spectralHueBass, sat, 1.0f) };
        spectralLights_[1] = LightSample{ center + Vector3{ r, 0.0f, 0.0f },
            frame.audio.MidLevel() * scale, ColorFromHSV(colorSettings_.spectralHueMid, sat, 1.0f) };
        spectralLights_[2] = LightSample{ center + Vector3{ 0.0f, r, 0.0f },
            frame.audio.TrebleLevel() * scale, ColorFromHSV(colorSettings_.spectralHueTreble, sat, 1.0f) };
        spectralLightCount_ = 3;
    }

    // Palette hue ramp, audio-shifted here (not in Draw(), which is const
    // and has no FrameContext of its own -- same reason coreLightColor_/
    // spectralLights_ above are computed here and just read in Draw()).
    // Shift rotates the whole ramp together; Spread widens B away from A
    // -- both additive on top of the panel's static Hue A/B, so 0 audio
    // reproduces exactly the static ramp the panel shows.
    float shift = frame.audio.EnergyLevel() * colorSettings_.paletteAudioShift;
    float spread = frame.audio.TrebleLevel() * colorSettings_.paletteAudioSpread;
    paletteHueA_ = colorSettings_.paletteHueA + shift;
    paletteHueB_ = colorSettings_.paletteHueB + shift + spread;

    // Delegates forces/shading/emission/sim-update to the emitter itself --
    // see ShapeFogEmitter::Update for the SDF-biased-spawn and recruited/
    // shed-lifecycle reasoning that used to live inline here. Shape
    // attraction/morph itself is still never audio-driven (morphStrength_
    // above comes purely from shapeSettings_.morphForce) -- but fog_.Update
    // now also folds in Turbulence x Excitement/Treble (see
    // FogAudioSettings) when fog_.audio.reactive, "destructive but
    // resisted": audio can rough the fog up, never weaken its hold on the
    // shape.
    fog_.Update(frame.dt, frame.time, *shapeField_, morphStrength_,
                Vector3Subtract(lightingSettings_.overheadLightPos, shapeField_->Center()), lightingSettings_.shadeAmbientFloor,
                frame.audio);

    // Lightning: strong beats fire a bolt; a short cooldown keeps a burst
    // of rapid beats from spawning bolts on top of each other.
    lightningCooldown_ = std::max(0.0f, lightningCooldown_ - frame.dt);
    bool strongBeat = frame.audio.BeatTriggered() && frame.audio.BeatIntensity() > lightningSettings_.beatIntensityThreshold;
    bool fireLightning = strongBeat && lightningCooldown_ <= 0.0f;
    float lightningStrength = Clamp(frame.audio.BeatIntensity() + frame.audio.Treble(), 0.0f, 1.0f);

    lightning_.Update(frame.dt, *shapeField_, fireLightning, lightningStrength, lightningSettings_);
    if (fireLightning) lightningCooldown_ = lightningSettings_.cooldownSeconds;

    // Kick impulse + impact burst: a separate hard-beat trigger/cooldown
    // from lightning's (see FogAudioSettings::kickBeatThreshold -- kicks
    // and bolts don't have to agree on what counts as "hard"). KickImpulse
    // nudges the *existing* mass (recruited particles wobble and get
    // pulled back by Shape Attraction; shed ones fly off and despawn on
    // schedule); EmitImpactBurst adds a dedicated, guaranteed-unrecruited
    // burst of debris on top so a hard hit unambiguously reads as
    // particles being expelled -- see ShapeFogEmitter::EmitImpactBurst's
    // comment -- without ever touching the forces that hold the
    // silhouette together.
    kickCooldown_ = std::max(0.0f, kickCooldown_ - frame.dt);
    if (fog_.audio.reactive) {
        bool hardBeat = frame.audio.BeatTriggered() && frame.audio.BeatIntensity() > fog_.audio.kickBeatThreshold;
        if (hardBeat && kickCooldown_ <= 0.0f) {
            fog_.KickImpulse(shapeField_->Center(), fog_.audio.kickImpulseStrength, fog_.audio.kickImpulseRadius);
            fog_.EmitImpactBurst(shapeField_->Center());
            kickCooldown_ = fog_.audio.kickCooldownSeconds;
        }
    }

    lastTime_ = frame.time; // Draw() is const with no FrameContext of its own -- see the member's comment
}

void NeonFogVisualizer::PreDraw() {
    if (!shapeField_ || !shadowTarget_.IsValid()) return;

    // A real top-down render of the actual particle mass -- see
    // gfx/VoidFloor.h's ShadowMap comment on why this replaces the old
    // static analytic-shape SDF shadow. Built as raw matrices, not
    // raylib's BeginMode3D/Camera3D: GpuParticleSystem::Draw/
    // ParticleRenderer::Draw already take an explicit viewProj +
    // camera-right/up basis rather than relying on rlgl's matrix stack
    // (see ParticleRenderer::Draw's signature), so no BeginMode3D is
    // needed here -- which matters, since App hasn't opened its own
    // BeginMode3D yet at this point in the frame (see Visualizer::PreDraw's
    // comment on why this whole pass must run before that and stay
    // entirely self-contained).
    Vector3 center = shapeField_->Center();
    // 1.3x margin so shed/turbulence particles drifting near the shape's
    // nominal edge still land inside the shadow frustum instead of being
    // silently clipped out of it.
    float halfExtent = shapeField_->HalfExtent() * 1.3f;
    float eyeHeight = halfExtent * 4.0f; // comfortably above the tallest particle excursion
    Vector3 eye = Vector3Add(center, Vector3{ 0.0f, eyeHeight, 0.0f });
    Vector3 upHint{ 0.0f, 0.0f, -1.0f };

    Matrix view = MatrixLookAt(eye, center, upHint);
    Matrix proj = MatrixOrtho(-halfExtent, halfExtent, -halfExtent, halfExtent, 0.1f, eyeHeight * 2.0f);
    Matrix viewProj = MatrixMultiply(view, proj);

    // Same forward-cross-up-hint basis derivation App::Draw uses for the
    // main camera, just for a camera looking straight down instead --
    // resolves to world +X/-Z here, so particle billboards lie flat in
    // the XZ plane exactly as they'd appear viewed from directly above.
    Vector3 forward{ 0.0f, -1.0f, 0.0f };
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, upHint));
    Vector3 up = Vector3CrossProduct(right, forward);

    shadowTarget_.BeginDraw();
    ClearBackground(Color{ 0, 0, 0, 0 });
    // Alpha-over compositing (glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA))
    // saturates to fully opaque after only a handful of overlapping full-
    // alpha sprites -- correct for the main visual pass, but it means this
    // density-accumulation pass needs a much smaller per-particle
    // contribution (see particle_render.frag's uAlphaScale) or the result
    // clips to a flat, hard-edged silhouette almost everywhere the mass
    // reaches, reading as a solid geometric blob (a "big box shadow" for
    // the Box shape) instead of a soft shadow that actually varies with
    // local density. spriteStyle 0 (clean circular, no wispy noise mask)
    // -- the mask exists to keep individual sprites from reading as
    // visible discs in the *lit* view; at 256x256 shadow-map resolution
    // its high-frequency detail is invisible anyway and just adds patchy
    // contrast to what should be a smooth density gradient.
    BeginBlendMode(BLEND_ALPHA);
    constexpr float kShadowAlphaScale = 0.05f;
    fog_.Draw(viewProj, right, up, nullptr, 0, lastTime_, /*spriteStyle=*/0, PaletteParams{}, kShadowAlphaScale);
    EndBlendMode();
    shadowTarget_.EndDraw();

    shadowMap_.texture = shadowTarget_.Texture();
    shadowMap_.center = Vector2{ center.x, center.z };
    shadowMap_.halfExtent = halfExtent;
}

namespace {
// Sample-direction grid for the Force Vectors gizmo (see the
// showForceVectors block below). Shape Bounds no longer needs a shared
// direction set -- it's drawn directly by the shape provider (see
// IShapeProvider::DrawDebugWireframe / ProceduralShapeProvider's
// override), which knows its own exact geometry instead of resampling
// the baked field into an approximation.
constexpr int kGizmoLatSteps = 6;
constexpr int kGizmoLonSteps = 8;
} // namespace

void NeonFogVisualizer::Draw(const RenderContext& ctx) const {
    // Core light occupies slot 0; lightning bolts fill the rest. No
    // overhead-light entry here deliberately -- see the class comment
    // and FogLightingSettings::overheadLightPos's comment: it's not a real
    // light on the particles, only a shading angle and a floor-pool center.
    // Reads shapeField_->Center(), not shapeSettings_.fieldCenter directly:
    // Update() (which keeps the two in sync -- see its rebake check) is
    // skipped while paused (see App::Run's `if (!paused_)` guard), so a
    // live Field Center drag while paused would otherwise light the fog
    // from a position the actual baked field hasn't moved to yet -- the
    // same class of desync Bug 1 was, just reachable through a different
    // path.
    //
    // Built *before* the floor draw below (this used to run after it) so
    // the same array can light the floor too -- see VoidFloor::Draw's
    // lights/lightCount parameters.
    std::array<LightSample, kMaxParticleLights> lights{};
    lights[0] = LightSample{ shapeField_->Center(), coreLightIntensity_, coreLightColor_ };
    int lightCount = 1;
    // Spectral lights (see Update()'s comment) fill the next slots, ahead
    // of lightning -- lightning is transient/rare (only on hard beats) and
    // its own GatherLights already caps at whatever room is left, so it
    // can never be starved into zero by these three.
    for (int i = 0; i < spectralLightCount_ && lightCount < kMaxParticleLights; i++) {
        lights[static_cast<size_t>(lightCount++)] = spectralLights_[static_cast<size_t>(i)];
    }
    lightCount += lightning_.GatherLights(lights.data() + lightCount, kMaxParticleLights - lightCount);

    // The stage: drawn first as opaque background geometry (default blend,
    // depth test+write on) so the fog correctly blends over it afterward
    // (ParticleRenderer::Draw disables depth *write* but keeps the test).
    // shadowMap_ was rendered this same frame in PreDraw(), before the
    // main scene target was even bound -- see Visualizer::PreDraw's
    // comment; lights[] above gives it real particle-color illumination
    // too, not just the static light-pool hint.
    floor_.Draw(floorParams_, shapeField_.get(), shadowMap_, lights.data(), lightCount);

    // Standard alpha blending, not additive: the fog is meant to read as
    // a gas being lit, not a self-luminous energy cloud -- see the class
    // comment. Unsorted (no per-particle depth sort): at this density,
    // order-of-blend errors between individual soft, dim sprites are
    // imperceptible, which is the standard real-time-VFX approximation
    // for dense smoke/fog (an order-independent-transparency shortcut),
    // not a shortcut specific to this engine.
    BeginBlendMode(BLEND_ALPHA);
    // fadeMode/sizeScale are fixed inside ShapeFogEmitter::Draw -- see its
    // own comment on why neither has needed to vary per-call.
    int spriteStyle = (ctx.debug != nullptr && ctx.debug->disableSpriteNoise) ? 0 : 1;

    // See gfx/PaletteParams.h -- palette.mode == Off (colorSettings_.paletteMode
    // defaults to 0) is an exact no-op all the way down to the shader, so
    // this is always safe to build and pass unconditionally.
    PaletteParams palette;
    palette.mode = static_cast<PaletteMode>(colorSettings_.paletteMode);
    palette.center = shapeField_->Center();
    palette.extent = shapeField_->HalfExtent();
    palette.hueA = paletteHueA_;
    palette.hueB = paletteHueB_;
    palette.saturation = colorSettings_.paletteSaturation;
    palette.strength = colorSettings_.paletteStrength;
    palette.lightTint = colorSettings_.paletteLightTint;

    fog_.Draw(ctx.viewProj, ctx.cameraRight, ctx.cameraUp, lights.data(), lightCount, lastTime_, spriteStyle, palette);
    EndBlendMode();

    // Lightning bolts are genuinely light-emitting, so additive is the
    // physically-appropriate blend mode for them specifically.
    BeginBlendMode(BLEND_ADDITIVE);
    lightning_.Draw(lightningSettings_);
    EndBlendMode();

    // Debug gizmos: opaque, default blend, drawn last/on top -- see
    // ui::DebugSettings and RenderContext::debug's comment. Every gizmo
    // below reads shapeField_'s own accessors (Center/HalfExtent), never
    // shapeSettings_.fieldCenter/gridHalfExtent directly -- shapeField_ is
    // the actual object the sim samples (see ShapeField::SetCenter's
    // comment: those two settings apply to it live, same-frame), so a
    // gizmo built from it structurally cannot drift out of sync the way
    // reading a separate settings mirror could.
    //
    // Origin Gizmo is deliberately outside the `shapeField_` guard below --
    // world (0,0,0) exists whether or not a shape is currently baked, and
    // it's specifically useful for judging how far Field Center has moved
    // *from* origin, so it can't depend on shapeField_ at all.
    if (ctx.debug != nullptr && ctx.debug->showOriginGizmo) {
        constexpr float kAxisLen = 2.0f;
        constexpr float kTipRadius = 0.08f;
        DrawLine3D(Vector3{ 0, 0, 0 }, Vector3{ kAxisLen, 0, 0 }, RED);
        DrawSphere(Vector3{ kAxisLen, 0, 0 }, kTipRadius, RED);
        DrawLine3D(Vector3{ 0, 0, 0 }, Vector3{ 0, kAxisLen, 0 }, LIME);
        DrawSphere(Vector3{ 0, kAxisLen, 0 }, kTipRadius, LIME);
        DrawLine3D(Vector3{ 0, 0, 0 }, Vector3{ 0, 0, kAxisLen }, BLUE);
        DrawSphere(Vector3{ 0, 0, kAxisLen }, kTipRadius, BLUE);
    }

    if (ctx.debug != nullptr && shapeField_) {
        // Shape Bounds and Force Vectors both sample shapeField_'s real
        // baked data via SampleWorld() -- the CPU-side cache it reads is
        // kept fresh unconditionally by Update()'s rebake block now
        // (LightningSystem needs it every beat, not just when these
        // gizmos happen to be shown -- see NeonFogVisualizer::Update()),
        // so no gated refresh is needed here anymore.
        const Vector3 center = shapeField_->Center();
        const float halfExtent = shapeField_->HalfExtent();
        const int resolution = shapeField_->Resolution();

        if (ctx.debug->showShapeBounds) {
            // Drawn by the shape provider itself, not reconstructed here --
            // see IShapeProvider::DrawDebugWireframe's comment.
            shapeProvider_->DrawDebugWireframe(*shapeField_, Fade(SKYBLUE, 0.5f));
            // Bake-grid extent (the actual sampled volume), distinct from
            // the shape wireframe above -- lets grid resolution/extent be
            // visually cross-checked against where the shape sits inside it.
            float gridSize = halfExtent * 2.0f;
            DrawCubeWiresV(center, Vector3{ gridSize, gridSize, gridSize }, Fade(DARKPURPLE, 0.35f));
            float voxelSize = gridSize / static_cast<float>(resolution);
            DrawCubeWiresV(center - Vector3{ halfExtent, halfExtent, halfExtent } + Vector3{ voxelSize * 0.5f, voxelSize * 0.5f, voxelSize * 0.5f },
                           Vector3{ voxelSize, voxelSize, voxelSize }, Fade(MAGENTA, 0.6f));
        }

        if (ctx.debug->showFieldAxes) {
            DrawSphere(center, 0.15f, Fade(WHITE, 0.6f));
            Vector3 toLight = Vector3Normalize(Vector3Subtract(lightingSettings_.overheadLightPos, center));
            Vector3 tip = center + Vector3Scale(toLight, 3.0f);
            DrawLine3D(center, tip, YELLOW);
            DrawSphere(tip, 0.1f, YELLOW);
        }

        if (ctx.debug->showLightGizmos) {
            // The real light: sized/colored by its actual current state,
            // so this gizmo also doubles as a live readout of it.
            float coreRadius = Clamp(coreLightIntensity_ * 0.05f, 0.1f, 0.6f);
            DrawSphere(center, coreRadius, coreLightColor_);
            DrawSphereWires(center, coreRadius + 0.05f, 8, 8, WHITE);
            // Shading-only position -- deliberately dim/muted, it's not a
            // real light on the particles (see the class comment).
            DrawSphere(lightingSettings_.overheadLightPos, 0.2f, Fade(WHITE, 0.35f));
            DrawSphereWires(lightingSettings_.overheadLightPos, 0.25f, 8, 8, Fade(WHITE, 0.5f));
        }

        if (ctx.debug->showEmitterBounds) {
            // Field Center + Position Offset -- see FogEmissionSettings::
            // positionOffset's comment. Equals `center` (and so visually
            // overlaps Shape Bounds) whenever Position Offset is still 0,
            // which is every preset that predates that field.
            Vector3 emitterCenter = Vector3Add(center, fog_.emission.positionOffset);
            float ext = fog_.attraction.candidateHalfExtent * 2.0f;
            DrawCubeWiresV(emitterCenter, Vector3{ ext, ext, ext }, Fade(GREEN, 0.6f));
        }

        if (ctx.debug->showVolumeBounds) {
            // Shell Thickness: the spawn-time-only random offset along the
            // surface normal (particle_emit.comp) -- these two wireframes
            // are the actual outer/inner bound newborn particles are
            // scattered across before the ongoing Shape Attraction force
            // (which targets the exact surface, offset 0, unless Volume
            // Depth pulls it inward below) takes over.
            if (fog_.attraction.shellThickness > 0.0001f) {
                shapeProvider_->DrawDebugWireframe(*shapeField_, Fade(YELLOW, 0.35f), fog_.attraction.shellThickness);
                shapeProvider_->DrawDebugWireframe(*shapeField_, Fade(YELLOW, 0.35f), -fog_.attraction.shellThickness);
            }
            // Volume Depth: the innermost depth the recruited fraction's
            // per-particle random target (shape_conform.glsl's targetDist,
            // uniform in [-volumeDepth, 0]) can reach -- this wireframe is
            // that depth's floor, not a typical particle's position; most
            // recruited particles target somewhere between this and the
            // exact surface (already shown by Shape Bounds).
            if (fog_.attraction.volumeDepth > 0.0001f) {
                shapeProvider_->DrawDebugWireframe(*shapeField_, Fade(MAGENTA, 0.35f), -fog_.attraction.volumeDepth);
            }
        }

        if (ctx.debug->showForceVectors) {
            // Shell radius scales with the shape's own configured extent
            // (Shape Grid Half-Extent) instead of a fixed guess, so the
            // sample shell always sits just outside whatever shape/size is
            // actually active. Gravity arrows were removed -- see
            // ui::DebugSettings::showForceVectors's comment.
            float shellRadius = Clamp(halfExtent * 0.75f, 1.0f, halfExtent);
            bool showAttraction = morphStrength_ > 0.0001f;
            // Arrow length scales with the *actual* current strength
            // setting (not a fixed idealized length) so the gizmo visibly
            // grows/shrinks as Shape Attraction is tuned -- a fixed-length
            // arrow that never responds to the setting it's supposedly
            // showing is worse than no gizmo at all. Skipped entirely once
            // morphStrength_ is negligible, matching ApplyShapeConform's
            // own early-out (shape_conform.glsl) -- no attraction is
            // actually being applied at that point, so no arrow should
            // claim otherwise.
            float attractArrowLen = Clamp(fog_.force.shapeAttraction * morphStrength_ * 0.25f, 0.0f, 1.5f);
            if (showAttraction) {
                for (int lat = 1; lat < kGizmoLatSteps; lat++) {
                    float theta = PI * float(lat) / kGizmoLatSteps; // polar angle; skip the exact poles
                    for (int lon = 0; lon < kGizmoLonSteps; lon++) {
                        float phi = 2.0f * PI * float(lon) / kGizmoLonSteps;
                        Vector3 dir{ sinf(theta) * cosf(phi), cosf(theta), sinf(theta) * sinf(phi) };
                        Vector3 samplePos = center + Vector3Scale(dir, shellRadius);

                        // Points from outside toward the surface, mirroring
                        // shape_conform.glsl's -sign(dist)*gradient.
                        // Sampled from the real baked field
                        // (shapeField_->SampleWorld -- same data
                        // ApplyShapeConform itself samples), not a
                        // hand-copied analytic SDF, so this can't drift out
                        // of sync with whatever shape is actually baked.
                        // Assumes Volume Depth == 0 (exact-surface
                        // targeting) -- with Volume Depth > 0 each
                        // particle's actual target depth is a per-particle
                        // random roll this gizmo can't cheaply replicate
                        // point-by-point (see Shell/Volume Bounds above for
                        // that case instead).
                        ShapeField::FieldSample fs = shapeField_->SampleWorld(samplePos);
                        Vector3 attractDir = Vector3Scale(fs.gradient, fs.distance > 0.0f ? -1.0f : 1.0f);
                        DrawLine3D(samplePos, samplePos + Vector3Scale(attractDir, attractArrowLen), ORANGE);
                    }
                }
            }
        }
    }
}

namespace {
const char* ShapeName(ProceduralShapeType type) {
    switch (type) {
        case ProceduralShapeType::Sphere: return "Sphere";
        case ProceduralShapeType::Box: return "Box";
        case ProceduralShapeType::Torus: return "Torus";
        case ProceduralShapeType::Cylinder: return "Cylinder";
    }
    return "?";
}
}

const char* NeonFogVisualizer::DebugInfoText() const {
    if (!shapeField_) return nullptr;
    Vector3 c = shapeField_->Center();
    float halfExtent = shapeField_->HalfExtent();
    int res = shapeField_->Resolution();
    float voxelSize = (halfExtent * 2.0f) / static_cast<float>(res);
    // Auto-cycle countdown folded in here (was the ImGui-redundant HUD's
    // ExtraStatusLine, since deleted -- see Visualizer.h) -- it's the one
    // value this window doesn't already show elsewhere via the Shape panel
    // group's own Auto-Cycle/Cycle Seconds fields.
    float secondsToNext = shapeSettings_.autoCycle ? std::max(0.0f, shapeSettings_.cycleSeconds - shapeTimer_) : 0.0f;
    return TextFormat(
        "Grid: %dx%dx%d over %.1f world units (voxel %.3f)\n"
        "Field Center: (%.2f, %.2f, %.2f)\n"
        "Shape: %s  Morph: %.0f%%%s\n"
        "Core Light: (%d,%d,%d) x %.2f",
        res, res, res, halfExtent * 2.0f, voxelSize,
        c.x, c.y, c.z,
        ShapeName(shapeProvider_->Type()), morphStrength_ * 100.0f,
        shapeSettings_.autoCycle ? TextFormat("  Next shape in: %.1fs", secondsToNext) : "",
        coreLightColor_.r, coreLightColor_.g, coreLightColor_.b, coreLightIntensity_);
}

void NeonFogVisualizer::CycleShapePreset() {
    // Walks Sphere -> Box -> Torus -> Cylinder -> Sphere ... Never disables
    // the shape entirely -- to see pure ambient fog, drive Morph Force to
    // 0 via '-' or the panel instead (see AdjustPrimary); 'M' separately
    // toggles whether this cycle advances on its own. Only touches
    // shapeSettings_.shapeType -- Update() is what notices the change and
    // actually re-bakes (see its comment), so this also works identically
    // when shapeType is instead edited from the settings panel's combo.
    //
    // No Head/face shape here -- an earlier crude analytic stand-in was
    // removed; the real thing comes later via a mesh-driven
    // MeshShapeProvider (see shapes/ShapeProvider.h), not an approximate
    // SDF baked in shape_bake.comp.
    shapeSettings_.shapeType = (shapeSettings_.shapeType + 1) % ui::ShapeSettings::kShapeTypeCount;
}

void NeonFogVisualizer::AdjustPrimary(float delta) {
    shapeSettings_.morphForce = Clamp(shapeSettings_.morphForce + delta, 0.0f, shapeSettings_.morphMax);
}

void NeonFogVisualizer::VisitSettings(ui::IParamVisitor& v) {
    // fog_'s own Emission/Forces/Attraction groups -- see
    // ShapeFogEmitter::VisitSettings. A second emitter member would add
    // one more BeginGroup(name)/fog2_.VisitSettings(v)/EndGroup() here,
    // no duplicated logic.
    v.BeginGroup("Fog");
    fog_.VisitSettings(v);
    v.EndGroup();

    v.BeginGroup("Shape");
    shapeSettings_.Visit(v);
    v.EndGroup();

    v.BeginGroup("Lighting");
    lightingSettings_.Visit(v);
    colorSettings_.Visit(v); // opens its own "Color" sub-group -- see FogColorSettings::Visit
    v.EndGroup();

    v.BeginGroup("Lightning");
    lightningSettings_.Visit(v);
    v.EndGroup();

    v.BeginGroup("Floor");
    ui::VisitFloorParams(floorParams_, v);
    v.EndGroup();
}
