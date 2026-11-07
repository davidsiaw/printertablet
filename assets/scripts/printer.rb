# The UI's view of the printer. The actual talking to Moonraker happens in two
# workers, so the screen never waits on the network:
#
#   printer/status_worker.rb   polls Moonraker, reports the status on change
#   printer/gcode_worker.rb    runs G-code (homing can take minutes)
#
#   printer = Printer.new("10.0.0.5", 7125)
#   printer.on_change { redraw }       # status changed, or a command started/ended
#   printer.send_gcode("GANTRY")       # false if a command is already running
class Printer
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
