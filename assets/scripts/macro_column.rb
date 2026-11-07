# A narrow column of buttons, each running a Klipper macro (or any G-code):
#
#   MacroColumn.new(printer, [["Park", "PARK"]], x: 1100, y: 26, w: 154, button_h: 78, gap: 13)
#
# While a macro runs its button reads e.g. "Park..."; while the printer is
# busy with any command, all the buttons are dimmed and ignore taps.
#
# While a print is paused, an orange Resume button appears below the macros.
class MacroColumn < Group
  GREY = 0x484848
  GREY_DOWN = 0x6a6a6a
  ORANGE = 0xff8c1a
  ORANGE_DOWN = 0xffa94d

  # macros: [[label, gcode], ...], top to bottom.
  def initialize(printer, macros, w:, button_h:, gap:, **props)
    super(**props)
    @printer = printer
    @buttons = macros.each_with_index.map do |(label, gcode), i|
      button = Button.new(y: i * (button_h + gap), w: w, h: button_h, text: label, size: button_h * 4 / 10,
                          color: GREY, pressed_color: GREY_DOWN) { run(i, gcode) }
      add button
    end
    @labels = macros.map(&:first)
    @resume = Button.new(y: macros.size * (button_h + gap), w: w, h: button_h, text: "Resume",
                         size: button_h * 4 / 10, color: ORANGE, pressed_color: ORANGE_DOWN,
                         visible: false) { @printer.resume }
    add @resume

    update
    printer.on_change { update }
  end

  private

  def run(index, gcode)
    @running = index if @printer.send_gcode(gcode)
    update
  end

  def update
    @running = nil unless @printer.busy?
    @buttons.each_with_index do |button, i|
      button.text = i == @running ? "#{@labels[i]}..." : @labels[i]
      button.enabled = !@printer.busy?
    end
    @resume.visible = @printer.paused?
    @resume.enabled = !@printer.busy?
  end
end
