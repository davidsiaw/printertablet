# While the printer is printing, this covers every control (all of the
# screen but the status bar) and says so; the controls come back when the
# print is paused, finished or cancelled. Pause runs Klipper's PAUSE.
#
#   PrintingModal.new(printer, height: Screen.height - StatusBar.height)
class PrintingModal < Group
  BACKDROP = 0x000000
  TEXT = 0xdddddd
  ORANGE = 0xff8c1a
  ORANGE_DOWN = 0xffa94d
  ERROR = 0xe04040

  def initialize(printer, height:, **props)
    super(visible: false, **props)
    @printer = printer

    # Covers the controls, and keeps taps from reaching them.
    backdrop = add Rect.new(w: Screen.width, h: height, color: BACKDROP, alpha: 0.85)
    backdrop.on_touch_down { |_, _| }

    big = height / 6
    add TextBox.new(y: height / 2 - big * 2, w: Screen.width, h: big, text: "Printing", size: big * 7 / 10,
                    text_color: TEXT)
    button_w = Screen.width / 4
    button_h = height / 9
    @pause = Button.new(x: (Screen.width - button_w) / 2, y: height / 2, w: button_w, h: button_h,
                        text: "Pause", size: button_h * 4 / 10, color: ORANGE, pressed_color: ORANGE_DOWN) { pause }
    add @pause
    @message = add TextBox.new(y: height / 2 + button_h + button_h / 3, w: Screen.width, h: button_h / 2,
                               size: button_h / 4, text: "", text_color: ERROR)

    update
    printer.on_change { update }
  end

  private

  def pause
    return unless @printer.pause
    @pausing = true
    @message.text = ""
    update
  end

  def update
    printing = @printer.printing?
    if printing && !visible && parent
      # Starting: close any keypad left open (e.g. while paused; it would
      # come back after the next pause), and go on top of everything.
      parent.children.select { |c| c.is_a?(KeypadModal) }.each(&:close)
      parent.add(self)
      @message.text = ""   # an old refusal isn't news
    end
    self.visible = printing
    busy = @printer.busy?
    @pause.text = @pausing && busy ? "Pausing..." : "Pause"
    @pause.enabled = !busy
    return unless @pausing && !busy
    @pausing = false
    # Klipper's messages can have more lines of explanation: the first says it.
    @message.text = @printer.last_error ? @printer.last_error.split("\n").first : ""
  end
end
