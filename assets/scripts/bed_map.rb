# A map of the bed: a grey square with a 10x10 grid, and a small orange
# square where the toolhead is. Y grows towards the back of the bed, i.e. up
# the screen.
#
#   map.on_pick { |x, y| ... }   # tapped: the bed position there, in mm
#   map.show_target(x, y)        # a faint square where it's going, until the
#                                # toolhead gets within 1 mm of it
class BedMap < Group
  GRID = 10
  GREY = 0x484848
  GRID_COLOR = 0x707070
  MARKER_COLOR = 0xff8c1a
  TARGET_ALPHA = 0.45

  def initialize(size:, **props)
    super(**props)
    @size = size
    background = add Rect.new(w: size, h: size, color: GREY)
    background.on_touch_up do |x, y|
      pick(x - screen_x, y - screen_y) if @on_pick && background.contains?(x, y)
    end

    line = [size / 300, 1].max
    (0..GRID).each do |i|
      at = i * (size - line) / GRID
      add Rect.new(x: at, w: line, h: size, color: GRID_COLOR)   # vertical
      add Rect.new(y: at, w: size, h: line, color: GRID_COLOR)   # horizontal
    end

    @marker_size = [size / 25, 6].max
    @target = add Rect.new(w: @marker_size * 3 / 2, h: @marker_size * 3 / 2, color: MARKER_COLOR,
                           alpha: TARGET_ALPHA, visible: false)
    @marker = add Rect.new(w: @marker_size, h: @marker_size, color: MARKER_COLOR, visible: false)
  end

  # Runs the block with the bed position (mm) when the map is tapped, while
  # it knows the printer's travel limits.
  def on_pick(&block)
    @on_pick = block
    self
  end

  # A faint, bigger square at a bed position (mm), e.g. where a move is going.
  def show_target(x, y)
    return unless @limits
    min, max = @limits
    fx = ((x - min[0]) / (max[0] - min[0]).to_f).clamp(0.0, 1.0)
    fy = ((y - min[1]) / (max[1] - min[1]).to_f).clamp(0.0, 1.0)
    travel = @size - @marker_size
    half = (@target.w - @marker_size) / 2
    @target.move((fx * travel).to_i - half, ((1 - fy) * travel).to_i - half)
    @target.show
    @target_mm = [x, y]
  end

  def hide_target
    @target.hide
    @target_mm = nil
  end

  # Moves the marker to the toolhead's position (a toolhead hash from the
  # printer's status), or hides it if nil.
  def show_toolhead(toolhead)
    fractions = toolhead && bed_fractions(toolhead)
    unless fractions
      @marker.hide
      @limits = nil
      hide_target
      return
    end
    @limits = [toolhead["axis_minimum"], toolhead["axis_maximum"]]
    pos = toolhead["position"]
    hide_target if @target_mm && (pos[0] - @target_mm[0]).abs < 1 && (pos[1] - @target_mm[1]).abs < 1
    fx, fy = fractions
    travel = @size - @marker_size
    @marker.move((fx * travel).to_i, ((1 - fy) * travel).to_i)
    @marker.show
  end

  private

  # A tap at (lx, ly) on the map: the inverse of where the marker is drawn
  # (the marker's centre is the toolhead), clamped to the bed.
  def pick(lx, ly)
    return unless @limits
    min, max = @limits
    travel = (@size - @marker_size).to_f
    fx = ((lx - @marker_size / 2.0) / travel).clamp(0.0, 1.0)
    fy = 1 - ((ly - @marker_size / 2.0) / travel).clamp(0.0, 1.0)
    x = min[0] + fx * (max[0] - min[0])
    y = min[1] + fy * (max[1] - min[1])
    @on_pick.call((x * 10).round / 10.0, (y * 10).round / 10.0)   # to 0.1 mm
  end

  # How far across the bed the toolhead is in x and y, each 0..1; nil if the
  # printer's travel limits are unusable.
  def bed_fractions(toolhead)
    pos, min, max = toolhead["position"], toolhead["axis_minimum"], toolhead["axis_maximum"]
    [0, 1].map do |axis|
      span = max[axis] - min[axis]
      return nil unless span > 0
      ((pos[axis] - min[axis]) / span.to_f).clamp(0.0, 1.0)
    end
  end
end
