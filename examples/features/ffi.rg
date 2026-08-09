module ffi

# Native FFI declarations: functions + an opaque foreign type a C++ host provides.
# `Canvas` is a foreign handle — it can be passed to natives and stored, but never
# constructed or inspected in-script. There's no host attached when this file is
# run directly, so main() only prints; the declarations still type-check, and
# `area` below compiles against the foreign type (a real host would link them).
extern:
    type Canvas
    fn draw_line(c: Canvas, x1: Int, y1: Int, x2: Int, y2: Int) -> Void
    fn pixel_count(c: Canvas) -> Int

# Typed against the opaque Canvas (type-checked + compiled, not called here).
fn area(c: Canvas) -> Int:
    return pixel_count(c)

fn main():
    print("ffi: declared an opaque Canvas type + 2 native functions")
