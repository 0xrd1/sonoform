#include "ShaderLibrary.h"
#include "rlgl.h"

#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        TraceLog(LOG_ERROR, "ShaderLibrary: failed to open %s", path.c_str());
        return {};
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

} // namespace

std::string ShaderLibrary::ResolveIncludes(const std::string& path, std::unordered_map<std::string, bool>& included) {
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(fs::path(path), ec);
    if (ec) canonical = fs::path(path);
    std::string key = canonical.string();

    if (included.count(key) != 0) return ""; // pragma-once: already spliced in
    included[key] = true;

    std::string source = ReadFile(canonical.string());
    if (source.empty()) return source;

    fs::path dir = canonical.parent_path();
    std::ostringstream out;
    std::istringstream in(source);
    std::string line;

    while (std::getline(in, line)) {
        size_t firstNonSpace = line.find_first_not_of(" \t");
        if (firstNonSpace != std::string::npos && line.compare(firstNonSpace, 8, "#include") == 0) {
            size_t firstQuote = line.find('"', firstNonSpace);
            size_t lastQuote = line.rfind('"');
            if (firstQuote != std::string::npos && lastQuote != std::string::npos && lastQuote > firstQuote) {
                std::string includeRel = line.substr(firstQuote + 1, lastQuote - firstQuote - 1);
                fs::path includePath = dir / includeRel;
                out << ResolveIncludes(includePath.string(), included) << "\n";
                continue;
            }
        }
        out << line << "\n";
    }

    return out.str();
}

std::string ShaderLibrary::LoadWithIncludes(const std::string& path) {
    std::unordered_map<std::string, bool> included;
    return ResolveIncludes(path, included);
}

unsigned int ShaderLibrary::LoadCompute(const std::string& path) {
    std::string source = LoadWithIncludes(path);
    if (source.empty()) {
        TraceLog(LOG_ERROR, "ShaderLibrary: empty or unreadable compute source %s", path.c_str());
        return 0;
    }

    unsigned int shaderId = rlCompileShader(source.c_str(), RL_COMPUTE_SHADER);
    if (shaderId == 0) {
        TraceLog(LOG_ERROR, "ShaderLibrary: failed to compile compute shader %s", path.c_str());
        return 0;
    }

    unsigned int program = rlLoadComputeShaderProgram(shaderId);
    if (program == 0) {
        TraceLog(LOG_ERROR, "ShaderLibrary: failed to link compute program %s", path.c_str());
    }
    return program;
}

void ShaderLibrary::UnloadCompute(unsigned int program) {
    if (program != 0) rlUnloadShaderProgram(program);
}

Shader ShaderLibrary::LoadGraphics(const std::string& vsPath, const std::string& fsPath) {
    std::string key = vsPath + "|" + fsPath;
    auto it = graphicsCache_.find(key);
    if (it != graphicsCache_.end()) return it->second;

    std::string vsSource = LoadWithIncludes(vsPath);
    std::string fsSource = LoadWithIncludes(fsPath);

    Shader shader = LoadShaderFromMemory(vsSource.c_str(), fsSource.c_str());
    if (!IsShaderValid(shader)) {
        TraceLog(LOG_ERROR, "ShaderLibrary: failed to load graphics shader %s / %s", vsPath.c_str(), fsPath.c_str());
    }

    graphicsCache_[key] = shader;
    return shader;
}

Shader ShaderLibrary::LoadGraphicsFS(const std::string& fsPath) {
    std::string key = "__default_vs__|" + fsPath;
    auto it = graphicsCache_.find(key);
    if (it != graphicsCache_.end()) return it->second;

    std::string fsSource = LoadWithIncludes(fsPath);
    Shader shader = LoadShaderFromMemory(nullptr, fsSource.c_str());
    if (!IsShaderValid(shader)) {
        TraceLog(LOG_ERROR, "ShaderLibrary: failed to load fragment-only shader %s", fsPath.c_str());
    }

    graphicsCache_[key] = shader;
    return shader;
}

void ShaderLibrary::Dispatch1D(unsigned int program, unsigned int itemCount, unsigned int localSizeX) {
    if (program == 0 || itemCount == 0) return;
    unsigned int groups = (itemCount + localSizeX - 1u) / localSizeX;
    rlEnableShader(program);
    rlComputeShaderDispatch(groups, 1, 1);
    rlDisableShader();
}

void ShaderLibrary::UnloadAll() {
    for (auto& [key, shader] : graphicsCache_) {
        (void)key;
        UnloadShader(shader);
    }
    graphicsCache_.clear();
}
