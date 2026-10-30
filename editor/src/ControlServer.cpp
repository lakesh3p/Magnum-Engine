#include "ControlServer.h"

#include "core/Log.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <fstream>
#include <random>

namespace magnum::editor {

namespace {

constexpr size_t kMaxLine = 64u * 1024u * 1024u; // a request bigger than this is a mistake

std::string RandomToken() {
    std::random_device rd;
    std::string token;
    for (int i = 0; i < 4; ++i) token += std::format("{:08x}", rd());
    return token;
}

SOCKET AsSocket(std::uintptr_t s) { return static_cast<SOCKET>(s); }

} // namespace

ControlServer::~ControlServer() {
    Stop();
}

bool ControlServer::Start(int port, const std::filesystem::path& infoFile, const std::string& project, std::string& error) {
    if (m_running) return true;
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        error = "WSAStartup failed";
        return false;
    }
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        error = "cannot create a socket";
        WSACleanup();
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr); // loopback only: nothing outside this computer can connect
    if (::bind(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(s, 1) != 0) {
        error = "cannot listen on port " + std::to_string(port);
        closesocket(s);
        WSACleanup();
        return false;
    }
    int length = sizeof(address);
    ::getsockname(s, reinterpret_cast<sockaddr*>(&address), &length);
    m_port = ntohs(address.sin_port);
    m_listen = static_cast<std::uintptr_t>(s);
    m_token = RandomToken();
    m_infoFile = infoFile;

    nlohmann::json info;
    info["port"] = m_port;
    info["token"] = m_token;
    info["pid"] = static_cast<int>(GetCurrentProcessId());
    info["project"] = project;
    std::error_code ec;
    std::filesystem::create_directories(infoFile.parent_path(), ec);
    std::ofstream(infoFile, std::ios::binary | std::ios::trunc) << info.dump(2);

    m_running = true;
    m_thread = std::thread([this] { Run(); });
    Log::Info("Control channel listening on 127.0.0.1:{}", m_port);
    return true;
}

void ControlServer::Stop() {
    if (!m_running.exchange(false)) return;
    {
        std::lock_guard lock(m_mutex);
        if (m_listen != ~std::uintptr_t{0}) closesocket(AsSocket(m_listen));
        m_listen = ~std::uintptr_t{0};
        if (m_client != ~std::uintptr_t{0}) closesocket(AsSocket(m_client));
        m_client = ~std::uintptr_t{0};
    }
    if (m_thread.joinable()) m_thread.join();
    std::error_code ec;
    std::filesystem::remove(m_infoFile, ec);
    WSACleanup();
}

bool ControlServer::Poll(nlohmann::json& request) {
    std::lock_guard lock(m_mutex);
    if (m_queue.empty()) return false;
    request = std::move(m_queue.front());
    m_queue.pop_front();
    return true;
}

void ControlServer::Send(const std::string& line) {
    std::lock_guard lock(m_mutex);
    if (m_client == ~std::uintptr_t{0}) return;
    const char* data = line.data();
    size_t left = line.size();
    while (left > 0) {
        const int sent = ::send(AsSocket(m_client), data, static_cast<int>(std::min<size_t>(left, 1u << 20)), 0);
        if (sent <= 0) return; // the client is gone; Run() notices on its next read
        data += sent;
        left -= static_cast<size_t>(sent);
    }
}

void ControlServer::Reply(const nlohmann::json& response) {
    // ensure_ascii off would let invalid UTF-8 from a log line throw; replace it instead.
    Send(response.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n");
}

void ControlServer::Run() {
    while (m_running) {
        SOCKET listener;
        {
            std::lock_guard lock(m_mutex);
            listener = AsSocket(m_listen);
        }
        const SOCKET client = ::accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (!m_running) break;
            continue;
        }
        BOOL noDelay = TRUE;
        ::setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
        {
            std::lock_guard lock(m_mutex);
            m_client = static_cast<std::uintptr_t>(client);
            m_queue.clear();
        }
        m_clientConnected = true;
        Log::Info("Control client connected");

        std::string buffer;
        char chunk[16384];
        for (;;) {
            const int got = ::recv(client, chunk, sizeof(chunk), 0);
            if (got <= 0) break;
            buffer.append(chunk, static_cast<size_t>(got));
            size_t newline;
            while ((newline = buffer.find('\n')) != std::string::npos) {
                const std::string line = buffer.substr(0, newline);
                buffer.erase(0, newline + 1);
                if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
                nlohmann::json request = nlohmann::json::parse(line, nullptr, false);
                nlohmann::json id = request.is_object() && request.contains("id") ? request["id"] : nlohmann::json(nullptr);
                if (!request.is_object()) {
                    Reply({{"id", id}, {"ok", false}, {"error", "the request is not a JSON object"}});
                } else if (request.value("token", std::string()) != m_token) {
                    Reply({{"id", id}, {"ok", false}, {"error", "wrong or missing token"}});
                } else {
                    std::lock_guard lock(m_mutex);
                    m_queue.push_back(std::move(request));
                }
            }
            if (buffer.size() > kMaxLine) break;
        }
        m_clientConnected = false;
        {
            std::lock_guard lock(m_mutex);
            if (m_client == static_cast<std::uintptr_t>(client)) m_client = ~std::uintptr_t{0};
            m_queue.clear();
        }
        closesocket(client);
        Log::Info("Control client disconnected");
    }
}

} // namespace magnum::editor
