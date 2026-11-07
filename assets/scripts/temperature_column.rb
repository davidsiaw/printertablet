# A column with every temperature the printer reports, a two-line row each
# (name, then the reading): the extruder and bed on top in big rows,
# everything else (other heaters, temperature sensors and fans) below in
# smaller ones.
#
#   Extruder
#           214.8° / 215°      heaters: current / target ("off" if 0),
#   Bed                        orange while a target is set
#             60.1° / 60°
#   Chamber
#               31.2°          sensors: current only
#
# Tap the extruder or bed (lighter grey: tappable) to set its target with the
# keypad. Text that doesn't fit is drawn smaller (TextBox shrinks to fit).
#   TemperatureColumn.new(printer, x: 574, y: 26, w: 494, main_h: 78, gap: 13)
class TemperatureColumn < Group
  MAIN = %w[extruder heater_bed]
  GREY = 0x484848
  DARK = 0x303030
  TEXT = 0xdddddd
  HEATING = 0xff8c1a

  def initialize(printer, w:, main_h:, gap:, **props)
    super(**props)
    @printer = printer
    @w = w
    @main_h = main_h
    @small_h = main_h * 7 / 10
    @gap = gap
    @rows = {}     # sensor name => value TextBox
    @names = []    # what the rows were built for

    update
    printer.on_change { update }
  end

  private

  def update
    temperatures = @printer.temperatures
    names = temperatures.map { |t| t["name"] }
    rebuild(names) unless names.empty? || names == @names   # keep old rows (with "-") while offline
    known = {}
    temperatures.each { |t| known[t["name"]] = t }
    @names.each do |name|
      t = known[name]
      box = @rows[name]
      box.text = t ? reading(t) : "-"
      box.text_color = t && t["target"].to_f > 0 ? HEATING : TEXT
    end
  end

  # A keypad for a heater's target. Klipper allows 0 (off) or min..max from
  # the printer config.
  def set_target(name)
    current = @printer.temperatures.find { |t| t["name"] == name }
    return unless current   # not connected
    label = KlipperNames.label(name)
    min, max = @printer.heater_limits(name)
    hint = format("now %.1f\u00B0   target %d\u00B0", current["temperature"], current["target"].to_f.round)
    hint += format("   max %g\u00B0", max) if max
    KeypadModal.open(title: "#{label} target (\u00B0C), 0 = off", hint: hint) do |value|
      if max && value != 0 && (value < min || value > max)
        next min > 0 ? format("0 (off) or %g - %g", min, max) : format("must be between 0 and %g", max)
      end
      next "printer is busy, try again" unless @printer.set_heater(name, value)
      nil
    end
  end

  def reading(t)
    text = format("%.1f\u00B0", t["temperature"])
    return text if t["target"].nil?
    target = t["target"].to_f
    text + (target > 0 ? format(" / %d\u00B0", target.round) : " / off")
  end

  # One row per sensor, main ones bigger.
  def rebuild(names)
    children.dup.each(&:destroy)
    @rows = {}
    @names = names
    y = 0
    names.each do |name|
      main = MAIN.include?(name)
      h = main ? @main_h : @small_h
      pad = @main_h / 6
      background = add Rect.new(y: y, w: @w, h: h, color: main ? GREY : DARK)
      background.on_tap { set_target(name) } if main
      add TextBox.new(y: y, w: @w, h: h * 4 / 10, size: h * (main ? 25 : 30) / 100, text: KlipperNames.label(name),
                      align: :left, padding: pad, text_color: TEXT)
      @rows[name] = add TextBox.new(y: y + h * 35 / 100, w: @w, h: h * 6 / 10, size: h * 4 / 10,
                                    align: :right, padding: pad)
      y += h + @gap
    end
  end
end
