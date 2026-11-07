# Testing without the tablet

There is no device, no docker and no working desktop build here. What works
is compiling the relevant C++ files into small test programs, against the
host-built mruby (`jni/mruby-glue/build/host/lib/libmruby.a`), and running
the real scripts from `assets/`. Always say in your report that the NDK
build and on-device behaviour were **not** verified.

Put test programs in `/tmp`. Test scripts that must be found by
`load_script` / `Worker.start` go in `assets/scripts/tmp_test/`: **delete
that folder afterwards**.

## Setup (per sandbox)

```sh
pa-apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libsdl2-mixer-dev libgl-dev
make mruby          # if jni/mruby-glue/build is missing
```

Link against the **system** runtime `/usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0`
(pa-apt's dev package `.so` has unresolved deps); pa-apt's SDL headers need
`-I$P/include/x86_64-linux-gnu`. GL tests run under `Xvfb` (`xvfb-run`
needs xauth, which is missing: start `Xvfb :NN &` yourself), with
`LD_LIBRARY_PATH=$P/lib/x86_64-linux-gnu` for libGLESv2 / SDL2_ttf.

Common flags (`J=jni`, `P=~/.local/pa-apt/usr`):

```sh
DEFS="-DMRB_USE_RATIONAL -DMRB_USE_COMPLEX -DMRB_USE_BIGINT"   # = MRUBY_APP_DEFINES in mruby_sources.mk
INC="-I$J/src -I$J/mruby-cpp -I$J/mruby-cpp/mruby/include -I$J/mruby-glue/build/host/include \
     -I$P/include/SDL2 -I$P/include -I$P/include/x86_64-linux-gnu"
LIBS="$J/mruby-glue/build/host/lib/libmruby.a /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0 -lm"
```

## 1. Compile check

```sh
g++ -std=gnu++11 -fsyntax-only -Wall -Wextra -Wno-unused-parameter -DHAVE_OPENGLES $DEFS \
    -I$J/src -I$J/SDL_ttf -I$J/glm -I$J/boost -I$J/box2d \
    -I$J/luacppinterface/LuaCppInterface -I$J/luacppinterface/lua/src $INC $J/src/main.cpp
```

`-I$J/SDL_ttf` must come before the system SDL headers (the fork has
`TTF_SizeUTF8_Wrapped`). The pre-existing warning at `main()`'s
`catch (std::exception e)` isn't ours. The NDK uses clang 3.2: avoid
very new C++ (stick to C++11, no `auto` lambdas params, no TLS).

## 2. Headless Ruby test (no GL)

Runs the real `main.rb` with the real bindings; then a test script pokes at
it. Run from `assets/` (scripts are found as `scripts/<name>`).

```cpp
#include <cstdio>
#include "scripting.hpp"
#include "native/native.hpp"
#include "ui/uitree.hpp"
int main(int argc, char** argv) {
    SDL_Init(SDL_INIT_TIMER);
    Scripting scripting; UiTree ui;
    Native::BindUI(scripting, ui, 1280, 800);       // as game() in main.cpp does
    Native::BindWorkers(scripting);
    printf("main ok=%d\n", scripting.RunFile(Scripting::MainScriptPath()));
    printf("test ok=%d\n", scripting.RunFile(argv[1]));
    Native::StopWorkers();
}
```

```sh
g++ -std=gnu++11 $DEFS $INC t.cpp $J/src/scripting.cpp $J/src/ui/uitree.cpp $J/src/native/*.cpp \
    $J/src/http.cpp $J/src/printlog.cpp $LIBS -o t && (cd assets && /tmp/t /tmp/checks.rb)
```

From the test script you can call the C++ entry points directly:
`Native::UI.touch(node.id, 0, x, y)` (0 down, 1 move, 2 up) and
`Native::UI.frame(Timer.now + 1)` to fire timers. To drive workers, loop in
C++ calling `Native::DeliverWorkerMessages(scripting)` (and the timer call
from main.cpp) every 16 ms.

## 3. GL harness (screenshots)

Same, plus: create an SDL window with a GLES 2 context
(`SDL_GL_CONTEXT_PROFILE_ES`, 2.0), `glViewport`, build the scene like
`makeScene` (a ProcessingScene-like scene for timers + `DeliverWorkerMessages`, UiScene, RippleScene if relevant), call
`Update`/`Render` per frame, and save frames with `glReadPixels` (flip rows)
to BMP; convert with `ffmpeg -i x.bmp x.png` and `read` the PNG.
Simulate touch by setting `InputState::SetFingered/SetFingerX/SetFingerY`
(0..1) between frames. Compile with `-DHAVE_OPENGLES`, link
`$P/lib/x86_64-linux-gnu/libSDL2_ttf-2.0.so.0` and `libGLESv2.so.2`, run under
`DISPLAY=:NN` from `assets/` (fonts are loaded from `fonts/`).

Note `compositescene.hpp` needs `#include <vector>` before it.

## 4. Fake Moonraker

A small Python `http.server` serving the endpoints the app uses. Use
`protocol_version = "HTTP/1.0"` and `ThreadingHTTPServer` (a slow `GANTRY`
must not block polls). Useful behaviour to fake:

```python
GET  /server/info                    -> {"result": {"klippy_state": "ready", "moonraker_version": "v0.9.3"}}
GET  /printer/objects/query?toolhead=... -> {"result": {"status": {"toolhead": {
         "position": [120, 200, 5, 0], "axis_minimum": [0, 0, -2, 0],
         "axis_maximum": [300, 300, 340, 0], "homed_axes": state["homed"]}}}}
POST /printer/gcode/script?script=GANTRY -> sleep 1.5 s, homed = "xyz", {"result": "ok"}
POST ...?script=M84                  -> homed = "", {"result": "ok"}
POST ...?script=SLOW                 -> sleep 30 s (to test shutdown)
POST anything else                   -> 400 {"error": {"code": 400, "message": "Unknown command:\"FOO\""}}
```

Log every request to a file so the test can assert exactly which G-code
reached the printer. Point a copy of `main.rb` at it (`sed` the
`PRINTER_HOST`/`PORT` lines into `assets/scripts/tmp_test/main.rb`).

**Don't `pkill -f fake.py`**: it matches your own shell command line and
kills it. Use `kill $(pgrep -f '^python3 fake.py')`.

## What has been verified this way

Taps vs. release-outside, drags, hidden/destroyed nodes, handler errors,
timers and `wake_at` (Ruby ran on 24 of 120 frames with 0.1 s + 0.25 s
timers, ~0.06 ms each), class names at top level, load errors (raise,
syntax, missing file), workers end to end (status flows during a slow
GANTRY, busy refusal, Klipper error text, crash -> `on_exit`, JSON
round trip, StopWorkers in ~100 ms with a request in flight), the C++
toolhead panel, both the old C++ one and the Ruby one (only GANTRY and M84
sent, nothing on partial slides, tapping the track or release-outside;
offline state), the ripple screensaver (no GL errors).

Panel coordinates at 1280x800 (for scripted touches): margin 26, map at
y 117, size 522; slider at y 652, height 78; knob travel 444.
