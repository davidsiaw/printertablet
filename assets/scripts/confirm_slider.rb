# Slide to confirm: a track with a hint, and an orange knob. Drag the knob all
# the way to the right and let go to run the block; let go anywhere short of
# the end and it springs back without doing anything.
#
#   ConfirmSlider.new(w: 400, h: 60, text: "slide to turn off motors") { send_m84 }
class ConfirmSlider < Group
  TRACK_COLOR = 0x484848
  HINT_COLOR = 0x999999
  KNOB_COLOR = 0xff8c1a

  def initialize(w:, h:, text:, **props, &on_confirm)
    raise ArgumentError, "ConfirmSlider needs a block (what to do when confirmed)" unless on_confirm
    super(**props)
    @on_confirm = on_confirm
    @travel = w - h
    size = h * 4 / 10

    add Rect.new(w: w, h: h, color: TRACK_COLOR)
    add TextBox.new(x: h, w: w - h, h: h, size: size, text: text, text_color: HINT_COLOR)
    @knob = add TextBox.new(w: h, h: h, size: size, text: "\u00BB", color: KNOB_COLOR)   # »

    @knob.on_touch_down do |x, _|
      @grab_x = x
    end
    @knob.on_touch_move do |x, _|
      @knob.x = (x - @grab_x).clamp(0, @travel) if @grab_x
    end
    @knob.on_touch_up do |_, _|
      confirmed = @grab_x && @knob.x >= @travel
      @grab_x = nil
      @knob.x = 0   # springs back
      @on_confirm.call if confirmed
    end
  end
end
