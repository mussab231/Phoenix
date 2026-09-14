#include "core/HttpListener.h"

#include "core/UrlCodec.h"

#include <QRandomGenerator>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kListenAddr = "127.0.0.1";
constexpr int kMaxRequest = 4096;

std::string goodResponse(const std::string& body) {
    return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n"
           "Content-Length: " +
           std::to_string(body.size()) + "\r\n\r\n" + body;
}

std::string unauthorizedResponse() {
    const std::string body = "unauthorized";
    return "HTTP/1.1 401 Unauthorized\r\nContent-Type: text/plain\r\n"
           "Connection: close\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\n\r\n" + body;
}

std::string badResponse() {
    const std::string body = "not found";
    return "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n"
           "Connection: close\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\n\r\n" + body;
}

// Extracts a query parameter value from a GET request line.
std::string queryParam(const std::string& raw, const std::string& key) {
    size_t q = raw.find('?');
    if (q == std::string::npos)
        return {};
    size_t end = raw.find(' ', q);
    if (end == std::string::npos)
        end = raw.size();
    std::string query = raw.substr(q + 1, end - q - 1);
    std::string k = key + "=";
    size_t p = query.find(k);
    if (p == std::string::npos)
        return {};
    size_t vs = p + k.size();
    size_t ve = query.find('&', vs);
    if (ve == std::string::npos)
        ve = query.size();
    return UrlCodec::decode(std::string_view(query).substr(vs, ve - vs));
}

} // namespace

HttpListener::HttpListener(QObject* parent) : QObject(parent) {
    // Per-launch capability token: secret to every other process on the
    // machine, revealed only to this instance's owner (UI / tests / NMH).
    QString t;
    t.reserve(32);
    QRandomGenerator* rng = QRandomGenerator::system();
    static const char* kHex = "0123456789abcdef";
    for (int i = 0; i < 16; ++i)
        t += QLatin1Char(kHex[rng->bounded(16)]);
    m_token = t;
}

HttpListener::~HttpListener() {
    stop();
}

quint16 HttpListener::start(quint16 preferredPort) {
    WSADATA wsa{};
    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0)
        return 0;

    for (quint16 tryPort = preferredPort == 0 ? 51040 : preferredPort; tryPort < 51060; ++tryPort) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
            break;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(tryPort);
        if (inet_pton(AF_INET, kListenAddr, &addr.sin_addr) != 1) {
            closesocket(s);
            break;
        }
        if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
            listen(s, 8) == 0) {
            m_listenSock = reinterpret_cast<void*>(s);
            m_port = tryPort;
            m_thread = new std::thread(&HttpListener::run, this);
            return m_port;
        }
        closesocket(s);
    }
    WSACleanup();
    return 0;
}

void HttpListener::stop() {
    if (!m_listenSock)
        return;
    SOCKET s = reinterpret_cast<SOCKET>(m_listenSock);
    m_listenSock = nullptr;
    shutdown(s, SD_BOTH);
    closesocket(s);
    if (m_thread) {
        m_thread->join();
        delete m_thread;
        m_thread = nullptr;
    }
    WSACleanup();
}

void HttpListener::run() {
    SOCKET listenSock = reinterpret_cast<SOCKET>(m_listenSock);

    for (;;) {
        SOCKET c = accept(listenSock, nullptr, nullptr);
        if (c == INVALID_SOCKET)
            break; // listening socket closed (shutdown by stop())

        char buf[kMaxRequest] = {};
        int n = recv(c, buf, sizeof(buf) - 1, 0);
        std::string response;
        if (n > 0) {
            buf[n] = '\0';
            std::string req(buf);
            bool isAdd = req.rfind("GET /add", 0) == 0;
            bool isStatus = req.rfind("GET /status", 0) == 0;
            if (isAdd || isStatus) {
                // Every request must carry the per-launch token.
                if (queryParam(req, "token") != m_token.toStdString()) {
                    response = unauthorizedResponse();
                } else if (isAdd) {
                    std::string url = queryParam(req, "url");
                    std::string name = queryParam(req, "name");
                    if (!url.empty()) {
                        emit urlReceived(QString::fromStdString(url),
                                         QString::fromStdString(name));
                        response = goodResponse("OK");
                    } else {
                        response = badResponse();
                    }
                } else { // /status
                    char body[512];
                    std::snprintf(body, sizeof(body),
                                  "{\"ok\":true,\"app\":\"Phoenix\",\"port\":%u,"
                                  "\"token\":\"%s\"}",
                                  static_cast<unsigned>(m_port),
                                  m_token.toStdString().c_str());
                    response = goodResponse(body);
                }
            } else {
                response = badResponse();
            }
        } else {
            response = badResponse();
        }

        send(c, response.data(), static_cast<int>(response.size()), 0);
        closesocket(c);
    }
}