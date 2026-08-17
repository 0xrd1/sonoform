#include "LightningSystem.h"
#include "EngineSettings.h"
#include "ShapeField.h"
#include "raymath.h"
#include <cmath>
#include <algorithm>

namespace {

// A tiny, fast integer hash-based PRNG (xorshift-ish), local to this file
// so LightningSystem doesn't need to pull in <random>'s heavier machinery
// for what's ultimately a handful of draws per bolt.
float RandFloat(unsigned int& state, float a, float b) {
    state = state * 747796405u + 2891336453u;
    unsigned int x = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    x = (x >> 22u) ^ x;
    return a + (static_cast<float>(x) / 4294967295.0f) * (b - a);
}

// Builds a jagged path from `start` to `end` via iterative midpoint
// displacement: each pass, every existing segment gets a new point
// inserted at its midpoint, offset perpendicular to the segment by a
// random amount that shrinks each level -- the standard fractal-
// lightning technique.
std::vector<Vector3> BuildFractalPath(Vector3 start, Vector3 end, int depth, float displacement, unsigned int& rng) {
    std::vector<Vector3> path = { start, end };

    for (int level = 0; level < depth; level++) {
        std::vector<Vector3> next;
        next.reserve(path.size() * 2);
        float levelDisp = displacement * std::pow(0.55f, static_cast<float>(level));

        for (size_t i = 0; i + 1 < path.size(); i++) {
            Vector3 a = path[i];
            Vector3 b = path[i + 1];
            Vector3 mid = Vector3Lerp(a, b, 0.5f);

            Vector3 dir = Vector3Normalize(Vector3Subtract(b, a));
            Vector3 arbitrary = (std::fabs(dir.y) < 0.9f) ? Vector3{ 0, 1, 0 } : Vector3{ 1, 0, 0 };
            Vector3 perp1 = Vector3Normalize(Vector3CrossProduct(dir, arbitrary));
            Vector3 perp2 = Vector3CrossProduct(dir, perp1);

            float rx = RandFloat(rng, -1.0f, 1.0f);
            float ry = RandFloat(rng, -1.0f, 1.0f);
            mid = Vector3Add(mid, Vector3Add(Vector3Scale(perp1, rx * levelDisp), Vector3Scale(perp2, ry * levelDisp)));

            next.push_back(a);
            next.push_back(mid);
        }
        next.push_back(path.back());
        path = std::move(next);
    }

    return path;
}

// Finds a point inside the shape's real baked SDF near `around`, using
// the field's own gradient to step each random candidate toward the
// interior (the same Newton-step-toward-the-surface technique
// particle_emit.comp's ShapeSurface spawn mode already uses) rather than
// blind rejection sampling. Blind rejection (try a uniform-random point,
// check if it's inside) was the first version of this function and had a
// real, observed failure mode: a thin shape like Torus occupies only a
// few percent of a search box sized to the field's full half-extent, so
// a bounded number of tries missed it roughly half the time, silently
// falling back to `around` for both endpoints and producing an invisible
// (zero-length) "bolt." Gradient-stepping converges onto the shape from
// any starting candidate in a handful of iterations regardless of how
// thin or small it is relative to the search box.
//
// `searchRadius` only controls how far the *starting* candidates are
// scattered around `around` (for variety between calls) -- it does not
// need to be shape-sized the way the old rejection radius effectively
// did. Falls back to `around` itself if every candidate still lands
// outside (e.g. `around` is far outside the field entirely), so this
// always returns *something* usable rather than looping forever.
Vector3 SampleInsidePoint(const ShapeField& field, Vector3 around, float searchRadius, unsigned int& rng) {
    constexpr int kCandidates = 8;
    constexpr int kGradientSteps = 4;

    for (int i = 0; i < kCandidates; i++) {
        Vector3 dir{ RandFloat(rng, -1.0f, 1.0f), RandFloat(rng, -1.0f, 1.0f), RandFloat(rng, -1.0f, 1.0f) };
        if (Vector3LengthSqr(dir) < 0.0001f) dir = Vector3{ 0.0f, 1.0f, 0.0f }; // avoid a degenerate zero vector
        dir = Vector3Normalize(dir);
        Vector3 candidate = Vector3Add(around, Vector3Scale(dir, RandFloat(rng, 0.0f, searchRadius)));

        for (int step = 0; step < kGradientSteps; step++) {
            ShapeField::FieldSample s = field.SampleWorld(candidate);
            if (s.distance < 0.0f) return candidate;
            // gradient points toward increasing distance (outward), so
            // stepping against it moves toward (and, with the small
            // overshoot, past) the surface.
            candidate = Vector3Subtract(candidate, Vector3Scale(s.gradient, s.distance + 0.05f));
        }
        if (field.SampleWorld(candidate).distance < 0.0f) return candidate;
    }
    return around;
}

} // namespace

void LightningSystem::SpawnBolt(const ShapeField& field, float strength, const ui::LightningSettings& s) {
    strength = Clamp(strength, 0.0f, 1.0f);

    Bolt bolt;

    // Both endpoints sampled from inside the real shape -- see
    // SampleInsidePoint's comment -- rather than a fixed length extended
    // in a random direction from a single origin, which routinely shot
    // bolts out past the fog into empty space. The fractal path between
    // two interior points automatically stays roughly contained and
    // automatically scales to whatever shape/size is currently baked; no
    // separate length knob needed.
    Vector3 start = SampleInsidePoint(field, field.Center(), field.HalfExtent(), rngState_);
    Vector3 end = SampleInsidePoint(field, field.Center(), field.HalfExtent(), rngState_);

    const int depth = s.fractalDepth;
    float displacement = s.displacementBase * (0.6f + strength * 0.6f);
    bolt.points = BuildFractalPath(start, end, depth, displacement, rngState_);

    int branchCount = static_cast<int>(RandFloat(rngState_, static_cast<float>(s.branchCountMin), static_cast<float>(s.branchCountMax) + 0.5f));
    for (int i = 0; i < branchCount && bolt.points.size() > 2; i++) {
        size_t startIdx = static_cast<size_t>(RandFloat(rngState_, 0.3f, 0.7f) * static_cast<float>(bolt.points.size() - 1));
        Vector3 branchStart = bolt.points[startIdx];
        // Local search radius (a fraction of the shape's own extent) so
        // branches read as short offshoots of the trunk, not independent
        // bolts jumping to a random spot in the shape.
        Vector3 branchEnd = SampleInsidePoint(field, branchStart, field.HalfExtent() * 0.4f, rngState_);
        bolt.branches.push_back(BuildFractalPath(branchStart, branchEnd, depth - 2, displacement * 0.6f, rngState_));
    }

    // Vivid, clearly colored (see LightningSettings::saturation's default)
    // rather than near-white.
    float hue = std::fmod(s.hueBase + RandFloat(rngState_, s.hueJitterMin, s.hueJitterMax) + 360.0f, 360.0f);
    bolt.color = ColorFromHSV(hue, s.saturation, 1.0f);
    bolt.maxLife = s.lifeMin + RandFloat(rngState_, 0.0f, s.lifeJitter);
    bolt.life = bolt.maxLife;
    bolt.brightness = s.brightnessBase + strength * s.brightnessStrengthMult;

    bolts_.push_back(std::move(bolt));

    // Cap concurrent bolts so a burst of rapid beats can't runaway the
    // draw call count.
    if (static_cast<int>(bolts_.size()) > s.maxBolts) bolts_.erase(bolts_.begin());
}

void LightningSystem::Update(float dt, const ShapeField& field, bool trigger, float triggerStrength, const ui::LightningSettings& settings) {
    if (trigger) SpawnBolt(field, triggerStrength, settings);

    for (auto it = bolts_.begin(); it != bolts_.end();) {
        it->life -= dt;
        if (it->life <= 0.0f) it = bolts_.erase(it);
        else ++it;
    }
}

void LightningSystem::Draw(const ui::LightningSettings& settings) const {
    for (const auto& bolt : bolts_) {
        float lifeRatio = bolt.life / bolt.maxLife;
        // Sharp attack, fast decay -- a flash, not a linear fade.
        float envelope = lifeRatio * lifeRatio;
        if (envelope < 0.01f) continue;

        auto drawPath = [&](const std::vector<Vector3>& pts, float coreRadius, float haloRadius) {
            for (size_t i = 0; i + 1 < pts.size(); i++) {
                DrawCylinderEx(pts[i], pts[i + 1], haloRadius, haloRadius, 6, Fade(bolt.color, 0.25f * envelope));
                DrawCylinderEx(pts[i], pts[i + 1], coreRadius, coreRadius, 6, Fade(WHITE, envelope));
            }
        };

        drawPath(bolt.points, settings.coreRadius, settings.haloRadius);
        for (const auto& branch : bolt.branches) drawPath(branch, settings.branchCoreRadius, settings.branchHaloRadius);
    }
}

int LightningSystem::GatherLights(LightSample* outLights, int maxLights) const {
    int count = 0;
    for (const auto& bolt : bolts_) {
        if (count >= maxLights) break;
        if (bolt.points.empty()) continue;

        float lifeRatio = bolt.life / bolt.maxLife;
        float envelope = lifeRatio * lifeRatio;
        if (envelope < 0.02f) continue;

        // Two samples (start + midpoint) so a long bolt lights fog along
        // more of its length, not just at its origin.
        outLights[count++] = LightSample{ bolt.points.front(), bolt.brightness * envelope * 2.0f, bolt.color };
        if (count < maxLights) {
            size_t midIdx = bolt.points.size() / 2;
            outLights[count++] = LightSample{ bolt.points[midIdx], bolt.brightness * envelope * 2.0f, bolt.color };
        }
    }
    return count;
}
