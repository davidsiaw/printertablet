#ifndef MOONRAKERCLIENT_HPP
#define MOONRAKERCLIENT_HPP

#include <string>
#include <SDL.h>

// Polls a Moonraker server's /server/info on a background thread, so the UI
// thread never blocks on the network. Call GetStatus() from anywhere.
class MoonrakerClient
{
public:
    enum State
    {
        Connecting,     // no answer yet
        Connected,      // Moonraker answered; see klippyState
        Disconnected    // last attempt failed; see error
    };

    struct Status
    {
        State state;
        std::string klippyState;       // "ready", "startup", "shutdown", "error", "disconnected"
        std::string moonrakerVersion;
        std::string error;

        Status() : state(Connecting) {}
    };

    MoonrakerClient(const std::string& host, int port);
    ~MoonrakerClient();

    Status GetStatus() const;
    const std::string& GetHost() const { return host; }
    int GetPort() const { return port; }

private:
    static int ThreadMain(void* self);
    void Poll();

    std::string host;
    int port;

    SDL_mutex* mutex;
    Status status;

    SDL_atomic_t stop;
    SDL_Thread* thread;

    MoonrakerClient(const MoonrakerClient&);            // not copyable
    MoonrakerClient& operator=(const MoonrakerClient&);
};

#endif // MOONRAKERCLIENT_HPP
