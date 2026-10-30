#include "game/GameBuilder.h"

#include "core/Log.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <vector>

namespace magnum {

namespace {

// Build tools print in the console's OEM code page; the log wants UTF-8.
std::string OemToUtf8(const std::string& text) {
    if (text.empty()) return text;
    const int wideLength = MultiByteToWideChar(static_cast<UINT>(GetOEMCP()), 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (wideLength <= 0) return text;
    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(static_cast<UINT>(GetOEMCP()), 0, text.data(), static_cast<int>(text.size()), wide.data(), wideLength);
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
    if (length <= 0) return text;
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, utf8.data(), length, nullptr, nullptr);
    return utf8;
}

bool Contains(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

} // namespace

GameBuilder::~GameBuilder() {
    if (m_thread.joinable()) m_thread.join();
}

void GameBuilder::Init(const Config& config) {
    m_config = config;
    m_seenStamp = SourceStamp();
    m_started = true;

    // A DLL older than the sources (or missing) needs a build right away.
    std::error_code ec;
    const bool dllExists = std::filesystem::is_regular_file(m_config.dll, ec);
    const auto dllTime = dllExists ? std::filesystem::last_write_time(m_config.dll, ec) : std::filesystem::file_time_type{};
    if (!dllExists || dllTime < m_seenStamp) {
        m_forceBuild = true;
        m_changedAt = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    } else {
        m_builtStamp = m_seenStamp;
    }
}

std::filesystem::file_time_type GameBuilder::SourceStamp() const {
    std::filesystem::file_time_type newest{};
    std::error_code ec;
    for (const std::filesystem::path& item : m_config.watch) {
        if (std::filesystem::is_directory(item, ec)) {
            for (const auto& file : std::filesystem::recursive_directory_iterator(item, ec)) {
                if (!file.is_regular_file(ec)) continue;
                newest = std::max(newest, std::filesystem::last_write_time(file.path(), ec));
            }
        } else if (std::filesystem::is_regular_file(item, ec)) {
            newest = std::max(newest, std::filesystem::last_write_time(item, ec));
        }
    }
    return newest;
}

bool GameBuilder::RequestBuild() {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(m_config.script, ec)) {
        Log::Error("Game build script not found: {}", m_config.script.string());
        return false;
    }
    m_forceBuild = true;
    m_changedAt = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    return true;
}

void GameBuilder::Poll() {
    if (!m_started) return;
    using namespace std::chrono;
    const auto now = steady_clock::now();

    if (now - m_lastScan >= milliseconds(400)) {
        m_lastScan = now;
        const auto stamp = SourceStamp();
        if (stamp != m_seenStamp) {
            m_seenStamp = stamp;
            m_changedAt = now; // wait for the editor to finish writing (saves often touch a file twice)
        }
    }

    const bool changed = m_seenStamp != m_builtStamp;
    if (!m_building && (changed || m_forceBuild) && now - m_changedAt >= milliseconds(350)) {
        m_forceBuild = false;
        StartBuild(m_seenStamp);
    }
}

void GameBuilder::StartBuild(std::filesystem::file_time_type target) {
    if (m_thread.joinable()) m_thread.join();
    m_building = true;
    m_builtStamp = target; // changes made while building will differ from this and trigger another build
    m_thread = std::thread([this, target] { Run(target); });
}

void GameBuilder::Run(std::filesystem::file_time_type) {
    const auto start = std::chrono::steady_clock::now();
    Log::Info("Game: building...");

    std::error_code ec;
    if (m_config.logFile.has_parent_path()) std::filesystem::create_directories(m_config.logFile.parent_path(), ec);
    std::ofstream logFile(m_config.logFile, std::ios::binary | std::ios::trunc);

    // cmd /c "<script>" 2>&1 -- the outer quotes are removed by cmd, the inner ones keep spaces in the path.
    const std::string command = "cmd /c \"\"" + m_config.script.string() + "\" \"" + m_config.projectRoot.string() + "\" 2>&1\"";
    FILE* pipe = _popen(command.c_str(), "r");
    int errors = 0, warnings = 0;
    int exitCode = -1;
    if (!pipe) {
        Log::Error("Game: could not start the build script");
    } else {
        std::string line;
        char buffer[4096];
        while (std::fgets(buffer, sizeof(buffer), pipe)) {
            line = buffer;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
            if (line.empty()) continue;
            line = OemToUtf8(line);
            logFile << line << '\n';

            // MSVC: "file(line): error C2065: ..."; linker: "LNK2019: ..."; ninja: "FAILED: ..." (skipped, redundant).
            const bool isError = Contains(line, ": error ") || Contains(line, "fatal error") || Contains(line, "error LNK") ||
                                 Contains(line, "CMake Error");
            const bool isWarning = !isError && (Contains(line, ": warning ") || Contains(line, "CMake Warning"));
            if (isError) {
                ++errors;
                Log::Error("{}", line);
            } else if (isWarning) {
                ++warnings;
                Log::Warn("{}", line);
            }
        }
        exitCode = _pclose(pipe);
    }

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    m_lastErrors = errors;
    if (exitCode == 0) {
        Log::Info("Game: build succeeded in {:.1f} s ({} warning(s))", seconds, warnings);
        m_lastFailed = false;
        m_success = true;
    } else {
        Log::Error("Game: build FAILED after {:.1f} s ({} error(s)); the previous version keeps running. Full output: {}", seconds,
                   errors, m_config.logFile.string());
        m_lastFailed = true;
    }
    m_building = false;
}

bool GameBuilder::TakeSuccessfulBuild() {
    return m_success.exchange(false);
}

std::string GameBuilder::Status() const {
    if (m_building) return "Game: building...";
    if (m_lastFailed) return "Game: build FAILED (" + std::to_string(m_lastErrors.load()) + " error(s), see Console)";
    return "Game: ready";
}

} // namespace magnum
