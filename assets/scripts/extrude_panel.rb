# Extrude and retract filament:
#
#   [ Length   50 mm  ]    tap a box to type a new value (keypad)
#   [ Speed  10 mm/s  ]
#   [Retract][Extrude ]
#   Extruding...            or Klipper's error, e.g. "Extrude below minimum temp"
#
# The length and speed are kept until the app restarts.
#
#   ExtrudePanel.new(printer, x: 860, y: 400, w: 234, row_h: 78, gap: 13)
class ExtrudePanel < Group
  GREY = 0x484848
  GREY_DOWN = 0x6a6a6a
  ORANGE = 0xff8c1a
  ORANGE_DOWN = 0xffa94d
  TEXT = 0xdddddd
  HINT = 0x999999
  ERROR = 0xe04040

  DEFAULT_LENGTH = 50.0   # mm
  DEFAULT_SPEED = 10.0    # mm/s
  LENGTH_RANGE = [0.1, 1000.0]
  SPEED_RANGE = [0.1, 100.0]
  MESSAGE_LINES = 3

  def initialize(printer, w:, row_h:, gap:, **props)
    super(**props)
    @printer = printer
    @length = DEFAULT_LENGTH
    @speed = DEFAULT_SPEED
    size = row_h * 35 / 100

    @length_box = add TextBox.new(w: w, h: row_h, color: GREY, size: size, text_color: TEXT)
    @length_box.on_tap { edit("Extrude length (mm)", @length, LENGTH_RANGE) { |v| @length = v } }
    @speed_box = add TextBox.new(y: row_h + gap, w: w, h: row_h, color: GREY, size: size, text_color: TEXT)
    @speed_box.on_tap { edit("Extrude speed (mm/s)", @speed, SPEED_RANGE) { |v| @speed = v } }

    y = 2 * (row_h + gap)
    half = (w - gap) / 2
    @buttons = [
      Button.new(y: y, w: half, h: row_h, text: "Retract", size: size,
                 color: GREY, pressed_color: GREY_DOWN) { run(-1, "Retracting...") },
      Button.new(x: half + gap, y: y, w: w - half - gap, h: row_h, text: "Extrude", size: size,
                 color: ORANGE, pressed_color: ORANGE_DOWN) { run(1, "Extruding...") },
    ]
    @buttons.each { |b| add b }

    # Up to MESSAGE_LINES lines, so Klipper's longer errors stay readable.
    line_h = row_h * 3 / 10
    @message_size = line_h * 8 / 10
    @message_width = w
    @message_lines = (0...MESSAGE_LINES).map do |i|
      add TextBox.new(y: y + row_h + gap / 2 + i * line_h, w: w, h: line_h, size: @message_size,
                      align: :left, text_color: HINT)
    end

    show_values
    show_message("", HINT)
    update
    printer.on_change { update }
  end

  private

  def show_values
    @length_box.text = format("Length  %g mm", @length)
    @speed_box.text = format("Speed  %g mm/s", @speed)
  end

  # A keypad for one of the values; the block stores it.
  def edit(title, current, range, &store)
    min, max = range
    KeypadModal.open(title: title, hint: format("now %g   range %g - %g", current, min, max)) do |value|
      next format("must be between %g and %g", min, max) unless value >= min && value <= max
      store.call(value)
      show_values
      nil
    end
  end

  def run(direction, doing)
    return unless @printer.extrude(direction * @length, @speed)
    @pending = true
    show_message(doing, HINT)
  end

  def update
    busy = @printer.busy?
    @buttons.each { |b| b.enabled = !busy }
    return unless @pending && !busy
    @pending = false
    error = @printer.last_error
    # Klipper's messages can have more lines of explanation: the first says it.
    error ? show_message(error.split("\n").first, ERROR) : show_message("", HINT)
  end

  def show_message(text, color)
    lines = wrap(text, (@message_width / (@message_size * 0.55)).to_i)
    @message_lines.each_with_index do |box, i|
      box.text = lines[i] || ""
      box.text_color = color
    end
  end

  # Splits text into lines of about `chars` characters at spaces. (A guess
  # at the width: TextBox shrinks a line that turns out too long.) The last
  # line takes whatever is left.
  def wrap(text, chars)
    lines = [""]
    text.split(" ").each do |word|
      if lines.last.empty? || lines.last.size + 1 + word.size <= chars || lines.size == MESSAGE_LINES
        lines[-1] = lines.last.empty? ? word : "#{lines.last} #{word}"
      else
        lines << word
      end
    end
    lines
  end
end
