#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace magnum {

enum class LogLevel { Info, Warn, Error };

struct LogEntry {
    LogLevel level;
    std::string text;
};

// Thread-safe log. Every line goes to the log file, stderr and an in-memory
// ring buffer that the editor console reads.
class Log {
public:
    static void Init(const std::string& filePath);
    static void Shutdown();

    static void Write(LogLevel level, std::string text);

    template <class... Args>
    static void Info(std::format_string<Args...> fmt, Args&&... args) {
        Write(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
    }
    template <class... Args>
    static void Warn(std::format_string<Args...> fmt, Args&&... args) {
        Write(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
    }
    template <class... Args>
    static void Error(std::format_string<Args...> fmt, Args&&... args) {
        Write(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
    }

    // Changes whenever entries are added or cleared; lets the UI skip copying.
    static uint64_t Revision();
    static std::vector<LogEntry> Snapshot();
    // Entries ever written (never goes down, unlike Snapshot().size()), and the entries written after the count was `total`
    // (as many as the ring buffer still holds). Lets a caller collect the output of one command.
    static uint64_t Total();
    static std::vector<LogEntry> Since(uint64_t total);
    static void Clear();
};

} // namespace magnum
