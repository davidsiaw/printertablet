# Every fan the printer has, with its speed:
#
#   Part Fan        100%      orange while spinning
#   Hotend Fan        0%
#
# Fans whose speed can be set (the part fan, fan_generic) are lighter grey:
# tap one to set its speed with the keypad. The others are run by Klipper.
#
#   FanList.new(printer, x: 860, y: 26, w: 234, row_h: 46, gap: 13)
class FanList < Group
  DARK = 0x303030
  GREY = 0x484848   # tappable
  TEXT = 0xdddddd
  SPINNING = 0xff8c1a

  def initialize(printer, w:, row_h:, gap:, **props)
    super(**props)
    @printer = printer
    @w = w
    @row_h = row_h
    @gap = gap
    @rows = {}     # fan name => speed TextBox
    @names = []    # what the rows were built for

    update
    printer.on_change { update }
  end

  private

  def update
    fans = @printer.fans
    names = fans.map { |f| f["name"] }
    rebuild(names) unless names.empty? || names == @names   # keep old rows (with "-") while offline
    known = {}
    fans.each { |f| known[f["name"]] = f }
    @names.each do |name|
      fan = known[name]
      box = @rows[name]
      box.text = fan ? format("%d%%", (fan["speed"] * 100).round) : "-"
      box.text_color = fan && fan["speed"] > 0 ? SPINNING : TEXT
    end
  end

  def set_speed(name)
    fan = @printer.fans.find { |f| f["name"] == name }
    return unless fan   # not connected
    hint = format("now %d%%   range 0 - 100", (fan["speed"] * 100).round)
    KeypadModal.open(title: "#{KlipperNames.label(name)} speed (%)", hint: hint) do |value|
      next "must be between 0 and 100" unless value >= 0 && value <= 100
      next "printer is busy, try again" unless @printer.set_fan(name, value)
      nil
    end
  end

  def rebuild(names)
    children.dup.each(&:destroy)
    @rows = {}
    @names = names
    pad = @row_h / 4
    speed_w = @w * 35 / 100
    names.each_with_index do |name, i|
      y = i * (@row_h + @gap)
      settable = Printer.settable_fan?(name)
      background = add Rect.new(y: y, w: @w, h: @row_h, color: settable ? GREY : DARK)
      background.on_tap { set_speed(name) } if settable
      add TextBox.new(y: y, w: @w - speed_w, h: @row_h, size: @row_h / 2, text: KlipperNames.label(name),
                      align: :left, padding: pad, text_color: TEXT)
      @rows[name] = add TextBox.new(x: @w - speed_w, y: y, w: speed_w, h: @row_h, size: @row_h / 2,
                                    align: :right, padding: pad)
    end
  end
end
