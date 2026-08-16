#include "SettingsIO.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace fs = std::filesystem;

namespace ui {

namespace {

// %.9g round-trips a 32-bit float exactly (9 significant decimal digits is
// the documented sufficient bound for IEEE-754 binary32) while staying
// readable in a hand-edited file, unlike a fixed-width hex dump.
std::string FormatFloat(float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    return buf;
}

float ParseFloat(const std::string& s, float fallback) {
    if (s.empty()) return fallback;
    char* end = nullptr;
    float v = std::strtof(s.c_str(), &end);
    return (end != s.c_str()) ? v : fallback;
}

int ParseInt(const std::string& s, int fallback) {
    if (s.empty()) return fallback;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    return (end != s.c_str()) ? static_cast<int>(v) : fallback;
}

template <typename T>
T ClampT(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Splits "a,b,c" (used for Vec3/Color) on commas, tolerating stray
// whitespace from a hand-edited file.
std::vector<std::string> SplitCsv(const std::string& s) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        size_t a = item.find_first_not_of(" \t");
        size_t b = item.find_last_not_of(" \t");
        parts.push_back(a == std::string::npos ? "" : item.substr(a, b - a + 1));
    }
    return parts;
}

} // namespace

// ---------------------------------------------------------------------
// SettingsWriter
// ---------------------------------------------------------------------

std::string SettingsWriter::CurrentPath() const {
    std::string p;
    for (size_t i = 0; i < path_.size(); i++) {
        if (i > 0) p += '/';
        p += path_[i];
    }
    return p;
}

void SettingsWriter::WriteLine(const char* label, const std::string& valueText) {
    out_ += CurrentPath();
    if (!path_.empty()) out_ += '/';
    out_ += label;
    out_ += '=';
    out_ += valueText;
    out_ += '\n';
}

void SettingsWriter::BeginGroup(const char* name) { path_.emplace_back(name); }
void SettingsWriter::EndGroup() { if (!path_.empty()) path_.pop_back(); }

void SettingsWriter::Float(float& value, float, float, float, const ParamMeta& meta) {
    WriteLine(meta.label, FormatFloat(value));
}
void SettingsWriter::Int(int& value, int, int, int, const ParamMeta& meta) {
    WriteLine(meta.label, std::to_string(value));
}
void SettingsWriter::Bool(bool& value, bool, const ParamMeta& meta) {
    WriteLine(meta.label, value ? "1" : "0");
}
void SettingsWriter::Vec3(Vector3& value, Vector3, float, float, const ParamMeta& meta) {
    WriteLine(meta.label, FormatFloat(value.x) + "," + FormatFloat(value.y) + "," + FormatFloat(value.z));
}
void SettingsWriter::ColorField(Color& value, Color, const ParamMeta& meta) {
    WriteLine(meta.label, std::to_string(value.r) + "," + std::to_string(value.g) + "," +
                           std::to_string(value.b) + "," + std::to_string(value.a));
}
void SettingsWriter::Enum(int& value, int, const char* const*, int, const ParamMeta& meta) {
    // Stored by index, not name: simplest round-trip and fine for this
    // project's small, stable enum lists (see EngineSettings.h). A
    // reordered enum would silently remap on load -- acceptable here, but
    // worth knowing if ShapeTypeNames() etc. ever gets reordered.
    WriteLine(meta.label, std::to_string(value));
}

// ---------------------------------------------------------------------
// SettingsReader
// ---------------------------------------------------------------------

SettingsReader::SettingsReader(const std::string& text) {
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue; // blank line, stray text -- ignore rather than fail the whole load
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        // Tolerate a trailing '\r' from a file saved/edited on Windows vs.
        // read with '\n'-only getline splitting.
        if (!val.empty() && val.back() == '\r') val.pop_back();
        values_.emplace(std::move(key), std::move(val));
    }
}

std::string SettingsReader::CurrentPath() const {
    std::string p;
    for (size_t i = 0; i < path_.size(); i++) {
        if (i > 0) p += '/';
        p += path_[i];
    }
    return p;
}

const std::string* SettingsReader::Find(const char* label) const {
    std::string key = CurrentPath();
    if (!path_.empty()) key += '/';
    key += label;
    auto it = values_.find(key);
    return it != values_.end() ? &it->second : nullptr;
}

void SettingsReader::BeginGroup(const char* name) { path_.emplace_back(name); }
void SettingsReader::EndGroup() { if (!path_.empty()) path_.pop_back(); }

void SettingsReader::Float(float& value, float defaultValue, float min, float max, const ParamMeta& meta) {
    if (const std::string* s = Find(meta.label)) value = ClampT(ParseFloat(*s, defaultValue), min, max);
}
void SettingsReader::Int(int& value, int defaultValue, int min, int max, const ParamMeta& meta) {
    if (const std::string* s = Find(meta.label)) value = ClampT(ParseInt(*s, defaultValue), min, max);
}
void SettingsReader::Bool(bool& value, bool, const ParamMeta& meta) {
    if (const std::string* s = Find(meta.label)) value = (*s == "1" || *s == "true");
}
void SettingsReader::Vec3(Vector3& value, Vector3 defaultValue, float min, float max, const ParamMeta& meta) {
    const std::string* s = Find(meta.label);
    if (s == nullptr) return;
    std::vector<std::string> parts = SplitCsv(*s);
    if (parts.size() != 3) return; // malformed -- keep whatever the field already held
    value.x = ClampT(ParseFloat(parts[0], defaultValue.x), min, max);
    value.y = ClampT(ParseFloat(parts[1], defaultValue.y), min, max);
    value.z = ClampT(ParseFloat(parts[2], defaultValue.z), min, max);
}
void SettingsReader::ColorField(Color& value, Color, const ParamMeta& meta) {
    const std::string* s = Find(meta.label);
    if (s == nullptr) return;
    std::vector<std::string> parts = SplitCsv(*s);
    if (parts.size() != 4) return;
    value.r = static_cast<unsigned char>(ClampT(ParseInt(parts[0], value.r), 0, 255));
    value.g = static_cast<unsigned char>(ClampT(ParseInt(parts[1], value.g), 0, 255));
    value.b = static_cast<unsigned char>(ClampT(ParseInt(parts[2], value.b), 0, 255));
    value.a = static_cast<unsigned char>(ClampT(ParseInt(parts[3], value.a), 0, 255));
}
void SettingsReader::Enum(int& value, int defaultValue, const char* const*, int count, const ParamMeta& meta) {
    if (const std::string* s = Find(meta.label)) value = ClampT(ParseInt(*s, defaultValue), 0, count - 1);
}

// ---------------------------------------------------------------------
// File-level helpers
// ---------------------------------------------------------------------

bool SaveSettings(const std::string& path, const std::function<void(IParamVisitor&)>& visitAll) {
    SettingsWriter writer;
    visitAll(writer);

    std::error_code ec;
    fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out << writer.Text();
    return static_cast<bool>(out);
}

bool LoadSettings(const std::string& path, const std::function<void(IParamVisitor&)>& visitAll) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false; // no preset saved yet -- normal, not an error

    std::stringstream buf;
    buf << in.rdbuf();

    SettingsReader reader(buf.str());
    visitAll(reader);
    return true;
}

std::vector<std::string> ListPresets(const std::string& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return names;

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".ini") continue;
        names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool DeletePreset(const std::string& dir, const std::string& name) {
    std::error_code ec;
    fs::path p = fs::path(dir) / (name + ".ini");
    return fs::remove(p, ec);
}

} // namespace ui
