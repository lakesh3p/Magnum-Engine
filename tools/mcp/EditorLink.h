#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace magnum::mcp {

using Json = nlohmann::json;

// The connection from the MCP server to a running Magnum editor (the control channel, see editor/src/ControlServer.h).
// It attaches to an editor that is already open on the project (through Saved/control.json) or starts one.
class EditorLink {
public:
    EditorLink(std::filesystem::path exeDir, std::filesystem::path engineRoot, std::filesystem::path project);
    ~EditorLink();

    const std::filesystem::path& EngineRoot() const { return m_engineRoot; }
    const std::filesystem::path& ExeDir() const { return m_exeDir; }
    // The project the tools work on: the one the editor has open, else the one asked for at start-up.
    const std::filesystem::path& Project() const { return m_project; }
    void SetProject(const std::filesystem::path& project) { m_project = project; }
    bool Connected() const { return m_socket != ~std::uintptr_t{0}; }
    // Connects to an editor that is already running on Project(), without starting one.
    bool AttachIfRunning() { return Connected() || TryAttach(m_project); }

    // Connects to an editor on Project(), starting one when none is running. `error` says what went wrong.
    bool Ensure(std::string& error);
    // Starts an editor on `project` (an editor that is open on another project is asked to quit first, unless it has unsaved
    // changes and `discardChanges` is false).
    bool Launch(const std::filesystem::path& project, bool discardChanges, std::string& error);
    // Sends one request and waits for its answer. Starts or attaches to an editor first when needed.
    bool Request(const std::string& op, Json args, Json& response, std::string& error, int timeoutSeconds = 60);
    // Closes the connection (the editor keeps running).
    void Disconnect();
    // Asks the editor to close and forgets it.
    bool Quit(bool force, std::string& error);

private:
    bool TryAttach(const std::filesystem::path& project);
    bool Send(const Json& request, Json& response, std::string& error, int timeoutSeconds);

    std::filesystem::path m_exeDir, m_engineRoot, m_project;
    std::uintptr_t m_socket = ~std::uintptr_t{0};
    std::string m_token;
    std::string m_buffer;
    int64_t m_nextId = 1;
};

} // namespace magnum::mcp
