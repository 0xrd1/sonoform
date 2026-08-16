#pragma once
#include <string>
#include <unordered_map>
#include "raylib.h"

// Loads and compiles GLSL sources (compute + graphics), with a minimal
// `#include "relative/path.glsl"` preprocessor: core-profile GLSL compiled
// at runtime via glCompileShader has no native #include, so shared code
// (particle struct layout, noise, force application) would otherwise have
// to be copy-pasted into every shader that needs it. Includes use
// pragma-once semantics (tracked per top-level load) so a file included
// from two different places -- e.g. common/noise.glsl pulled in by both
// forces.glsl and a future shape shader -- doesn't produce a GLSL
// redefinition error.
class ShaderLibrary {
public:
    // Loads, preprocesses, compiles, and links a compute shader program
    // from a single .comp file. Returns 0 on failure (the reason is
    // logged via raylib's TraceLog).
    unsigned int LoadCompute(const std::string& path);
    void UnloadCompute(unsigned int program);

    // Loads a standard vertex+fragment program, applying the same
    // #include preprocessing to both stages. Cached by (vsPath, fsPath)
    // so repeated calls don't reload or recompile.
    Shader LoadGraphics(const std::string& vsPath, const std::string& fsPath);

    // Convenience for full-screen post-process passes: uses raylib's
    // built-in default vertex shader (matches DrawTexturePro's expected
    // attributes) paired with a custom fragment shader.
    Shader LoadGraphicsFS(const std::string& fsPath);

    // Binds `program`, dispatches over ceil(itemCount / localSizeX) work
    // groups on the X axis (Y=Z=1), then unbinds. localSizeX must match
    // the shader's `layout(local_size_x = ...)`.
    static void Dispatch1D(unsigned int program, unsigned int itemCount, unsigned int localSizeX = 64);

    // Unloads every cached graphics shader. Compute programs are owned by
    // whoever loaded them (typically GpuParticleSystem) and must be
    // unloaded via UnloadCompute individually.
    void UnloadAll();

private:
    std::string LoadWithIncludes(const std::string& path);
    std::string ResolveIncludes(const std::string& path, std::unordered_map<std::string, bool>& included);

    std::unordered_map<std::string, Shader> graphicsCache_;
};
