# The screen itself.
module Screen
  def self.width;  Native::UI.screen_width;  end
  def self.height; Native::UI.screen_height; end

  # Shows a node (usually a Group holding a whole scene) as the screen.
  def self.show(node)
    @shown = node
    Native::UI.show(node.id)
  end

  def self.shown
    @shown
  end
end
