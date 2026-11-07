#ifndef HTTP_HPP
#define HTTP_HPP

#include <string>
#include <SDL.h>

namespace Http
{
    const int DEFAULT_TIMEOUT_MS = 3000;

    // Minimal blocking HTTP/1.0 request with an empty body: the server closes
    // the connection when done, and the body is never chunked. Returns false
    // (and sets error) on network failure, after readTimeoutMs without data, or
    // within 200ms of `cancel` becoming non-zero. Use from a background thread.
    bool Request(const char* method, const std::string& host, int port, const std::string& path,
                 int& statusCode, std::string& body, std::string& error,
                 SDL_atomic_t* cancel, int readTimeoutMs = DEFAULT_TIMEOUT_MS);
}

#endif // HTTP_HPP
