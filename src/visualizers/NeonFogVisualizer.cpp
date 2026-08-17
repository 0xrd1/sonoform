#include "NeonFogVisualizer.h"
#include "ForceFactory.h"
#include "raymath.h"
#include <algorithm>
#include <array>
#include <cmath>

void NeonFogVisualizer::Init(ShaderLibrary& shaders, ParticleRenderer& renderer) {
    // Re-entrant: this also runs on the settings panel's "Rebuild Systems"
    // action, so every value read below comes from the *current* settings
    // members (which the user may already have tuned), never from a local
    // constant -- re-running Init() must apply live edits, not discard them.
    fog_ = std::make_unique<GpuParticleSystem>(shaders, renderer, emissionSettings_.capacity);

    shapeField_ = std::make_unique<ShapeField>(shaders, shapeSettings_.gridResolution,
                                                shapeSettings_.fieldCenter, shapeSettings_.gridHalfExtent);
    shapeProvider_ = std::make_unique<ProceduralShapeProvider>(static_cast<ProceduralShapeType>(shapeSettings_.shapeType));
    shapeProvider_->BakeInto(*shapeField_, 0.0f);
    fog_->SetShapeField(shapeField_.get());

    gravityForceIndex_ = fog_->AddForce(
        gpu_force::GravityWell(shapeSettings_.fieldCenter, forceSettings_.gravityStrength, forceSettings_.gravitySoftening));
    turbulenceForceIndex_ = fog_->AddForce(gpu_force::Turbulence(forceSettings_.turbulenceStrength, forceSettings_.turbulenceScale));
    dragForceIndex_ = fog_->AddForce(gpu_force::Drag(forceSettings_.dragCoefficient));
    shapeConformForceIndex_ = fog_->AddForce(
        gpu_force::ShapeConform(forceSettings_.shapeAttraction, forceSettings_.shapeCurl, 0.0f, shapeSettings_.recruitFraction,
                                 shapeSettings_.volumeDepth, forceSettings_.flowNoiseScale));

    // Fake self-shadow shading direction matches the overhead key light's
    // actual angle, so the floor's light pool (gfx/VoidFloor) and the
    // fog's own brighter/dimmer sides agree on where "up toward the
    // light" is.
    fog_->SetShading(Vector3Subtract(lightingSettings_.overheadLightPos, shapeSettings_.fieldCenter), lightingSettings_.shadeAmbientFloor);

    floor_.Init(shaders);

    // See settingsSeeded_'s comment on why this only runs once: Init() is
    // also the "Rebuild Systems" entry point, and re-seeding the floor's
    // look on every rebuild would silently discard any floor tuning the
    // user had already dialed in via the panel.
    if (!settingsSeeded_) {
        settingsSeeded_ = true;
        // Close enough below the fog's typical extent (roughly y in
        // [-1.5, 6] around fieldCenter_) to read as clearly separate, but
        // not so far that the camera's default pitch (see App::Init) puts
        // it below the frame -- at a shallow viewing angle a ground plane
        // recedes toward the horizon far faster than its raw distance
        // below the subject suggests.
        constexpr float kInitialFloorY = -2.2f;
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

    fog_->SetForce(shapeConformForceIndex_,
        gpu_force::ShapeConform(forceSettings_.shapeAttraction, forceSettings_.shapeCurl, morphStrength_, shapeSettings_.recruitFraction,
                                 shapeSettings_.volumeDepth, forceSettings_.flowNoiseScale));

    // Forces are re-pushed every frame so panel edits apply live -- see
    // ui::FogForceSettings.
    fog_->SetForce(gravityForceIndex_,
        gpu_force::GravityWell(shapeSettings_.fieldCenter, forceSettings_.gravityStrength, forceSettings_.gravitySoftening));
    fog_->SetForce(turbulenceForceIndex_, gpu_force::Turbulence(forceSettings_.turbulenceStrength, forceSettings_.turbulenceScale));
    fog_->SetForce(dragForceIndex_, gpu_force::Drag(forceSettings_.dragCoefficient));

    // Cheap CPU-side (two field writes -- see GpuParticleSystem::SetShading),
    // so re-calling every frame is fine and is what makes Lighting's
    // Overhead Light Position / Shade Ambient Floor live-tunable.
    fog_->SetShading(Vector3Subtract(lightingSettings_.overheadLightPos, shapeSettings_.fieldCenter), lightingSettings_.shadeAmbientFloor);

    // The shape target re-bakes whenever shapeType actually changes, from
    // *either* the keyboard ('S' -> CycleShapePreset -> shapeSettings_.shapeType)
    // or the settings panel's Shape combo editing shapeSettings_.shapeType
    // directly -- both funnel through this one check, so there's exactly
    // one place that talks to shapeProvider_/shapeField_.
    if (shapeSettings_.shapeType != static_cast<int>(shapeProvider_->Type())) {
        shapeProvider_->SetType(static_cast<ProceduralShapeType>(shapeSettings_.shapeType));
        shapeProvider_->BakeInto(*shapeField_, 0.0f);
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
    // this is what "glowing from within" actually means: everything the
    // fog's color does comes from here. Bass/treble nudge its hue within
    // the cool cyan-blue family (Tron Legacy palette, not the earlier
    // violet).
    float coreHue = lightingSettings_.coreHueBase + frame.audio.Bass() * lightingSettings_.coreHueBassScale +
                     frame.audio.Treble() * lightingSettings_.coreHueTrebleScale +
                     frame.time * lightingSettings_.hueCycleSpeed;
    coreLightColor_ = ColorFromHSV(std::fmod(coreHue + 360.0f, 360.0f), lightingSettings_.coreSaturation, lightingSettings_.coreValue);
    coreLightIntensity_ = (lightingSettings_.intensityBase + frame.audio.Energy() * lightingSettings_.intensityEnergyScale +
                            beatFlash_ * lightingSettings_.intensityBeatFlashScale) * frame.intensity;

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
    // GpuEmitParams::recruitFraction): the recruited fraction
    // (ShapeSettings::recruitFraction) gets a long life and is what the
    // ShapeConform force actually holds onto the shape, so it persists
    // across a shape change and *flows* from the old silhouette to the new
    // one -- this is what makes the whole thing read as one volume
    // morphing, not a fresh population replacing the old one. Only the
    // fraction that misses recruitment gets a short life, reading as
    // occasional wisps peeling off and dissipating. Constant/not
    // audio-driven -- see the class comment.
    spawnAccumulator_ += frame.dt * emissionSettings_.spawnRate;
    int spawnCount = static_cast<int>(spawnAccumulator_);
    if (spawnCount > 0) {
        spawnAccumulator_ -= static_cast<float>(spawnCount);

        GpuEmitParams ep;
        ep.mode = GpuEmitMode::ShapeSurface;
        ep.position = shapeSettings_.fieldCenter;
        ep.positionJitter = { shapeSettings_.candidateHalfExtent, shapeSettings_.candidateHalfExtent, shapeSettings_.candidateHalfExtent };
        ep.shellThickness = shapeSettings_.shellThickness;
        ep.velocity = emissionSettings_.velocity;
        ep.velocityJitter = emissionSettings_.velocityJitter;

        ep.colorA = emissionSettings_.colorA;
        ep.colorB = emissionSettings_.colorB;

        ep.size = emissionSettings_.size;
        ep.sizeJitter = emissionSettings_.sizeJitter;

        ep.life = emissionSettings_.life;
        ep.lifeJitter = emissionSettings_.lifeJitter;
        ep.recruitFraction = shapeSettings_.recruitFraction;
        ep.shedLife = emissionSettings_.shedLife;
        ep.shedLifeJitter = emissionSettings_.shedLifeJitter;

        fog_->Emit(ep, spawnCount);
    }

    // Lightning: strong beats fire a bolt; a short cooldown keeps a burst
    // of rapid beats from spawning bolts on top of each other.
    lightningCooldown_ = std::max(0.0f, lightningCooldown_ - frame.dt);
    bool strongBeat = frame.audio.BeatTriggered() && frame.audio.BeatIntensity() > lightningSettings_.beatIntensityThreshold;
    bool fireLightning = strongBeat && lightningCooldown_ <= 0.0f;
    float lightningStrength = Clamp(frame.audio.BeatIntensity() + frame.audio.Treble(), 0.0f, 1.0f);

    lightning_.Update(frame.dt, shapeSettings_.fieldCenter, fireLightning, lightningStrength, lightningSettings_);
    if (fireLightning) lightningCooldown_ = lightningSettings_.cooldownSeconds;

    fog_->Update(frame.dt, frame.time);
    lastTime_ = frame.time; // Draw() is const with no FrameContext of its own -- see the member's comment
}

namespace {
// Debug-gizmo-only analytic SDFs, ported directly from shape_bake.comp's
// EvalShape (sdSphere/sdRoundBox/sdTorus/sdCappedCylinder) for a CPU-side
// gradient (central differences) used only by the Force Vectors gizmo --
// see NeonFogVisualizer::Draw. Same cross-file-must-match-the-GLSL risk
// already accepted for ProceduralShapeType's enum values; if the shader's
// dimensions change, these (and DrawShapeWireframe below) need updating
// too. Not used by the sim itself -- that always samples the real baked
// ShapeField, never this.
float EvalShapeSdf(ProceduralShapeType type, Vector3 p) {
    switch (type) {
        case ProceduralShapeType::Sphere:
            return Vector3Length(p) - 3.0f;
        case ProceduralShapeType::Box: {
            Vector3 q{ fabsf(p.x) - 2.4f, fabsf(p.y) - 2.4f, fabsf(p.z) - 2.4f };
            Vector3 qMax{ fmaxf(q.x, 0.0f), fmaxf(q.y, 0.0f), fmaxf(q.z, 0.0f) };
            float outside = Vector3Length(qMax);
            float inside = fminf(fmaxf(q.x, fmaxf(q.y, q.z)), 0.0f);
            return outside + inside - 0.4f;
        }
        case ProceduralShapeType::Torus: {
            float qx = sqrtf(p.x * p.x + p.z * p.z) - 2.5f;
            return sqrtf(qx * qx + p.y * p.y) - 1.0f;
        }
        case ProceduralShapeType::Cylinder: {
            float dx = fabsf(sqrtf(p.x * p.x + p.z * p.z)) - 2.0f;
            float dy = fabsf(p.y) - 2.6f;
            float ax = fmaxf(dx, 0.0f), ay = fmaxf(dy, 0.0f);
            return fminf(fmaxf(dx, dy), 0.0f) + sqrtf(ax * ax + ay * ay);
        }
    }
    return 0.0f;
}

Vector3 EvalShapeGradient(ProceduralShapeType type, Vector3 p) {
    constexpr float e = 0.05f;
    float dx = EvalShapeSdf(type, p + Vector3{ e, 0, 0 }) - EvalShapeSdf(type, p - Vector3{ e, 0, 0 });
    float dy = EvalShapeSdf(type, p + Vector3{ 0, e, 0 }) - EvalShapeSdf(type, p - Vector3{ 0, e, 0 });
    float dz = EvalShapeSdf(type, p + Vector3{ 0, 0, e }) - EvalShapeSdf(type, p - Vector3{ 0, 0, e });
    return Vector3Normalize(Vector3{ dx, dy, dz });
}

// Wireframe dimensions must match shape_bake.comp's EvalShape exactly --
// see the cross-file comment above. Box ignores the shader's 0.4 corner
// rounding (a sharp-cornered wire is a fine debug approximation); the
// torus is simplified to two flat horizontal rings at its outer/inner
// major radius rather than a full tube wireframe (per-segment tangent-
// frame math not worth it for a debug gizmo) -- both still confirm
// scale/position at a glance, which is the gizmo's whole job.
void DrawShapeWireframe(ProceduralShapeType type, Vector3 center, Color color) {
    switch (type) {
        case ProceduralShapeType::Sphere:
            DrawSphereWires(center, 3.0f, 12, 12, color);
            break;
        case ProceduralShapeType::Box:
            DrawCubeWiresV(center, Vector3{ 4.8f, 4.8f, 4.8f }, color);
            break;
        case ProceduralShapeType::Torus:
            DrawCircle3D(center, 3.5f, Vector3{ 1, 0, 0 }, 90.0f, color);
            DrawCircle3D(center, 1.5f, Vector3{ 1, 0, 0 }, 90.0f, color);
            break;
        case ProceduralShapeType::Cylinder:
            DrawCylinderWires(center - Vector3{ 0, 2.6f, 0 }, 2.0f, 2.0f, 5.2f, 16, color);
            break;
    }
}
} // namespace

void NeonFogVisualizer::Draw(const RenderContext& ctx) const {
    // The stage: drawn first as opaque background geometry (default blend,
    // depth test+write on) so the fog correctly blends over it afterward
    // (ParticleRenderer::Draw disables depth *write* but keeps the test).
    floor_.Draw(floorParams_, shapeField_.get());

    // Core light occupies slot 0; lightning bolts fill the rest. No
    // overhead-light entry here deliberately -- see the class comment
    // and FogLightingSettings::overheadLightPos's comment: it's not a real
    // light on the particles, only a shading angle and a floor-pool center.
    std::array<LightSample, kMaxParticleLights> lights{};
    lights[0] = LightSample{ shapeSettings_.fieldCenter, coreLightIntensity_, coreLightColor_ };
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
    lightning_.Draw(lightningSettings_);
    EndBlendMode();

    // Debug gizmos: opaque, default blend, drawn last/on top -- see
    // ui::DebugSettings and RenderContext::debug's comment.
    if (ctx.debug != nullptr) {
        const Vector3 center = shapeSettings_.fieldCenter;

        if (ctx.debug->showShapeBounds) {
            DrawShapeWireframe(static_cast<ProceduralShapeType>(shapeSettings_.shapeType), center, Fade(SKYBLUE, 0.5f));
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
            float ext = shapeSettings_.candidateHalfExtent * 2.0f;
            DrawCubeWiresV(center, Vector3{ ext, ext, ext }, Fade(GREEN, 0.6f));
        }

        if (ctx.debug->showForceVectors) {
            ProceduralShapeType curType = static_cast<ProceduralShapeType>(shapeSettings_.shapeType);
            constexpr float kShellRadius = 4.0f;
            constexpr float kArrowLength = 0.6f;
            constexpr int kLatSteps = 6, kLonSteps = 8;
            for (int lat = 1; lat < kLatSteps; lat++) {
                float theta = PI * float(lat) / kLatSteps; // polar angle; skip the exact poles
                for (int lon = 0; lon < kLonSteps; lon++) {
                    float phi = 2.0f * PI * float(lon) / kLonSteps;
                    Vector3 dir{ sinf(theta) * cosf(phi), cosf(theta), sinf(theta) * sinf(phi) };
                    Vector3 samplePos = center + Vector3Scale(dir, kShellRadius);

                    // Gravity: closed-form, mirrors forces.glsl's
                    // FORCE_GRAVITY_WELL exactly (direction only -- actual
                    // magnitude varies by orders of magnitude across the
                    // shell and isn't useful to show at gizmo-arrow scale).
                    Vector3 toCenter = Vector3Subtract(center, samplePos);
                    Vector3 gravityDir = Vector3Normalize(toCenter);
                    DrawLine3D(samplePos, samplePos + Vector3Scale(gravityDir, kArrowLength), SKYBLUE);

                    // Shape-attraction direction: points from outside
                    // toward the surface, mirroring shape_conform.glsl's
                    // -sign(dist)*gradient (see EvalShapeGradient above).
                    Vector3 grad = EvalShapeGradient(curType, samplePos);
                    float dist = EvalShapeSdf(curType, samplePos);
                    Vector3 attractDir = Vector3Scale(grad, dist > 0.0f ? -1.0f : 1.0f);
                    DrawLine3D(samplePos, samplePos + Vector3Scale(attractDir, kArrowLength), ORANGE);
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

const char* NeonFogVisualizer::ExtraStatusLine() const {
    float secondsToNext = shapeSettings_.autoCycle ? std::max(0.0f, shapeSettings_.cycleSeconds - shapeTimer_) : 0.0f;
    return TextFormat(
        "Shape: %s (S to cycle)  Morph force: %.2f (-/=)  Morph: %.0f%%  Auto-cycle: %s (M)%s",
        ShapeName(shapeProvider_->Type()), shapeSettings_.morphForce, morphStrength_ * 100.0f,
        shapeSettings_.autoCycle ? "on" : "off",
        shapeSettings_.autoCycle ? TextFormat("  Next in: %.1fs", secondsToNext) : "");
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
    v.BeginGroup("Emission");
    emissionSettings_.Visit(v);
    v.EndGroup();

    v.BeginGroup("Forces");
    forceSettings_.Visit(v);
    v.EndGroup();

    v.BeginGroup("Shape");
    shapeSettings_.Visit(v);
    v.EndGroup();

    v.BeginGroup("Lighting");
    lightingSettings_.Visit(v);
    v.EndGroup();

    v.BeginGroup("Lightning");
    lightningSettings_.Visit(v);
    v.EndGroup();

    v.BeginGroup("Floor");
    ui::VisitFloorParams(floorParams_, v);
    v.EndGroup();
}
