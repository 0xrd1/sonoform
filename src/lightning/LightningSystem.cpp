#include "LightningSystem.h"
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

} // namespace

void LightningSystem::SpawnBolt(Vector3 origin, float strength) {
    strength = Clamp(strength, 0.0f, 1.0f);

    Bolt bolt;

    float dirAngleXZ = RandFloat(rngState_, 0.0f, 2.0f * PI);
    float dirElevation = RandFloat(rngState_, -0.4f, 0.9f); // biased upward/outward
    float length = (4.0f + RandFloat(rngState_, 0.0f, 4.0f)) * (0.6f + strength * 0.8f);

    Vector3 dir = Vector3Normalize(Vector3{
        std::cos(dirAngleXZ) * std::cos(dirElevation),
        std::sin(dirElevation),
        std::sin(dirAngleXZ) * std::cos(dirElevation)
    });

    Vector3 start = Vector3Add(origin, Vector3Scale(dir, 1.0f));
    Vector3 end = Vector3Add(origin, Vector3Scale(dir, length));

    const int depth = 5;
    float displacement = 1.2f * (0.6f + strength * 0.6f);
    bolt.points = BuildFractalPath(start, end, depth, displacement, rngState_);

    int branchCount = static_cast<int>(RandFloat(rngState_, 1.0f, 3.5f));
    for (int i = 0; i < branchCount && bolt.points.size() > 2; i++) {
        size_t startIdx = static_cast<size_t>(RandFloat(rngState_, 0.3f, 0.7f) * static_cast<float>(bolt.points.size() - 1));
        Vector3 branchStart = bolt.points[startIdx];
        Vector3 randOffset{ RandFloat(rngState_, -1.0f, 1.0f), RandFloat(rngState_, -1.0f, 1.0f), RandFloat(rngState_, -1.0f, 1.0f) };
        Vector3 branchDir = Vector3Normalize(Vector3Add(dir, randOffset));
        Vector3 branchEnd = Vector3Add(branchStart, Vector3Scale(branchDir, length * 0.4f));
        bolt.branches.push_back(BuildFractalPath(branchStart, branchEnd, depth - 2, displacement * 0.6f, rngState_));
    }

    // Electric blue-violet-cyan range, low saturation so it reads as
    // bright near-white light rather than a flat colored line.
    float hue = std::fmod(200.0f + RandFloat(rngState_, -20.0f, 40.0f) + 360.0f, 360.0f);
    bolt.color = ColorFromHSV(hue, 0.35f, 1.0f);
    bolt.maxLife = 0.12f + RandFloat(rngState_, 0.0f, 0.08f);
    bolt.life = bolt.maxLife;
    bolt.brightness = 1.5f + strength * 2.5f;

    bolts_.push_back(std::move(bolt));

    // Cap concurrent bolts so a burst of rapid beats can't runaway the
    // draw call count.
    if (bolts_.size() > 6) bolts_.erase(bolts_.begin());
}

void LightningSystem::Update(float dt, Vector3 origin, bool trigger, float triggerStrength) {
    if (trigger) SpawnBolt(origin, triggerStrength);

    for (auto it = bolts_.begin(); it != bolts_.end();) {
        it->life -= dt;
        if (it->life <= 0.0f) it = bolts_.erase(it);
        else ++it;
    }
}

void LightningSystem::Draw() const {
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

        drawPath(bolt.points, 0.035f, 0.16f);
        for (const auto& branch : bolt.branches) drawPath(branch, 0.02f, 0.1f);
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
