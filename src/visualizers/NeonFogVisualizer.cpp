#include "NeonFogVisualizer.h"
#include "ForceFactory.h"
#include "raymath.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace {
// GPU particle systems comfortably run into the hundreds of thousands to
// millions on modern hardware (this is a from-scratch compute pipeline,
// not the CPU-particle-era constraint that made low counts necessary);
// capacity is set close to the target steady-state population rather
// than wildly over-provisioned, since every particle system draws its
// full capacity as instances every frame (dead ones collapse to
// degenerate triangles in-shader -- see particle_render.vert -- so
// capacity, not just alive count, drives render cost).
constexpr int kFogCapacity = 600000;
constexpr int kShapeGridResolution = 40;
constexpr float kShapeGridHalfExtent = 4.5f;
}

void NeonFogVisualizer::Init(ShaderLibrary& shaders, ParticleRenderer& renderer) {
    fog_ = std::make_unique<GpuParticleSystem>(shaders, renderer, kFogCapacity);

    shapeField_ = std::make_unique<ShapeField>(shaders, kShapeGridResolution, fieldCenter_, kShapeGridHalfExtent);
    shapeProvider_ = std::make_unique<ProceduralShapeProvider>(ProceduralShapeType::Head);
    shapeProvider_->BakeInto(*shapeField_, 0.0f);
    fog_->SetShapeField(shapeField_.get());

    gravityForceIndex_ = fog_->AddForce(gpu_force::GravityWell(fieldCenter_, 2.0f, 2.5f));
    turbulenceForceIndex_ = fog_->AddForce(gpu_force::Turbulence(0.4f, 0.08f));
    fog_->AddForce(gpu_force::Drag(0.45f));
    shapeConformForceIndex_ = fog_->AddForce(gpu_force::ShapeConform(1.1f, 0.7f, 0.0f));
}

void NeonFogVisualizer::Update(const FrameContext& frame) {
    // Structure emerges on musical swells (bass + overall energy) and
    // eases toward its target so shape changes read as organic
    // emergence/dissolution, not a snap.
    morphTarget_ = shapeEnabled_
        ? Clamp((frame.audio.Energy() * 3.0f + frame.audio.Bass() * 1.5f) * frame.intensity, 0.0f, 1.0f)
        : 0.0f;
    morphStrength_ += (morphTarget_ - morphStrength_) * std::min(1.0f, frame.dt * 1.5f);

    fog_->SetForce(shapeConformForceIndex_,
        gpu_force::ShapeConform(1.1f * frame.intensity, 0.7f, morphStrength_));

    // Kept modest and with generous softening: strong gravity concentrates
    // too much mass right at the core light's position, which -- combined
    // with that light's intensity -- is exactly the recipe for the fog's
    // brightest point blowing out to solid white.
    float gravityStrength = 1.0f + frame.audio.Bass() * 1.5f * frame.intensity;
    fog_->SetForce(gravityForceIndex_, gpu_force::GravityWell(fieldCenter_, gravityStrength, 3.5f));

    float turbStrength = 0.3f + frame.audio.Treble() * 1.8f * frame.intensity;
    fog_->SetForce(turbulenceForceIndex_, gpu_force::Turbulence(turbStrength, 0.08f));

    if (frame.audio.BeatTriggered()) beatFlash_ = 1.0f;
    beatFlash_ = std::max(0.0f, beatFlash_ - frame.dt * 2.0f);

    // The core light: the fog's one persistent light source, positioned
    // at the shape's center. Bass pulls its hue toward magenta, treble
    // toward cyan; energy and beat flashes drive its intensity. This is
    // what actually colors the fog (see the class comment) -- particle
    // albedo itself stays close to neutral.
    float coreHue = 285.0f - frame.audio.Bass() * 40.0f + frame.audio.Treble() * 30.0f;
    coreLightColor_ = ColorFromHSV(std::fmod(coreHue + 360.0f, 360.0f), 0.75f, 1.0f);
    coreLightIntensity_ = (1.5f + frame.audio.Energy() * 4.0f + beatFlash_ * 2.5f) * frame.intensity;

    // Continuous ambient emission, distributed through a volume around
    // the shape's center so it reads as a cloud rather than a point
    // source, replenishing particles as they die to keep the fog dense.
    float spawnRate = 60000.0f + frame.audio.Energy() * 60000.0f * frame.intensity;
    spawnAccumulator_ += frame.dt * spawnRate;
    int spawnCount = static_cast<int>(spawnAccumulator_);
    if (spawnCount > 0) {
        spawnAccumulator_ -= static_cast<float>(spawnCount);

        GpuEmitParams ep;
        ep.mode = GpuEmitMode::Box;
        ep.position = fieldCenter_;
        ep.positionJitter = { 5.5f, 5.5f, 5.5f };
        // Slow, gentle drift -- real fog moves like a gas, not a spray.
        ep.velocity = { 0, 0.08f, 0 };
        ep.velocityJitter = { 0.12f, 0.12f, 0.12f };

        // Near-neutral albedo: low saturation, moderate-low value, with
        // only the faintest violet/cyan tinge as the "material" color.
        // Visible color overwhelmingly comes from the core light and
        // lightning via the light-boost in particle_render.vert, not
        // from this base -- see the class comment for why.
        ep.colorA = ColorFromHSV(275.0f, 0.12f, 0.16f);
        ep.colorB = ColorFromHSV(190.0f, 0.12f, 0.14f);

        // Large, soft, overlapping sprites read as an actual volume
        // under alpha blending (which naturally caps brightness as
        // layers stack, unlike additive) -- favoring size over raw count
        // for density is the standard real-time-VFX lever for this look.
        ep.size = 1.3f + frame.audio.Energy() * 0.5f * frame.intensity;
        ep.sizeJitter = 0.5f;
        ep.life = 7.0f;
        ep.lifeJitter = 2.5f;

        fog_->Emit(ep, spawnCount);
    }

    if (frame.audio.BeatTriggered()) {
        fog_->ApplyRadialImpulse(fieldCenter_, 2.0f * frame.intensity * (0.5f + frame.audio.BeatIntensity()), 8.0f);
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
}

void NeonFogVisualizer::Draw(const RenderContext& ctx) const {
    // Core light always occupies slot 0; lightning bolts fill the rest.
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
        fog_->Draw(ctx.viewProj, ctx.cameraRight, ctx.cameraUp, /*fadeMode=*/1, /*sizeScale=*/1.0f,
                   lights.data(), lightCount);
    }
    EndBlendMode();

    // Lightning bolts are genuinely light-emitting, so additive is the
    // physically-appropriate blend mode for them specifically.
    BeginBlendMode(BLEND_ADDITIVE);
    lightning_.Draw();
    EndBlendMode();
}

const char* NeonFogVisualizer::ExtraStatusLine() const {
    if (!shapeEnabled_) return TextFormat("Shape: off (pure fog)  Morph: %.0f%%", morphStrength_ * 100.0f);

    const char* name = "?";
    switch (shapeProvider_->Type()) {
        case ProceduralShapeType::Sphere: name = "Sphere"; break;
        case ProceduralShapeType::Torus: name = "Torus"; break;
        case ProceduralShapeType::Head: name = "Head"; break;
    }
    return TextFormat("Shape: %s (S to cycle)  Morph: %.0f%%", name, morphStrength_ * 100.0f);
}

void NeonFogVisualizer::CycleShapePreset() {
    if (!shapeEnabled_) {
        shapeEnabled_ = true;
        shapeProvider_->SetType(ProceduralShapeType::Sphere);
    } else {
        switch (shapeProvider_->Type()) {
            case ProceduralShapeType::Sphere: shapeProvider_->SetType(ProceduralShapeType::Torus); break;
            case ProceduralShapeType::Torus: shapeProvider_->SetType(ProceduralShapeType::Head); break;
            case ProceduralShapeType::Head: shapeEnabled_ = false; break;
        }
    }

    if (shapeEnabled_) shapeProvider_->BakeInto(*shapeField_, 0.0f);
}
