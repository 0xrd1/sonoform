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
constexpr int kFogCapacity = 400000;
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

constexpr float kTurbulenceStrength = 0.3f;
constexpr float kTurbulenceScale = 0.08f;

// Moderate: particles already spawn on the surface (see Update's emission
// block), so this force's job in steady state is mostly *maintaining*
// that position against turbulence/gravity and pulling still-alive
// particles across to a new shape when one is selected -- not doing the
// initial recruiting work the old design relied on it for.
constexpr float kShapeAttraction = 1.4f;
constexpr float kShapeCurl = 0.6f;
// The fraction of particles that stay locked to the shape for their whole
// life; the rest shed off it under gravity/turbulence alone once spawned,
// reading as haze drifting away from a solid core -- see the emission
// comment in Update().
constexpr float kShapeRecruitFraction = 0.65f;

// How far out (from the shape's center) candidate spawn points are chosen
// before being projected onto the surface -- see GpuEmitMode::ShapeSurface.
// Kept within kShapeGridHalfExtent with margin (worst-case box corner is
// ~1.73x this) so projection never samples a clamped edge voxel.
constexpr float kShapeCandidateHalfExtent = 3.5f;

// How long a shape holds before auto-cycling to the next one.
constexpr float kShapeCycleSeconds = 6.0f;

// The environment/stage (see gfx/VoidFloor.h): a floor well below the fog
// volume (which normally spans roughly y in [-1.5, 6] around fieldCenter_)
// so it's visibly a separate, distant surface, plus a genuine second light
// source -- distinct from the internal core light -- positioned high above
// so it acts like an overhead key light, giving the fog real directional
// shading (see GpuParticleSystem::SetShading) instead of only the core
// light's inverse-square falloff.
// Close enough below the fog's typical extent (roughly y in [-1.5, 6]
// around fieldCenter_) to read as clearly separate, but not so far that
// the camera's default pitch (see App::Init) puts it below the frame --
// at a shallow viewing angle a ground plane recedes toward the horizon
// far faster than its raw distance below the subject suggests.
constexpr float kFloorY = -2.2f;
const Vector3 kOverheadLightPos{ 1.5f, 16.0f, -3.0f };
constexpr float kOverheadLightIntensity = 3.0f;
// Cool near-white -- Tron Legacy palette, not the fog's own violet-free
// blue tint (see the core light / albedo colors in Update()).
const Color kOverheadLightColor = ColorFromHSV(200.0f, 0.15f, 1.0f);
// Keeps the self-shadowed side of the structure dim, not pure black --
// still reads as fog, not a hard-shaded solid.
constexpr float kShadeAmbientFloor = 0.35f;
}

void NeonFogVisualizer::Init(ShaderLibrary& shaders, ParticleRenderer& renderer) {
    fog_ = std::make_unique<GpuParticleSystem>(shaders, renderer, kFogCapacity);

    shapeField_ = std::make_unique<ShapeField>(shaders, kShapeGridResolution, fieldCenter_, kShapeGridHalfExtent);
    shapeProvider_ = std::make_unique<ProceduralShapeProvider>(ProceduralShapeType::Sphere);
    shapeProvider_->BakeInto(*shapeField_, 0.0f);
    fog_->SetShapeField(shapeField_.get());

    gravityForceIndex_ = fog_->AddForce(gpu_force::GravityWell(fieldCenter_, kGravityStrength, kGravitySoftening));
    turbulenceForceIndex_ = fog_->AddForce(gpu_force::Turbulence(kTurbulenceStrength, kTurbulenceScale));
    fog_->AddForce(gpu_force::Drag(0.8f));
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

    // The core light: the fog's one persistent light source, positioned
    // at the shape's center. Bass/treble nudge its hue within the cool
    // cyan-blue family (Tron Legacy palette, not the earlier violet);
    // energy and beat flashes drive its intensity, and lower saturation
    // than before means a bright swell pushes toward white rather than
    // staying a saturated color. This is what actually colors the fog
    // (see the class comment) -- particle albedo itself stays neutral.
    float coreHue = 200.0f - frame.audio.Bass() * 15.0f + frame.audio.Treble() * 20.0f;
    coreLightColor_ = ColorFromHSV(std::fmod(coreHue + 360.0f, 360.0f), 0.55f, 1.0f);
    coreLightIntensity_ = (1.5f + frame.audio.Energy() * 4.0f + beatFlash_ * 2.5f) * frame.intensity;

    // Emission: every particle is born already on (or just off) the
    // *current* shape's surface via GpuEmitMode::ShapeSurface -- a single
    // Newton/gradient step against the bound ShapeField (see
    // particle_emit.comp), not a slow drift-in from a diffuse spawn
    // volume. This is the standard professional-VFX pattern for
    // "structure that reacts instantly": bias birth position by the SDF
    // instead of relying purely on a force to drag particles there over
    // several seconds. It also makes a shape change (CycleShapePreset)
    // read immediately -- freshly spawned particles appear on the *new*
    // shape within a fraction of a second, layered with the still-alive
    // recruited particles from before, which the ShapeConform force
    // visibly migrates from the old surface to the new one (see the
    // class comment: that migration is the actual "attraction" this
    // pipeline exists to prove). Constant/not audio-driven -- see the
    // class comment. Paced so steady-state population (spawnRate * life)
    // sits well under kFogCapacity: 50000 * 6 = 300000 of 400000, leaving
    // headroom for transition bursts.
    constexpr float kSpawnRate = 50000.0f;
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
        // Short enough for fast turnover (a shape change reads within
        // roughly one lifetime) but long enough for shed (unrecruited)
        // particles to visibly drift as haze before dying.
        ep.life = 6.0f;
        ep.lifeJitter = 2.0f;

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

    // Core light occupies slot 0, the overhead key light slot 1; lightning
    // bolts fill the rest.
    std::array<LightSample, kMaxParticleLights> lights{};
    lights[0] = LightSample{ fieldCenter_, coreLightIntensity_, coreLightColor_ };
    lights[1] = LightSample{ kOverheadLightPos, kOverheadLightIntensity, kOverheadLightColor };
    int lightCount = 2 + lightning_.GatherLights(lights.data() + 2, kMaxParticleLights - 2);

    // Standard alpha blending, not additive: the fog is meant to read as
    // a gas being lit, not a self-luminous energy cloud -- see the class
    // comment. Unsorted (no per-particle depth sort): at this density,
    // order-of-blend errors between individual soft, dim sprites are
    // imperceptible, which is the standard real-time-VFX approximation
    // for dense smoke/fog (an order-independent-transparency shortcut),
    // not a shortcut specific to this engine.
    BeginBlendMode(BLEND_ALPHA);
    if (fog_) {
        fog_->Draw(ctx.viewProj, ctx.cameraRight, ctx.cameraUp, /*fadeMode=*/1, /*sizeScale=*/1.0f,
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
