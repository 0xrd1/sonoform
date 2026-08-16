#include "ShapeField.h"
#include "ShaderLibrary.h"
#include "GpuBindings.h"
#include "GlUniforms.h"
#include "GlCompat.h"
#include "rlgl.h"

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
}

void ShapeField::BindForSampling(unsigned int program) const {
    buffer_.BindBase(gpu_bindings::kShapeFieldBuffer);
    gl_uniforms::SetIVec3(program, "uShapeGridDim", resolution_, resolution_, resolution_);
    gl_uniforms::SetVec3(program, "uShapeGridCenter", center_.x, center_.y, center_.z);
    gl_uniforms::SetFloat(program, "uShapeGridHalfExtent", halfExtent_);
}
