// GPU port of src/math/Noise.cpp — a lightweight smoothed value-noise field
// used for turbulence and shape-conform surface flow. Kept behaviorally
// equivalent to the CPU version it replaces (same hash constants, same
// curl construction) so ported visualizers move the same way they did
// before the GPU port; only the curl's cost was changed, from six
// finite-difference noise taps to one analytic-gradient tap (see
// ValueNoise3DGrad).

float NoiseHash3(ivec3 p) {
    uint h = uint(p.x) * 374761393u + uint(p.y) * 668265263u + uint(p.z) * 2147483647u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h ^= (h >> 16u);
    return float(h & 0xFFFFFFu) / float(0xFFFFFFu) * 2.0 - 1.0;
}

float NoiseSmooth(float t) { return t * t * (3.0 - 2.0 * t); }
float NoiseSmoothDeriv(float t) { return 6.0 * t * (1.0 - t); }

float ValueNoise3D(vec3 p) {
    ivec3 p0 = ivec3(floor(p));
    ivec3 p1 = p0 + ivec3(1);
    vec3 t = vec3(
        NoiseSmooth(p.x - float(p0.x)),
        NoiseSmooth(p.y - float(p0.y)),
        NoiseSmooth(p.z - float(p0.z))
    );

    float c000 = NoiseHash3(ivec3(p0.x, p0.y, p0.z));
    float c100 = NoiseHash3(ivec3(p1.x, p0.y, p0.z));
    float c010 = NoiseHash3(ivec3(p0.x, p1.y, p0.z));
    float c110 = NoiseHash3(ivec3(p1.x, p1.y, p0.z));
    float c001 = NoiseHash3(ivec3(p0.x, p0.y, p1.z));
    float c101 = NoiseHash3(ivec3(p1.x, p0.y, p1.z));
    float c011 = NoiseHash3(ivec3(p0.x, p1.y, p1.z));
    float c111 = NoiseHash3(ivec3(p1.x, p1.y, p1.z));

    float x00 = mix(c000, c100, t.x);
    float x10 = mix(c010, c110, t.x);
    float x01 = mix(c001, c101, t.x);
    float x11 = mix(c011, c111, t.x);

    float y0 = mix(x00, x10, t.y);
    float y1 = mix(x01, x11, t.y);

    return mix(y0, y1, t.z);
}

// Value + analytic gradient in one pass: x = value, yzw = (d/dx, d/dy, d/dz).
// Trilinear interpolation is differentiable in closed form from the same 8
// corner hashes the value itself needs (see Inigo Quilez's noise-
// derivatives writeups for the general technique). This exists purely so
// CurlNoise3D (below) can get a gradient from ONE evaluation instead of the
// six offset re-evaluations (48 corner hashes total) a naive finite-
// difference curl needs; every alive particle pays for a curl evaluation
// each frame (see forces.glsl's Turbulence and shape_conform.glsl), so this
// ~6x cut in hash traffic is the single biggest lever on GPU particle-sim
// cost at high particle counts.
vec4 ValueNoise3DGrad(vec3 p) {
    ivec3 p0 = ivec3(floor(p));
    ivec3 p1 = p0 + ivec3(1);
    vec3 f = p - vec3(p0);
    vec3 S = vec3(NoiseSmooth(f.x), NoiseSmooth(f.y), NoiseSmooth(f.z));
    vec3 dS = vec3(NoiseSmoothDeriv(f.x), NoiseSmoothDeriv(f.y), NoiseSmoothDeriv(f.z));

    float c000 = NoiseHash3(ivec3(p0.x, p0.y, p0.z));
    float c100 = NoiseHash3(ivec3(p1.x, p0.y, p0.z));
    float c010 = NoiseHash3(ivec3(p0.x, p1.y, p0.z));
    float c110 = NoiseHash3(ivec3(p1.x, p1.y, p0.z));
    float c001 = NoiseHash3(ivec3(p0.x, p0.y, p1.z));
    float c101 = NoiseHash3(ivec3(p1.x, p0.y, p1.z));
    float c011 = NoiseHash3(ivec3(p0.x, p1.y, p1.z));
    float c111 = NoiseHash3(ivec3(p1.x, p1.y, p1.z));

    float x00 = mix(c000, c100, S.x);
    float x10 = mix(c010, c110, S.x);
    float x01 = mix(c001, c101, S.x);
    float x11 = mix(c011, c111, S.x);
    float dx00 = c100 - c000, dx10 = c110 - c010, dx01 = c101 - c001, dx11 = c111 - c011;

    float y0 = mix(x00, x10, S.y);
    float y1 = mix(x01, x11, S.y);
    float dy0_dSy = x10 - x00, dy1_dSy = x11 - x01;
    float dy0_dSx = mix(dx00, dx10, S.y);
    float dy1_dSx = mix(dx01, dx11, S.y);

    float value = mix(y0, y1, S.z);
    float dV_dSz = y1 - y0;
    float dV_dSy = mix(dy0_dSy, dy1_dSy, S.z);
    float dV_dSx = mix(dy0_dSx, dy1_dSx, S.z);

    return vec4(value, dV_dSx * dS.x, dV_dSy * dS.y, dV_dSz * dS.z);
}

// Cheap pseudo-curl built from one analytic-gradient noise sample (see
// ValueNoise3DGrad) instead of six finite-difference taps — not
// divergence-free in the strict mathematical sense, but visually
// indistinguishable at particle scale (same construction the old finite-
// difference version used: a 90-degree rotation of the scalar field's
// gradient) and ~6x cheaper.
vec3 CurlNoise3D(vec3 p, float t) {
    vec4 n = ValueNoise3DGrad(vec3(p.x, p.y, p.z + t));
    float a = n.z, b = n.y, c = n.w; // dy, dx, dz
    return vec3(b - c, c - a, a - b);
}
