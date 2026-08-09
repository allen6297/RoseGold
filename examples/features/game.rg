module game

# ---------------------------------------------------------------------
#  Game math in RoseGold: a Vec2 with operator overloading (+ - *) and a
#  length() via the math stdlib, driving a simple physics step. This is the
#  kind of per-frame logic a game engine would run as a script.
# ---------------------------------------------------------------------

class Vec2:
    var x: Float
    var y: Float
    init(x: Float, y: Float):
        self.x = x
        self.y = y
    fn add(self, o: Vec2) -> Vec2:
        return Vec2(self.x + o.x, self.y + o.y)
    fn sub(self, o: Vec2) -> Vec2:
        return Vec2(self.x - o.x, self.y - o.y)
    fn mul(self, s: Float) -> Vec2:                 # scale
        return Vec2(self.x * s, self.y * s)
    fn dot(self, o: Vec2) -> Float:
        return self.x * o.x + self.y * o.y
    fn length(self) -> Float:
        return sqrt(self.x * self.x + self.y * self.y)

# A physics body: position + velocity integrated each frame, bouncing off y = 0.
class Ball:
    var pos: Vec2
    var vel: Vec2
    init(pos: Vec2, vel: Vec2):
        self.pos = pos
        self.vel = vel
    fn update(self, dt: Float):
        var gravity = Vec2(0.0, -9.8)
        self.vel = self.vel + gravity * dt            # v += g * dt   (operator overloading)
        self.pos = self.pos + self.vel * dt           # p += v * dt
        if self.pos.y < 0.0:                          # bounce, with damping
            self.pos = Vec2(self.pos.x, 0.0)
            self.vel = Vec2(self.vel.x, abs(self.vel.y) * 0.7)

fn main():
    var ball = Ball(Vec2(0.0, 10.0), Vec2(3.0, 0.0))
    var dt = 0.1
    var frame = 0
    while frame < 24:
        ball.update(dt)
        if frame % 4 == 0:
            print("frame", frame, ": y =", round(ball.pos.y * 100.0), "/100   speed =", round(ball.vel.length() * 100.0), "/100")
        frame = frame + 1
    print("landed near y =", round(ball.pos.y * 100.0), "/100")
