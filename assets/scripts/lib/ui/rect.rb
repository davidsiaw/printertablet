# A filled rectangle.
class Rect < Node
  attr_reader :color

  def initialize(**props)
    super("rect", **props)
  end

  def color=(v); @color = v; set_color("color", v); end
end
