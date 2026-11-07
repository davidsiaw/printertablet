#include "moonrakerclient.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "printlog.hpp"

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
    // Moonraker answers /printer/gcode/script only when the G-code has finished,
    // and homing can take a while.
    const int COMMAND_TIMEOUT_MS = 10 * 60 * 1000;
    const int INFO_INTERVAL_MS = 2000;       // /server/info
    const int TOOLHEAD_INTERVAL_MS = 250;    // toolhead position, while Klipper is ready

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

    // Minimal HTTP/1.0 request with an empty body: the server closes the
    // connection when done, and the body is never chunked. Returns false (and
    // sets error) on network failure, after readTimeoutMs without data, or as
    // soon as `cancel` becomes non-zero.
    bool HttpRequest(const char* method, const std::string& host, int port, const std::string& path,
                     int& statusCode, std::string& body, std::string& error,
                     SDL_atomic_t* cancel, int readTimeoutMs = TIMEOUT_MS)
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

        std::string request = std::string(method) + " " + path + " HTTP/1.0\r\nHost: " + host +
                              "\r\nAccept: application/json\r\nContent-Length: 0\r\n\r\n";
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
            // Wait in short slices so a long wait can be cancelled.
            bool readable = false;
            for (int waited = 0; waited < readTimeoutMs && !readable; waited += 200)
            {
                if (SDL_AtomicGet(cancel))
                {
                    error = "cancelled";
                    CloseSocket(s);
                    return false;
                }
                readable = WaitFor(s, false, 200);
            }
            if (!readable)
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
: host(host), port(port), mutex(SDL_CreateMutex()), commandCond(SDL_CreateCond()),
  thread(nullptr), commandThread(nullptr)
{
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    SDL_AtomicSet(&stop, 0);
    SDL_AtomicSet(&commandBusy, 0);
    thread = SDL_CreateThread(ThreadMain, "moonraker", this);
    commandThread = SDL_CreateThread(CommandThreadMain, "moonraker-gcode", this);
}

MoonrakerClient::~MoonrakerClient()
{
    SDL_AtomicSet(&stop, 1);
    SDL_LockMutex(mutex);
    SDL_CondSignal(commandCond);
    SDL_UnlockMutex(mutex);
    SDL_WaitThread(thread, nullptr);          // requests notice `stop` within 200ms
    SDL_WaitThread(commandThread, nullptr);
    SDL_DestroyCond(commandCond);
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

bool MoonrakerClient::SendGcode(const std::string& script)
{
    if (SDL_AtomicCAS(&commandBusy, 0, 1) == SDL_FALSE)
    {
        return false;   // one at a time
    }
    SDL_LockMutex(mutex);
    pendingCommand = script;
    SDL_CondSignal(commandCond);
    SDL_UnlockMutex(mutex);
    return true;
}

bool MoonrakerClient::IsCommandRunning() const
{
    return SDL_AtomicGet(const_cast<SDL_atomic_t*>(&commandBusy)) != 0;
}

int MoonrakerClient::CommandThreadMain(void* self)
{
    MoonrakerClient* client = static_cast<MoonrakerClient*>(self);
    for (;;)
    {
        SDL_LockMutex(client->mutex);
        while (client->pendingCommand.empty() && !SDL_AtomicGet(&client->stop))
        {
            SDL_CondWait(client->commandCond, client->mutex);
        }
        std::string script = client->pendingCommand;
        client->pendingCommand.clear();
        SDL_UnlockMutex(client->mutex);

        if (SDL_AtomicGet(&client->stop))
        {
            return 0;
        }

        // Percent-encode everything but unreserved characters.
        std::string encoded;
        for (size_t i = 0; i < script.size(); i++)
        {
            unsigned char c = script[i];
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            {
                encoded += (char)c;
            }
            else
            {
                char hex[4];
                snprintf(hex, sizeof(hex), "%%%02X", c);
                encoded += hex;
            }
        }

        int statusCode = 0;
        std::string body, error;
        printlog("Moonraker: sending %s\n", script.c_str());
        if (!HttpRequest("POST", client->host, client->port, "/printer/gcode/script?script=" + encoded,
                         statusCode, body, error, &client->stop, COMMAND_TIMEOUT_MS))
        {
            printlog("Moonraker: %s failed: %s\n", script.c_str(), error.c_str());
        }
        else if (statusCode != 200)
        {
            printlog("Moonraker: %s failed: HTTP %d %s\n", script.c_str(), statusCode, body.c_str());
        }
        SDL_AtomicSet(&client->commandBusy, 0);
    }
}

int MoonrakerClient::ThreadMain(void* self)
{
    MoonrakerClient* client = static_cast<MoonrakerClient*>(self);
    Uint32 lastInfo = 0;
    bool first = true;
    while (!SDL_AtomicGet(&client->stop))
    {
        Uint32 started = SDL_GetTicks();
        if (first || started - lastInfo >= (Uint32)INFO_INTERVAL_MS)
        {
            client->PollInfo();
            lastInfo = started;
            first = false;
        }

        Status status = client->GetStatus();
        int interval = INFO_INTERVAL_MS;
        if (status.state == Connected && status.klippyState == "ready")
        {
            client->PollToolhead();
            interval = TOOLHEAD_INTERVAL_MS;
        }

        while (!SDL_AtomicGet(&client->stop) && SDL_GetTicks() - started < (Uint32)interval)
        {
            SDL_Delay(interval < 100 ? interval : 50);
        }
    }
    return 0;
}

void MoonrakerClient::PollInfo()
{
    Status next;
    int statusCode = 0;
    std::string body, error;

    if (!HttpRequest("GET", host, port, "/server/info", statusCode, body, error, &stop))
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
    if (next.state == Connected && next.klippyState == "ready")
    {
        // PollToolhead owns these; don't wipe them every info poll.
        next.hasToolhead = status.hasToolhead;
        next.homedAxes = status.homedAxes;
        for (int i = 0; i < 3; i++)
        {
            next.position[i] = status.position[i];
            next.axisMin[i] = status.axisMin[i];
            next.axisMax[i] = status.axisMax[i];
        }
    }
    status = next;   // otherwise hasToolhead is false: stale once Klipper is gone
    SDL_UnlockMutex(mutex);
}

namespace
{
    // Reads a JSON array of numbers such as [x, y, z, e] into out[0..count).
    bool ReadVector(const boost::property_tree::ptree& tree, const char* path, double* out, int count)
    {
        boost::optional<const boost::property_tree::ptree&> array = tree.get_child_optional(path);
        if (!array)
        {
            return false;
        }
        int i = 0;
        for (boost::property_tree::ptree::const_iterator it = array->begin(); it != array->end() && i < count; ++it, ++i)
        {
            out[i] = it->second.get_value<double>();
        }
        return i == count;
    }
}

void MoonrakerClient::PollToolhead()
{
    int statusCode = 0;
    std::string body, error;
    bool ok = false;
    double position[3], axisMin[3], axisMax[3];
    std::string homedAxes;

    if (HttpRequest("GET", host, port, "/printer/objects/query?toolhead=position,axis_minimum,axis_maximum,homed_axes",
                    statusCode, body, error, &stop) && statusCode == 200)
    {
        try
        {
            boost::property_tree::ptree tree;
            std::istringstream in(body);
            boost::property_tree::read_json(in, tree);
            homedAxes = tree.get<std::string>("result.status.toolhead.homed_axes", "");
            ok = ReadVector(tree, "result.status.toolhead.position", position, 3)
              && ReadVector(tree, "result.status.toolhead.axis_minimum", axisMin, 3)
              && ReadVector(tree, "result.status.toolhead.axis_maximum", axisMax, 3);
        }
        catch (const std::exception&)
        {
            ok = false;
        }
    }

    // Only the toolhead fields; connection state is PollInfo's job.
    SDL_LockMutex(mutex);
    status.hasToolhead = ok;
    if (ok)
    {
        status.homedAxes = homedAxes;
        for (int i = 0; i < 3; i++)
        {
            status.position[i] = position[i];
            status.axisMin[i] = axisMin[i];
            status.axisMax[i] = axisMax[i];
        }
    }
    SDL_UnlockMutex(mutex);
}
