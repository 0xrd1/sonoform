#include "GlCompat.h"
#include "raylib.h"
#include "rlgl.h"

// --- Reaching into raylib's already-loaded GLAD state -----------------
//
// raylib bundles a single-header GLAD (src/external/glad.h) whose include
// guard covers the *entire* file, declarations and storage definitions
// together. It is included exactly once, from rlgl.c, when raylib itself
// is compiled with GRAPHICS_API_OPENGL_43 (set via the OPENGL_VERSION
// CMake cache var in CMakeLists.txt). That means `glad_glMemoryBarrier`
// already exists as a globally-linkable C symbol inside raylib.lib,
// populated by gladLoadGL() during InitWindow().
//
// We deliberately do NOT `#include "external/glad.h"` here: doing so from
// a second translation unit would re-expand the whole header (include
// guards are per-TU) and emit a second definition of the same global,
// which fails to link against the one already inside raylib.lib.
// Instead we declare a minimal `extern "C"` reference to the exact symbol
// raylib already populated, matching its C linkage (rlgl.c is compiled as
// plain C, so the symbol carries no C++ name mangling).
//
// This relies on raylib's internal GLAD symbol name, which is stable for
// the raylib version this project pins in CMakeLists.txt (5.5) but is not
// a documented public API. If a future raylib upgrade renames or removes
// it, GL_COMPAT_SELF_TEST below will fail loudly at startup rather than
// silently misbehaving.
#if defined(_WIN32)
    #define GLCOMPAT_APIENTRY __stdcall
#else
    #define GLCOMPAT_APIENTRY
#endif

extern "C" {
    using PFNGLMEMORYBARRIERPROC = void(GLCOMPAT_APIENTRY*)(unsigned int barriers);
    extern PFNGLMEMORYBARRIERPROC glad_glMemoryBarrier;
}

namespace {

// Standard OpenGL 4.3 core enum values (ARB_shader_storage_buffer_object /
// core spec). Hardcoded rather than pulled from a GL header so this file
// has no dependency beyond raylib's own headers.
constexpr unsigned int kShaderStorageBarrierBit = 0x00002000;
constexpr unsigned int kAllBarrierBits = 0xFFFFFFFFu;

bool gComputeAvailable = false;

} // namespace

namespace gl_compat {

bool Init() {
    gComputeAvailable = false;

    if (rlGetVersion() != RL_OPENGL_43) {
        TraceLog(LOG_FATAL,
                 "GlCompat: OpenGL context is not 4.3 (rlGetVersion()=%d). "
                 "This engine drives all particle simulation on the GPU via "
                 "compute shaders and has no CPU fallback. Check that "
                 "OPENGL_VERSION=4.3 was set before raylib was built (see "
                 "CMakeLists.txt) and that the GPU/driver supports OpenGL 4.3.",
                 rlGetVersion());
        return false;
    }

    if (glad_glMemoryBarrier == nullptr) {
        TraceLog(LOG_FATAL,
                 "GlCompat: glMemoryBarrier did not resolve. raylib's GLAD "
                 "loader may not have populated it (unexpected on a 4.3 "
                 "context) or its internal symbol name has changed.");
        return false;
    }

    gComputeAvailable = true;
    TraceLog(LOG_INFO, "GlCompat: OpenGL 4.3 compute-capable context confirmed.");
    return true;
}

bool IsComputeAvailable() {
    return gComputeAvailable;
}

void ShaderStorageBarrier() {
    glad_glMemoryBarrier(kShaderStorageBarrierBit);
}

void FullBarrier() {
    glad_glMemoryBarrier(kAllBarrierBits);
}

} // namespace gl_compat
