# Worker: runs G-code on the printer, one command at a time. Started by
# Printer (printer.rb) with the printer's "host" and "port".
#
# Receives  { "gcode" => "G28" }
# Replies   { "gcode" => "G28", "ok" => true/false, "error" => nil or why }
#
# Moonraker only answers when the G-code has finished, and homing can take
# minutes, which is why this is a worker of its own: status updates carry on
# meanwhile in printer/status_worker.rb.

load_script "lib/worker.rb"
load_script "lib/url.rb"

COMMAND_TIMEOUT = 10 * 60.0

BASE = "http://#{Worker.args["host"]}:#{Worker.args["port"]}"

# Klipper's own error message from a failed request, if there is one.
def error_message(status, body)
  message = JSON.parse(body)["error"]["message"] rescue nil
  message ? message.to_s : "HTTP #{status}"
end

until Worker.stopping?
  msg = Worker.receive
  next unless msg && msg["gcode"]
  gcode = msg["gcode"]
  puts "Printer: sending #{gcode}"
  begin
    status, body = Native::Http.request("POST", "#{BASE}/printer/gcode/script?script=#{URL.encode(gcode)}",
                                        COMMAND_TIMEOUT)
    error = status == 200 ? nil : error_message(status, body)
  rescue => e
    error = e.message
  end
  puts "Printer: #{gcode} failed: #{error}" if error
  Worker.reply("gcode" => gcode, "ok" => error.nil?, "error" => error)
end
