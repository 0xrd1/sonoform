#pragma once
#include "ParamVisitor.h"
#include "VoidFloor.h"
#include "PaletteParams.h"

// The settings structs behind the runtime editor: every value that used to
// live as a `constexpr` in an anonymous namespace, an unlabeled member, or a
// literal inline in a call, promoted to a named, tooltipped, live-tunable
// field. Grouped roughly the way NeonFogVisualizer's own class comment
// groups its subsystems (emission / forces / shape / lighting / lightning),
// plus App-level camera and post-process/global settings.
//
// Each struct's Visit() is the *only* place its fields are named -- see
// ParamVisitor.h's class comment for why that single declaration is what
// keeps the panel, the preset file round-trip, and reset-to-default from
// ever disagreeing about what fields exist.
namespace ui {

// -----------------------------------------------------------------------
// Neon Fog: fog-body emission (was the inline GpuEmitParams block +
// kSpawnRate in NeonFogVisualizer.cpp's Update).
// -----------------------------------------------------------------------
struct FogEmissionSettings {
    // SSBO pool size, allocated once at construction (see
    // GpuParticleSystem's ctor) -- takes effect only after "Rebuild Systems".
    // Sized for the SDF-biased-spawn design: since particles spawn already
    // on/near the shape surface rather than filling a diffuse volume, far
    // fewer are needed for a dense result than a naive approach would need.
    // Pushed toward the high end (small+numerous over large+sparse -- see
    // `size` below) for a denser, more volumetric look; tuned empirically
    // against the actual uncapped framerate on the dev machine (see
    // PerformanceSettings' vsync/targetFps defaults), not a guess.
    int capacity = 2000000;

    // particles/sec; steady-state alive count ~= rate * life. core ~=
    // 0.85*55000*30 ~= 1,402,500, shed ~= 0.15*55000*6 ~= 49,500, total ~=
    // 1,452,000 -- comfortably under the capacity above with headroom for
    // Recruit Fraction/life tuning.
    float spawnRate = 55000.0f;

    Vector3 velocity{ 0.0f, 0.08f, 0.0f };
    Vector3 velocityJitter{ 0.12f, 0.12f, 0.12f };

    Color colorA{ 0, 0, 0, 255 };       // set to the code defaults in EngineSettings.cpp-free init below
    Color colorB{ 0, 0, 0, 255 };

    // Small and numerous (see capacity/spawnRate above) reads as
    // continuous volumetric fog; large and sparse reads as "video game
    // particles" -- pushed near the slider's floor for the most
    // realistic look the sprite-billboard approach can produce.
    float size = 0.045f;
    float sizeJitter = 0.02f;

    float life = 30.0f;                 // "core" life -- the population ShapeConform holds onto the shape
    float lifeJitter = 8.0f;
    float shedLife = 6.0f;              // life for the fraction that misses recruitment (see ShapeSettings::recruitFraction)
    float shedLifeJitter = 2.0f;

    FogEmissionSettings() {
        // ColorFromHSV isn't constexpr, so the near-neutral cool-blue
        // defaults (see NeonFogVisualizer's class comment on why particle
        // albedo is deliberately low-saturation) are assigned in the body
        // rather than the in-class initializers above.
        colorA = ColorFromHSV(205.0f, 0.10f, 0.20f);
        colorB = ColorFromHSV(195.0f, 0.06f, 0.28f);
    }

    void Visit(IParamVisitor& v) {
        FogEmissionSettings d;
        v.Int(capacity, d.capacity, 1000, 4000000,
            { "Fog Capacity", "Total SSBO pool size. Every alive particle pays for a full force evaluation each frame, so this is the single biggest lever on GPU cost. Changing this needs Rebuild Systems (bottom of panel) to actually reallocate the pool -- the slider alone does nothing until then.", ParamFlags::NeedsRebuild });
        v.Float(spawnRate, d.spawnRate, 500.0f, 60000.0f,
            { "Spawn Rate", "Particles spawned per second. Steady-state alive count is roughly rate x life; watch Fog Capacity above to avoid saturating the pool." });
        v.Vec3(velocity, d.velocity, -2.0f, 2.0f,
            { "Base Velocity", "Constant birth velocity, world-space. Real fog drifts, it doesn't spray -- keep this small." });
        v.Vec3(velocityJitter, d.velocityJitter, 0.0f, 2.0f,
            { "Velocity Jitter", "Per-axis random range added to Base Velocity at spawn." });
        v.ColorField(colorA, d.colorA, { "Color A", "Particle albedo, low end of the random range. Visible color mostly comes from the core light, not this -- see Lighting." });
        v.ColorField(colorB, d.colorB, { "Color B", "Particle albedo, high end of the random range." });
        v.Float(size, d.size, 0.02f, 1.0f, { "Sprite Size", "Base sprite radius, world units. Small + dense reads as continuous fog; large reads as visible discs." });
        v.Float(sizeJitter, d.sizeJitter, 0.0f, 0.5f, { "Size Jitter", "Random range added to Sprite Size at spawn." });
        v.Float(life, d.life, 1.0f, 120.0f, { "Core Life", "Lifetime (seconds) of the recruited fraction -- the population that stays locked to the shape. Long life is what makes the fog persist as one volume across shape changes." });
        v.Float(lifeJitter, d.lifeJitter, 0.0f, 40.0f, { "Core Life Jitter", "Random range added to Core Life." });
        v.Float(shedLife, d.shedLife, 0.2f, 30.0f, { "Shed Life", "Lifetime (seconds) of the fraction that misses shape recruitment -- reads as haze wisps peeling off." });
        v.Float(shedLifeJitter, d.shedLifeJitter, 0.0f, 10.0f, { "Shed Life Jitter", "Random range added to Shed Life." });
    }
};

// -----------------------------------------------------------------------
// Neon Fog: forces acting on the fog body (was kGravityStrength,
// kGravitySoftening, kTurbulenceStrength, kTurbulenceScale, kShapeAttraction,
// kShapeCurl, and the previously-fixed Drag coefficient).
// -----------------------------------------------------------------------
struct FogForceSettings {
    float gravityStrength = 0.35f;
    float gravitySoftening = 3.5f;
    float turbulenceStrength = 0.12f;
    float turbulenceScale = 0.08f;
    float dragCoefficient = 1.0f;
    float shapeAttraction = 1.8f;
    float shapeCurl = 0.3f;
    float flowNoiseScale = 0.35f;

    void Visit(IParamVisitor& v) {
        FogForceSettings d;
        v.Float(gravityStrength, d.gravityStrength, 0.0f, 3.0f,
            { "Gravity Strength", "Pull toward the field center. Loose containment only -- keep well below Shape Attraction or gravity competes with it for the recruited fraction." });
        v.Float(gravitySoftening, d.gravitySoftening, 0.1f, 10.0f,
            { "Gravity Softening", "Distance at which gravity's inverse-square falloff is clamped, preventing a singularity at the center." });
        v.Float(turbulenceStrength, d.turbulenceStrength, 0.0f, 2.0f,
            { "Turbulence Strength", "Curl-noise motion strength. Kept weak by design -- too strong and it fights Shape Attraction hard enough that structure never firms up." });
        v.Float(turbulenceScale, d.turbulenceScale, 0.01f, 1.0f,
            { "Turbulence Scale", "Spatial frequency of the curl noise. Higher = smaller, busier eddies." });
        v.Float(dragCoefficient, d.dragCoefficient, 0.0f, 5.0f,
            { "Drag", "Velocity damping applied every step. Higher settles motion faster." });
        v.Float(shapeAttraction, d.shapeAttraction, 0.0f, 8.0f,
            { "Shape Attraction", "How hard particles are pulled onto the current ShapeField surface. Particles already spawn on-surface, so this mostly maintains position against turbulence/gravity and pulls survivors to a new shape on morph." });
        v.Float(shapeCurl, d.shapeCurl, 0.0f, 3.0f,
            { "Shape Curl", "Tangential flow-around-the-surface strength, layered on top of Shape Attraction so particles slide across the shape rather than snapping straight to it." });
        v.Float(flowNoiseScale, d.flowNoiseScale, 0.02f, 2.0f,
            { "Flow Noise Scale", "Spatial frequency of Shape Curl's noise, sampled at each particle's own position. Smaller = broader/slower swirls; larger = busier/finer ones." });
    }
};

// -----------------------------------------------------------------------
// Neon Fog: the SDF shape target, its bake grid, and the morph driver.
// Shared, visualizer-level state -- describes *what the target shape is*,
// not how any one particle population relates to it (see
// FogAttractionSettings below, owned per-emitter).
// -----------------------------------------------------------------------
struct ShapeSettings {
    // Grid resolution is baked into the ShapeField's SSBO at construction
    // (voxel count -- see ShapeField::ShapeField), so changing it only
    // takes effect after "Rebuild Systems". Grid half-extent and field
    // center are applied live instead -- see ShapeField::SetCenter/
    // SetHalfExtent and NeonFogVisualizer::Update's rebake check.
    int gridResolution = 40;
    float gridHalfExtent = 6.0f;
    Vector3 fieldCenter{ 0.0f, 2.0f, 0.0f };

    int shapeType = 0;                  // indexes ProceduralShapeType; see ShapeTypeNames() below
    bool autoCycle = true;
    float cycleSeconds = 15.0f;         // long enough to actually take in a shape's form before it changes

    float morphForce = 1.0f;            // independent driver, also nudged live via '-'/'='
    float morphMax = 1.5f;
    float morphEaseRate = 1.5f;         // per-second ease toward morphForce

    static const char* const* ShapeTypeNames() {
        static const char* names[] = { "Sphere", "Box", "Torus", "Cylinder" };
        return names;
    }
    static constexpr int kShapeTypeCount = 4;

    void Visit(IParamVisitor& v) {
        ShapeSettings d;
        v.Int(gridResolution, d.gridResolution, 8, 96,
            { "Shape Grid Resolution", "Voxel resolution of the baked SDF grid per axis. Higher = smoother shape surfaces, cubically more bake cost.", ParamFlags::NeedsRebuild });
        v.Float(gridHalfExtent, d.gridHalfExtent, 2.0f, 20.0f,
            { "Shape Grid Half-Extent", "World-space half-size of the bake grid. Must comfortably exceed the largest shape's radius plus the spawn candidate box, or samples clamp to the edge instead of the real surface. Live -- ShapeField re-bakes in place when this changes, no Rebuild Systems needed." });
        v.Vec3(fieldCenter, d.fieldCenter, -20.0f, 20.0f,
            { "Field Center", "World-space center of the shape/gravity/lighting system. Live -- ShapeField re-bakes in place when this changes, no Rebuild Systems needed." });
        v.Enum(shapeType, d.shapeType, ShapeTypeNames(), kShapeTypeCount, { "Shape", "Current analytic SDF target. Selecting one re-bakes the field immediately." });
        v.Bool(autoCycle, d.autoCycle, { "Auto-Cycle", "Advance through shapes on a fixed timer (also toggled by 'M')." });
        v.Float(cycleSeconds, d.cycleSeconds, 1.0f, 60.0f, { "Cycle Seconds", "How long a shape holds before auto-cycling to the next." });
        v.Float(morphForce, d.morphForce, 0.0f, morphMax,
            { "Morph Force", "Independent driver for shape attraction, deliberately not derived from audio -- 0 dissolves all structure back into ambient fog. Also nudged by '-'/'='." });
        v.Float(morphMax, d.morphMax, 0.5f, 3.0f, { "Morph Force Max", "Upper clamp on Morph Force." });
        v.Float(morphEaseRate, d.morphEaseRate, 0.1f, 10.0f, { "Morph Ease Rate", "How fast the applied morph strength eases toward Morph Force (per second). Higher snaps faster; lower reads as more organic emergence/dissolution." });
    }
};

// -----------------------------------------------------------------------
// Neon Fog: how a single particle population (a ShapeFogEmitter) relates
// to the shared ShapeField target -- per-emitter, unlike ShapeSettings
// above. A second emitter attracted to the same shape (different color/
// size/behavior) gets its own FogAttractionSettings instance.
// -----------------------------------------------------------------------
struct FogAttractionSettings {
    float candidateHalfExtent = 3.5f;   // spawn candidate box half-extent, projected onto the field
    float shellThickness = 0.2f;        // +/- offset along the surface normal after projection
    float recruitFraction = 0.85f;      // fraction that gets Core Life (vs. Shed Life) and stays locked to the shape
    // 0 = every recruited particle targets the exact shape surface (a hollow
    // lit skin). >0 = each spreads across nested iso-surfaces down to this
    // depth instead (a solid glowing body) -- see ApplyShapeConform in
    // shapes/shape_conform.glsl. One continuous surface<->volume control.
    float volumeDepth = 0.0f;

    void Visit(IParamVisitor& v) {
        FogAttractionSettings d;
        v.Float(candidateHalfExtent, d.candidateHalfExtent, 0.5f, 10.0f,
            { "Spawn Candidate Extent", "Half-size of the box new particles' birth position is chosen from before being projected onto the shape surface. Must stay within Shape Grid Half-Extent with margin." });
        v.Float(shellThickness, d.shellThickness, 0.0f, 2.0f, { "Shell Thickness", "Random offset along the surface normal after projection, so the birth shell isn't a razor-thin surface." });
        v.Float(recruitFraction, d.recruitFraction, 0.0f, 1.0f,
            { "Recruit Fraction", "Fraction of new particles that get Core Life and stay locked to the shape (vs. Shed Life haze). High = the volume reads as one coherent mass." });
        v.Float(volumeDepth, d.volumeDepth, 0.0f, 4.0f,
            { "Volume Depth", "0 = particles hug the exact surface (a hollow lit skin). Raise it and they instead spread across nested depths inside the shape (a solid glowing body) -- a continuous surface<->volume control, not a fixed toggle." });
    }
};

// -----------------------------------------------------------------------
// Neon Fog: how this emitter's forces respond to music -- "destructive
// but resisted": distinct musical features drive distinct existing
// forces (Bass -> Gravity, Mid -> Shape Curl, Treble/Excitement ->
// Turbulence), plus one-shot beat impacts, but nothing here ever touches
// Shape Attraction/Morph Force/Recruit Fraction (FogForceSettings/
// ShapeSettings/this struct's own recruit knobs live elsewhere), so the
// silhouette always holds against whatever the music throws at it,
// structurally rather than by convention. Gravity is safe to drive hard
// because it's always been "loose containment, well below Shape
// Attraction" by design; Shape Curl is safe because shape_conform.glsl
// projects it tangential to the surface (it can never pull a particle
// off the shape, only slide it along the shape it's already attracted
// to). Independent of ui::AudioSettings::enabled (which gates whether
// audio exists at all this session) -- `reactive` lets this emitter's
// own response be muted separately, e.g. for a future second emitter
// that shouldn't react the same way.
// -----------------------------------------------------------------------
struct FogAudioSettings {
    bool reactive = true;

    // Shared low-pass time constant for Bass/Mid/Excitement (Treble stays
    // raw/instantaneous -- the one deliberately fast, "sparkle" signal).
    // Audio-band values are noisy frame-to-frame straight out of the FFT;
    // smoothing keeps the forces they drive from looking twitchy.
    float motionSmoothing = 0.6f;

    // Each added on top of its target force (FogForceSettings) each frame
    // -- the base slider stays the resting/ambient value, audio adds on
    // top, same "base + audio scale" pattern FogLightingSettings' hue/
    // intensity fields already use.
    float bassGravityScale = 2.0f;       // Gravity Strength += smoothed Bass * this -- the mass pulses/breathes inward on bass swells
    float midCurlScale = 2.5f;           // Shape Curl += smoothed Mid * this -- more surface flow/swirl on rhythmic/melodic content
    float trebleTurbulenceScale = 0.8f;  // Turbulence Strength += instantaneous Treble * this -- fast, fizzy top-end response
    float excitementTurbulenceScale = 1.5f; // Turbulence Strength += smoothed overall Energy * this -- ambient agitation floor

    // A hard beat both (a) gives the existing mass a one-shot outward kick
    // (GpuParticleSystem::ApplyRadialImpulse -- recruited particles wobble
    // and get pulled back by Shape Attraction, shed ones fly off and
    // despawn on schedule) and (b) emits a dedicated burst of guaranteed-
    // unrecruited debris (see ShapeFogEmitter::EmitImpactBurst) so the hit
    // unambiguously reads as particles being expelled, not just a subtle
    // nudge to the ambient population. Separate threshold from
    // LightningSettings' own -- kicks and bolts don't have to agree on
    // what counts as "hard."
    float kickBeatThreshold = 0.5f;
    float kickImpulseStrength = 3.0f;
    float kickImpulseRadius = 3.5f;
    float kickCooldownSeconds = 0.12f;
    int kickBurstCount = 400;
    float kickBurstSpeed = 4.0f;
    float kickBurstSpeedJitter = 2.0f;
    float kickBurstLife = 1.2f;
    float kickBurstLifeJitter = 0.4f;

    void Visit(IParamVisitor& v) {
        FogAudioSettings d;
        v.Bool(reactive, d.reactive, { "Reactive", "Whether this emitter's forces respond to music at all. Independent of the master Audio Enabled toggle." });
        v.Float(motionSmoothing, d.motionSmoothing, 0.05f, 3.0f,
            { "Motion Smoothing", "Seconds; how slowly Bass/Mid/Excitement's effect on forces follows raw audio (Treble stays instantaneous). Higher = calmer, slower-building reactivity." });
        v.Float(bassGravityScale, d.bassGravityScale, 0.0f, 10.0f,
            { "Gravity x Bass", "Gravity Strength added per unit of smoothed Bass -- the fog pulses/pulls inward on bass swells." });
        v.Float(midCurlScale, d.midCurlScale, 0.0f, 10.0f,
            { "Curl x Mid", "Shape Curl added per unit of smoothed Mid -- more surface flow/swirl. Tangential-only, so this can never pull particles off the shape." });
        v.Float(trebleTurbulenceScale, d.trebleTurbulenceScale, 0.0f, 6.0f,
            { "Turbulence x Treble", "Turbulence Strength added per unit of instantaneous Treble -- a fast, fizzy high-end response." });
        v.Float(excitementTurbulenceScale, d.excitementTurbulenceScale, 0.0f, 6.0f,
            { "Turbulence x Excitement", "Turbulence Strength added per unit of smoothed overall Energy -- a slow-moving ambient agitation floor." });
        v.Float(kickBeatThreshold, d.kickBeatThreshold, 0.0f, 1.0f,
            { "Kick Beat Threshold", "Minimum beat intensity that fires an outward kick impulse + debris burst." });
        v.Float(kickImpulseStrength, d.kickImpulseStrength, 0.0f, 15.0f, { "Kick Impulse Strength", "Outward velocity kick applied to particles near the field center on a hard beat." });
        v.Float(kickImpulseRadius, d.kickImpulseRadius, 0.5f, 15.0f, { "Kick Impulse Radius", "Radius around the field center the kick impulse affects." });
        v.Float(kickCooldownSeconds, d.kickCooldownSeconds, 0.0f, 2.0f, { "Kick Cooldown", "Minimum seconds between kick impulses, so a burst of rapid beats can't stack them." });
        v.Int(kickBurstCount, d.kickBurstCount, 0, 5000, { "Kick Burst Count", "Particles emitted in the debris burst on a hard beat. 0 disables the burst (impulse-only kicks)." });
        v.Float(kickBurstSpeed, d.kickBurstSpeed, 0.0f, 20.0f, { "Kick Burst Speed", "Minimum outward speed of debris burst particles." });
        v.Float(kickBurstSpeedJitter, d.kickBurstSpeedJitter, 0.0f, 20.0f, { "Kick Burst Speed Jitter", "Random range added to Kick Burst Speed." });
        v.Float(kickBurstLife, d.kickBurstLife, 0.1f, 5.0f, { "Kick Burst Life", "Lifetime of debris burst particles -- always short and unrecruited, so they fade and despawn rather than resettling on the shape." });
        v.Float(kickBurstLifeJitter, d.kickBurstLifeJitter, 0.0f, 2.0f, { "Kick Burst Life Jitter", "Random range added to Kick Burst Life." });
    }
};

// -----------------------------------------------------------------------
// Neon Fog: the core light -- the fog's only real light source (see
// NeonFogVisualizer's class comment on why color/intensity carry all of the
// fog's apparent color) -- plus the fake self-shadow shading angle.
// -----------------------------------------------------------------------
struct FogLightingSettings {
    float coreHueBase = 200.0f;
    float coreHueBassScale = -15.0f;
    float coreHueTrebleScale = 20.0f;
    // Was 8.0 (a full 360 deg cycle every 45s) -- at that speed the
    // unbounded time*speed term swamps coreHueBassScale/coreHueTrebleScale
    // (a handful of degrees each) so completely that the fog reads as
    // "hue just rotates", not audio-reactive at all. Defaulted to 0 (opt-in
    // via the panel) now that coreHueBassScale/coreHueTrebleScale multiply
    // AudioAnalyzer's normalized *Level() getters instead of raw band
    // magnitudes (see FogColorSettings' own comment below) and can
    // actually compete for the hue on their own.
    float hueCycleSpeed = 0.0f;         // degrees/sec of continuous drift; 0 = audio owns the hue entirely
    float coreSaturation = 0.75f;
    float coreValue = 1.0f;

    float intensityBase = 3.0f;
    float intensityEnergyScale = 4.0f;
    float intensityBeatFlashScale = 2.5f;
    float beatFlashDecayRate = 2.0f;    // per second

    Vector3 overheadLightPos{ 1.5f, 16.0f, -3.0f }; // shading angle + floor light-pool hint only, never a real particle light
    float shadeAmbientFloor = 0.75f;    // 1.0 = no self-shadow darkening

    void Visit(IParamVisitor& v) {
        FogLightingSettings d;
        v.BeginGroup("Core Light Color");
        v.Float(coreHueBase, d.coreHueBase, 0.0f, 360.0f, { "Hue Base", "Core light hue (degrees) at rest." });
        v.Float(coreHueBassScale, d.coreHueBassScale, -60.0f, 60.0f, { "Hue x Bass", "Hue shift (degrees) at full-scale bass -- multiplies AudioAnalyzer::BassLevel(), which is normalized to [0,1] against the track's own recent peak, not the raw FFT magnitude." });
        v.Float(coreHueTrebleScale, d.coreHueTrebleScale, -60.0f, 60.0f, { "Hue x Treble", "Hue shift (degrees) at full-scale treble -- multiplies AudioAnalyzer::TrebleLevel(), normalized the same way as Hue x Bass." });
        v.Float(hueCycleSpeed, d.hueCycleSpeed, 0.0f, 90.0f, { "Hue Cycle Speed", "Continuous hue drift, degrees/sec -- slow ambient color cycling layered on top of the audio-driven hue. 0 (the default) leaves the hue entirely to the music." });
        v.Float(coreSaturation, d.coreSaturation, 0.0f, 1.0f, { "Saturation", "Core light saturation. Lower reads as white-hot; higher as vividly colored." });
        v.Float(coreValue, d.coreValue, 0.0f, 1.0f, { "Value", "Core light HSV value." });
        v.EndGroup();
        v.BeginGroup("Core Light Intensity");
        v.Float(intensityBase, d.intensityBase, 0.0f, 15.0f, { "Base", "Resting intensity with zero audio energy." });
        v.Float(intensityEnergyScale, d.intensityEnergyScale, 0.0f, 15.0f, { "x Energy", "Intensity added per unit of overall audio energy." });
        v.Float(intensityBeatFlashScale, d.intensityBeatFlashScale, 0.0f, 15.0f, { "x Beat Flash", "Intensity added per unit of the current beat-flash envelope." });
        v.Float(beatFlashDecayRate, d.beatFlashDecayRate, 0.1f, 10.0f, { "Beat Flash Decay", "How fast the beat flash envelope decays back to zero (per second)." });
        v.EndGroup();
        v.BeginGroup("Shading");
        v.Vec3(overheadLightPos, d.overheadLightPos, -30.0f, 30.0f,
            { "Overhead Light Position", "Angle used for the fake self-shadow and the floor's light-pool hint only -- deliberately never added as a real particle light (see the class comment on why the fog should read as lit from within)." });
        v.Float(shadeAmbientFloor, d.shadeAmbientFloor, 0.0f, 1.0f,
            { "Shade Ambient Floor", "How dim the self-shadowed side gets. 1.0 = no darkening; kept high by design so it reads as gentle form, not an external key light." });
        v.EndGroup();
    }
};

// -----------------------------------------------------------------------
// Neon Fog: everything that makes color actually vary with the music and
// across the fog's own volume, on top of FogLightingSettings' single core-
// light hue above. Three independent mechanisms, addressing three separate
// gaps a first pass at "the color just rotates" surfaced:
//   - Spectral lights: up to three extra LightSample entries (see
//     NeonFogVisualizer::Update/Draw), one per band, each positioned near
//     the field center and colored/sized by that band's own AudioAnalyzer::
//     *Level(). Pure C++, no shader change -- the 16-slot light array and
//     particle_render.vert's light-boost loop already exist and only slot
//     0 (the core light) was ever filled. This is what makes color
//     visually *localized* to where a frequency's energy is, not just a
//     single scene-wide hue.
//   - Palette: a per-particle tint sampled live from world position each
//     frame in particle_render.vert (see gfx/PaletteParams.h) -- what
//     makes color vary *across the body itself* (rim vs. core, top vs.
//     bottom, around the ring), which no light-based approach can do no
//     matter how many lights are added, since a light's color is uniform
//     across every particle it reaches.
//   - paletteAudioShift/Spread let the palette itself breathe with the
//     music (the whole hue ramp rotates and widens) without needing a
//     second copy of the spectral-light machinery.
// All three read AudioAnalyzer's normalized *Level() getters (see its
// header comment), not the raw Bass()/Mid()/Treble()/Energy() -- those are
// un-normalized FFT magnitudes (typically 0.0-0.15), which is the actual
// root cause "color just rotates" traced back to: a scale of even 20-30
// against a ~0.05 raw value moves nothing, so anything wired to the raw
// getters silently reads as static regardless of how it's tuned.
// -----------------------------------------------------------------------
struct FogColorSettings {
    // Added on top of FogLightingSettings' own coreHueBassScale/
    // coreHueTrebleScale (this struct doesn't touch the core light's hue
    // math directly -- see NeonFogVisualizer::Update) via Mid, which the
    // core light alone never responded to.
    float hueMidScale = 20.0f;           // degrees at full-scale MidLevel()
    float saturationEnergyScale = 0.15f; // core saturation += EnergyLevel() * this
    float valueEnergyScale = 0.0f;       // core value += EnergyLevel() * this (0 = value stays constant; intensity already carries loudness)

    bool spectralEnabled = true;
    float spectralHueBass = 210.0f;      // deep blue -- low, near the floor
    float spectralHueMid = 160.0f;       // teal -- lateral
    float spectralHueTreble = 60.0f;     // warm gold -- high, sparkling accent
    float spectralRadius = 3.0f;         // offset distance from field center, world units
    float spectralIntensity = 6.0f;      // scales *Level() (already [0,1]) to a light-intensity range comparable to the core light
    float spectralSaturation = 0.85f;

    int paletteMode = 0;                 // indexes PaletteModeNames() below; 0 = Off
    float paletteHueA = 190.0f;
    float paletteHueB = 320.0f;
    float paletteSaturation = 0.7f;
    float paletteStrength = 0.0f;        // 0 = today's untinted look; the panel/presets opt in
    float paletteLightTint = 0.0f;       // see PaletteParams::lightTint's comment on why this exists separately from paletteStrength
    float paletteAudioShift = 0.0f;      // degrees; rotates hueA/hueB together, scaled by EnergyLevel()
    float paletteAudioSpread = 0.0f;     // degrees; widens hueB away from hueA, scaled by TrebleLevel()

    static const char* const* PaletteModeNames() {
        static const char* names[] = { "Off", "Height", "Radius", "Angle", "Random" };
        return names;
    }
    static constexpr int kPaletteModeCount = 5;

    void Visit(IParamVisitor& v) {
        FogColorSettings d;
        v.BeginGroup("Color");
        v.Float(hueMidScale, d.hueMidScale, -60.0f, 60.0f,
            { "Hue x Mid", "Core light hue shift (degrees) at full-scale MidLevel() -- the one band the core light's own hue math (Bass/Treble, see Lighting/Core Light Color) doesn't already cover." });
        v.Float(saturationEnergyScale, d.saturationEnergyScale, -1.0f, 1.0f,
            { "Saturation x Energy", "Core light saturation added per unit of EnergyLevel() -- louder passages read as more vividly colored, quieter ones drift toward white." });
        v.Float(valueEnergyScale, d.valueEnergyScale, -1.0f, 1.0f,
            { "Value x Energy", "Core light HSV value added per unit of EnergyLevel(). Usually left at 0 -- Core Light Intensity already carries loudness; this would double up on top of it." });
        v.EndGroup();

        v.BeginGroup("Spectral Lights");
        v.Bool(spectralEnabled, d.spectralEnabled,
            { "Enabled", "Adds up to three extra lights (bass/mid/treble), positioned near the field center and pulsing with that band's own level, alongside the single core light." });
        v.Float(spectralHueBass, d.spectralHueBass, 0.0f, 360.0f, { "Hue: Bass", "Hue (degrees) of the bass light." });
        v.Float(spectralHueMid, d.spectralHueMid, 0.0f, 360.0f, { "Hue: Mid", "Hue (degrees) of the mid light." });
        v.Float(spectralHueTreble, d.spectralHueTreble, 0.0f, 360.0f, { "Hue: Treble", "Hue (degrees) of the treble light." });
        v.Float(spectralRadius, d.spectralRadius, 0.0f, 10.0f, { "Radius", "Offset distance of each spectral light from the field center, world units." });
        v.Float(spectralIntensity, d.spectralIntensity, 0.0f, 20.0f, { "Intensity", "Scales each band's [0,1] level to a light intensity comparable to the core light's own (see Lighting/Core Light Intensity)." });
        v.Float(spectralSaturation, d.spectralSaturation, 0.0f, 1.0f, { "Saturation", "Saturation shared by all three spectral lights." });
        v.EndGroup();

        v.BeginGroup("Palette");
        v.Enum(paletteMode, d.paletteMode, PaletteModeNames(), kPaletteModeCount,
            { "Mode", "How a particle's world position maps to a ramp position between Hue A and Hue B -- Off leaves particles untinted (today's look)." });
        v.Float(paletteHueA, d.paletteHueA, 0.0f, 360.0f, { "Hue A", "Ramp start hue (degrees)." });
        v.Float(paletteHueB, d.paletteHueB, 0.0f, 360.0f, { "Hue B", "Ramp end hue (degrees)." });
        v.Float(paletteSaturation, d.paletteSaturation, 0.0f, 1.0f, { "Saturation", "Palette tint saturation." });
        v.Float(paletteStrength, d.paletteStrength, 0.0f, 1.0f,
            { "Strength", "How strongly the tint mixes into particle albedo. Albedo is dim by design (see Emission's Color A/B) -- Light Tint below is what actually makes the palette read clearly." });
        v.Float(paletteLightTint, d.paletteLightTint, 0.0f, 1.0f,
            { "Light Tint", "How strongly the tint mixes into the light response each particle receives -- since lit intensity (3-9) dwarfs raw albedo (0.1-0.28), this is the primary lever for a visible palette." });
        v.Float(paletteAudioShift, d.paletteAudioShift, 0.0f, 180.0f,
            { "Shift x Energy", "Degrees the whole Hue A/B ramp rotates at full-scale EnergyLevel() -- the palette itself drifts with loudness." });
        v.Float(paletteAudioSpread, d.paletteAudioSpread, 0.0f, 180.0f,
            { "Spread x Treble", "Degrees Hue B widens away from Hue A at full-scale TrebleLevel() -- the ramp gets more colorful on bright/sparkly passages." });
        v.EndGroup();
    }
};

// -----------------------------------------------------------------------
// Fractal lightning bolts (was literals in LightningSystem.cpp's SpawnBolt/
// Draw plus the beat-intensity gate + cooldown in NeonFogVisualizer::Update).
// -----------------------------------------------------------------------
struct LightningSettings {
    float beatIntensityThreshold = 0.45f; // only beats stronger than this fire a bolt
    float cooldownSeconds = 0.1f;         // minimum gap between bolts

    int maxBolts = 6;
    int fractalDepth = 5;
    float displacementBase = 1.2f;

    // Bolt endpoints are rejection-sampled from inside the shape's real
    // baked SDF (see LightningSystem::SampleInsidePoint) rather than
    // extended a fixed length in a random direction -- so bolt "length"
    // is however far apart two interior points happen to be, and
    // automatically scales with whatever shape is currently baked. No
    // separate length knob needed (an earlier fixed-length-in-a-random-
    // direction version routinely shot bolts out past the fog into empty
    // space, which is what this replaced).

    int branchCountMin = 1;
    int branchCountMax = 3;

    float hueBase = 200.0f;
    float hueJitterMin = -20.0f;
    float hueJitterMax = 40.0f;
    float saturation = 0.8f; // vividly colored, not near-white -- see the Visit() tooltip

    float lifeMin = 0.12f;
    float lifeJitter = 0.08f;
    float brightnessBase = 1.5f;
    float brightnessStrengthMult = 2.5f;

    float coreRadius = 0.035f;
    float haloRadius = 0.16f;
    float branchCoreRadius = 0.02f;
    float branchHaloRadius = 0.1f;

    void Visit(IParamVisitor& v) {
        LightningSettings d;
        v.Float(beatIntensityThreshold, d.beatIntensityThreshold, 0.0f, 1.0f, { "Beat Threshold", "Minimum beat intensity that fires a bolt." });
        v.Float(cooldownSeconds, d.cooldownSeconds, 0.0f, 2.0f, { "Cooldown", "Minimum seconds between bolts, so a burst of rapid beats can't spawn them on top of each other." });
        v.Int(maxBolts, d.maxBolts, 1, 24, { "Max Concurrent Bolts", "Caps live bolts so a beat burst can't run away the draw call count." });
        v.Int(fractalDepth, d.fractalDepth, 1, 8, { "Fractal Depth", "Midpoint-displacement subdivision levels. Higher = more jagged detail, more segments to draw." });
        v.Float(displacementBase, d.displacementBase, 0.0f, 5.0f, { "Displacement", "Base perpendicular offset for the fractal path, halved each subdivision level." });
        v.Int(branchCountMin, d.branchCountMin, 0, 10, { "Branch Count Min", "Minimum side-branches per bolt." });
        v.Int(branchCountMax, d.branchCountMax, 0, 10, { "Branch Count Max", "Maximum side-branches per bolt." });
        v.Float(hueBase, d.hueBase, 0.0f, 360.0f, { "Hue Base", "Bolt hue (degrees) before random jitter." });
        v.Float(hueJitterMin, d.hueJitterMin, -180.0f, 180.0f, { "Hue Jitter Min", "Lower bound of random hue offset." });
        v.Float(hueJitterMax, d.hueJitterMax, -180.0f, 180.0f, { "Hue Jitter Max", "Upper bound of random hue offset." });
        v.Float(saturation, d.saturation, 0.0f, 1.0f, { "Saturation", "Bolt color saturation -- kept high by default so bolts read as clearly colored jagged lightning, not near-white light." });
        v.Float(lifeMin, d.lifeMin, 0.02f, 1.0f, { "Life Min", "Minimum bolt lifetime, seconds." });
        v.Float(lifeJitter, d.lifeJitter, 0.0f, 0.5f, { "Life Jitter", "Random range added to Life Min." });
        v.Float(brightnessBase, d.brightnessBase, 0.0f, 10.0f, { "Brightness Base", "Light-sample brightness at zero trigger strength." });
        v.Float(brightnessStrengthMult, d.brightnessStrengthMult, 0.0f, 10.0f, { "Brightness x Strength", "Additional brightness scaled by trigger strength." });
        v.Float(coreRadius, d.coreRadius, 0.005f, 0.3f, { "Core Radius", "Bright inner cylinder radius of the main path." });
        v.Float(haloRadius, d.haloRadius, 0.01f, 0.6f, { "Halo Radius", "Dim outer cylinder radius of the main path." });
        v.Float(branchCoreRadius, d.branchCoreRadius, 0.002f, 0.2f, { "Branch Core Radius", "Bright inner cylinder radius of side-branches." });
        v.Float(branchHaloRadius, d.branchHaloRadius, 0.005f, 0.4f, { "Branch Halo Radius", "Dim outer cylinder radius of side-branches." });
    }
};

// -----------------------------------------------------------------------
// App-level: camera and post-process/global reactivity. Owned by App, not
// any one visualizer.
// -----------------------------------------------------------------------
struct CameraSettings {
    float yaw = 0.0f;
    float pitch = 0.55f;
    float distance = 20.0f;
    bool autoRotate = true;

    float autoRotateSpeed = 0.15f;      // radians/sec
    float dragSensitivity = 0.005f;
    float zoomSpeed = 1.5f;
    float minDistance = 4.0f;
    float maxDistance = 60.0f;
    float pitchLimit = 1.4f;
    float fovy = 45.0f;

    void Visit(IParamVisitor& v) {
        CameraSettings d;
        v.Float(yaw, d.yaw, -3.1416f, 3.1416f, { "Yaw", "Current orbit yaw, radians." });
        v.Float(pitch, d.pitch, -1.4f, 1.4f, { "Pitch", "Current orbit pitch, radians." });
        v.Float(distance, d.distance, 1.0f, 100.0f, { "Distance", "Current orbit distance from target." });
        v.Bool(autoRotate, d.autoRotate, { "Auto-Rotate", "Continuously advance yaw (also toggled by 'C')." });
        v.Float(autoRotateSpeed, d.autoRotateSpeed, 0.0f, 2.0f, { "Auto-Rotate Speed", "Yaw advance rate, radians/sec, when Auto-Rotate is on." });
        v.Float(dragSensitivity, d.dragSensitivity, 0.0005f, 0.02f, { "Drag Sensitivity", "Yaw/pitch change per pixel of right-drag mouse movement." });
        v.Float(zoomSpeed, d.zoomSpeed, 0.1f, 10.0f, { "Zoom Speed", "Distance change per mouse-wheel notch." });
        v.Float(minDistance, d.minDistance, 0.5f, 50.0f, { "Min Distance", "Closest allowed zoom." });
        v.Float(maxDistance, d.maxDistance, 10.0f, 200.0f, { "Max Distance", "Farthest allowed zoom." });
        v.Float(pitchLimit, d.pitchLimit, 0.1f, 1.5f, { "Pitch Limit", "Maximum |pitch|, radians -- keeps the orbit from flipping over the poles." });
        v.Float(fovy, d.fovy, 10.0f, 120.0f, { "Field of View", "Vertical FOV, degrees." });
    }
};

struct PostSettings {
    float bloomThreshold = 0.35f;
    float bloomIntensity = 1.1f;
    float reactivityIntensity = 1.0f;   // global audio-reactivity multiplier, was App::intensity_

    void Visit(IParamVisitor& v) {
        PostSettings d;
        v.Float(bloomThreshold, d.bloomThreshold, 0.0f, 1.0f, { "Bloom Threshold", "Luma cutoff for the bright-pass extraction. Lower = more of the scene blooms." });
        v.Float(bloomIntensity, d.bloomIntensity, 0.0f, 5.0f, { "Bloom Intensity", "Additive strength of the blurred bright-pass over the scene." });
        v.Float(reactivityIntensity, d.reactivityIntensity, 0.1f, 4.0f, { "Reactivity Intensity", "Global multiplier applied to audio-reactive lighting (also nudged by '['/']')." });
    }
};

// -----------------------------------------------------------------------
// Display/pacing -- deliberately *not* part of VisitAll's preset round-trip
// (see EngineUi.cpp): vsync/framerate are a machine characteristic, not
// part of "the look", so they'd have no business being saved into a preset
// file. Drawn as its own small panel group directly in DrawDebugPanel
// instead. App applies changes at runtime (SetWindowState/SetTargetFPS)
// only when a value actually changes -- see App::ApplyPerformanceSettings.
// -----------------------------------------------------------------------
struct PerformanceSettings {
    bool vsync = false;   // off by default -- see targetFps
    int targetFps = 0;    // 0 = uncapped -- lets you see the true achievable framerate

    void Visit(IParamVisitor& v) {
        PerformanceSettings d;
        v.Bool(vsync, d.vsync, { "VSync", "Synchronize frame presentation to the display's refresh rate." });
        v.Int(targetFps, d.targetFps, 0, 500,
            { "Target FPS", "0 = uncapped. Useful with VSync off, to see how much particle-count headroom the GPU actually has before it starts to slow down." });
    }
};

// -----------------------------------------------------------------------
// Whether audio exists at all this session, and at what volume --
// deliberately *not* part of VisitAll's preset round-trip (see
// PerformanceSettings' own comment just above for the identical
// reasoning): whether audio is on, and which track/volume, are session
// state, not "the look" -- loading a preset shouldn't hijack playback.
// App applies `enabled` transitions lazily (audio device / track list
// only initialize the first time it flips true -- see App::ApplyAudioSettings)
// and `volume` only when it actually changes, same edge-triggered-apply
// pattern ApplyPerformanceSettings already uses for vsync/fps.
// -----------------------------------------------------------------------
struct AudioSettings {
    // On by default: this is a music visualizer, and the whole point --
    // the transport bar, the reactive presets, every audio-driven force
    // and light in FogAudioSettings/FogColorSettings -- is inert with
    // audio off. Still a one-click toggle in the panel for a silent/
    // screenshot session.
    bool enabled = true;
    float volume = 0.6f;

    void Visit(IParamVisitor& v) {
        AudioSettings d;
        v.Bool(enabled, d.enabled, { "Enabled", "Turns on the audio device, track playback, and every audio-reactive visual (lighting, turbulence, kick impulses). On by default -- this is a music visualizer." });
        v.Float(volume, d.volume, 0.0f, 1.0f, { "Volume", "Music playback volume." });
    }
};

// -----------------------------------------------------------------------
// Debug visualization: in-scene gizmo toggles. App-owned (like Camera/Post)
// rather than visualizer-owned, since the concept ("show me the shape
// bounds/lights/emitter/forces") is generic even though only Neon Fog
// currently draws anything for it -- see Visualizer.h's
// RenderContext::debug and NeonFogVisualizer::Draw.
// -----------------------------------------------------------------------
struct DebugSettings {
    bool showShapeBounds = false;
    bool showLightGizmos = false;
    bool showFieldAxes = false;
    bool showEmitterBounds = false;
    bool showForceVectors = false;
    bool disableSpriteNoise = false; // compare the raw circular sprite against the styled wispy look

    void Visit(IParamVisitor& v) {
        DebugSettings d;
        v.Bool(showShapeBounds, d.showShapeBounds,
            { "Shape Bounds", "Wireframe of the current analytic shape target, for comparing against where particles actually sit." });
        v.Bool(showLightGizmos, d.showLightGizmos,
            { "Light Gizmos", "Small sphere at the core light (real -- colored/sized by its current color and intensity) and a dim marker at the overhead shading position (not a real light -- see Neon Fog's class comment)." });
        v.Bool(showFieldAxes, d.showFieldAxes,
            { "Field Axes", "Field center plus a line toward the overhead shading direction." });
        v.Bool(showEmitterBounds, d.showEmitterBounds,
            { "Emitter Bounds", "Wireframe box showing the region new particles' spawn candidates are drawn from before being projected onto the shape surface." });
        v.Bool(showForceVectors, d.showForceVectors,
            { "Force Vectors", "Sampled gravity and shape-attraction direction arrows on a coarse shell around the shape. Turbulence/curl-noise isn't shown -- not cheap to mirror on the CPU without a GPU readback." });
        v.Bool(disableSpriteNoise, d.disableSpriteNoise,
            { "Disable Sprite Noise", "Compare the raw circular sprite (off) against the styled wispy noise-mask look (on, the default render)." });
    }
};

// VoidFloor::Params is already a proper struct (see VoidFloor.h) -- this is
// a free function rather than a member so VoidFloor (gfx/) doesn't need to
// depend on ui/. Covers all 13 fields; NeonFogVisualizer stores one Params
// instance as a member instead of default-constructing a fresh one every
// Draw() call, which is what makes the 9 fields the old per-frame override
// never touched (voidRadius, baseColor, gridColor, gridSpacing,
// gridLineWidth, shadowRadius, shadowStrength, lightPoolRadius,
// lightPoolStrength) reachable at all.
inline void VisitFloorParams(VoidFloor::Params& p, IParamVisitor& v) {
    VoidFloor::Params d;
    v.Vec3(p.center, d.center, -50.0f, 50.0f, { "Center", "World-space floor plane center." });
    v.Float(p.size, d.size, 10.0f, 400.0f, { "Size", "Plane mesh size, world units. Large relative to the fog so it reads as ground extending well past it before fading into the void." });
    v.Float(p.voidRadius, d.voidRadius, 1.0f, 150.0f, { "Void Radius", "Distance at which the floor fades to fully transparent." });
    v.ColorField(p.baseColor, d.baseColor, { "Base Color", "Deliberately neutral -- the fog/lighting owns the palette, not the stage." });
    v.ColorField(p.gridColor, d.gridColor, { "Grid Color", "Color of the procedural grid lines and (at half weight) the light pool." });
    v.Float(p.gridSpacing, d.gridSpacing, 0.1f, 20.0f, { "Grid Spacing", "World-space distance between grid lines." });
    v.Float(p.gridLineWidth, d.gridLineWidth, 0.005f, 1.0f, { "Grid Line Width", "Grid line thickness." });
    v.Float(p.shapeSampleY, d.shapeSampleY, -20.0f, 20.0f, { "Shadow Sample Y", "World Y at which the ShapeField is sampled for the contact shadow -- normally the shape's own vertical center." });
    v.Float(p.shadowRadius, d.shadowRadius, 0.1f, 20.0f, { "Shadow Radius", "Contact-shadow falloff radius." });
    v.Float(p.shadowStrength, d.shadowStrength, 0.0f, 1.0f, { "Shadow Strength", "Contact-shadow darkening strength." });
    v.Vec3(p.lightPoolCenter, d.lightPoolCenter, -50.0f, 50.0f, { "Light Pool Center", "Center of the soft light-pool hint on the floor." });
    v.Float(p.lightPoolRadius, d.lightPoolRadius, 1.0f, 100.0f, { "Light Pool Radius", "Light-pool falloff radius." });
    v.Float(p.lightPoolStrength, d.lightPoolStrength, 0.0f, 1.0f, { "Light Pool Strength", "Deliberately faint -- a hint of where the key light falls, not a visible glow." });
}

} // namespace ui
