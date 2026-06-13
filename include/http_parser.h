#pragma once
#include <string>
#include <unordered_map>

/*
 * HttpRequest
 * -----------
 * Parsed representation of a client's HTTP proxy request.
 *
 * Two request types we handle:
 *   1. HTTP   → GET http://example.com/path HTTP/1.1
 *   2. HTTPS  → CONNECT example.com:443 HTTP/1.1  (tunnel mode)
 */
struct HttpRequest {
    std::string method;
    std::string url;          // Full URL for HTTP; host:port for CONNECT
    std::string version;      // "HTTP/1.1"
    std::string host;         // Target hostname (parsed from URL or Host header)
    int         port{80};     // Target port (default 80; 443 for HTTPS)
    std::unordered_map<std::string, std::string> headers;
    std::string raw;          // Original raw bytes — used to forward as-is first pass
    bool        is_connect{false};
    bool        valid{false};
};

/*
 * HttpParser
 * ----------
 * Stateless parser. All methods are static.
 *
 * rewrite_for_origin() is the key method: proxy requests use absolute URLs
 * (GET http://example.com/path HTTP/1.1) but origin servers expect relative
 * paths (GET /path HTTP/1.1). This rewrites the request accordingly.
 */
class HttpParser {
public:
    // Parse a raw HTTP request string into an HttpRequest struct
    static HttpRequest parse(const std::string& raw);

    // Rewrite absolute URL to relative path for direct-to-origin forwarding
    // Also sets Connection: close so we know when the response ends
    static std::string rewrite_for_origin(const HttpRequest& req);

private:
    static std::string extract_path(const std::string& url);
};
