// GPU port of src/math/Noise.cpp — a lightweight smoothed value-noise field
// used for turbulence and, later, surface-parallel shape flow. Kept
// behaviorally equivalent to the CPU version it replaces (same hash
// constants, same finite-difference curl approximation) so ported
// visualizers move the same way they did before the GPU port.

float NoiseHash3(ivec3 p) {
    uint h = uint(p.x) * 374761393u + uint(p.y) * 668265263u + uint(p.z) * 2147483647u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h ^= (h >> 16u);
    return float(h & 0xFFFFFFu) / float(0xFFFFFFu) * 2.0 - 1.0;
}

float NoiseSmooth(float t) { return t * t * (3.0 - 2.0 * t); }

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

// Cheap pseudo-curl via finite differences of the scalar noise field —
// not divergence-free in the strict mathematical sense, but visually
// indistinguishable at particle scale and far cheaper than a true
// vector-potential curl.
vec3 CurlNoise3D(vec3 p, float t) {
    const float e = 0.15;

    float n1 = ValueNoise3D(vec3(p.x, p.y + e, p.z + t));
    float n2 = ValueNoise3D(vec3(p.x, p.y - e, p.z + t));
    float a = (n1 - n2) / (2.0 * e);

    float n3 = ValueNoise3D(vec3(p.x + e, p.y, p.z + t));
    float n4 = ValueNoise3D(vec3(p.x - e, p.y, p.z + t));
    float b = (n3 - n4) / (2.0 * e);

    float n5 = ValueNoise3D(vec3(p.x, p.y, p.z + e + t));
    float n6 = ValueNoise3D(vec3(p.x, p.y, p.z - e + t));
    float c = (n5 - n6) / (2.0 * e);

    return vec3(b - c, c - a, a - b);
}
