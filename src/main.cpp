#include <iostream>
#include <fstream>
#include <string>
#include <csignal>
#include <stdexcept>
#include "proxy_server.h"

// Global pointer for signal handler (signal handlers can't capture lambdas)
static ProxyServer* g_proxy = nullptr;

void on_signal(int) {
    std::cout << "\n[Proxy] Shutting down gracefully...\n";
    if (g_proxy) g_proxy->stop();
}

ProxyConfig load_config(const std::string& path) {
    ProxyConfig cfg;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cout << "[Config] " << path << " not found — using defaults\n";
        return cfg;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        if      (k == "PORT")        cfg.port           = std::stoi(v);
        else if (k == "THREADS")     cfg.num_threads    = std::stoi(v);
        else if (k == "CACHE_SIZE")  cfg.cache_capacity = std::stoi(v);
        else if (k == "TIMEOUT")     cfg.timeout_sec    = std::stoi(v);
    }
    std::cout << "[Config] Loaded from " << path
              << " | port=" << cfg.port
              << " threads=" << cfg.num_threads
              << " cache=" << cfg.cache_capacity << "\n";
    return cfg;
}

int main(int argc, char* argv[]) {
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe (client disconnected mid-send)

    std::string config_path = (argc > 1) ? argv[1] : "config.txt";
    ProxyConfig config = load_config(config_path);

    try {
        ProxyServer proxy(config);
        g_proxy = &proxy;
        proxy.start();          // Blocks until stop() is called
    } catch (const std::exception& e) {
        std::cerr << "[Fatal] " << e.what() << "\n";
        return 1;
    }
    return 0;
}
