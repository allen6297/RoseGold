module entities

# A component the host instantiates per entity and ticks each frame. `Node` is
# an OPAQUE foreign type: the host creates the handle in C++, the script stores
# and passes it back to node_move, but can never inspect or construct it.
extern:
    type Node
    fn node_move(n: Node, dx: Float, dy: Float) -> Void

class Mover:
    var node: Node
    var speed: Float
    init(node: Node, speed: Float):
        self.node = node
        self.speed = speed
    fn update(self, dt: Float):
        node_move(self.node, self.speed * dt, 0.0)
