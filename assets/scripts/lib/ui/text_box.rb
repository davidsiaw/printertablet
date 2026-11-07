# A single line of text in a box. The text is vertically centred and
# aligned :left, :center (default) or :right, `padding` pixels in from the
# sides. `color` is the box's background (nil: none).
class TextBox < Node
  attr_reader :text, :color, :text_color, :size, :align, :padding

  def initialize(**props)
    super("text", **{ text_color: 0xffffff, size: 20 }.merge(props))
  end

  def text=(v);       @text = v.to_s; Native::UI.set_str(@id, "text", @text); end
  def color=(v);      @color = v; set_color("color", v); end
  def text_color=(v); @text_color = v; Native::UI.set_num(@id, "text_color", v); end
  def size=(v);       @size = v; Native::UI.set_num(@id, "size", v); end
  def align=(v);      @align = v; Native::UI.set_str(@id, "align", v.to_s); end
  def padding=(v);    @padding = v; Native::UI.set_num(@id, "padding", v); end
end
