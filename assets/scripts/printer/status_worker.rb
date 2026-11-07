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
#   "temperatures" nil, or (while Klipper is ready) every temperature Klipper
#                 reports, extruder and heater_bed first:
#                 [{ "name" => "extruder", "temperature" => 214.8, "target" => 215.0 },
#                  { "name" => "temperature_sensor chamber", "temperature" => 31.2,
#                    "target" => nil }, ...]   target: heaters only; rounded to 0.1
#   "fans"        nil, or (while Klipper is ready) every fan, part fan first:
#                 [{ "name" => "fan", "speed" => 0.5 }, ...]   speed 0..1, rounded to 0.01
#   "print_state" nil, or (while Klipper is ready) print_stats' state:
#                 "standby", "printing", "paused", "complete", "cancelled", "error"
#   "heater_limits" nil, or (while Klipper is ready) each heater's allowed
#                 targets from the printer config: { "extruder" => [0, 300], ... }
#                 (min_temp, max_temp; a target of 0, off, is always allowed)

load_script "lib/worker.rb"
load_script "lib/url.rb"

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

MAIN_SENSORS = %w[extruder heater_bed]
FAN_KINDS = %w[fan heater_fan controller_fan fan_generic temperature_fan]

# Every temperature sensor Klipper has (heaters included), main ones first:
# [[name, heater?], ...], or nil if it can't be read.
def poll_sensors
  status, body = get("/printer/objects/query?heaters=available_sensors,available_heaters")
  return nil unless status == 200
  heaters = JSON.parse(body)["result"]["status"]["heaters"]
  names = heaters["available_sensors"] || []
  main = MAIN_SENSORS.select { |n| names.include?(n) }
  (main + (names - main).sort).map { |n| [n, (heaters["available_heaters"] || []).include?(n)] }
rescue => e
  nil
end

# min_temp/max_temp of each heater, from the printer config:
# { name => [min, max] }, or nil if it can't be read. (The whole config comes
# back, so this is only asked for when Klipper becomes ready.)
def poll_heater_limits(sensors)
  status, body = get("/printer/objects/query?configfile=settings")
  return nil unless status == 200
  settings = JSON.parse(body)["result"]["status"]["configfile"]["settings"]
  limits = {}
  sensors.each do |name, heater|
    section = heater && settings[name]
    limits[name] = [section["min_temp"].to_f, section["max_temp"].to_f] if section && section["max_temp"]
  end
  limits
rescue => e
  nil
end

# Every fan Klipper has, part fan ("fan") first, or nil if it can't be read.
def poll_fans
  status, body = get("/printer/objects/list")
  return nil unless status == 200
  names = JSON.parse(body)["result"]["objects"].select { |n| FAN_KINDS.include?(n.split(" ").first) }
  (names.include?("fan") ? ["fan"] : []) + (names - ["fan"]).sort
rescue => e
  nil
end

# The toolhead, temperatures, fans and print state, in one request:
# [toolhead, temperatures, fans, print state], each nil if it can't be read.
def poll_printer(sensors, fans)
  # Each object once, with every attribute we want from it (a temperature_fan
  # is both a sensor and a fan).
  wanted = { "toolhead" => %w[position axis_minimum axis_maximum homed_axes], "print_stats" => %w[state] }
  sensors.each { |name, _| (wanted[name] ||= []).concat(%w[temperature target]) }
  fans.each { |name| (wanted[name] ||= []) << "speed" }
  query = wanted.map { |name, attributes| "#{URL.encode(name)}=#{attributes.join(",")}" }.join("&")
  status, body = get("/printer/objects/query?" + query)
  return [nil, nil, nil, nil] unless status == 200
  objects = JSON.parse(body)["result"]["status"]
  [toolhead_from(objects["toolhead"]), temperatures_from(objects, sensors), fans_from(objects, fans),
   (objects["print_stats"] || {})["state"]]
rescue => e
  [nil, nil, nil, nil]
end

def fans_from(objects, fans)
  fans.map do |name|
    object = objects[name]
    next nil unless object && object["speed"]
    { "name" => name, "speed" => object["speed"].to_f.round(2) }
  end.compact
end

def toolhead_from(toolhead)
  ok = toolhead && %w[position axis_minimum axis_maximum].all? { |k| toolhead[k].is_a?(Array) && toolhead[k].size >= 3 }
  ok ? toolhead : nil
end

def temperatures_from(objects, sensors)
  sensors.map do |name, heater|
    object = objects[name]
    next nil unless object && object["temperature"]
    { "name" => name,
      "temperature" => object["temperature"].to_f.round(1),   # rounded: fewer changes to report
      "target" => heater ? object["target"].to_f.round(1) : nil }
  end.compact
end

status = { "connection" => "connecting" }
sent = nil
last_info = nil
sensors = nil   # these are re-read whenever Klipper becomes ready (its
fans = nil      # config may have changed)
limits = nil

until Worker.stopping?
  started = Time.now.to_f
  if last_info.nil? || started - last_info >= INFO_INTERVAL
    status = poll_info
    last_info = started
  end

  ready = status["connection"] == "connected" && status["klippy"] == "ready"
  sensors = fans = limits = nil unless ready
  sensors ||= poll_sensors if ready
  fans ||= poll_fans if ready
  limits ||= poll_heater_limits(sensors) if ready && sensors
  status["heater_limits"] = limits
  status["toolhead"], status["temperatures"], status["fans"], status["print_state"] =
    ready ? poll_printer(sensors || [], fans || []) : [nil, nil, nil, nil]

  took = Time.now.to_f - started
  puts format("Status worker: poll took %d ms", took * 1000) if took > 0.5   # diagnostics

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
