# Printer Tablet: developer docs

An Android app for an old tablet that sits next to a 3D printer and shows /
controls it through [Moonraker](https://moonraker.readthedocs.io/) (the API
in front of Klipper). It started life as "Aldebaran", an SDL2 + OpenGL ES 2
game engine, and is being turned into a small app whose **UI and printer
logic are written in mruby**, on top of a thin C++ engine.

Read these in order if you are picking the project up:

| File | What it covers |
|---|---|
| [design-principles.md](design-principles.md) | The rules the owner asked for. Follow them. |
| [architecture.md](architecture.md) | How the pieces fit: C++ engine, Ruby app, `Native::`, workers, the main loop. |
| [findings.md](findings.md) | Things that bit us (mruby limitations, build traps). Read before you debug. |
| [testing.md](testing.md) | How to verify changes without the tablet (headless + GL harnesses, fake Moonraker). |
| [history.md](history.md) | What has been done so far, in order. |

## Where things stand

The owner builds and installs the app on the tablet. All UI and printer
logic is now Ruby (`assets/scripts/`):

- `StatusBar` (bottom): connection state. Tap it to hide the address,
  Moonraker version and error details (for photos); tap again to show.
- `ToolheadPanel` (left): X/Y/Z boxes (tap one while homed: a
  `KeypadModal` to type a position and move there); a `BedMap` (10x10 grid,
  toolhead marker; tap it while homed to move there in X/Y); a big orange **HOME** button (sends `GANTRY`) when not
  homed; a `ConfirmSlider` that sends `M84` when homed.
- `TemperatureColumn` (middle-left): every temperature Klipper reports;
  extruder and bed big on top (tap: set the target), other heaters/sensors
  below.
- Middle-right, split in two: `FanList` (every fan's speed; tap the part fan / fan_generic ones to set
  it) on top, and
  `ExtrudePanel` (Length / Speed boxes with the keypad, Retract / Extrude,
  Klipper's error if it refuses) below.
- `MacroColumn` (right): a button per macro, from `MACROS` in `main.rb`
  (for now just Park -> `PARK`); a Resume button while a print is paused.
- `PrintingModal`: while printing, covers all controls (not the status bar)
  with "Printing" and a Pause button.
- `Printer` talks to Moonraker through two worker threads
  (`printer/status_worker.rb`, `printer/gcode_worker.rb`).
- Still native (Java/C++): screen dims after 1 min idle, ripple screensaver
  after 2 min, one touch wakes it (`PrinterTablet.java`, `ripplescene.hpp`,
  `screensaver.cpp`).

All of this, including mruby-json and the workers, builds with the NDK and
works on the tablet (owner checked, after step 3).

## The plan (agreed with the owner)

1. ~~UI building blocks in Ruby (`Rect`, `TextBox`, `Group`, touch, timers); status bar in Ruby.~~ Done.
2. ~~Workers (threads with their own VM, message queues), `Native::Http`, mruby-json; printer logic in Ruby.~~ Done.
3. ~~Toolhead panel in Ruby; delete `ToolheadPanelScene`, `MoonrakerClient`, the `moonraker` binding.~~ Done.

After step 3: tap-to-move keypad, macro column, temperature column, fans
and extrude (see history.md). No next step has been
agreed yet: ask the owner.

Ideas raised but not done (ask before doing): hide the M84 slider while
printing; show G-code errors on screen (`Printer#last_error`); move static UI
a few pixels now and then against burn-in; wake the screen on printer events;
use Moonraker's websocket instead of polling; a minus key on the keypad
(for negative axis minimums); setting other heaters (heater_generic)
from the screen; a text-width measuring primitive (wrapping is estimated now); `motion_report` live position
instead of `toolhead.position`; let `main.rb` configure dim/screensaver times.

## Building and running

- `make run-android`: builds in the `davidsiaw/ndk-env:original-api-level-10`
  docker image (NDK r9, clang 3.2, API 10, `armeabi-v7a`, `APP_OPTIM := release`), installs with `adb`,
  starts the app and follows logcat. **This sandbox has no docker**: the owner
  runs it. The first build after `make clean` or a `build_config.rb` change
  also builds mruby on the host (`make mruby`; needs any `ruby` + `rake`).
- `make push-script`: copies `assets/scripts/` to the tablet and restarts the
  app, so Ruby changes need no rebuild. `make unpush-script` undoes it.
- `make clean`: removes build output (bin, gen, libs, obj, mruby build, lua).
- The desktop build (`make run`) is broken: the bundled SDL 2.0.1 doesn't
  compile against modern kernel headers. Use the harnesses in testing.md.
