// GPU-side PRNG for emission jitter. Not cryptographic — a fast, well
// distributed integer hash (the "PCG-ish" mix popularized by Mark Jarzynski
// & Marc Olano's hash functions paper), seeded per-particle so emission
// jitter looks random without uploading any host-generated randomness.

uint HashU(uint x) {
    x ^= x >> 16u; x *= 0x7feb352du;
    x ^= x >> 15u; x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

float Rand01(inout uint state) {
    state = HashU(state);
    return float(state) * (1.0 / 4294967296.0);
}

float RandRange(inout uint state, float a, float b) {
    return a + Rand01(state) * (b - a);
}

vec3 RandBox(inout uint state, vec3 halfExtents) {
    return vec3(
        RandRange(state, -halfExtents.x, halfExtents.x),
        RandRange(state, -halfExtents.y, halfExtents.y),
        RandRange(state, -halfExtents.z, halfExtents.z)
    );
}

// Uniform-random point on the unit sphere (Marsaglia-ish via z/angle).
vec3 RandUnitSphere(inout uint state) {
    float z = RandRange(state, -1.0, 1.0);
    float a = RandRange(state, 0.0, 6.28318530718);
    float r = sqrt(max(0.0, 1.0 - z * z));
    return vec3(r * cos(a), r * sin(a), z);
}
