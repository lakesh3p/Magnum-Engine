#include "EditorLink.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <chrono>
#include <fstream>
#include <thread>

namespace magnum::mcp {

namespace fs = std::filesystem;

namespace {

SOCKET AsSocket(std::uintptr_t s) { return static_cast<SOCKET>(s); }

fs::path InfoFile(const fs::path& project) { return project / "Saved" / "control.json"; }

bool ReadInfo(const fs::path& project, Json& info) {
    std::ifstream in(InfoFile(project), std::ios::binary);
    if (!in) return false;
    info = Json::parse(in, nullptr, false);
    return info.is_object() && info.contains("port") && info.contains("token");
}

void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

} // namespace

EditorLink::EditorLink(fs::path exeDir, fs::path engineRoot, fs::path project)
    : m_exeDir(std::move(exeDir)), m_engineRoot(std::move(engineRoot)), m_project(std::move(project)) {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
}

EditorLink::~EditorLink() {
    Disconnect();
    WSACleanup();
}

void EditorLink::Disconnect() {
    if (m_socket != ~std::uintptr_t{0}) closesocket(AsSocket(m_socket));
    m_socket = ~std::uintptr_t{0};
    m_buffer.clear();
}

bool EditorLink::TryAttach(const fs::path& project) {
    Json info;
    if (!ReadInfo(project, info)) return false;
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(info["port"].get<int>()));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (::connect(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        closesocket(s);
        return false; // a stale file of an editor that is gone
    }
    BOOL noDelay = TRUE;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    Disconnect();
    m_socket = static_cast<std::uintptr_t>(s);
    m_token = info["token"].get<std::string>();
    if (info.contains("project") && info["project"].is_string()) m_project = fs::path(info["project"].get<std::string>());
    return true;
}

bool EditorLink::Send(const Json& request, Json& response, std::string& error, int timeoutSeconds) {
    if (!Connected()) {
        error = "not connected to an editor";
        return false;
    }
    const std::string line = request.dump(-1, ' ', false, Json::error_handler_t::replace) + "\n";
    size_t sent = 0;
    while (sent < line.size()) {
        const int n = ::send(AsSocket(m_socket), line.data() + sent, static_cast<int>(line.size() - sent), 0);
        if (n <= 0) {
            Disconnect();
            error = "the connection to the editor was lost (did it close or crash?)";
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    const DWORD timeout = static_cast<DWORD>(timeoutSeconds) * 1000;
    ::setsockopt(AsSocket(m_socket), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    const int64_t wanted = request.value("id", static_cast<int64_t>(0));
    for (;;) {
        size_t newline;
        while ((newline = m_buffer.find('\n')) != std::string::npos) {
            const std::string text = m_buffer.substr(0, newline);
            m_buffer.erase(0, newline + 1);
            Json parsed = Json::parse(text, nullptr, false);
            if (!parsed.is_object()) continue;
            if (parsed.value("id", static_cast<int64_t>(-1)) != wanted) continue; // an answer to an earlier, abandoned request
            response = std::move(parsed);
            return true;
        }
        char chunk[65536];
        const int got = ::recv(AsSocket(m_socket), chunk, sizeof(chunk), 0);
        if (got <= 0) {
            const bool timedOut = WSAGetLastError() == WSAETIMEDOUT;
            if (timedOut) {
                // The editor may still be working on it; the late answer is skipped by id. Keep the connection.
                error = "the editor did not answer within " + std::to_string(timeoutSeconds) + " seconds";
            } else {
                Disconnect();
                error = "the connection to the editor was lost (did it close or crash?)";
            }
            return false;
        }
        m_buffer.append(chunk, static_cast<size_t>(got));
    }
}

bool EditorLink::Launch(const fs::path& projectIn, bool discardChanges, std::string& error) {
    std::error_code ec;
    fs::path project = fs::weakly_canonical(fs::absolute(projectIn), ec);
    if (ec) project = projectIn;

    // An editor that is open on this very project is simply used.
    if (TryAttach(project)) {
        Json status, ignored;
        std::string err;
        if (Send({{"id", m_nextId++}, {"token", m_token}, {"op", "status"}}, status, err, 20)) return true;
    }
    // An editor on another project must close first (a project is opened by starting an editor on it).
    if (Connected() && fs::weakly_canonical(m_project, ec) != project) {
        std::string quitError;
        if (!Quit(discardChanges, quitError)) {
            error = quitError;
            return false;
        }
    }

    const fs::path exe = m_exeDir / "MagnumEditor.exe";
    if (!fs::exists(exe, ec)) {
        error = "MagnumEditor.exe was not found next to MagnumMcp.exe (" + exe.string() + "); build the engine first";
        return false;
    }
    fs::remove(InfoFile(project), ec); // a leftover of an editor that crashed
    std::wstring command = L"\"" + exe.wstring() + L"\" --project \"" + project.wstring() + L"\" --control-port 0";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // No inherited handles: the editor must not hold on to this process's stdin/stdout, which belong to the MCP client.
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr, m_engineRoot.wstring().c_str(),
                        &startup, &process)) {
        error = "could not start the editor (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    // Wait for the editor to publish its port, then for its first frames.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (std::chrono::steady_clock::now() < deadline) {
        Json info;
        if (ReadInfo(project, info) && info.value("pid", 0) == static_cast<int>(process.dwProcessId) && TryAttach(project)) {
            for (int i = 0; i < 300; ++i) {
                Json status;
                std::string err;
                if (Send({{"id", m_nextId++}, {"token", m_token}, {"op", "status"}}, status, err, 20) && status.value("frame", 0) >= 4) {
                    m_project = project;
                    return true;
                }
                Sleep(100);
            }
            error = "the editor started but did not become ready";
            return false;
        }
        Sleep(200);
    }
    error = "the editor did not open its control channel within 120 seconds (see " + (project / "Saved" / "Logs" / "magnum.log").string() + ")";
    return false;
}

bool EditorLink::Ensure(std::string& error) {
    if (Connected()) return true;
    if (TryAttach(m_project)) return true;
    return Launch(m_project, false, error);
}

bool EditorLink::Request(const std::string& op, Json args, Json& response, std::string& error, int timeoutSeconds) {
    if (!Connected() && !Ensure(error)) return false;
    args["id"] = m_nextId++;
    args["token"] = m_token;
    args["op"] = op;
    if (!Send(args, response, error, timeoutSeconds)) {
        if (!Connected()) { // one retry on a fresh connection: the editor may have been restarted
            std::string attachError;
            if (Ensure(attachError)) {
                args["id"] = m_nextId++;
                args["token"] = m_token;
                error.clear();
                return Send(args, response, error, timeoutSeconds);
            }
        }
        return false;
    }
    if (!response.value("ok", false)) {
        error = response.value("error", std::string("the editor refused the request"));
        return false;
    }
    return true;
}

bool EditorLink::Quit(bool force, std::string& error) {
    Json status;
    if (!Connected()) return true;
    if (Send({{"id", m_nextId++}, {"token", m_token}, {"op", "status"}}, status, error, 20)) {
        if (status.value("dirty", false) && !force) {
            error = "the editor has unsaved changes; save the scene first (scene_save) or ask to discard them";
            return false;
        }
    }
    Json response;
    Send({{"id", m_nextId++}, {"token", m_token}, {"op", "quit"}, {"force", true}}, response, error, 20);
    Disconnect();
    error.clear();
    // Give it a moment to close, so its control file and window are gone before another editor starts.
    for (int i = 0; i < 100 && fs::exists(InfoFile(m_project)); ++i) Sleep(100);
    return true;
}

} // namespace magnum::mcp
