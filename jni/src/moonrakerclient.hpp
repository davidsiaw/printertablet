#ifndef MOONRAKERCLIENT_HPP
#define MOONRAKERCLIENT_HPP

#include <string>
#include <SDL.h>

// Polls a Moonraker server's /server/info, and the toolhead while Klipper is
// ready, on a background thread, so the UI thread never blocks on the network.
// Call GetStatus() from anywhere.
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

        // Toolhead, from Klipper's toolhead object. Only valid if hasToolhead.
        bool hasToolhead;
        double position[3];              // x, y, z in mm
        double axisMin[3], axisMax[3];   // travel limits from the printer config
        std::string homedAxes;           // e.g. "xyz"; "" after M84 or a restart

        Status() : state(Connecting), hasToolhead(false)
        {
            for (int i = 0; i < 3; i++)
            {
                position[i] = axisMin[i] = axisMax[i] = 0;
            }
        }
    };

    MoonrakerClient(const std::string& host, int port);
    ~MoonrakerClient();

    Status GetStatus() const;

    // Runs G-code on the printer in the background. Only one at a time: returns
    // false (and does nothing) while another is still running.
    bool SendGcode(const std::string& script);
    bool IsCommandRunning() const;
    const std::string& GetHost() const { return host; }
    int GetPort() const { return port; }

private:
    static int ThreadMain(void* self);
    void PollInfo();
    void PollToolhead();
    static int CommandThreadMain(void* self);

    std::string host;
    int port;

    SDL_mutex* mutex;
    Status status;

    SDL_cond* commandCond;           // with mutex: wakes the command thread
    std::string pendingCommand;      // guarded by mutex
    SDL_atomic_t commandBusy;        // from SendGcode until the command finishes

    SDL_atomic_t stop;
    SDL_Thread* thread;
    SDL_Thread* commandThread;

    MoonrakerClient(const MoonrakerClient&);            // not copyable
    MoonrakerClient& operator=(const MoonrakerClient&);
};

#endif // MOONRAKERCLIENT_HPP
