# The bar along the bottom of the screen: a coloured dot and the state of
# the connection to Moonraker. Redraws whenever the Printer changes.
class StatusBar < Group
  GREEN  = 0x40d060
  YELLOW = 0xe0c030
  RED    = 0xe04040

  def self.height
    [Screen.height / 18, 28].max
  end

  def initialize(printer)
    h = StatusBar.height
    super(x: 0, y: Screen.height - h)
    @printer = printer
    @address = "#{printer.host}:#{printer.port}"

    pad = h / 3
    size = h * 6 / 10
    add Rect.new(w: Screen.width, h: h, color: 0x202020)
    @dot = add TextBox.new(x: pad, w: size, h: h, size: size, text: "\u25CF")   # ●
    @label = add TextBox.new(x: pad + size + pad / 2, w: Screen.width, h: h, size: size,
                             align: :left, text_color: 0xdddddd)

    refresh
    printer.on_change { refresh }
  end

  def refresh
    text, color = describe
    @label.text = text
    @dot.text_color = color
  end

  def describe
    case @printer.connection
    when "connected"
      text = "#{@address}   Connected   Klipper: #{@printer.klippy}"
      text += "   Moonraker #{@printer.version}" unless @printer.version.to_s.empty?
      [text, @printer.ready? ? GREEN : YELLOW]
    when "disconnected"
      ["#{@address}   Disconnected: #{@printer.error} (retrying)", RED]
    else
      ["#{@address}   Connecting...", YELLOW]
    end
  end
end
