# The base of everything on screen. See lib/ui.rb for the overview.
class Node
  attr_reader :id, :x, :y, :w, :h, :visible, :parent

  # Nodes by id, so C++ touch events can find their Ruby object.
  @@nodes = {}
  def self.find(id)
    @@nodes[id]
  end

  def initialize(kind, **props)
    @id = Native::UI.create(kind)
    @@nodes[@id] = self
    @x = @y = @w = @h = 0
    @visible = true
    props.each { |name, value| send("#{name}=", value) }
  end

  def x=(v);       @x = v; Native::UI.set_num(@id, "x", v); end
  def y=(v);       @y = v; Native::UI.set_num(@id, "y", v); end
  def w=(v);       @w = v; Native::UI.set_num(@id, "w", v); end
  def h=(v);       @h = v; Native::UI.set_num(@id, "h", v); end
  def visible=(v); @visible = v; Native::UI.set_num(@id, "visible", v ? 1 : 0); end

  # 0 (invisible) .. 1 (opaque) for everything this node draws, e.g. a
  # half-transparent backdrop. Doesn't affect its children.
  def alpha; @alpha || 1; end
  def alpha=(v);   @alpha = v; Native::UI.set_num(@id, "alpha", v); end

  # Where this node is on the screen (x and y are relative to the parent).
  def screen_x; (@parent ? @parent.screen_x : 0) + @x; end
  def screen_y; (@parent ? @parent.screen_y : 0) + @y; end

  def move(x, y)
    self.x = x
    self.y = y
  end

  def show; self.visible = true; end
  def hide; self.visible = false; end

  # Removes this node and everything in it from the screen for good.
  def destroy
    each_node { |n| @@nodes.delete(n.id) }
    @parent.children.delete(self) if @parent
    Native::UI.destroy(@id)
  end

  # Is the screen point inside this node? (Touch coordinates are screen points.)
  def contains?(sx, sy)
    Native::UI.contains(@id, sx, sy)
  end

  # Touch. The node that gets the touch-down also gets the moves and the
  # touch-up, even if the finger has left it. Blocks get screen x, y.
  def on_touch_down(&block); @on_touch_down = block; touchable!; self; end
  def on_touch_move(&block); @on_touch_move = block; touchable!; self; end
  def on_touch_up(&block);   @on_touch_up = block;   touchable!; self; end

  # Touched and let go inside this node.
  def on_tap(&block); @on_tap = block; touchable!; self; end

  def each_node
    yield self
  end

  protected

  # Group#add / #remove keep the child's parent up to date with this.
  def attach_to(parent)
    @parent = parent
  end

  private

  # For subclasses' colour setters (Rect#color=, TextBox#color=): nil is no colour.
  def set_color(name, value)
    Native::UI.set_num(@id, name, value.nil? ? -1 : value)
  end

  def touchable!
    Native::UI.set_num(@id, "touchable", 1)
  end

  # A touch event from C++ (see Native::UI.touch in lib/ui.rb); kind is 0 down,
  # 1 move, 2 up.
  def handle_touch(kind, x, y)
    case kind
    when 0 then @on_touch_down&.call(x, y)
    when 1 then @on_touch_move&.call(x, y)
    when 2
      @on_touch_up&.call(x, y)
      @on_tap&.call if @on_tap && contains?(x, y)
    end
  end
end
