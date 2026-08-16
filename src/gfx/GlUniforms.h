#pragma once
#include "rlgl.h"

// Small helpers for setting uniforms on raw compute-shader programs
// (an unsigned int from rlLoadComputeShaderProgram, not a raylib Shader
// struct with a cached location array), so callers can't reach for
// raylib's SetShaderValue by mistake -- these go through rlgl's raw
// rlGetLocationUniform/rlSetUniform pair instead. Every setter no-ops on
// a location miss (e.g. a uniform the GLSL compiler optimized away for
// being unused on a given code path) rather than asserting, matching
// raylib's own SetShaderValue behavior.
namespace gl_uniforms {

inline void SetFloat(unsigned int program, const char* name, float v) {
    int loc = rlGetLocationUniform(program, name);
    if (loc != -1) rlSetUniform(loc, &v, RL_SHADER_UNIFORM_FLOAT, 1);
}

inline void SetInt(unsigned int program, const char* name, int v) {
    int loc = rlGetLocationUniform(program, name);
    if (loc != -1) rlSetUniform(loc, &v, RL_SHADER_UNIFORM_INT, 1);
}

inline void SetUint(unsigned int program, const char* name, unsigned int v) {
    int loc = rlGetLocationUniform(program, name);
    if (loc != -1) rlSetUniform(loc, &v, RL_SHADER_UNIFORM_UINT, 1);
}

inline void SetVec3(unsigned int program, const char* name, float x, float y, float z) {
    int loc = rlGetLocationUniform(program, name);
    if (loc != -1) {
        float v[3] = { x, y, z };
        rlSetUniform(loc, v, RL_SHADER_UNIFORM_VEC3, 1);
    }
}

inline void SetIVec3(unsigned int program, const char* name, int x, int y, int z) {
    int loc = rlGetLocationUniform(program, name);
    if (loc != -1) {
        int v[3] = { x, y, z };
        rlSetUniform(loc, v, RL_SHADER_UNIFORM_IVEC3, 1);
    }
}

// Templated on the vec4-like type (GpuVec4, raylib's Vector4, ...) so
// this header doesn't need to depend on any one of them; T just needs
// four public float members laid out contiguously as x,y,z,w.
template <typename Vec4Like>
inline void SetVec4(unsigned int program, const char* name, const Vec4Like& v) {
    int loc = rlGetLocationUniform(program, name);
    if (loc != -1) rlSetUniform(loc, &v, RL_SHADER_UNIFORM_VEC4, 1);
}

} // namespace gl_uniforms
