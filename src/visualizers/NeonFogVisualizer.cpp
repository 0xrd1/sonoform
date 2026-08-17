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
    // The stage: drawn first as opaque background geometry (default blend,
    // depth test+write on) so the fog correctly blends over it afterward
    // (ParticleRenderer::Draw disables depth *write* but keeps the test).
    floor_.Draw(floorParams_, shapeField_.get());

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
    std::array<LightSample, kMaxParticleLights> lights{};
    lights[0] = LightSample{ shapeField_->Center(), coreLightIntensity_, coreLightColor_ };
    int lightCount = 1 + lightning_.GatherLights(lights.data() + 1, kMaxParticleLights - 1);

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
    fog_.Draw(ctx.viewProj, ctx.cameraRight, ctx.cameraUp, lights.data(), lightCount, lastTime_, spriteStyle);
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
            float ext = fog_.attraction.candidateHalfExtent * 2.0f;
            DrawCubeWiresV(center, Vector3{ ext, ext, ext }, Fade(GREEN, 0.6f));
        }

        if (ctx.debug->showForceVectors) {
            constexpr float kShellRadius = 4.0f;
            // Arrow lengths scale with the *actual* current strength
            // settings (not a fixed idealized length) so the gizmo visibly
            // grows/shrinks as Gravity Strength/Shape Attraction are tuned
            // -- a fixed-length arrow that never responds to the setting
            // it's supposedly showing is worse than no gizmo at all. Scale
            // factors are just a legible-on-screen mapping, not physically
            // literal. Shape-attraction is additionally skipped entirely
            // once morphStrength_ is negligible, matching
            // ApplyShapeConform's own early-out (shape_conform.glsl) -- no
            // attraction is actually being applied at that point, so no
            // arrow should claim otherwise.
            float gravityArrowLen = Clamp(fog_.force.gravityStrength * 0.4f, 0.0f, 1.5f);
            bool showAttraction = morphStrength_ > 0.0001f;
            float attractArrowLen = Clamp(fog_.force.shapeAttraction * morphStrength_ * 0.25f, 0.0f, 1.5f);
            for (int lat = 1; lat < kGizmoLatSteps; lat++) {
                float theta = PI * float(lat) / kGizmoLatSteps; // polar angle; skip the exact poles
                for (int lon = 0; lon < kGizmoLonSteps; lon++) {
                    float phi = 2.0f * PI * float(lon) / kGizmoLonSteps;
                    Vector3 dir{ sinf(theta) * cosf(phi), cosf(theta), sinf(theta) * sinf(phi) };
                    Vector3 samplePos = center + Vector3Scale(dir, kShellRadius);

                    // Gravity: closed-form, mirrors forces.glsl's
                    // FORCE_GRAVITY_WELL exactly (direction only -- actual
                    // per-particle magnitude also depends on distance/
                    // softening and isn't useful to show at gizmo-arrow
                    // scale; only the overall strength setting is reflected
                    // in the arrow length above).
                    if (gravityArrowLen > 0.0001f) {
                        Vector3 toCenter = Vector3Subtract(center, samplePos);
                        Vector3 gravityDir = Vector3Normalize(toCenter);
                        DrawLine3D(samplePos, samplePos + Vector3Scale(gravityDir, gravityArrowLen), SKYBLUE);
                    }

                    // Shape-attraction direction: points from outside
                    // toward the surface, mirroring shape_conform.glsl's
                    // -sign(dist)*gradient. Sampled from the real baked
                    // field (shapeField_->SampleWorld -- same data
                    // ApplyShapeConform itself samples), not a hand-copied
                    // analytic SDF, so this can't drift out of sync with
                    // whatever shape is actually baked. Assumes Volume
                    // Depth == 0 (exact-surface targeting) -- with Volume
                    // Depth > 0 each particle's actual target depth is a
                    // per-particle random roll (see ApplyShapeConform's
                    // targetDist) this gizmo can't cheaply replicate
                    // point-by-point, so it always shows the
                    // surface-normal case as a known simplification.
                    if (showAttraction) {
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

const char* NeonFogVisualizer::ExtraStatusLine() const {
    float secondsToNext = shapeSettings_.autoCycle ? std::max(0.0f, shapeSettings_.cycleSeconds - shapeTimer_) : 0.0f;
    return TextFormat(
        "Shape: %s (S to cycle)  Morph force: %.2f (-/=)  Morph: %.0f%%  Auto-cycle: %s (M)%s",
        ShapeName(shapeProvider_->Type()), shapeSettings_.morphForce, morphStrength_ * 100.0f,
        shapeSettings_.autoCycle ? "on" : "off",
        shapeSettings_.autoCycle ? TextFormat("  Next in: %.1fs", secondsToNext) : "");
}

const char* NeonFogVisualizer::DebugInfoText() const {
    if (!shapeField_) return nullptr;
    Vector3 c = shapeField_->Center();
    float halfExtent = shapeField_->HalfExtent();
    int res = shapeField_->Resolution();
    float voxelSize = (halfExtent * 2.0f) / static_cast<float>(res);
    return TextFormat(
        "Grid: %dx%dx%d over %.1f world units (voxel %.3f)\n"
        "Field Center: (%.2f, %.2f, %.2f)\n"
        "Shape: %s  Morph: %.0f%%\n"
        "Core Light: (%d,%d,%d) x %.2f",
        res, res, res, halfExtent * 2.0f, voxelSize,
        c.x, c.y, c.z,
        ShapeName(shapeProvider_->Type()), morphStrength_ * 100.0f,
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
    v.EndGroup();

    v.BeginGroup("Lightning");
    lightningSettings_.Visit(v);
    v.EndGroup();

    v.BeginGroup("Floor");
    ui::VisitFloorParams(floorParams_, v);
    v.EndGroup();
}
