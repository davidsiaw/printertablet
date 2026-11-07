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

## Tap-to-move keypad

- Tap X, Y or Z (while homed): `KeypadModal` to type a position; Move sends
  `G90` + `G1` at `Printer::MOVE_SPEED`, after checking the printer's travel
  limits. Out of range, empty, busy: a red message, the modal stays open.
  Cancel or tapping outside closes it.
- New: `keypad_modal.rb`, `Printer#move_to`, `#position`, `#axis_range`; an
  `alpha` node property (C++: `uitree.hpp`, `native/ui.cpp`, `uiscene.hpp`;
  Ruby: `Node#alpha`).
- Verified on the GL harness against a fake Moonraker: typing, backspace,
  second dot ignored, "." then Move sends 0, out-of-range Z, empty, busy,
  Cancel, tap outside, unhomed taps ignored; the printer received exactly
  the two moves. Not yet verified on the tablet.

## Macro column

- `MacroColumn` on the right, from `MACROS` in `main.rb` (one button: Park ->
  `PARK`). Running: "Park...", and every macro button is disabled while the
  printer is busy (also during homing, moves, M84).
- New `Button` class (`button.rb`); `KeypadModal` now uses it instead of
  its private helper.
- Verified on the GL harness: PARK sent once, second tap and taps during a
  keypad move ignored, back to normal after; the keypad still works. Not
  yet verified on the tablet.

## Temperature column

- `TemperatureColumn` in the middle (`temperature_column.rb`): every
  temperature in Klipper's `heaters.available_sensors`; extruder and bed big
  on top. `Printer#temperatures`; `ToolheadPanel#width`; `url_encode`
  moved from gcode_worker.rb to `lib/url.rb` (`URL.encode`).
- Verified on the GL harness with a fake Moonraker (6 sensors, jitter
  ±0.04): labels, values, orange while heating, one combined query, 0
  updates in 4 s from jitter, "-" while Klipper restarts, rows rebuilt from
  a new sensor list. Not yet verified on the tablet.

## Fans and extrude

- Temperature column halved; its rows are now all two-line. The other half
  is `FanList` (top) and `ExtrudePanel` (bottom).
- `Printer#fans`, `Printer#extrude`; status worker finds fans in
  `/printer/objects/list` and queries each object once.
- `KlipperNames` (shared labels). C++: TextBox shrinks text to fit.
- Verified on the GL harness with a fake Moonraker: 4 fans incl. a
  temperature_fan, Extrude/Retract defaults (`M83\nG1 E50.000 F600.0`),
  busy refusal, keypad for length and speed (range errors), Klipper's
  "too long" and "below minimum temp" errors wrapped on 3 lines. Not yet
  verified on the tablet.

## Setting temperatures and fans

- Tap Extruder/Bed: keypad for the target (limits from the config's
  min_temp/max_temp, read once per ready; 0 = off). Tap a settable fan
  (part fan, fan_generic; lighter grey): keypad for 0-100 %.
- `Printer#heater_limits`, `#set_heater`, `.settable_fan?`, `#set_fan`;
  `KeypadModal` `action:` label ("Move" for coordinates, "Set" otherwise).
- Verified on the GL harness: limits read, out-of-range rejected (400 °C,
  101 %), exactly the expected G-code sent (`SET_HEATER_TEMPERATURE ...`,
  `M106 S128` for 50 %, `SET_FAN_SPEED FAN=nevermore SPEED=0.75`, bed off),
  non-settable rows ignore taps, busy refusal. Not yet verified on the
  tablet.

## Private status bar

- Tap the status bar to hide the address, Moonraker version and error
  details (they can contain the host); tap again to show. Not remembered
  across restarts. Verified headlessly (connected, disconnected with a
  host in the error, connecting: nothing leaks). Not yet verified on the
  tablet.

## Performance (hangs every few seconds)

- Owner reported taps not responding and temperatures freezing for seconds.
  Added diagnostics (logged only when slow; see findings.md). The tablet's
  log showed Ruby taking 186-747 ms per status update, the main loop
  spinning at 17,000-62,000 passes/s for 6-28 frames/s, and first frames
  taking 10-34 s to draw.
- Fixes: `APP_OPTIM := release` (was -O0 because of debuggable="true"),
  `APP_ABI := armeabi-v7a` (tablet is arm64-v8a; armeabi had no hardware
  float), the main loop waits for events between frames
  (`SDL_WaitEventTimeout`). A change to the text-fitting loop was tried and
  reverted (it opened as many font sizes as before).
- The owner confirmed the new build works well.

## Tap the bed map to move

- While homed, tapping the bed map moves the toolhead there in X/Y
  (`BedMap#on_pick`, `Printer#move_xy`), with a faint target square until
  it arrives (or Klipper refuses). `Node#screen_x/screen_y` added.
- Verified on the GL harness: centre -> X150 Y150, corners clamped to the
  travel limits, an arbitrary tap matches the marker mapping, taps while
  busy and press-then-release-elsewhere ignored, target hidden on arrival
  and on a refused move. Not yet verified on the tablet.

## Printing lock

- While `print_stats.state` is "printing": `PrintingModal` covers the
  controls ("Printing", Pause -> `PAUSE`). Paused: controls back, and a
  Resume button in the macro column (`RESUME`; added on my initiative, so
  a paused print can be resumed from the tablet).
- Verified on the GL harness with a fake Moonraker: modal shown/hidden by
  state, taps on the X box, Park, bed map and Retract blocked while
  printing, Pause -> "Pausing..." -> paused -> Resume shown, Resume ->
  printing, a refused PAUSE shows Klipper's message, a keypad left open
  while paused is closed when printing resumes (it had swallowed a later
  Resume tap). The printer got exactly PAUSE, PAUSE, RESUME, PAUSE. Not yet
  verified on the tablet.

