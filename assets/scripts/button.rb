# A TextBox you can press: lights up while touched, runs the block when
# tapped (touched and let go on it). Disabled buttons are dimmed and ignore
# taps.
#
#   Button.new(x: 10, y: 10, w: 120, h: 60, text: "Park", size: 24,
#              color: 0x484848, pressed_color: 0x6a6a6a) { printer.send_gcode("PARK") }
class Button < TextBox
  attr_reader :enabled

  def initialize(color:, pressed_color:, **props, &on_press)
    raise ArgumentError, "Button needs a block (what to do when pressed)" unless on_press
    super(color: color, text_color: 0xffffff, **props)
    @up_color = color
    @enabled = true

    on_touch_down { |_, _| self.color = pressed_color if @enabled }
    on_touch_up { |_, _| self.color = @up_color }
    on_tap { on_press.call if @enabled }
  end

  def enabled=(v)
    @enabled = v
    self.alpha = v ? 1 : 0.4
  end
end
