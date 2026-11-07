# The panel on the left: X/Y/Z boxes on top, the bed map below, and
#   - while the printer isn't homed (after M84, or a restart): a big orange
#     HOME button over the map that runs GANTRY (HOMING... while it runs)
#   - while homed: a slider under the map that turns the motors off (M84)
# While homed, tapping X, Y or Z opens a keypad to move the toolhead there,
# and tapping the bed map moves it there in X and Y.
#
#   ToolheadPanel.new(printer, x: 26, y: 26, height: 680, gap: 13)
class ToolheadPanel < Group
  GREY = 0x484848
  ORANGE = 0xff8c1a
  TEXT = 0xdddddd

  # How wide it came out (the bed map is square, so it depends on the height).
  attr_reader :width

  def initialize(printer, height:, gap:, **props)
    super(**props)
    @printer = printer

    box_h = height / 9
    slider_h = box_h
    size = height - box_h - slider_h - 2 * gap   # the bed map is square
    @width = size
    map_y = box_h + gap
    box_w = (size - 2 * gap) / 3

    @boxes = %w[X Y Z].each_with_index.map do |axis, i|
      box = add TextBox.new(x: i * (box_w + gap), w: box_w, h: box_h, color: GREY,
                            size: box_h * 4 / 10, text_color: TEXT)
      box.on_tap { open_keypad(axis) if printer.homed? }
      box
    end

    @map = add BedMap.new(y: map_y, size: size)
    @map.on_pick do |x, y|
      @map.show_target(x, y) if printer.homed? && printer.move_xy(x, y)   # busy: refused
    end

    @home = add TextBox.new(y: map_y, w: size, h: size, color: ORANGE, size: size / 7, visible: false)
    @home.on_tap { printer.send_gcode("GANTRY") if @home.visible }   # busy: send_gcode refuses

    # (A do...end block here would go to `add`, not `new`.)
    @slider = ConfirmSlider.new(y: map_y + size + gap, w: size, h: slider_h,
                                text: "slide to turn off motors", visible: false) { printer.send_gcode("M84") if printer.homed? }
    add @slider

    update
    printer.on_change { update }
  end

  private

  # A keypad to type where `axis` should go; checks it against the printer's
  # travel limits before moving.
  def open_keypad(axis)
    min, max = @printer.axis_range(axis)
    hint = format("now %.2f   range %g - %g", @printer.position(axis), min, max)
    KeypadModal.open(title: "Move #{axis} to", hint: hint, action: "Move") do |value|
      next format("%s must be between %g and %g", axis, min, max) unless value >= min && value <= max
      next "not homed" unless @printer.homed?
      next "printer is busy, try again" unless @printer.move_to(axis, value)
      nil
    end
  end

  def update
    toolhead = @printer.toolhead
    position = toolhead && toolhead["position"]
    %w[X Y Z].each_with_index do |axis, i|
      @boxes[i].text = position ? format("%s %.2f", axis, position[i]) : "#{axis}  -"
    end

    homed = @printer.homed?
    @home.visible = !toolhead.nil? && !homed   # unknown toolhead: just the empty map
    @home.text = @printer.busy? ? "HOMING..." : "HOME"
    @map.show_toolhead(homed ? toolhead : nil)
    @map.hide_target if !@printer.busy? && @printer.last_error   # Klipper refused the move
    @slider.visible = homed
  end
end
