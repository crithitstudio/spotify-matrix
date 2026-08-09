// IRIS engine - core utilities: logging, files, time.
#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace iris {

enum class LogLevel { Info, Warn, Error };

inline void logMsg(LogLevel lv, const char* fmt, ...) {
    const char* tag = lv == LogLevel::Info ? "[iris]" : (lv == LogLevel::Warn ? "[iris:warn]" : "[iris:ERROR]");
    std::fprintf(stderr, "%s ", tag);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "\n");
}
#define IRIS_INFO(...)  ::iris::logMsg(::iris::LogLevel::Info, __VA_ARGS__)
#define IRIS_WARN(...)  ::iris::logMsg(::iris::LogLevel::Warn, __VA_ARGS__)
#define IRIS_ERROR(...) ::iris::logMsg(::iris::LogLevel::Error, __VA_ARGS__)

#define IRIS_FATAL(...)                                    \
    do {                                                   \
        ::iris::logMsg(::iris::LogLevel::Error, __VA_ARGS__); \
        std::abort();                                      \
    } while (0)

inline bool readFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0) { std::fclose(f); return false; }
    out.resize((size_t)sz);
    size_t rd = sz ? std::fread(out.data(), 1, (size_t)sz, f) : 0;
    std::fclose(f);
    return rd == (size_t)sz;
}

inline bool readFileText(const std::string& path, std::string& out) {
    std::vector<uint8_t> b;
    if (!readFileBytes(path, b)) return false;
    out.assign((const char*)b.data(), b.size());
    return true;
}

inline bool writeFileText(const std::string& path, const std::string& text) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t wr = std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    return wr == text.size();
}

inline bool fileExists(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f) { std::fclose(f); return true; }
    return false;
}

// Per-user writable directory for saves/settings/photos.
inline std::string userDataDir(const char* appName) {
#ifdef _WIN32
    const char* base = std::getenv("APPDATA");
    std::string dir = std::string(base ? base : ".") + "\\" + appName;
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    std::string dir = xdg ? std::string(xdg) + "/" + appName
                          : std::string(home ? home : ".") + "/.local/share/" + appName;
#endif
    return dir;
}

} // namespace iris
