module behavior

# A game "behavior" script driven by a C++ host. It calls engine functions
# (engine_log / input_axis / move_by) that the host registered via the FFI, and
# the host calls update(dt) every frame. Module globals below are the component's
# state -- they persist across frames because the host reuses one runtime.

var x = 0.0
var vx = 0.0
var announced = false

fn ready():
    engine_log("behavior ready")

fn update(dt: Float):
    var axis = input_axis()               # ask the engine for input
    vx = vx + axis * 20.0 * dt            # accelerate
    vx = vx * 0.9                         # friction
    x = x + vx * dt
    move_by(vx * dt, 0.0)                 # tell the engine to move us
    if x > 5.0 && !announced:
        engine_log("crossed x = 5")
        announced = true
