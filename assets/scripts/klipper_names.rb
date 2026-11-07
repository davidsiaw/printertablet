# Readable names for Klipper objects:
#   "extruder" -> "Extruder", "extruder1" -> "Extruder 1", "heater_bed" -> "Bed",
#   "fan" -> "Part Fan", "temperature_sensor raspberry_pi" -> "Raspberry Pi",
#   "heater_fan hotend_fan" -> "Hotend Fan"
module KlipperNames
  def self.label(name)
    return "Bed" if name == "heater_bed"
    return "Part Fan" if name == "fan"
    words = name.split(" ")
    base = words.size > 1 ? words[1..-1].join(" ") : words[0]   # drop the kind
    base = base.sub("extruder", "extruder ") if base.start_with?("extruder") && base != "extruder"
    base.tr("_", " ").split(" ").map(&:capitalize).join(" ")
  end
end
