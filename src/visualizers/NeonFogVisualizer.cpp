#include "NeonFogVisualizer.h"
#include "ForceFactory.h"
#include "raymath.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace {
// Particle count is sized for the SDF-biased-spawn design below, not for
// raw visual mass: every alive particle pays for a full force evaluation
// (gravity + turbulence + drag + shape-conform, the latter two each doing
// a curl-noise sample) every frame in particle_sim.comp, so count is the
// second-biggest lever on GPU cost after the noise-derivative optimization
// in noise.glsl. Because particles now spawn already on/near the current
// shape's surface (see kShapeCandidateHalfExtent) instead of filling a
// large ambient volume, far fewer are needed for a dense, legible result
// than the old "big diffuse box" approach required. Raised well past that
// iteration's 150000 once the real fix for sprite legibility (noise-broken
// alpha in particle_render.frag, not raw count) landed and confirmed
// headroom at 60fps -- see NeonFogVisualizer.h's class comment.
constexpr int kFogCapacity = 320000;
constexpr int kShapeGridResolution = 40;
// Grid half-extent must comfortably exceed the largest shape's radius
// (~3.5, the torus) plus the emission candidate box below it, or samples
// land outside the field and hit clamped edge voxels instead of the real
// surface -- see shape_field_sample.glsl's FetchShapeVoxel clamp.
constexpr float kShapeGridHalfExtent = 6.0f;

// Loose containment only: keeps particles that have shed off the shape
// (see kShapeRecruitFraction) from drifting away indefinitely before they
// die. Must stay well below ShapeConform's attraction (kShapeAttraction)
// or gravity would compete with it for the recruited fraction.
constexpr float kGravityStrength = 0.35f;
constexpr float kGravitySoftening = 3.5f;

// Kept deliberately weak: real per-particle turbulence-as-a-motion-driver
// is a later phase (wind/swirl forces, explicitly deferred). Today it
// should read as barely-there surface shimmer, not the dominant motion --
// too strong and it fights ShapeConform hard enough that the structure
// never firms up, reading as "video game particles" instead of a held
// fog shape.
constexpr float kTurbulenceStrength = 0.12f;
constexpr float kTurbulenceScale = 0.08f;

// Firm hold: particles already spawn on the surface (see Update's
// emission block), so this force's job in steady state is mostly
// *maintaining* that position against turbulence/gravity and pulling
// still-alive particles across to a new shape when one is selected.
// Attraction raised and curl-flow lowered vs. earlier tuning so the
// structure reads as solidly held rather than wobbling.
constexpr float kShapeAttraction = 1.8f;
constexpr float kShapeCurl = 0.3f;
// The fraction of particles that stay locked to the shape for their whole
// life (see GpuEmitParams::recruitFraction -- this same value also
// decides which particles get the long "core" life vs. the short "shed"
// life in Update()'s emission block, via the shared RecruitRoll hash).
// High: the volume should read as mostly-one-coherent-mass, with only a
// small fraction ever shedding off as haze.
constexpr float kShapeRecruitFraction = 0.85f;

// How far out (from the shape's center) candidate spawn points are chosen
// before being projected onto the surface -- see GpuEmitMode::ShapeSurface.
// Kept within kShapeGridHalfExtent with margin (worst-case box corner is
// ~1.73x this) so projection never samples a clamped edge voxel.
constexpr float kShapeCandidateHalfExtent = 3.5f;

// How long a shape holds before auto-cycling to the next one.
constexpr float kShapeCycleSeconds = 6.0f;

// The environment/stage (see gfx/VoidFloor.h): a floor well below the fog
// volume (which normally spans roughly y in [-1.5, 6] around fieldCenter_)
// so it's visibly a separate, distant surface. kOverheadLightPos is
// *not* a real light on the particles (see Draw() -- it's deliberately
// left out of the lights[] array): its only jobs are (1) the angle for
// the fake self-shadow (GpuParticleSystem::SetShading), kept subtle via
// a high kShadeAmbientFloor so it reads as gentle form, not external
// sunlight, and (2) VoidFloor's soft light-pool center. The fog's color
// should read as coming from within (the core light, see Update()), not
// from being lit from outside.
// Close enough below the fog's typical extent (roughly y in [-1.5, 6]
// around fieldCenter_) to read as clearly separate, but not so far that
// the camera's default pitch (see App::Init) puts it below the frame --
// at a shallow viewing angle a ground plane recedes toward the horizon
// far faster than its raw distance below the subject suggests.
constexpr float kFloorY = -2.2f;
const Vector3 kOverheadLightPos{ 1.5f, 16.0f, -3.0f };
// High: keeps the self-shadowed side of the structure only slightly
// dimmer than the lit side -- a subtle sense of form/dimension, not a
// bright-side/dark-side split that would read as an external key light
// rather than "a mass glowing from within."
constexpr float kShadeAmbientFloor = 0.75f;
}

void NeonFogVisualizer::Init(ShaderLibrary& shaders, ParticleRenderer& renderer) {
    fog_ = std::make_unique<GpuParticleSystem>(shaders, renderer, kFogCapacity);

    shapeField_ = std::make_unique<ShapeField>(shaders, kShapeGridResolution, fieldCenter_, kShapeGridHalfExtent);
    shapeProvider_ = std::make_unique<ProceduralShapeProvider>(ProceduralShapeType::Sphere);
    shapeProvider_->BakeInto(*shapeField_, 0.0f);
    fog_->SetShapeField(shapeField_.get());

    gravityForceIndex_ = fog_->AddForce(gpu_force::GravityWell(fieldCenter_, kGravityStrength, kGravitySoftening));
    turbulenceForceIndex_ = fog_->AddForce(gpu_force::Turbulence(kTurbulenceStrength, kTurbulenceScale));
    fog_->AddForce(gpu_force::Drag(1.0f));
    shapeConformForceIndex_ = fog_->AddForce(
        gpu_force::ShapeConform(kShapeAttraction, kShapeCurl, 0.0f, kShapeRecruitFraction));

    // Fake self-shadow shading direction matches the overhead key light's
    // actual angle, so the floor's light pool (gfx/VoidFloor) and the
    // fog's own brighter/dimmer sides agree on where "up toward the
    // light" is.
    fog_->SetShading(Vector3Subtract(kOverheadLightPos, fieldCenter_), kShadeAmbientFloor);

    floor_.Init(shaders);
}

void NeonFogVisualizer::Update(const FrameContext& frame) {
    // Shape attraction is driven purely by morphForce_ -- an independent
    // value the user controls directly (see AdjustPrimary), not derived
    // from audio in any way. Eased toward its target so shape changes
    // still read as organic emergence/dissolution rather than a snap.
    morphTarget_ = Clamp(morphForce_, 0.0f, 1.5f);
    morphStrength_ += (morphTarget_ - morphStrength_) * std::min(1.0f, frame.dt * 1.5f);

    fog_->SetForce(shapeConformForceIndex_,
        gpu_force::ShapeConform(kShapeAttraction, kShapeCurl, morphStrength_, kShapeRecruitFraction));

    // Gravity/turbulence are constant now too -- see the class comment:
    // audio drives lighting/color only, motion is independent of it.
    fog_->SetForce(gravityForceIndex_, gpu_force::GravityWell(fieldCenter_, kGravityStrength, kGravitySoftening));
    fog_->SetForce(turbulenceForceIndex_, gpu_force::Turbulence(kTurbulenceStrength, kTurbulenceScale));

    // Auto-advance through shape presets on a fixed timer so the morph is
    // visible without any input; 'S'/'M' (SecondaryAction/TertiaryAction)
    // still let the user force a change or stop the timer.
    if (autoCycle_) {
        shapeTimer_ += frame.dt;
        if (shapeTimer_ >= kShapeCycleSeconds) {
            shapeTimer_ = 0.0f;
            CycleShapePreset();
        }
    }

    if (frame.audio.BeatTriggered()) beatFlash_ = 1.0f;
    beatFlash_ = std::max(0.0f, beatFlash_ - frame.dt * 2.0f);

    // The core light: the fog's *only* real light source now (see Draw()
    // -- the overhead position no longer contributes to lightBoost), so
    // this is what "glowing from within" actually means: everything the
    // fog's color does comes from here. Bass/treble nudge its hue within
    // the cool cyan-blue family (Tron Legacy palette, not the earlier
    // violet). Intensity raised and saturation raised back up from the
    // last pass -- at the previous resting value the inverse-square
    // falloff (particle_render.vert) was too weak to saturate color out
    // to the shape's own surface (radius ~2.5-3.5), reading as grey
    // particles rather than a vivid internal glow.
    float coreHue = 200.0f - frame.audio.Bass() * 15.0f + frame.audio.Treble() * 20.0f;
    coreLightColor_ = ColorFromHSV(std::fmod(coreHue + 360.0f, 360.0f), 0.75f, 1.0f);
    coreLightIntensity_ = (3.0f + frame.audio.Energy() * 4.0f + beatFlash_ * 2.5f) * frame.intensity;

    // Emission: every particle is born already on (or just off) the
    // *current* shape's surface via GpuEmitMode::ShapeSurface -- a single
    // Newton/gradient step against the bound ShapeField (see
    // particle_emit.comp), not a slow drift-in from a diffuse spawn
    // volume. This is the standard professional-VFX pattern for
    // "structure that reacts instantly": bias birth position by the SDF
    // instead of relying purely on a force to drag particles there over
    // several seconds.
    //
    // Life is differentiated (ep.life vs ep.shedLife, both gated by
    // ep.recruitFraction -- see particle_emit.comp and
    // GpuEmitParams::recruitFraction): the ~85% "core" fraction gets a
    // long life and is what the ShapeConform force actually holds onto
    // the shape, so it persists across a shape change and *flows* from
    // the old silhouette to the new one -- this is what makes the whole
    // thing read as one volume morphing, not a fresh population
    // replacing the old one. Only the ~15% "shed" fraction gets a short
    // life, reading as occasional wisps peeling off and dissipating.
    // Constant/not audio-driven -- see the class comment.
    //
    // Paced for steady-state (spawnRate * life, split by recruitFraction)
    // well under kFogCapacity: core 0.85*9800*30 ~= 250000, shed
    // 0.15*9800*6 ~= 8800, total ~= 259000 of 320000.
    constexpr float kSpawnRate = 9800.0f;
    spawnAccumulator_ += frame.dt * kSpawnRate;
    int spawnCount = static_cast<int>(spawnAccumulator_);
    if (spawnCount > 0) {
        spawnAccumulator_ -= static_cast<float>(spawnCount);

        GpuEmitParams ep;
        ep.mode = GpuEmitMode::ShapeSurface;
        ep.position = fieldCenter_;
        ep.positionJitter = { kShapeCandidateHalfExtent, kShapeCandidateHalfExtent, kShapeCandidateHalfExtent };
        // Soft skin, not a razor-thin shell -- see particle_emit.comp.
        ep.shellThickness = 0.2f;
        // Slow, gentle drift -- real fog moves like a gas, not a spray.
        ep.velocity = { 0, 0.08f, 0 };
        ep.velocityJitter = { 0.12f, 0.12f, 0.12f };

        // Near-neutral albedo: low saturation, moderate-low value, both
        // cool blue (Tron Legacy palette -- no violet anywhere anymore).
        // Visible color overwhelmingly comes from the core light and
        // lightning via the light-boost in particle_render.vert, not
        // from this base -- see the class comment for why.
        ep.colorA = ColorFromHSV(205.0f, 0.10f, 0.20f);
        ep.colorB = ColorFromHSV(195.0f, 0.06f, 0.28f);

        // Small and dense rather than large and sparse: individually
        // visible circles is a sprite-shape problem, not just a size
        // problem (see particle_render.frag's noise-broken alpha mask),
        // but sprites this small still need heavy overlap to read as
        // continuous fog texture instead of scattered wisps -- that
        // overlap is what the higher kFogCapacity/kSpawnRate above buys.
        ep.size = 0.16f;
        ep.sizeJitter = 0.07f;

        // Long "core" life: this is the population ShapeConform actually
        // holds onto the shape (see kShapeRecruitFraction) and what makes
        // the fog persist as one mass across shape changes instead of
        // popping in/out every few seconds.
        ep.life = 30.0f;
        ep.lifeJitter = 8.0f;
        ep.recruitFraction = kShapeRecruitFraction;
        // Short "shed" life: only the ~15% that misses recruitment gets
        // this -- long enough to visibly drift as a wisp of haze before
        // dying, short enough that it reads as occasional, not the norm.
        ep.shedLife = 6.0f;
        ep.shedLifeJitter = 2.0f;

        fog_->Emit(ep, spawnCount);
    }

    // Lightning: strong beats fire a bolt; a short cooldown keeps a burst
    // of rapid beats from spawning bolts on top of each other.
    lightningCooldown_ = std::max(0.0f, lightningCooldown_ - frame.dt);
    bool strongBeat = frame.audio.BeatTriggered() && frame.audio.BeatIntensity() > 0.45f;
    bool fireLightning = strongBeat && lightningCooldown_ <= 0.0f;
    float lightningStrength = Clamp(frame.audio.BeatIntensity() + frame.audio.Treble(), 0.0f, 1.0f);

    lightning_.Update(frame.dt, fieldCenter_, fireLightning, lightningStrength);
    if (fireLightning) lightningCooldown_ = 0.1f;

    fog_->Update(frame.dt, frame.time);
    lastTime_ = frame.time; // Draw() is const with no FrameContext of its own -- see the member's comment
}

void NeonFogVisualizer::Draw(const RenderContext& ctx) const {
    // The stage: drawn first as opaque background geometry (default blend,
    // depth test+write on) so the fog correctly blends over it afterward
    // (ParticleRenderer::Draw disables depth *write* but keeps the test).
    VoidFloor::Params floorParams;
    floorParams.center = { fieldCenter_.x, kFloorY, fieldCenter_.z };
    floorParams.shapeSampleY = fieldCenter_.y;
    floorParams.lightPoolCenter = floorParams.center; // pool sits under the overhead light, which is above fieldCenter_
    floor_.Draw(floorParams, shapeField_.get());

    // Core light occupies slot 0; lightning bolts fill the rest. No
    // overhead-light entry here deliberately -- see the class comment
    // and kOverheadLightPos's comment: it's not a real light on the
    // particles, only a shading angle and a floor-pool center.
    std::array<LightSample, kMaxParticleLights> lights{};
    lights[0] = LightSample{ fieldCenter_, coreLightIntensity_, coreLightColor_ };
    int lightCount = 1 + lightning_.GatherLights(lights.data() + 1, kMaxParticleLights - 1);

    // Standard alpha blending, not additive: the fog is meant to read as
    // a gas being lit, not a self-luminous energy cloud -- see the class
    // comment. Unsorted (no per-particle depth sort): at this density,
    // order-of-blend errors between individual soft, dim sprites are
    // imperceptible, which is the standard real-time-VFX approximation
    // for dense smoke/fog (an order-independent-transparency shortcut),
    // not a shortcut specific to this engine.
    BeginBlendMode(BLEND_ALPHA);
    if (fog_) {
        // fadeMode 2 (particle_render.vert's two-sided fade, in over the
        // first ~25% of life and out over the final ~17%) instead of 1
        // (instant-full-opacity-at-spawn): with mode 1 every newly
        // spawned particle snapped to full brightness the instant it
        // existed, which read as a visible "pop in" no matter how the
        // lifecycle/spawn-rate was tuned.
        fog_->Draw(ctx.viewProj, ctx.cameraRight, ctx.cameraUp, /*fadeMode=*/2, /*sizeScale=*/1.0f,
                   lights.data(), lightCount, lastTime_);
    }
    EndBlendMode();

    // Lightning bolts are genuinely light-emitting, so additive is the
    // physically-appropriate blend mode for them specifically.
    BeginBlendMode(BLEND_ADDITIVE);
    lightning_.Draw();
    EndBlendMode();
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

const char* NeonFogVisualizer::ExtraStatusLine() const {
    float secondsToNext = autoCycle_ ? std::max(0.0f, kShapeCycleSeconds - shapeTimer_) : 0.0f;
    return TextFormat(
        "Shape: %s (S to cycle)  Morph force: %.2f (-/=)  Morph: %.0f%%  Auto-cycle: %s (M)%s",
        ShapeName(shapeProvider_->Type()), morphForce_, morphStrength_ * 100.0f,
        autoCycle_ ? "on" : "off",
        autoCycle_ ? TextFormat("  Next in: %.1fs", secondsToNext) : "");
}

void NeonFogVisualizer::CycleShapePreset() {
    // Walks Sphere -> Box -> Torus -> Cylinder -> Sphere ... Never disables
    // the shape entirely -- to see pure ambient fog, drive morphForce_ to
    // 0 via '-' instead (see AdjustPrimary); 'M' separately toggles
    // whether this cycle advances on its own.
    //
    // No Head/face shape here -- an earlier crude analytic stand-in was
    // removed; the real thing comes later via a mesh-driven
    // MeshShapeProvider (see shapes/ShapeProvider.h), not an approximate
    // SDF baked in shape_bake.comp.
    switch (shapeProvider_->Type()) {
        case ProceduralShapeType::Sphere: shapeProvider_->SetType(ProceduralShapeType::Box); break;
        case ProceduralShapeType::Box: shapeProvider_->SetType(ProceduralShapeType::Torus); break;
        case ProceduralShapeType::Torus: shapeProvider_->SetType(ProceduralShapeType::Cylinder); break;
        case ProceduralShapeType::Cylinder: shapeProvider_->SetType(ProceduralShapeType::Sphere); break;
    }

    shapeProvider_->BakeInto(*shapeField_, 0.0f);
}

void NeonFogVisualizer::AdjustPrimary(float delta) {
    morphForce_ = Clamp(morphForce_ + delta, 0.0f, 1.5f);
}
