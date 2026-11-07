# A container: children are positioned relative to it and drawn in the
# order they were added (later ones on top). A Group has no look of its own;
# give it w/h only if it should receive touches itself.
class Group < Node
  attr_reader :children

  def initialize(**props)
    @children = []
    super("group", **props)
  end

  # Adds a node (moving it from any other group) and returns it.
  def add(node)
    node.parent.children.delete(node) if node.parent
    @children << node
    node.attach_to(self)
    Native::UI.add(@id, node.id)
    node
  end

  def remove(node)
    if @children.delete(node)
      node.attach_to(nil)
      Native::UI.remove(node.id)
    end
    node
  end

  def each_node(&block)
    yield self
    @children.dup.each { |c| c.each_node(&block) }
  end
end
