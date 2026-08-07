module coroutine

# ---------------------------------------------------------------------
#  Coroutines: functions that pause with `yield` and continue on resume(),
#  keeping their place and local state. This is how game scripts run logic
#  across frames -- the engine resume()s each coroutine once per frame.
# ---------------------------------------------------------------------

# A generator: produces a sequence of values, one per resume().
func fib_gen(n: Int):
    var a = 0
    var b = 1
    var i = 0
    while i < n:
        yield a
        var t = a + b
        a = b
        b = t
        i = i + 1

# A "wait N frames" helper. Because a yield inside a called function suspends
# the WHOLE coroutine, wait() composes: a routine can `wait(...)` between steps.
func wait(frames: Int):
    var i = 0
    while i < frames:
        yield 0
        i = i + 1

# A scripted behavior (think: an NPC routine or a cutscene) that unfolds over
# many frames. It keeps its position automatically across the waits.
func routine():
    print("  spawn")
    wait(2)
    print("  attack")
    wait(1)
    print("  retreat")

func main():
    print("fib via generator:")
    var g = coroutine(fib_gen, 8)          # args after the function are passed to it
    var out: List<Int> = []
    var k = 0
    while k < 8:
        push(out, resume(g))
        k = k + 1
    print(" ", out)

    print("frame-driven routine:")
    var c = coroutine(routine)
    var frame = 0
    while !done(c):
        print("frame", frame)
        resume(c)
        frame = frame + 1
