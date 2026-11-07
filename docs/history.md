# History: what has been done

In order. "Verified" means on the desktop harnesses (testing.md) unless it
says the owner checked it on the tablet.

## Rename: Aldebaran -> Printer Tablet

- Display name "Printer Tablet"; package `net.astrobunny.aldebaran` ->
  `net.astrobunny.printertablet`; activity `Aldebaran` -> `PrinterTablet`;
  desktop binary `printertablet`; APK name (was `Bomberman`); log tag; the
  folder `androiddev/aldebaran` -> `androiddev/printertablet`.
- Left alone on purpose: Xcode/MSVC project files and `//  aldebaran`
  header comments.

## Build fixes

- `make clean` now also removes `bin gen libs` and lua objects.
- `-include` -> `include` for `mruby_sources.mk` (failures were hidden).
- `MRUBY_RAKE` immune to Bundler (rake 13.0.6 pin in mruby's Gemfile.lock).
  The owner confirmed the build works.
- `.gitignore`: bin, gen, libs, obj, SDL2Framework, the desktop binary,
  `build_config.rb.lock`.
- `jni/mruby-cpp` registered as a submodule.
- Unused assets deleted (only `scripts/` and `fonts/sazanami-gothic.ttf`
  are used). Commands were given to the owner to strip them from git
  history with `git filter-repo` (whether that was done is unknown).

## Screen: dimming and screensaver (Java + C++)

- `PrinterTablet.java`: `FLAG_KEEP_SCREEN_ON`; after 60 s without a touch the
  window brightness drops to 5%; after 120 s `nativeSetScreensaver(true)`.
  A touch restores both; the wake-up touch (whole gesture) is swallowed.
- `ripplescene.hpp`: rings spreading from random points (additive, fading);
  `screensaver.cpp/.hpp`: the JNI flag and `ScreensaverSwitch`.

## Toolhead panel (first in C++, ported to Ruby in step 3)

Was `toolheadpanelscene.hpp` (deleted in step 3); the Ruby version keeps
this behaviour exactly:

- Three grey boxes on top: `X 120.00`, `Y 200.00`, `Z 5.00` (two decimals);
  `X  -` etc. when the toolhead isn't known.
- A grey square (10x10 grid) mapping the bed using `axis_minimum` /
  `axis_maximum`; an orange marker square at the position, clamped; **Y up**
  (back of the bed at the top).
- When the toolhead is known but not homed on x, y and z: the square is a
  big orange **HOME** button; a tap (released on the button) sends `GANTRY`;
  it reads **HOMING...** while the command runs and ignores taps.
- When homed: a slider under the grid ("slide to turn off motors", orange
  knob with "»"). Drag the knob to the very end and release: `M84`.
  Released short of the end, it snaps back and sends nothing. Hidden when
  not homed. After M84, the next poll shows not homed, so HOME is back.
- Layout (`main.cpp`): margin = H/30, gap = margin/2, panel height = H minus
  the status bar minus 2 margins; boxes and slider each 1/9 of that.
- Colours: grey 0x484848, grid lines 0x707070, orange 0xff8c1a, text 0xdddddd.
- `inputmachine.cpp` passes `SDL_FINGERMOTION` (added for the slider).
- The C++ `MoonrakerClient` behind it: info every 2 s, toolhead every 0.25 s
  while ready, commands on their own thread (one at a time, 10 min timeout).
  All of this now exists in Ruby (`Printer` + the two workers).

## Step 1: UI building blocks in Ruby

- C++ retained node tree (`ui/uitree.*`) and renderer (`ui/uiscene.hpp`);
  Ruby classes `Node`, `Group`, `Rect`, `TextBox`, `Screen`, `Timer`,
  `every`, `after` (one file each under `lib/ui/`).
- The owner rejected a per-frame Ruby call: Ruby now tells C++ when its next
  timer is due (`wake_at`), and C++ calls in only then.
- Status bar ported to Ruby (`status_bar.rb`); `statusbarscene.hpp` deleted.
- `make push-script` pushes the whole `assets/scripts/` folder.

## Cleanup rounds

- C++ UI code into `jni/src/ui/`; Ruby classes one per file.
- All bindings under `Native::`; one C++ file per Ruby module in
  `jni/src/native/` with the table in `native.hpp`; `scripting.cpp` is just
  the VM.
- `__`-prefixed globals removed: internals are `private`/`protected`
  (documentation only in mruby), logging/script helpers moved into
  `Native::Log`/`Native::Script`, the C++ -> Ruby entry points are
  `Native::UI.frame`/`.touch`.
- Scripts run at the top level (fixed `Kernel::Node` class names); syntax
  errors captured; error backtraces show the `load_script` chain.

## Step 2: workers and printer logic in Ruby

- `jni/mruby-json` submodule (mattn/mruby-json) in `build_config.rb`;
  `sources_mk` handles external gems.
- `http.cpp/.hpp` (moved out of `moonrakerclient.cpp`), `native/http.cpp`,
  `native/worker.cpp`, `lib/worker.rb` (`post` rather than `send`, which
  is Ruby's own).
- `printer.rb` + `printer/status_worker.rb` + `printer/gcode_worker.rb`;
  the status bar listens to `Printer#on_change` (no timer).
- `Native::Moonraker` (temporary bridge to the C++ client) removed.
- GC arena save/restore around main-loop calls into Ruby.
- Verified on the tablet together with step 3.

## Step 3: toolhead panel in Ruby

- `toolhead_panel.rb` (`ToolheadPanel`), `bed_map.rb` (`BedMap`),
  `confirm_slider.rb` (`ConfirmSlider`, reusable), laid out in `main.rb`.
- Deleted: `toolheadpanelscene.hpp`, `moonrakerclient.cpp/.hpp`, the
  `moonraker` binding and its globals and the panel layout in `main.cpp`.
  The printer is now polled once, by the status worker.
- Verified on the GL harness against a fake Moonraker: the same five states
  as the C++ panel (HOME, HOMING..., grid + marker + slider, sliding, HOME
  after M84) and the offline state; only `GANTRY` and `M84` reached the
  printer (nothing for release-outside, half slides, tapping the track).
  With no timers left, Ruby's timer path ran on 0 of ~400 frames.
- The owner built it with the NDK (so mruby-json compiles with clang 3.2)
  and confirmed on the tablet that everything works as expected.
