# Building blocks for the screen. Ruby creates nodes and sets their
# properties; C++ (jni/src/ui/) keeps them and draws them every frame.
# Change a property and the next frame shows it, e.g.
#
#   marker = Rect.new(x: 10, y: 10, w: 20, h: 20, color: 0xff8c1a)
#   marker.x = 200
#
# Positions are in screen pixels, relative to the parent Group. Colours are
# 0xRRGGBB, or nil for none.
#
# One class per file in lib/ui/; this file loads them and holds the
# top-level helpers and the functions C++ calls.

load_script "lib/ui/node.rb"        # base class: load first
load_script "lib/ui/group.rb"
load_script "lib/ui/rect.rb"
load_script "lib/ui/text_box.rb"
load_script "lib/ui/screen.rb"
load_script "lib/ui/timer.rb"

# Runs the block every `seconds` (from now). Returns the Timer (#cancel).
def every(seconds, &block)
  Timer.new(seconds, true, block)
end

# Runs the block once, `seconds` from now. Returns the Timer (#cancel).
def after(seconds, &block)
  Timer.new(seconds, false, block)
end

# ---- called by C++ -------------------------------------------------------
# The rest of Native::UI is C++ (jni/src/native/ui.cpp); these two are the
# other direction, so they're Ruby. Errors are logged and swallowed so one bad
# handler can't stop the app. They reach private methods with `send`: they're
# the framework's own way in.

module Native
  module UI
    # When the earliest timer is due (see Native::UI.wake_at).
    def self.frame(now)
      Timer.send(:run_due, now)
    rescue Exception => e
      Native::Log.error(e)
    end

    # A touch on node `id`; kind is 0 down, 1 move, 2 up.
    def self.touch(id, kind, x, y)
      node = Node.find(id)
      node.send(:handle_touch, kind, x, y) if node
    rescue Exception => e
      Native::Log.error(e)
    end
  end
end
