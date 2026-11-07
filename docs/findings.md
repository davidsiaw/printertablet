# Findings: things that bit us

## mruby (3.3.0) limitations

- **No method visibility.** `private`, `protected`, `private_class_method`
  are dummies (mruby `doc/limitations.md`); `obj.private_thing` just works.
  We still mark internals for documentation and CRuby-correctness.
  `private_class_method` doesn't even exist: use
  `class << self; private :a, :b; end`.
- **`public_send` doesn't exist.** A test that used it "proved" internals
  were hidden; the `NoMethodError` was for `public_send` itself. Check
  that your test can actually fail.
- **No `Regexp`.** No `=~`, `gsub(/../)`, `grep(/../)`. Use `include?`,
  `index`, `split`, byte loops (see `url_encode` in `printer/gcode_worker.rb`).
- **`eval` takes its namespace from the calling method.** Code `eval`ed
  inside a method of `module Kernel` defined `Kernel::Node`; inside
  `Native::Script.run` it would have defined `Native::Script::StatusBar`.
  Hence scripts are compiled and run at the top level in C++
  (`Scripting::Load`), not `eval`ed.
- **Nested loads longjmp.** `mrb_load_*` called while Ruby is running
  (from a native method) raises by `longjmp` to the outer handler, which
  would skip C++ destructors. `Scripting::Load` wraps it in
  `mrb_protect_error`.
- **Parser errors go to stderr** unless `mrbc_context->capture_errors` is
  set (stderr is lost on Android).
- **Floats can be heap objects** (32-bit word boxing on the tablet): make
  `mrb_float_value` / `mrb_str_new` for calls inside
  `mrb_gc_arena_save`/`restore`, or the GC arena grows forever.
- The mruby build has no `io`/`socket`/`dir` gems on purpose; networking is
  `Native::Http`.
- **TextBox shrinks text to fit its width** (since the fans/extrude work;
  before, it drew past the edge). Shrunk text can get tiny: lay out long
  content on two lines rather than relying on it (temperature rows,
  ExtrudePanel's wrapped message).
- **Klipper error messages have several lines**; the first says what went
  wrong ("Extrude below minimum temp" / "See the 'min_extrude_temp'...").
- **`do ... end` binds to the outermost call.** `add Foo.new(x) do ... end`
  gives the block to `add`, not `new` (it silently became nil). Use `{ }`
  or build the object on its own line. Widgets that need a block should
  raise without one (`ConfirmSlider` does).

## mruby-thread (rejected)

We looked at `mattn/mruby-thread` for workers. Each `Thread.new` calls
`mrb_open()` (a fresh VM: no classes from our scripts, no C++ bindings) and
copies the block across VMs by poking mruby internals (irep, REnv layout);
unmaintained since 2021. Instead: our own `Native::Worker` (one VM per
thread, a script file each, JSON messages).

## mruby-cpp

- `mruby::VM` keeps its `mrb_state` private; `Scripting` opens the state
  itself and passes it to `VM(mrb_state*)` (non-owning), so it can use the C
  API too.
- `bind_method` functions get no `mrb_state`; use the C API when you need it.
- C++ exceptions thrown from bound functions are turned into Ruby
  exceptions (only `mruby::Ruby*Error` keep their class; others become
  RuntimeError "... in C binding: ..."). There is no `RubyArgumentError`.
- `create_module("X")` returns the existing module if defined (like
  reopening `module X`).
- Include `<mruby.hpp>`, not its sub-headers (they need the namespace).
- `jni/mruby-cpp` has **uncommitted local changes** (e.g. floats accepting
  Integers) that the app relies on. They need committing upstream.

## Build traps

- **`-include` hides failures.** `Makefile.common` uses plain
  `include $(MRUBY_SOURCES_MK)`: with `-include`, a failed mruby build shows
  up only as "No rule to make target .../mruby_sources.mk".
- **Bundler hijacks minirake.** mruby's `Gemfile.lock` pins rake 13.0.6;
  if anything activates Bundler (RUBYOPT, a binstub, BUNDLE_* vars) the build
  dies ("Could not find proper version of rake", exit 7). `MRUBY_RAKE`
  clears those vars and runs rake as a library.
- **The owner's machine builds mruby, not this sandbox.** An earlier
  session built mruby inside the sandbox; deleting `jni/mruby-glue/build`
  exposed that the owner's machine had never run minirake. Don't delete
  build output that took a working environment to produce without saying so.
- `make clean` didn't clean Android output; it now removes `bin gen libs`
  and lua objects too.
- **External gems in `sources_mk`:** gems outside mruby (mruby-json) build
  into `build/host/mrbgems/<name>/` but their sources are in the gem's own
  folder; `build_config.rb`'s `sources_mk` maps them.
- **Desktop build is broken:** bundled SDL 2.0.1 fails on modern kernel
  headers (`KDGKBTYPE`/`KB_101` in `SDL_evdev.c`). Trying it configures
  inside `jni/SDL` and dirties the submodule: revert with
  `git -C jni/SDL checkout -- . && git -C jni/SDL clean -fdq`.
- `main.cpp` needs the owner's **SDL_ttf fork** headers
  (`TTF_SizeUTF8_Wrapped`): put `-Ijni/SDL_ttf` before system includes.
- `compositescene.hpp` uses `std::vector` without including it (works
  because `main.cpp` includes `<vector>` first).
- **No docker here** (no Android build); `pa-apt` provides desktop SDL2,
  SDL2_ttf, GLES, a JDK, etc. for the harnesses.

## Android / device

- **Performance:** with `android:debuggable="true"` the NDK defaults to
  `APP_OPTIM=debug` (-O0) for all native code; `jni/Application.mk` forces
  `release`. The old `armeabi` (ARMv5) target had no hardware floating
  point. The tablet is `arm64-v8a` (`adb shell getprop ro.product.cpu.abi`);
  we build `armeabi-v7a`, which it runs (NDK r9 can't build arm64). Before
  these, Ruby took 186-747 ms per status update and the main loop spun at
  60,000 passes/s (it now waits for events between frames).
- Diagnostics in the log: `Slow frame` (>100 ms between frames, with Ruby
  time), `Main loop: N passes/s` (every 10 s), `Slow HTTP request` (>500 ms),
  `Status worker: poll took` (>0.5 s).
- Target: API 10 (Android 2.3), `armeabi-v7a`, NDK r9, clang 3.2,
  gnustl. No `__thread` TLS; use `mrb->ud` and the like for per-VM data.
- GLES2 samples non-power-of-two textures with default REPEAT as black:
  all textures are padded to power of two.
- `SDL_GetTicks()` is the main clock (seconds = ticks / 1000.0 in Ruby).
- `PrinterTablet.onDestroy` calls `System.exit(0)`: SDL 2.0.1 can't run
  `main()` twice, and a background destroy would hang. So native
  destructors don't run on Android; save state on `SDL_APP_WILLENTERBACKGROUND`.
- The GL context can be replaced on resume; `run()` rebuilds the scene
  (`makeScene`).
- The wake-up touch is swallowed in Java (`dispatchTouchEvent`), so it never
  reaches native code.
- Old pushed scripts lived at `files/main.rb`; now `files/scripts/...`.

## Moonraker

- `POST /printer/gcode/script` returns only when the G-code finishes (homing:
  minutes), hence the separate G-code worker and 10 min timeout.
- Errors come back as `{"error": {"message": "Unknown command:\"FOO\""}}`.
- After `M84` (or a restart), `toolhead.homed_axes` is `""`; homed is `"xyz"`.
- 401/403 means the tablet isn't in `trusted_clients` in `moonraker.conf`.
- Ask for each object once in `/printer/objects/query` (combine attributes);
  how Moonraker treats a repeated object isn't certain.
- Extruding needs the hotend above `min_extrude_temp`; a single extrude-only
  move is limited by `max_extrude_only_distance` (50 mm by default).
- `toolhead.position` is the planned position (can run ahead of the nozzle);
  `motion_report.live_position` is the measured one (not used yet).
- The owner's printers have a `GANTRY` macro used for homing.
