module externdemo

# `extern func` declares a native the HOST provides. RoseGold type-checks calls
# against these signatures (no host needed for `--check`); at runtime each binds
# by name to a function the host registered in its NativeRegistry.
extern func host_log(msg: String) -> Void
extern func host_add(a: Int, b: Int) -> Int
extern func host_now() -> Float

func main():
    host_log("script started")
    var sum = host_add(20, 22)
    host_log("20 + 22 = " + str(sum))
    print("host_now() =", host_now())
