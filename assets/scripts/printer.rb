# The UI's view of the printer. The actual talking to Moonraker happens in two
# workers, so the screen never waits on the network:
#
#   printer/status_worker.rb   polls Moonraker, reports the status on change
#   printer/gcode_worker.rb    runs G-code (homing can take minutes)
#
#   printer = Printer.new("10.0.0.5", 7125)
#   printer.on_change { redraw }       # status changed, or a command started/ended
#   printer.send_gcode("GANTRY")       # false if a command is already running
#   printer.move_to("X", 120)          # absolute move, same rules as send_gcode
class Printer
  # mm/min for move_to
  MOVE_SPEED = { "X" => 6000, "Y" => 6000, "Z" => 600 }

  attr_reader :host, :port

  # Latest status from status_worker.rb (see there for the fields).
  attr_reader :status

  # Why the last command failed, or nil.
  attr_reader :last_error

  def initialize(host, port)
    @host = host
    @port = port
    @status = { "connection" => "connecting" }
    @busy = false
    @listeners = []

    args = { "host" => host, "port" => port }
    @status_worker = Worker.start("printer/status_worker.rb", args)
                           .on_message { |status| @status = status; changed }
                           .on_exit { |error| worker_ended("status", error) }
    @gcode_worker = Worker.start("printer/gcode_worker.rb", args)
                          .on_message { |result| command_done(result) }
                          .on_exit { |error| worker_ended("G-code", error) }
  end

  def on_change(&block)
    @listeners << block
    self
  end

  def connection;  @status["connection"]; end
  def klippy;      @status["klippy"];     end
  def version;     @status["version"];    end
  def error;       @status["error"];      end
  def toolhead;    @status["toolhead"];   end

  # Every temperature Klipper reports, extruder and bed first; [] if unknown.
  # See status_worker.rb for the fields.
  def temperatures
    @status["temperatures"] || []
  end

  # Klipper's print_stats state ("printing", "paused", "standby", ...), or nil.
  def print_state; @status["print_state"]; end
  def printing?;   print_state == "printing"; end
  def paused?;     print_state == "paused";   end

  # Klipper's PAUSE / RESUME (its pause_resume module or your macros).
  # Same rules as send_gcode.
  def pause;  send_gcode("PAUSE");  end
  def resume; send_gcode("RESUME"); end

  # Every fan, part fan first; [] if unknown. See status_worker.rb.
  def fans
    @status["fans"] || []
  end

  # A heater's allowed targets from the printer config, [min, max], or nil
  # if unknown. (0, off, is always allowed too.)
  def heater_limits(name)
    (@status["heater_limits"] || {})[name]
  end

  # Sets a heater's target in °C (0: off). `name` as Klipper calls the
  # object: "extruder", "heater_bed", "heater_generic chamber".
  def set_heater(name, target)
    heater = name.split(" ").last   # SET_HEATER_TEMPERATURE wants "chamber", not "heater_generic chamber"
    send_gcode("SET_HEATER_TEMPERATURE HEATER=#{heater} TARGET=#{format("%.1f", target)}")
  end

  # Fans whose speed can be set: the part fan and fan_generic ones (the
  # others are run by Klipper itself).
  def self.settable_fan?(name)
    name == "fan" || name.start_with?("fan_generic ")
  end

  # Sets a fan's speed, 0..100 %.
  def set_fan(name, percent)
    if name == "fan"
      send_gcode("M106 S#{(percent * 255 / 100.0).round}")   # (not * 2.55: 127.4999.. for 50%)
    else
      send_gcode("SET_FAN_SPEED FAN=#{name.split(" ").last} SPEED=#{format("%.2f", percent / 100.0)}")
    end
  end

  def ready?
    connection == "connected" && klippy == "ready"
  end

  # Homed on every axis (false if the toolhead isn't known).
  def homed?
    axes = toolhead && toolhead["homed_axes"]
    !!axes && %w[x y z].all? { |a| axes.include?(a) }
  end

  # A command is running.
  def busy?
    @busy
  end

  # Where the toolhead is on an axis ("X", "Y" or "Z"), or nil if unknown.
  def position(axis)
    i = %w[X Y Z].index(axis)
    pos = toolhead && toolhead["position"]
    i && pos ? pos[i] : nil
  end

  # The axis's travel limits from the printer config, [min, max], or nil.
  def axis_range(axis)
    i = %w[X Y Z].index(axis)
    return nil unless i && toolhead
    [toolhead["axis_minimum"][i], toolhead["axis_maximum"][i]]
  end

  # Moves the toolhead to `value` (mm) on one axis, at MOVE_SPEED. In G-code
  # coordinates, which equal the toolhead position shown unless there's a
  # G92 or other offset. Returns false (doing nothing) if busy.
  def move_to(axis, value)
    speed = MOVE_SPEED[axis] or raise ArgumentError, "axis must be X, Y or Z"
    send_gcode("G90\nG1 #{axis}#{format("%.3f", value)} F#{speed}")
  end

  # Moves the toolhead to (x, y) mm, at the X/Y MOVE_SPEED. Same rules as
  # move_to.
  def move_xy(x, y)
    send_gcode("G90\nG1 X#{format("%.3f", x)} Y#{format("%.3f", y)} F#{MOVE_SPEED["X"]}")
  end

  # Pushes `length` mm of filament through the extruder (negative: pulls it
  # back) at `speed` mm/s, like Mainsail: relative extrusion (M83), then G1.
  # Klipper refuses below min_extrude_temp, or beyond
  # max_extrude_only_distance (50 mm by default): see last_error.
  def extrude(length, speed)
    send_gcode("M83\nG1 E#{format("%.3f", length)} F#{format("%.1f", speed * 60)}")
  end

  # Runs G-code in the background. Returns false (doing nothing) if a command
  # is still running.
  def send_gcode(gcode)
    return false if @busy || !@gcode_worker.running?
    @busy = true
    @last_error = nil
    @gcode_worker.post("gcode" => gcode)
    changed
    true
  end

  private

  def command_done(result)
    @busy = false
    @last_error = result["error"]
    changed
  end

  def worker_ended(which, error)
    puts "Printer: #{which} worker ended#{error ? ": #{error}" : ""}"
    @busy = false if which == "G-code"
    changed
  end

  def changed
    @listeners.each(&:call)
  end
end
