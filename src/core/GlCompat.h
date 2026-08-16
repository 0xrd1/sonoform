#pragma once

// Bridges the handful of raw OpenGL 4.3 entry points that raylib's rlgl.h
// does not wrap (most importantly glMemoryBarrier, which every compute ->
// render dependency in the GPU particle pipeline requires between passes).
//
// raylib itself already loads these function pointers via its bundled GLAD
// loader during InitWindow(). Rather than running a second GL loader (which
// would either fight raylib's or duplicate its function-pointer globals and
// fail to link), this module reaches directly into the symbols raylib's
// rlgl.c already defined and populated, and exposes a small, purpose-built
// wrapper API instead of raw GL calls scattered through the codebase.
namespace gl_compat {

// Must be called once, after InitWindow() (i.e. after an OpenGL context
// exists and raylib has loaded its GL function pointers). Verifies the
// context is OpenGL 4.3 (required for compute shaders / SSBOs) and that
// the barrier function pointer resolved correctly.
//
// Returns false if GPU compute is unavailable; the engine has no CPU
// particle fallback, so callers should treat false as fatal.
bool Init();

// True once Init() has succeeded. Cheap to call repeatedly.
bool IsComputeAvailable();

// Issues glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT), i.e. "writes from
// the shader storage buffer object stage must be visible before subsequent
// SSBO reads." Call this between a compute dispatch that writes an SSBO
// and any later dispatch/draw that reads it.
void ShaderStorageBarrier();

// Issues glMemoryBarrier(GL_ALL_BARRIER_BITS). Coarser and slower than
// ShaderStorageBarrier(); use only when debugging a suspected missing
// barrier, not in the steady-state pipeline.
void FullBarrier();

} // namespace gl_compat
