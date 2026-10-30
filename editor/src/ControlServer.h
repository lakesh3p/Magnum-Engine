#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace magnum::editor {

// A local control channel for tools and AI clients (the MagnumMcp server): one TCP client at a time, loopback only, one JSON
// object per line in each direction. Every request must carry the token that was written to the info file, so another
// program (or a web page) that merely finds the port cannot drive the editor. The socket work happens on a thread of
// its own; requests are handed to the main thread through Poll() and answered with Reply().
//
//   request   {"id": 7, "token": "...", "op": "exec", "line": "spawn cube"}
//   response  {"id": 7, "ok": true, ...}   or   {"id": 7, "ok": false, "error": "..."}
class ControlServer {
public:
    ControlServer() = default;
    ~ControlServer();
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    // port 0 picks a free one. Writes {"port", "token", "pid", "project"} to `infoFile` and removes it on Stop().
    bool Start(int port, const std::filesystem::path& infoFile, const std::string& project, std::string& error);
    void Stop();
    bool Running() const { return m_running; }
    int Port() const { return m_port; }

    // Main thread: the next request that passed the token check (false when there is none).
    bool Poll(nlohmann::json& request);
    // Main thread: sends a response to the connected client (dropped when it went away).
    void Reply(const nlohmann::json& response);
    bool HasClient() const { return m_clientConnected; }

private:
    void Run();
    void Send(const std::string& line);

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_clientConnected{false};
    std::thread m_thread;
    std::uintptr_t m_listen = ~std::uintptr_t{0};
    std::uintptr_t m_client = ~std::uintptr_t{0};
    std::mutex m_mutex; // guards the queue and the client socket
    std::deque<nlohmann::json> m_queue;
    std::string m_token;
    std::filesystem::path m_infoFile;
    int m_port = 0;
};

} // namespace magnum::editor
