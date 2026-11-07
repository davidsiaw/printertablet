# Design principles

These come from the owner, mostly as corrections along the way. When in
doubt, ask; don't guess.

## The split between C++ and Ruby

1. **Ruby is the app; C++ is the engine.** UI layout, screens, wording,
   touch behaviour and all printer logic belong in Ruby
   (`assets/scripts/`). C++ provides primitives Ruby can't or shouldn't do:
   drawing, input, threads, sockets, OpenGL.

2. **Retained mode: Ruby places things, C++ draws every frame.** Ruby
   creates nodes (`Rect`, `TextBox`, `Group`) and sets properties
   (`marker.x = 300`, `label.text = "X 12.00"`). C++ keeps them and redraws
   every frame. Ruby never draws, and is never asked to "render".

3. **Minimise Ruby per frame.** C++ calls into Ruby only when something is
   due: a timer (Ruby tells C++ when via `Native::UI.wake_at`), a touch, or a
   worker message. A frame with nothing going on runs no Ruby at all. Don't
   add an unconditional per-frame Ruby call; if something truly needs every
   frame (an animation), make it an explicit opt-in.

4. **Primitives are rich enough that Ruby only sets values.** e.g. a
   `TextBox` primitive: Ruby sets the text, C++ renders and caches it (and
   only re-renders when text or size changes).

## Threads and the printer

5. **Printer logic is separate from UI logic, and they talk through
   queues.** Workers are Ruby scripts on their own threads in their own VMs
   (`Worker.start("printer/status_worker.rb", args)`). Nothing is shared;
   messages are plain data sent as JSON. The main VM never blocks on the
   network.

6. **Ruby decides when to start a worker and which file it runs.** C++
   provides the mechanism (`Native::Worker`), not the policy.

7. **What a VM can do depends on where it runs.** Only the main VM has
   `Native::UI` (OpenGL belongs to the main thread). Only workers have
   `Native::Http` (it blocks). Keep it that way.

## Code organisation

8. **One file per Ruby class** (`lib/ui/node.rb`, `lib/ui/group.rb`, ...).
   A loader file (`lib/ui.rb`) loads them in dependency order and holds
   top-level helpers.

9. **Every C++ binding lives under `Native::`, and each Ruby module maps to
   one C++ file.** `Native::UI` -> `jni/src/native/ui.cpp`, `Native::Log` ->
   `native/log.cpp`, and so on. `native/native.hpp` has the table of modules,
   files and which VMs get them. If a module has Ruby-side methods too
   (e.g. `Native::Log.error`), they live in the same `.cpp`, unless they
   drive app-side classes (`Native::UI.frame`/`.touch` are in `lib/ui.rb`;
   `Native::Worker.deliver`/`.finished` in `lib/worker.rb`); then the
   `.cpp` header says where.

10. **No `__`-prefixed plumbing in the global namespace.** Framework
    internals are `private`/`protected` and reached with `send` by the
    framework's own entry points. Note mruby does not enforce visibility
    (see findings.md), so this is documentation, but keep it accurate.

11. **C++ UI code lives in `jni/src/ui/`**, bindings in `jni/src/native/`.

## How to work (the owner's expectations)

12. **Ask before big or ambiguous changes; propose a plan first.** The owner
    likes to discuss design (they changed the plan several times for the
    better). For small, clear requests, just do it.

13. **Small diffs. Don't refactor what you weren't asked to.** Mention other
    problems you notice instead of fixing them silently.

14. **Verify, and say exactly what was and wasn't verified.** There is no
    device or docker here; the NDK build and on-device behaviour are always
    "not verified". Never claim a test proved something it didn't (see the
    `public_send` mistake in findings.md).

15. **Never leave the tree dirty.** Failed experiments that touch submodules
    (e.g. building desktop SDL) must be reverted. Test files go in `/tmp`,
    or are deleted afterwards.
