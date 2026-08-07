module entities

# A component the host instantiates per entity and ticks each frame. `node` is
# an OPAQUE engine handle (created in C++, stored here, and passed back to the
# engine's node_move native) -- the script never inspects it, just moves it.
class Mover:
    var node
    var speed: Float
    init(node, speed: Float):
        self.node = node
        self.speed = speed
    func update(self, dt: Float):
        node_move(self.node, self.speed * dt, 0.0)
