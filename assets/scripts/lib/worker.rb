# Workers: Ruby scripts that run on their own threads, each in its own VM,
# so they can block (on the network, say) without freezing the screen. They
# share nothing with the main VM: they talk through messages, which are plain
# data (hashes, arrays, strings, numbers, true/false/nil) sent as JSON. So a
# message's hash keys always arrive as strings, even if sent as symbols.
#
# In the main VM:
#
#   printer = Worker.start("printer/status_worker.rb", "host" => "10.0.0.5")
#   printer.on_message { |msg| ... }      # runs on the main thread
#   printer.on_exit { |error| ... }       # its script ended (error: nil if cleanly)
#   printer.post("gcode" => "G28")
#
# In the worker's script (which loads this file itself):
#
#   load_script "lib/worker.rb"
#   until Worker.stopping?
#     msg = Worker.receive(timeout: 0.25)   # nil if nothing came in time
#     Worker.reply("got" => msg) if msg
#   end
#
# Workers can block on the network (Native::Http) but can't touch the screen,
# and can't start workers of their own. When the app quits, Worker.stopping?
# turns true, receive returns nil and a request in progress is abandoned.
class Worker
  @@workers = {}

  # ---- in the main VM ----

  def self.start(script, args = {})
    new(script, args)
  end

  def self.find(id)
    @@workers[id]
  end

  attr_reader :script

  def initialize(script, args = {})
    @script = script
    @id = Native::Worker.start(script, JSON.generate(args))
    @running = true
    @@workers[@id] = self
  end

  def post(message)
    Native::Worker.post(@id, JSON.generate(message))
    self
  end

  def on_message(&block); @on_message = block; self; end
  def on_exit(&block);    @on_exit = block;    self; end

  def running?
    @running
  end

  # Asks the worker to stop: Worker.stopping? turns true for it. on_exit
  # still runs when it actually ends.
  def stop
    Native::Worker.stop(@id) if @running
  end

  # ---- inside a worker ----

  # The next message from the main VM. Waits up to `timeout` seconds (nil:
  # until one comes). nil on timeout or when stopping.
  def self.receive(timeout: nil)
    json = Native::Worker.receive(timeout ? timeout.to_f : -1.0)
    json && JSON.parse(json)
  end

  def self.reply(message)
    Native::Worker.reply(JSON.generate(message))
  end

  def self.stopping?
    Native::Worker.stopping?
  end

  # What Worker.start was given.
  def self.args
    @args ||= JSON.parse(Native::Worker.args)
  end

  private

  def deliver(json)
    @on_message&.call(JSON.parse(json))
  end

  def finish(error)
    @running = false
    @@workers.delete(@id)
    @on_exit&.call(error)
  end
end

# Called by C++ from the main loop (native/worker.cpp), only on frames where a
# worker has replied or ended. Errors are logged and swallowed.
module Native
  module Worker
    def self.deliver(id, json)
      worker = ::Worker.find(id)
      worker.send(:deliver, json) if worker
    rescue Exception => e
      Native::Log.error(e)
    end

    def self.finished(id, error)
      worker = ::Worker.find(id)
      worker.send(:finish, error) if worker
    rescue Exception => e
      Native::Log.error(e)
    end
  end
end
