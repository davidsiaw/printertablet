#include "moonrakerclient.hpp"

#include <cstdio>
#include <cstring>
#include <sstream>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace
{
    const int TIMEOUT_MS = 3000;     // per connect / per read
    const int POLL_INTERVAL_MS = 2000;

#ifdef _WIN32
    typedef SOCKET Socket;
    const Socket BAD_SOCKET = INVALID_SOCKET;
    void CloseSocket(Socket s) { closesocket(s); }
    int LastError() { return WSAGetLastError(); }
    bool InProgress(int err) { return err == WSAEWOULDBLOCK; }
    std::string ErrorString(int err)
    {
        std::stringstream ss;
        ss << "socket error " << err;
        return ss.str();
    }
    void SetNonBlocking(Socket s, bool on)
    {
        u_long mode = on ? 1 : 0;
        ioctlsocket(s, FIONBIO, &mode);
    }
#else
    typedef int Socket;
    const Socket BAD_SOCKET = -1;
    void CloseSocket(Socket s) { close(s); }
    int LastError() { return errno; }
    bool InProgress(int err) { return err == EINPROGRESS; }
    std::string ErrorString(int err) { return strerror(err); }
    void SetNonBlocking(Socket s, bool on)
    {
        int flags = fcntl(s, F_GETFL, 0);
        fcntl(s, F_SETFL, on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
    }
#endif

#ifdef MSG_NOSIGNAL
    const int SEND_FLAGS = MSG_NOSIGNAL;   // a closed peer must not SIGPIPE-kill the app
#else
    const int SEND_FLAGS = 0;
#endif

    // Waits until the socket is readable (or writable). False on timeout.
    bool WaitFor(Socket s, bool forWrite, int ms)
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(s, &set);
        timeval tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        int rc = select((int)s + 1, forWrite ? nullptr : &set, forWrite ? &set : nullptr, nullptr, &tv);
        return rc > 0;
    }

    bool ConnectWithTimeout(Socket s, const sockaddr* addr, socklen_t len, std::string& error)
    {
        SetNonBlocking(s, true);
        if (connect(s, addr, len) != 0 && !InProgress(LastError()))
        {
            error = ErrorString(LastError());
            return false;
        }
        if (!WaitFor(s, true, TIMEOUT_MS))
        {
            error = "connection timed out";
            return false;
        }
        int soError = 0;
        socklen_t soLen = sizeof(soError);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soError, &soLen);
        if (soError != 0)
        {
            error = ErrorString(soError);
            return false;
        }
        SetNonBlocking(s, false);
        return true;
    }

    // Minimal HTTP/1.0 GET: the server closes the connection when done, and the
    // body is never chunked. Returns false (and sets error) on network failure.
    bool HttpGet(const std::string& host, int port, const std::string& path,
                 int& statusCode, std::string& body, std::string& error)
    {
        std::stringstream portStr;
        portStr << port;

        addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* addrs = nullptr;
        if (getaddrinfo(host.c_str(), portStr.str().c_str(), &hints, &addrs) != 0)
        {
            error = "cannot resolve " + host;
            return false;
        }

        Socket s = BAD_SOCKET;
        for (addrinfo* a = addrs; a; a = a->ai_next)
        {
            s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (s == BAD_SOCKET)
            {
                continue;
            }
            if (ConnectWithTimeout(s, a->ai_addr, (socklen_t)a->ai_addrlen, error))
            {
                break;
            }
            CloseSocket(s);
            s = BAD_SOCKET;
        }
        freeaddrinfo(addrs);
        if (s == BAD_SOCKET)
        {
            return false;
        }

        std::string request = "GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\nAccept: application/json\r\n\r\n";
        for (size_t sent = 0; sent < request.size(); )
        {
            int n = send(s, request.data() + sent, (int)(request.size() - sent), SEND_FLAGS);
            if (n <= 0)
            {
                error = ErrorString(LastError());
                CloseSocket(s);
                return false;
            }
            sent += n;
        }

        std::string response;
        char buf[4096];
        for (;;)
        {
            if (!WaitFor(s, false, TIMEOUT_MS))
            {
                error = "no response (timed out)";
                CloseSocket(s);
                return false;
            }
            int n = recv(s, buf, sizeof(buf), 0);
            if (n < 0)
            {
                error = ErrorString(LastError());
                CloseSocket(s);
                return false;
            }
            if (n == 0)
            {
                break;
            }
            response.append(buf, n);
        }
        CloseSocket(s);

        size_t headerEnd = response.find("\r\n\r\n");
        if (headerEnd == std::string::npos || sscanf(response.c_str(), "HTTP/%*s %d", &statusCode) != 1)
        {
            error = "not an HTTP response";
            return false;
        }
        body = response.substr(headerEnd + 4);
        return true;
    }
}

MoonrakerClient::MoonrakerClient(const std::string& host, int port)
: host(host), port(port), mutex(SDL_CreateMutex()), thread(nullptr)
{
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    SDL_AtomicSet(&stop, 0);
    thread = SDL_CreateThread(ThreadMain, "moonraker", this);
}

MoonrakerClient::~MoonrakerClient()
{
    SDL_AtomicSet(&stop, 1);
    SDL_WaitThread(thread, nullptr);   // at most one request timeout
    SDL_DestroyMutex(mutex);
#ifdef _WIN32
    WSACleanup();
#endif
}

MoonrakerClient::Status MoonrakerClient::GetStatus() const
{
    SDL_LockMutex(mutex);
    Status copy = status;
    SDL_UnlockMutex(mutex);
    return copy;
}

int MoonrakerClient::ThreadMain(void* self)
{
    MoonrakerClient* client = static_cast<MoonrakerClient*>(self);
    while (!SDL_AtomicGet(&client->stop))
    {
        client->Poll();
        for (int waited = 0; waited < POLL_INTERVAL_MS && !SDL_AtomicGet(&client->stop); waited += 100)
        {
            SDL_Delay(100);
        }
    }
    return 0;
}

void MoonrakerClient::Poll()
{
    Status next;
    int statusCode = 0;
    std::string body, error;

    if (!HttpGet(host, port, "/server/info", statusCode, body, error))
    {
        next.state = Disconnected;
        next.error = error;
    }
    else if (statusCode == 401 || statusCode == 403)
    {
        next.state = Disconnected;
        next.error = "not authorised - add this device to trusted_clients in moonraker.conf";
    }
    else if (statusCode != 200)
    {
        std::stringstream ss;
        ss << "HTTP " << statusCode;
        next.state = Disconnected;
        next.error = ss.str();
    }
    else
    {
        try
        {
            boost::property_tree::ptree tree;
            std::istringstream in(body);
            boost::property_tree::read_json(in, tree);
            next.state = Connected;
            next.klippyState = tree.get<std::string>("result.klippy_state", "unknown");
            next.moonrakerVersion = tree.get<std::string>("result.moonraker_version", "");
        }
        catch (const std::exception&)
        {
            next.state = Disconnected;
            next.error = "unexpected reply (is this Moonraker?)";
        }
    }

    SDL_LockMutex(mutex);
    status = next;
    SDL_UnlockMutex(mutex);
}
