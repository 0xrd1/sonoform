// Manual trilinear sampling of a baked ShapeField SSBO. It's a flat SSBO
// rather than a GL 3D texture (keeping everything inside raylib's
// supported SSBO API -- see gfx/GpuBuffer -- with no raw-GL texture
// work), so there's no hardware texture filtering to lean on; this
// reimplements it by hand.

layout(std430, binding = 3) buffer ShapeFieldBuffer {
    vec4 shapeField[]; // xyz = gradient (points toward increasing distance), w = signed distance
};

uniform ivec3 uShapeGridDim;
uniform vec3 uShapeGridCenter;
uniform float uShapeGridHalfExtent;

vec4 FetchShapeVoxel(ivec3 c) {
    c = clamp(c, ivec3(0), uShapeGridDim - ivec3(1));
    int index = c.x + c.y * uShapeGridDim.x + c.z * uShapeGridDim.x * uShapeGridDim.y;
    return shapeField[index];
}

// Returns (gradient.xyz, signed distance.w) trilinearly interpolated at
// `worldPos`. Outside the grid's bounds, samples clamp to the nearest
// edge voxel (FetchShapeVoxel's clamp) rather than wrapping or reading
// out of bounds.
vec4 SampleShapeField(vec3 worldPos) {
    vec3 t = (worldPos - uShapeGridCenter) / uShapeGridHalfExtent; // [-1, 1] over the grid's cube
    vec3 gridPos = (t * 0.5 + 0.5) * vec3(uShapeGridDim) - 0.5;

    ivec3 c0 = ivec3(floor(gridPos));
    vec3 f = fract(gridPos);

    vec4 c000 = FetchShapeVoxel(c0 + ivec3(0, 0, 0));
    vec4 c100 = FetchShapeVoxel(c0 + ivec3(1, 0, 0));
    vec4 c010 = FetchShapeVoxel(c0 + ivec3(0, 1, 0));
    vec4 c110 = FetchShapeVoxel(c0 + ivec3(1, 1, 0));
    vec4 c001 = FetchShapeVoxel(c0 + ivec3(0, 0, 1));
    vec4 c101 = FetchShapeVoxel(c0 + ivec3(1, 0, 1));
    vec4 c011 = FetchShapeVoxel(c0 + ivec3(0, 1, 1));
    vec4 c111 = FetchShapeVoxel(c0 + ivec3(1, 1, 1));

    vec4 x00 = mix(c000, c100, f.x);
    vec4 x10 = mix(c010, c110, f.x);
    vec4 x01 = mix(c001, c101, f.x);
    vec4 x11 = mix(c011, c111, f.x);

    vec4 y0 = mix(x00, x10, f.y);
    vec4 y1 = mix(x01, x11, f.y);

    return mix(y0, y1, f.z);
}
