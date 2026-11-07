# Worker: keeps track of the printer through Moonraker, and tells the main VM
# whenever anything changes. Started by Printer (printer.rb) with the
# printer's "host" and "port".
#
# Each reply is the whole status:
#   "connection"  "connecting", "connected" or "disconnected"
#   "klippy"      Klipper's state, e.g. "ready" (when connected)
#   "version"     Moonraker's version (when connected)
#   "error"       why not connected (when disconnected)
#   "toolhead"    nil, or (while Klipper is ready) "position" [x, y, z, e],
#                 "axis_minimum"/"axis_maximum" [x, y, z, e], "homed_axes" e.g. "xyz"

load_script "lib/worker.rb"

INFO_INTERVAL = 2.0        # /server/info
TOOLHEAD_INTERVAL = 0.25   # the toolhead, while Klipper is ready
REQUEST_TIMEOUT = 3.0

BASE = "http://#{Worker.args["host"]}:#{Worker.args["port"]}"

def get(path)
  Native::Http.request("GET", BASE + path, REQUEST_TIMEOUT)
end

# Connection state from /server/info.
def poll_info
  status, body = get("/server/info")
  case status
  when 200
    result = JSON.parse(body)["result"]
    { "connection" => "connected",
      "klippy" => result["klippy_state"] || "unknown",
      "version" => result["moonraker_version"] || "" }
  when 401, 403
    { "connection" => "disconnected",
      "error" => "not authorised - add this device to trusted_clients in moonraker.conf" }
  else
    { "connection" => "disconnected", "error" => "HTTP #{status}" }
  end
rescue JSON::ParserError, NoMethodError, TypeError
  { "connection" => "disconnected", "error" => "unexpected reply (is this Moonraker?)" }
rescue => e
  { "connection" => "disconnected", "error" => e.message }
end

# The toolhead, or nil if it can't be read.
def poll_toolhead
  status, body = get("/printer/objects/query?toolhead=position,axis_minimum,axis_maximum,homed_axes")
  return nil unless status == 200
  toolhead = JSON.parse(body)["result"]["status"]["toolhead"]
  ok = %w[position axis_minimum axis_maximum].all? { |k| toolhead[k].is_a?(Array) && toolhead[k].size >= 3 }
  ok ? toolhead : nil
rescue => e
  nil
end

status = { "connection" => "connecting" }
sent = nil
last_info = nil

until Worker.stopping?
  started = Time.now.to_f
  if last_info.nil? || started - last_info >= INFO_INTERVAL
    status = poll_info
    last_info = started
  end

  ready = status["connection"] == "connected" && status["klippy"] == "ready"
  status["toolhead"] = ready ? poll_toolhead : nil

  unless status == sent   # only tell the main VM about changes
    Worker.reply(status)
    sent = status.dup
  end

  # Wait for the next poll. Nothing is sent to this worker, so this is just a
  # sleep that ends early when the app quits.
  interval = ready ? TOOLHEAD_INTERVAL : INFO_INTERVAL
  wait = interval - (Time.now.to_f - started)
  Worker.receive(timeout: wait) if wait > 0
end
