module externdemo

# `extern fn` declares a native the HOST provides. RoseGold type-checks calls
# against these signatures (no host needed for `--check`); at runtime each binds
# by name to a function the host registered in its NativeRegistry.
extern fn host_log(msg: String) -> Void
extern fn host_add(a: Int, b: Int) -> Int
extern fn host_now() -> Float

fn main():
    host_log("script started")
    var sum = host_add(20, 22)
    host_log("20 + 22 = " + str(sum))
    print("host_now() =", host_now())
