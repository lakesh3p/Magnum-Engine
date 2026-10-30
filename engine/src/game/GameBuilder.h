#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace magnum {

// Watches the game's source files and rebuilds the game DLL in the background with the project's build
// script (which runs `cmake --build`). Compiler output goes to the log (errors and warnings) and to
// Saved/Logs/game_build.log (everything). Poll() once per frame from the main thread.
class GameBuilder {
public:
    struct Config {
        std::filesystem::path projectRoot;
        std::filesystem::path script;   // batch file that builds the MagnumGame target; it gets the project folder as its argument
        std::filesystem::path dll;      // the file the build produces
        std::filesystem::path logFile;  // full build output
        std::vector<std::filesystem::path> watch; // directories (recursive) and files whose changes trigger a build
    };

    ~GameBuilder();

    void Init(const Config& config);
    void Poll();

    // Starts a build now (or after the running one). Returns false if the script is missing.
    bool RequestBuild();
    bool Building() const { return m_building.load(); }
    // A build is running, queued, or the sources changed since the last one started.
    bool Pending() const { return m_building.load() || m_forceBuild || m_seenStamp != m_builtStamp; }

    // True once for every successful build, so the caller can load the new DLL.
    bool TakeSuccessfulBuild();

    // Short text for the UI: "Game: ready", "Game: building...", "Game: build failed (2 errors)".
    std::string Status() const;
    bool LastBuildFailed() const { return m_lastFailed.load(); }

private:
    std::filesystem::file_time_type SourceStamp() const;
    void StartBuild(std::filesystem::file_time_type target);
    void Run(std::filesystem::file_time_type target);

    Config m_config;
    std::thread m_thread;
    std::atomic<bool> m_building{false};
    std::atomic<bool> m_success{false};   // a build finished OK and was not taken yet
    std::atomic<bool> m_lastFailed{false};
    std::atomic<int> m_lastErrors{0};

    std::filesystem::file_time_type m_builtStamp{};   // newest source time covered by the last build
    std::filesystem::file_time_type m_seenStamp{};
    std::chrono::steady_clock::time_point m_changedAt{};
    std::chrono::steady_clock::time_point m_lastScan{};
    bool m_forceBuild = false;
    bool m_started = false;
};

} // namespace magnum
