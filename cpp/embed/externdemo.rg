module externdemo

# An `extern:` block declares the natives the HOST provides. RoseGold type-checks
# calls against these signatures (no host needed for `--check`); at load the host
# runtime link-checks them against its registry, then binds each by name.
extern:
    fn host_log(msg: String) -> Void
    fn host_add(a: Int, b: Int) -> Int
    fn host_now() -> Float

fn main():
    host_log("script started")
    var sum = host_add(20, 22)
    host_log("20 + 22 = " + str(sum))
    print("host_now() =", host_now())
