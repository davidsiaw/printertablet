# Architecture

## Layers

```
 Java (src/net/astrobunny/printertablet/PrinterTablet.java, extends SDLActivity)
   idle timer: dim at 1 min, screensaver at 2 min, swallow the wake-up touch
        │ nativeSetScreensaver(bool)  (JNI, jni/src/screensaver.cpp)
        ▼
 C++ engine (jni/src/)                               SDL 2.0.1, GLES 2, SDL_ttf
   main.cpp: game() builds the scene, run() is the main loop
   ui/       UiTree (the node tree Ruby edits) + UiScene (draws it, routes touch)
   native/   everything Ruby can call: Native::Log, Script, UI, Worker, Http
   scripting.cpp  one mruby VM (Scripting class)
   http.cpp       blocking HTTP/1.0 client (used by Native::Http)
        │ Native::...
        ▼
 Ruby app (assets/scripts/), mruby 3.3
   main.rb          entry point: loads lib, starts Printer, builds the screen
   lib/ui.rb, lib/ui/*.rb   Node, Group, Rect, TextBox, Screen, Timer
   lib/worker.rb    Worker
   printer.rb       Printer (main VM's view of the printer)
   printer/*_worker.rb      worker scripts that talk to Moonraker
   status_bar.rb    StatusBar
   toolhead_panel.rb, bed_map.rb, confirm_slider.rb   ToolheadPanel and its parts
```

Legacy engine code from Aldebaran is still in `jni/src/` and still compiled
(VboScene, DefaultShader, QuadCollectionVbo, GuiScene, RpgMapScene, Box2D,
Lua via luacppinterface, ...), though nothing on screen uses it any more
except the scene interfaces (`CompositeScene`, `ScreensaverSwitch`,
`RippleScene` has its own shader). Removing it is a possible cleanup (ask).

## Main loop

`run()` in `main.cpp` polls one SDL event per iteration, updates the scene,
and renders every ~15 ms. The scene built by `game()`'s `makeScene` is:

```
ScreensaverSwitch( normal = CompositeScene[
                       ProcessingScene   -> Ruby timers + worker messages (only when due)
                       UiScene           -> draws the UiTree, sends touches to Ruby ],
                   saver = RippleScene )
```

`makeScene` is called again if Android replaced the GL context on resume.
Scenes own all GL objects; the UiTree and the Ruby VM survive, so the screen
comes back as it was.

Per frame, C++ calls Ruby only if needed:
- `Native::UI.frame(now)` if `UiTree::WakeDue(now)` (Ruby's earliest timer).
- `Native::Worker.deliver(id, json)` / `.finished(id, error)`, from
  `Native::DeliverWorkerMessages`, only if a worker replied or ended
  (an atomic counter makes the check free).
- `Native::UI.touch(id, kind, x, y)` from `UiScene::Update` on finger
  down/move/up over a node with a handler.

## UI: UiTree, UiScene and the Ruby classes

- `ui/uitree.hpp/.cpp`: nodes `{kind (GROUP/RECT/TEXT), parent, children,
  x, y, w, h, visible, touchable, color, text, textColor, fontSize, align,
  padding}`. Positions are relative to the parent. No GL. Hit testing:
  topmost visible touchable node (children are checked last-drawn first).
- `ui/uiscene.hpp`: one small shader (solid colour, or texture x colour).
  TextBox text is rendered with SDL_ttf (white, tinted by `text_color`)
  into power-of-two textures, cached per node, re-rendered only when text or
  font size changes; textures of nodes not drawn this frame are freed. Fonts
  cached per size (`fonts/sazanami-gothic.ttf`). Touch: the node that got the
  touch-down also gets moves and the touch-up.
- `native/ui.cpp`: `Native::UI.create/destroy/add/remove/show/set_num/
  set_str/contains/screen_width/screen_height/now/wake_at`. Flat ids and
  values; the Ruby classes wrap them.
- Ruby (`lib/ui/`): `Node` (x, y, w, h, visible, move, show/hide, destroy,
  contains?, on_tap, on_touch_down/move/up), `Group` (add, remove, children),
  `Rect` (color), `TextBox` (text, color = background, text_color, size,
  align :left/:center/:right, padding), `Screen` (width, height, show,
  shown), `Timer` + `every(s) {}` / `after(s) {}` (return a Timer with
  `cancel`). Colours are `0xRRGGBB` or `nil`.

## Native modules

See the table in `jni/src/native/native.hpp`. Summary:

| Ruby | File | VMs |
|---|---|---|
| `Native::Log` (`line`; Ruby: `write`, `flush`, `error(e)`; makes `print`/`puts` log) | native/log.cpp | all |
| `Native::Script` (`load`; Ruby: `load_script`) | native/script.cpp | all |
| `Native::UI` | native/ui.cpp (+ `frame`/`touch` in lib/ui.rb) | main |
| `Native::Worker` main side: `start`, `post`, `stop` | native/worker.cpp (+ `deliver`/`finished` in lib/worker.rb) | main |
| `Native::Worker` worker side: `receive(timeout)`, `reply`, `stopping?`, `args` | native/worker.cpp | workers |
| `Native::Http.request(method, url, timeout)` -> `[status, body]` | native/http.cpp | workers |

Bindings use mruby-cpp's `bind_method` where possible; functions that need
the `mrb_state` (`Script.load`, `Worker.receive`/`reply`/..., `Http.request`)
use the plain mruby C API. Those must not raise while C++ objects with
destructors are alive in their frame: they compute an exception value in a
helper and `mrb_exc_raise` it at the end.

## Scripts: loading, paths, errors

- `Scripting::ScriptPath(name)`: on Android, prefers
  `/sdcard/Android/data/net.astrobunny.printertablet/files/scripts/<name>`
  (what `make push-script` writes), else `scripts/<name>` from the APK assets.
- Scripts run at the **top level** (`Scripting::Load` compiles with
  `mrb_load_nstring_cxt` inside `mrb_protect_error`), like Ruby's `load`, so
  classes are top-level (`Node`, not `Kernel::Node`). `load_script` raises
  into its caller on error, with the file/line chain in the backtrace;
  syntax errors are captured (`capture_errors`) rather than printed to
  stderr.
- `Scripting::Report` logs an exception with its backtrace via
  `Native::Log.error`. Every C++ -> Ruby call goes through `Scripting::Call`,
  which logs and clears errors; the Ruby entry points also rescue, so a bad
  handler never stops the app.

## Workers

- `Worker.start(script, args)` (main VM) -> `Native::Worker.start` creates a
  `Channel` (mutex, condition variable, two string queues, stop flag) and an
  SDL thread. The thread makes its own `Scripting` (so its own `mrb_state`;
  `mrb->ud` points at the Channel), binds the worker side of
  `Native::Worker` and `Native::Http`, and runs the script at top level.
  Worker scripts begin with `load_script "lib/worker.rb"`.
- Messages are JSON strings (`JSON.generate`/`JSON.parse`, mruby-json).
- When a script ends (or raises, or is missing), the thread records it; the
  main loop joins the thread and calls the worker's `on_exit(error)`.
- Shutdown: `Native::StopWorkers()` (end of `game()`) sets every stop flag,
  wakes `receive`, and `Http::Request` gives up within 200 ms; then it joins
  the threads. Measured ~100 ms with a 30 s request in flight.
- Workers can't start workers (nothing would deliver their messages).

## Printer

- `Printer` (`printer.rb`, main VM) starts `printer/status_worker.rb` and
  `printer/gcode_worker.rb` with `{"host", "port"}` and exposes
  `status`, `connection`, `klippy`, `version`, `error`, `toolhead`, `ready?`,
  `homed?`, `busy?`, `last_error`, `send_gcode(gcode)` (false if busy) and
  `on_change { }`.
- `status_worker.rb`: `/server/info` every 2 s; while Klipper is ready,
  `/printer/objects/query?toolhead=position,axis_minimum,axis_maximum,homed_axes`
  every 0.25 s. Replies with the whole status **only when it changed**.
- `gcode_worker.rb`: one command at a time via
  `POST /printer/gcode/script?script=...` (Moonraker answers when the G-code
  finishes; timeout 10 min). Replies `{"gcode", "ok", "error"}` with
  Klipper's own error message. A separate worker so homing doesn't stall
  status updates.

## Screen

`main.rb` builds one `Group` holding `StatusBar.new(printer)` and
`ToolheadPanel.new(printer, x:, y:, height:, gap:)` (margin = H/30, gap =
margin/2, height = H - StatusBar.height - 2 margins). Both redraw from
`Printer#on_change`; neither uses a timer, so with an idle printer Ruby
doesn't run at all.

- `ToolheadPanel`: boxes and slider are each height/9 tall; the map is
  square. HOME (a `TextBox` with `on_tap`) shows when the toolhead is known
  but not homed on x, y and z; text "HOMING..." while `printer.busy?`.
  The slider shows when homed.
- `BedMap#show_toolhead(toolhead or nil)`: maps `position` between
  `axis_minimum`/`axis_maximum`, Y up, clamped.
- `ConfirmSlider.new(w:, h:, text:) { ... }`: runs the block when the knob is
  dragged to the end and released; springs back otherwise.

## Build

- `jni/src/Android.mk` compiles `jni/src/*.cpp`, `ui/*.cpp`, `native/*.cpp`
  (`jni/src` is on the include path).
- mruby is built **on the host** by its own build system
  (`jni/mruby-glue/build_config.rb`: gems, plus `../mruby-json`). The
  `sources_mk` task writes `build/host/mruby_sources.mk`, the exact C file
  list, defines and include paths; `jni/mruby-glue/Android.mk` compiles
  that list with the NDK. `Makefile.common` runs it (`MRUBY_RAKE`) when
  `build_config.rb` changes, clearing Bundler env vars and loading rake as a
  library (see findings.md).
- Submodules: SDL, SDL_image, SDL_mixer, SDL_ttf (owner's forks), boost,
  box2d, glm, luacppinterface, mruby-cpp (owner's, with uncommitted local
  changes the app relies on), mruby-json.
