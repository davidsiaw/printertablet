# A map of the bed: a grey square with a 10x10 grid, and a small orange
# square where the toolhead is. Y grows towards the back of the bed, i.e. up
# the screen.
class BedMap < Group
  GRID = 10
  GREY = 0x484848
  GRID_COLOR = 0x707070
  MARKER_COLOR = 0xff8c1a

  def initialize(size:, **props)
    super(**props)
    @size = size
    add Rect.new(w: size, h: size, color: GREY)

    line = [size / 300, 1].max
    (0..GRID).each do |i|
      at = i * (size - line) / GRID
      add Rect.new(x: at, w: line, h: size, color: GRID_COLOR)   # vertical
      add Rect.new(y: at, w: size, h: line, color: GRID_COLOR)   # horizontal
    end

    @marker_size = [size / 25, 6].max
    @marker = add Rect.new(w: @marker_size, h: @marker_size, color: MARKER_COLOR, visible: false)
  end

  # Moves the marker to the toolhead's position (a toolhead hash from the
  # printer's status), or hides it if nil.
  def show_toolhead(toolhead)
    fractions = toolhead && bed_fractions(toolhead)
    unless fractions
      @marker.hide
      return
    end
    fx, fy = fractions
    travel = @size - @marker_size
    @marker.move((fx * travel).to_i, ((1 - fy) * travel).to_i)
    @marker.show
  end

  private

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
