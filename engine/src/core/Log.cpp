#include "core/Log.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace magnum {

namespace {

constexpr size_t kMaxEntries = 5000;

std::mutex g_mutex;
std::deque<LogEntry> g_entries;
std::ofstream g_file;
uint64_t g_revision = 0;
uint64_t g_total = 0;

const char* LevelTag(LogLevel level) {
    switch (level) {
    case LogLevel::Info: return "INFO ";
    case LogLevel::Warn: return "WARN ";
    case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

} // namespace

void Log::Init(const std::string& filePath) {
    std::lock_guard lock(g_mutex);
    std::filesystem::path path(filePath);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    g_file.open(path, std::ios::out | std::ios::trunc);
}

void Log::Shutdown() {
    std::lock_guard lock(g_mutex);
    if (g_file.is_open()) {
        g_file.close();
    }
}

void Log::Write(LogLevel level, std::string text) {
    using namespace std::chrono;
    const auto now = zoned_time{current_zone(), floor<milliseconds>(system_clock::now())};
    const std::string line = std::format("[{:%H:%M:%S}] [{}] {}", now, LevelTag(level), text);

    std::lock_guard lock(g_mutex);
    std::fprintf(level == LogLevel::Info ? stdout : stderr, "%s\n", line.c_str());
    if (g_file.is_open()) {
        g_file << line << '\n';
        g_file.flush(); // so the file is readable even if we crash right after
    }
    g_entries.push_back({level, std::move(text)});
    if (g_entries.size() > kMaxEntries) {
        g_entries.pop_front();
    }
    ++g_revision;
    ++g_total;
}

uint64_t Log::Revision() {
    std::lock_guard lock(g_mutex);
    return g_revision;
}

std::vector<LogEntry> Log::Snapshot() {
    std::lock_guard lock(g_mutex);
    return {g_entries.begin(), g_entries.end()};
}

uint64_t Log::Total() {
    std::lock_guard lock(g_mutex);
    return g_total;
}

std::vector<LogEntry> Log::Since(uint64_t total) {
    std::lock_guard lock(g_mutex);
    if (total >= g_total) return {};
    const size_t wanted = static_cast<size_t>(std::min<uint64_t>(g_total - total, g_entries.size()));
    return {g_entries.end() - static_cast<std::ptrdiff_t>(wanted), g_entries.end()};
}

void Log::Clear() {
    std::lock_guard lock(g_mutex);
    g_entries.clear();
    ++g_revision;
}

} // namespace magnum
