#include "ShapeField.h"
#include "ShaderLibrary.h"
#include "GpuBindings.h"
#include "GlUniforms.h"
#include "GlCompat.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>

ShapeField::ShapeField(ShaderLibrary& shaders, int resolution, Vector3 center, float halfExtent)
    : shaders_(shaders), resolution_(resolution), center_(center), halfExtent_(halfExtent) {
    unsigned int voxelCount = static_cast<unsigned int>(resolution_) * static_cast<unsigned int>(resolution_) * static_cast<unsigned int>(resolution_);
    buffer_.Allocate(voxelCount * sizeof(float) * 4); // vec4 per voxel

    bakeProgram_ = shaders_.LoadCompute("assets/shaders/shapes/shape_bake.comp");
}

ShapeField::~ShapeField() {
    shaders_.UnloadCompute(bakeProgram_);
}

void ShapeField::BakeProcedural(int shapeType) {
    if (bakeProgram_ == 0) return;

    rlEnableShader(bakeProgram_);

    gl_uniforms::SetIVec3(bakeProgram_, "uGridDim", resolution_, resolution_, resolution_);
    gl_uniforms::SetVec3(bakeProgram_, "uGridCenter", center_.x, center_.y, center_.z);
    gl_uniforms::SetFloat(bakeProgram_, "uGridHalfExtent", halfExtent_);
    gl_uniforms::SetInt(bakeProgram_, "uShapeType", shapeType);

    buffer_.BindBase(gpu_bindings::kShapeFieldBuffer);

    // local_size is 4x4x4 (see shape_bake.comp); ceil-divide the grid
    // resolution by that in each dimension.
    unsigned int groups = (static_cast<unsigned int>(resolution_) + 3u) / 4u;
    rlComputeShaderDispatch(groups, groups, groups);

    rlDisableShader();

    gl_compat::ShaderStorageBarrier();

    // The SSBO just changed -- any previously-read CPU-side cache is now
    // stale. Not refreshed here (that's a real readback cost -- see
    // RefreshSampleCache's comment); just marked, so a caller that
    // actually needs SampleWorld() this frame knows to refresh first.
    sampleCacheStale_ = true;
}

void ShapeField::BindForSampling(unsigned int program) const {
    buffer_.BindBase(gpu_bindings::kShapeFieldBuffer);
    gl_uniforms::SetIVec3(program, "uShapeGridDim", resolution_, resolution_, resolution_);
    gl_uniforms::SetVec3(program, "uShapeGridCenter", center_.x, center_.y, center_.z);
    gl_uniforms::SetFloat(program, "uShapeGridHalfExtent", halfExtent_);
}

void ShapeField::RefreshSampleCache() {
    unsigned int voxelCount = static_cast<unsigned int>(resolution_) * static_cast<unsigned int>(resolution_) * static_cast<unsigned int>(resolution_);
    cpuCache_.resize(voxelCount);
    buffer_.Read(cpuCache_.data(), voxelCount * sizeof(Vector4), 0);
    sampleCacheStale_ = false;
}

namespace {
// Fetches one voxel, clamping to the nearest edge voxel out of bounds --
// mirrors shape_field_sample.glsl's FetchShapeVoxel exactly (including its
// clamp-not-wrap boundary behavior).
Vector4 FetchVoxel(const std::vector<Vector4>& cache, int dim, int x, int y, int z) {
    x = std::clamp(x, 0, dim - 1);
    y = std::clamp(y, 0, dim - 1);
    z = std::clamp(z, 0, dim - 1);
    int index = x + y * dim + z * dim * dim;
    return cache[static_cast<size_t>(index)];
}

Vector4 Mix(Vector4 a, Vector4 b, float t) {
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
}
} // namespace

ShapeField::FieldSample ShapeField::SampleWorld(Vector3 worldPos) const {
    if (cpuCache_.empty()) return {};

    // Same world -> grid transform as shape_field_sample.glsl's
    // SampleShapeField.
    Vector3 t{ (worldPos.x - center_.x) / halfExtent_, (worldPos.y - center_.y) / halfExtent_, (worldPos.z - center_.z) / halfExtent_ };
    Vector3 gridPos{
        (t.x * 0.5f + 0.5f) * static_cast<float>(resolution_) - 0.5f,
        (t.y * 0.5f + 0.5f) * static_cast<float>(resolution_) - 0.5f,
        (t.z * 0.5f + 0.5f) * static_cast<float>(resolution_) - 0.5f,
    };

    int c0x = static_cast<int>(floorf(gridPos.x));
    int c0y = static_cast<int>(floorf(gridPos.y));
    int c0z = static_cast<int>(floorf(gridPos.z));
    float fx = gridPos.x - static_cast<float>(c0x);
    float fy = gridPos.y - static_cast<float>(c0y);
    float fz = gridPos.z - static_cast<float>(c0z);

    Vector4 c000 = FetchVoxel(cpuCache_, resolution_, c0x, c0y, c0z);
    Vector4 c100 = FetchVoxel(cpuCache_, resolution_, c0x + 1, c0y, c0z);
    Vector4 c010 = FetchVoxel(cpuCache_, resolution_, c0x, c0y + 1, c0z);
    Vector4 c110 = FetchVoxel(cpuCache_, resolution_, c0x + 1, c0y + 1, c0z);
    Vector4 c001 = FetchVoxel(cpuCache_, resolution_, c0x, c0y, c0z + 1);
    Vector4 c101 = FetchVoxel(cpuCache_, resolution_, c0x + 1, c0y, c0z + 1);
    Vector4 c011 = FetchVoxel(cpuCache_, resolution_, c0x, c0y + 1, c0z + 1);
    Vector4 c111 = FetchVoxel(cpuCache_, resolution_, c0x + 1, c0y + 1, c0z + 1);

    Vector4 x00 = Mix(c000, c100, fx);
    Vector4 x10 = Mix(c010, c110, fx);
    Vector4 x01 = Mix(c001, c101, fx);
    Vector4 x11 = Mix(c011, c111, fx);

    Vector4 y0 = Mix(x00, x10, fy);
    Vector4 y1 = Mix(x01, x11, fy);

    Vector4 result = Mix(y0, y1, fz);

    FieldSample sample;
    sample.gradient = { result.x, result.y, result.z };
    sample.distance = result.w;
    return sample;
}
