#include "proxy_server.h"
#include "http_parser.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <chrono>

// ── Helpers ────────────────────────────────────────────────────────────────

static void set_socket_timeout(int fd, int seconds) {
    struct timeval tv { seconds, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static void send_error(int fd, int code, const char* msg) {
    std::ostringstream resp;
    resp << "HTTP/1.1 " << code << " " << msg << "\r\n"
         << "Content-Length: 0\r\nConnection: close\r\n\r\n";
    std::string s = resp.str();
    send(fd, s.c_str(), s.size(), MSG_NOSIGNAL);
}

// ── ProxyServer ────────────────────────────────────────────────────────────

ProxyServer::ProxyServer(const ProxyConfig& cfg) : config(cfg) {
    thread_pool = std::make_unique<ThreadPool>(config.num_threads);
    cache       = std::make_unique<LRUCache>(config.cache_capacity);
}

ProxyServer::~ProxyServer() { stop(); }

void ProxyServer::start() {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) throw std::runtime_error("socket() failed");

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(config.port);

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0)
        throw std::runtime_error("bind() failed — port in use?");
    if (listen(server_fd, SOMAXCONN) < 0)
        throw std::runtime_error("listen() failed");

    running = true;
    std::cout << "\n[Proxy] ✓ Listening on :" << config.port
              << "  threads=" << config.num_threads
              << "  cache=" << config.cache_capacity << " entries\n"
              << "[Proxy] Test: curl -x http://localhost:" << config.port
              << " http://httpbin.org/get\n\n";

    while (running) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (running) std::cerr << "[Proxy] accept() error: " << strerror(errno) << "\n";
            continue;
        }
        // Capture client_fd by value — the lambda owns it
        thread_pool->enqueue([this, client_fd] {
            handle_client(client_fd);
        });
    }
}

void ProxyServer::stop() {
    running = false;
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
}

// ── Per-connection handler (runs in a worker thread) ──────────────────────

void ProxyServer::handle_client(int client_fd) {
    set_socket_timeout(client_fd, config.timeout_sec);

    // Read the HTTP request (single recv — fine for headers up to 64KB)
    char buf[65536];
    int  bytes = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (bytes <= 0) { close(client_fd); return; }
    buf[bytes] = '\0';

    std::string raw(buf, bytes);
    HttpRequest req = HttpParser::parse(raw);

    if (!req.valid) {
        send_error(client_fd, 400, "Bad Request");
        close(client_fd);
        return;
    }

    auto t0 = std::chrono::steady_clock::now();

    // ── HTTPS: CONNECT tunnel ────────────────────────────────────────────────
    if (req.is_connect) {
        tunnel_connect(client_fd, req.host, req.port);
        // tunnel_connect closes client_fd internally
        return;
    }

    // ── HTTP: cache-aware proxy ──────────────────────────────────────────────
    std::string cache_key = req.method + "|" + req.url;
    bool cache_hit = false;
    std::string response;

    if (req.method == "GET") {
        auto cached = cache->get(cache_key);
        if (cached) {
            response  = *cached;
            cache_hit = true;
        }
    }

    if (!cache_hit) {
        std::string fwd_request = HttpParser::rewrite_for_origin(req);
        response = forward_request(req.host, req.port, fwd_request);
        if (req.method == "GET" && !response.empty()) {
            cache->put(cache_key, response);
        }
    }

    if (response.empty()) {
        send_error(client_fd, 502, "Bad Gateway");
    } else {
        send(client_fd, response.c_str(), response.size(), MSG_NOSIGNAL);
    }

    auto t1 = std::chrono::steady_clock::now();
    long ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    log_request(req.method, req.url, cache_hit, ms);

    close(client_fd);
}

// ── Forward HTTP request to origin and read response ──────────────────────

std::string ProxyServer::forward_request(const std::string& host, int port,
                                          const std::string& request) {
    int origin_fd = connect_to_origin(host, port);
    if (origin_fd < 0) return "";

    set_socket_timeout(origin_fd, config.timeout_sec);

    if (send(origin_fd, request.c_str(), request.size(), MSG_NOSIGNAL) < 0) {
        close(origin_fd);
        return "";
    }

    // Read until origin closes connection (we set Connection: close)
    std::string response;
    response.reserve(8192);
    char rbuf[8192];
    int  n;
    while ((n = recv(origin_fd, rbuf, sizeof(rbuf), 0)) > 0) {
        response.append(rbuf, n);
    }

    close(origin_fd);
    return response;
}

// ── HTTPS CONNECT tunnel ──────────────────────────────────────────────────

void ProxyServer::tunnel_connect(int client_fd, const std::string& host, int port) {
    int origin_fd = connect_to_origin(host, port);
    if (origin_fd < 0) {
        send_error(client_fd, 502, "Bad Gateway");
        close(client_fd);
        return;
    }

    // Tell client the tunnel is established
    const char* ok = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(client_fd, ok, strlen(ok), MSG_NOSIGNAL);

    // Bidirectional relay using select() — forwards TLS bytes blindly
    // (we can't decrypt without certificate injection)
    char relay_buf[8192];
    while (true) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(client_fd, &rfds);
        FD_SET(origin_fd, &rfds);
        int maxfd = std::max(client_fd, origin_fd) + 1;

        struct timeval tv { config.timeout_sec, 0 };
        int ready = select(maxfd, &rfds, nullptr, nullptr, &tv);
        if (ready <= 0) break;

        if (FD_ISSET(client_fd, &rfds)) {
            int n = recv(client_fd, relay_buf, sizeof(relay_buf), 0);
            if (n <= 0) break;
            if (send(origin_fd, relay_buf, n, MSG_NOSIGNAL) < 0) break;
        }
        if (FD_ISSET(origin_fd, &rfds)) {
            int n = recv(origin_fd, relay_buf, sizeof(relay_buf), 0);
            if (n <= 0) break;
            if (send(client_fd, relay_buf, n, MSG_NOSIGNAL) < 0) break;
        }
    }

    close(origin_fd);
    close(client_fd);
}

// ── DNS + TCP connect to origin ───────────────────────────────────────────

int ProxyServer::connect_to_origin(const std::string& host, int port) {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;    // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0)
        return -1;

    int fd = -1;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        set_socket_timeout(fd, config.timeout_sec);
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

// ── Logging ───────────────────────────────────────────────────────────────

void ProxyServer::log_request(const std::string& method, const std::string& url,
                               bool cache_hit, long duration_ms) {
    size_t total = cache->hits() + cache->misses();
    double hit_rate = total > 0 ? (100.0 * cache->hits()) / total : 0.0;

    std::cout << (cache_hit ? "[HIT ] " : "[MISS] ")
              << std::left << std::setw(6) << method
              << " " << url
              << "  | " << duration_ms << "ms"
              << "  | hit_rate=" << std::fixed << std::setprecision(1) << hit_rate << "%"
              << "  | cache_size=" << cache->size() << "\n";
}
