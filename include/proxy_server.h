#pragma once
#include <string>
#include <memory>
#include <atomic>
#include "thread_pool.h"
#include "lru_cache.h"

/*
 * ProxyConfig
 * -----------
 * Loaded from config.txt at startup. All fields have sane defaults.
 */
struct ProxyConfig {
    int port{8080};
    int num_threads{8};
    int cache_capacity{200};   // Max cached GET responses
    int timeout_sec{10};       // Socket recv/send timeout
};

/*
 * ProxyServer
 * -----------
 * Architecture:
 *
 *   Client ──► [accept loop / main thread]
 *                       │  push(client_fd)
 *                       ▼
 *              [ThreadPool: N workers]
 *                       │  handle_client(fd)
 *                       ▼
 *              [HttpParser]  →  parse request
 *                       │
 *              [LRUCache]    →  GET hit?  → return cached response
 *                       │          miss? → forward_request()
 *                       ▼
 *              [Origin Server]  →  TCP socket connect + relay
 *
 * HTTPS: CONNECT method → 200 Connection Established + blind TCP tunnel
 * HTTP:  Parse → cache check → forward → cache store → log
 */
class ProxyServer {
public:
    explicit ProxyServer(const ProxyConfig& config);
    ~ProxyServer();

    void start();   // Blocking: runs the accept loop
    void stop();    // Signal-handler safe

private:
    // Called by each worker thread for one client connection
    void handle_client(int client_fd);

    // HTTP: forward request to origin, return full response string
    std::string forward_request(const std::string& host, int port,
                                const std::string& request);

    // HTTPS: bidirectional TCP tunnel after CONNECT handshake
    void tunnel_connect(int client_fd, const std::string& host, int port);

    // DNS resolve + TCP connect to origin; returns fd or -1 on failure
    int connect_to_origin(const std::string& host, int port);

    // Structured log line: [HIT/MISS] METHOD URL | Xms | hit_rate%
    void log_request(const std::string& method, const std::string& url,
                     bool cache_hit, long duration_ms);

    ProxyConfig             config;
    int                     server_fd{-1};
    std::atomic<bool>       running{false};
    std::unique_ptr<ThreadPool> thread_pool;
    std::unique_ptr<LRUCache>   cache;
};
