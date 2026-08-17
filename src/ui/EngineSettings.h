#pragma once
#include "ParamVisitor.h"
#include "VoidFloor.h"

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
    int capacity = 600000;

    // particles/sec; steady-state alive count ~= rate * life. Scaled up
    // from the old 9800 default to keep the same recruited/shed ratio at
    // the new, higher capacity: core ~= 0.85*18000*30 ~= 459000, shed
    // ~= 0.15*18000*6 ~= 16200, total ~= 475000 -- comfortably under the
    // new default capacity.
    float spawnRate = 18000.0f;

    Vector3 velocity{ 0.0f, 0.08f, 0.0f };
    Vector3 velocityJitter{ 0.12f, 0.12f, 0.12f };

    Color colorA{ 0, 0, 0, 255 };       // set to the code defaults in EngineSettings.cpp-free init below
    Color colorB{ 0, 0, 0, 255 };

    // Finer/denser-reading at the higher default particle count above --
    // 0.16 read as "video game particles" rather than realistic fog.
    float size = 0.11f;
    float sizeJitter = 0.07f;

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
// Neon Fog: the core light -- the fog's only real light source (see
// NeonFogVisualizer's class comment on why color/intensity carry all of the
// fog's apparent color) -- plus the fake self-shadow shading angle.
// -----------------------------------------------------------------------
struct FogLightingSettings {
    float coreHueBase = 200.0f;
    float coreHueBassScale = -15.0f;
    float coreHueTrebleScale = 20.0f;
    float hueCycleSpeed = 8.0f;         // degrees/sec of continuous drift; a full 360 deg cycle every 45s
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
        v.Float(coreHueBassScale, d.coreHueBassScale, -60.0f, 60.0f, { "Hue x Bass", "Hue shift per unit of bass energy." });
        v.Float(coreHueTrebleScale, d.coreHueTrebleScale, -60.0f, 60.0f, { "Hue x Treble", "Hue shift per unit of treble energy." });
        v.Float(hueCycleSpeed, d.hueCycleSpeed, 0.0f, 90.0f, { "Hue Cycle Speed", "Continuous hue drift, degrees/sec -- slow ambient color cycling. 0 disables it." });
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
// Fractal lightning bolts (was literals in LightningSystem.cpp's SpawnBolt/
// Draw plus the beat-intensity gate + cooldown in NeonFogVisualizer::Update).
// -----------------------------------------------------------------------
struct LightningSettings {
    float beatIntensityThreshold = 0.45f; // only beats stronger than this fire a bolt
    float cooldownSeconds = 0.1f;         // minimum gap between bolts

    int maxBolts = 6;
    int fractalDepth = 5;
    float displacementBase = 1.2f;

    float lengthBase = 4.0f;
    float lengthJitter = 4.0f;
    float lengthStrengthBase = 0.6f;
    float lengthStrengthMult = 0.8f;

    int branchCountMin = 1;
    int branchCountMax = 3;

    float hueBase = 200.0f;
    float hueJitterMin = -20.0f;
    float hueJitterMax = 40.0f;
    float saturation = 0.35f;

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
        v.Float(lengthBase, d.lengthBase, 0.5f, 20.0f, { "Length Base", "Minimum bolt length before strength scaling." });
        v.Float(lengthJitter, d.lengthJitter, 0.0f, 20.0f, { "Length Jitter", "Random range added to Length Base." });
        v.Float(lengthStrengthBase, d.lengthStrengthBase, 0.0f, 2.0f, { "Length x Strength Base", "Length multiplier at zero trigger strength." });
        v.Float(lengthStrengthMult, d.lengthStrengthMult, 0.0f, 2.0f, { "Length x Strength Scale", "Additional length multiplier scaled by trigger strength (beat intensity + treble)." });
        v.Int(branchCountMin, d.branchCountMin, 0, 10, { "Branch Count Min", "Minimum side-branches per bolt." });
        v.Int(branchCountMax, d.branchCountMax, 0, 10, { "Branch Count Max", "Maximum side-branches per bolt." });
        v.Float(hueBase, d.hueBase, 0.0f, 360.0f, { "Hue Base", "Bolt hue (degrees) before random jitter." });
        v.Float(hueJitterMin, d.hueJitterMin, -180.0f, 180.0f, { "Hue Jitter Min", "Lower bound of random hue offset." });
        v.Float(hueJitterMax, d.hueJitterMax, -180.0f, 180.0f, { "Hue Jitter Max", "Upper bound of random hue offset." });
        v.Float(saturation, d.saturation, 0.0f, 1.0f, { "Saturation", "Kept low by design so bolts read as bright near-white light, not a flat colored line." });
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
    bool vsync = true;
    int targetFps = 60;   // 0 = uncapped -- lets you see the true achievable framerate

    void Visit(IParamVisitor& v) {
        PerformanceSettings d;
        v.Bool(vsync, d.vsync, { "VSync", "Synchronize frame presentation to the display's refresh rate." });
        v.Int(targetFps, d.targetFps, 0, 500,
            { "Target FPS", "0 = uncapped. Useful with VSync off, to see how much particle-count headroom the GPU actually has before it starts to slow down." });
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
