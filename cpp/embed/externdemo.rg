module externdemo

# An `extern "host":` block declares the natives the HOST provides under the
# "host" library tag (they bind to registry keys host::host_log, ...). RoseGold
# type-checks calls against these signatures (no host needed for `--check`); at
# load the runtime link-checks them against its registry, then binds by tag+name.
extern "host":
    fn host_log(msg: String) -> Void
    fn host_add(a: Int, b: Int) -> Int
    fn host_now() -> Float

fn main():
    host_log("script started")
    var sum = host_add(20, 22)
    host_log("20 + 22 = " + str(sum))
    print("host_now() =", host_now())
