#include "http_parser.h"
#include <sstream>
#include <algorithm>

HttpRequest HttpParser::parse(const std::string& raw) {
    HttpRequest req;
    req.raw = raw;

    std::istringstream stream(raw);
    std::string line;

    // ── Request Line ──────────────────────────────────────────────────────────
    if (!std::getline(stream, line)) return req;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::istringstream req_line(line);
    req_line >> req.method >> req.url >> req.version;
    if (req.method.empty() || req.url.empty()) return req;

    req.is_connect = (req.method == "CONNECT");

    // ── Headers ───────────────────────────────────────────────────────────────
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;  // Blank line = end of headers

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string k = line.substr(0, colon);
        std::string v = line.substr(colon + 1);
        // Trim leading whitespace from value (RFC 7230 §3.2)
        size_t start = v.find_first_not_of(" \t");
        if (start != std::string::npos) v = v.substr(start);

        req.headers[k] = v;
    }

    // ── Resolve host + port ───────────────────────────────────────────────────
    if (req.is_connect) {
        // CONNECT host:port HTTP/1.1 — URL is "host:port"
        auto colon = req.url.rfind(':');
        if (colon != std::string::npos) {
            req.host = req.url.substr(0, colon);
            try { req.port = std::stoi(req.url.substr(colon + 1)); }
            catch (...) { req.port = 443; }
        }
    } else {
        // HTTP proxy request — extract host from Host header or URL
        auto host_it = req.headers.find("Host");
        if (host_it != req.headers.end()) {
            req.host = host_it->second;
            // Strip port from Host if present (e.g. "example.com:8080")
            auto colon = req.host.rfind(':');
            if (colon != std::string::npos) {
                try {
                    req.port = std::stoi(req.host.substr(colon + 1));
                } catch (...) {}
                req.host = req.host.substr(0, colon);
            }
        } else if (req.url.size() > 7 && req.url.substr(0, 7) == "http://") {
            // Fallback: parse host from URL
            std::string rest = req.url.substr(7);
            auto slash = rest.find('/');
            std::string authority = (slash != std::string::npos)
                                    ? rest.substr(0, slash) : rest;
            auto colon = authority.rfind(':');
            if (colon != std::string::npos) {
                req.host = authority.substr(0, colon);
                try { req.port = std::stoi(authority.substr(colon + 1)); }
                catch (...) {}
            } else {
                req.host = authority;
            }
        }
    }

    req.valid = !req.host.empty();
    return req;
}

std::string HttpParser::rewrite_for_origin(const HttpRequest& req) {
    /*
     * Proxy request:  GET http://example.com/path?q=1 HTTP/1.1
     * Origin expects: GET /path?q=1 HTTP/1.1
     *
     * We also force Connection: close so we know when the response ends
     * (avoids needing to parse Content-Length or chunked encoding fully).
     */
    std::string path = extract_path(req.url);

    std::ostringstream out;
    out << req.method << " " << path << " " << req.version << "\r\n";

    for (const auto& [k, v] : req.headers) {
        if (k == "Connection" || k == "Proxy-Connection") continue;
        out << k << ": " << v << "\r\n";
    }
    out << "Connection: close\r\n\r\n";
    return out.str();
}

std::string HttpParser::extract_path(const std::string& url) {
    // "http://example.com/path?q=1" → "/path?q=1"
    // "http://example.com"          → "/"
    if (url.size() > 7 && url.substr(0, 7) == "http://") {
        auto rest  = url.substr(7);
        auto slash = rest.find('/');
        return (slash != std::string::npos) ? rest.substr(slash) : "/";
    }
    return url.empty() ? "/" : url;  // Already a relative path
}
