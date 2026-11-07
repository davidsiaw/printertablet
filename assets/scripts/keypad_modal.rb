# A modal window with a numeric keypad, over a dimmed screen:
#
#   KeypadModal.open(title: "Move X to", hint: "range 0 - 300") do |value|
#     next "too far" if value > 300   # a message keeps it open and shows it
#     move(value)
#     nil                             # nil closes it
#   end
#
# Keys: 0-9, "." and backspace; Cancel and the action (`action:`, "Set" by
# default). Tapping outside the window cancels too.
class KeypadModal < Group
  BACKDROP = 0x000000
  WINDOW = 0x303030
  DISPLAY = 0x181818
  KEY = 0x484848
  KEY_DOWN = 0x6a6a6a
  ORANGE = 0xff8c1a
  ORANGE_DOWN = 0xffa94d
  TEXT = 0xdddddd
  HINT = 0x999999
  ERROR = 0xe04040
  MAX_LENGTH = 8
  BACKSPACE = "\u2190"   # ←

  # Builds one and puts it on top of the screen.
  def self.open(title:, hint: "", action: "Set", &on_enter)
    modal = new(title: title, hint: hint, action: action, &on_enter)
    Screen.shown.add(modal)
    modal
  end

  def initialize(title:, hint: "", action: "Set", &on_enter)
    raise ArgumentError, "KeypadModal needs a block (what to do with the number)" unless on_enter
    super()
    @on_enter = on_enter
    @hint_text = hint
    @text = ""

    # Covers the whole screen: dims it, and keeps taps from reaching it.
    backdrop = add Rect.new(w: Screen.width, h: Screen.height, color: BACKDROP, alpha: 0.6)
    backdrop.on_tap { close }

    key = Screen.height / 10     # key height
    key_w = key * 3 / 2          # wider than tall, so the hint line fits
    gap = key / 8
    pad = gap * 2
    width = 3 * key_w + 2 * gap
    title_h = key * 6 / 10
    hint_h = key / 2
    keys_h = 4 * key + 3 * gap
    height = pad + title_h + gap + key + gap + hint_h + gap + keys_h + gap + key + pad

    window = add Group.new(x: (Screen.width - width - 2 * pad) / 2, y: (Screen.height - height) / 2)
    frame = window.add Rect.new(w: width + 2 * pad, h: height, color: WINDOW)
    frame.on_touch_down { |_, _| }   # taps on the window itself don't cancel

    y = pad
    window.add TextBox.new(x: pad, y: y, w: width, h: title_h, text: title, size: title_h * 6 / 10,
                           align: :left, text_color: TEXT)
    y += title_h + gap
    @display = window.add TextBox.new(x: pad, y: y, w: width, h: key, color: DISPLAY, size: key * 55 / 100,
                                      align: :right, padding: gap * 2, text_color: 0xffffff)
    y += key + gap
    @hint = window.add TextBox.new(x: pad, y: y, w: width, h: hint_h, size: hint_h * 6 / 10, align: :left)
    y += hint_h + gap

    key_size = key * 4 / 10
    [%w[7 8 9], %w[4 5 6], %w[1 2 3], [".", "0", BACKSPACE]].each_with_index do |row, r|
      row.each_with_index do |label, c|
        window.add Button.new(x: pad + c * (key_w + gap), y: y + r * (key + gap), w: key_w, h: key, text: label,
                              size: key_size, color: KEY, pressed_color: KEY_DOWN) { press(label) }
      end
    end
    y += keys_h + gap

    half = (width - gap) / 2
    window.add Button.new(x: pad, y: y, w: half, h: key, text: "Cancel", size: key_size,
                          color: KEY, pressed_color: KEY_DOWN) { close }
    window.add Button.new(x: pad + half + gap, y: y, w: width - half - gap, h: key, text: action, size: key_size,
                          color: ORANGE, pressed_color: ORANGE_DOWN) { enter }

    show_hint(@hint_text)
    refresh
  end

  def close
    destroy
  end

  private

  def press(label)
    case label
    when BACKSPACE
      @text = @text[0..-2]
    when "."
      @text = (@text.empty? ? "0" : @text) + "." unless @text.include?(".") || @text.size >= MAX_LENGTH
    else
      @text += label if @text.size < MAX_LENGTH
    end
    show_hint(@hint_text)
    refresh
  end

  def enter
    if @text.empty?
      show_hint("type a number first", ERROR)
      return
    end
    error = @on_enter.call(@text.to_f)
    error ? show_hint(error, ERROR) : close
  end

  def refresh
    @display.text = @text
  end

  def show_hint(text, color = HINT)
    @hint.text = text
    @hint.text_color = color
  end
end
