// Negative FFI test: a script declares a native the host never registers, so
// rt.load() must reject it at LINK time (not crash mid-run). Proves that
// `extern` declarations are verified against the registry before execution.
#include "../src/runtime.hpp"
#include <iostream>

int main() {
    Runtime rt;
    // Register host_add, but deliberately NOT host_missing (which linkfail.rg declares).
    rt.registerNative("host_add", {"Int", "Int"}, "Int",
        [](std::vector<Value>& a) -> Value { return Value{std::get<int64_t>(a[0]) + std::get<int64_t>(a[1])}; });

    if (rt.load("cpp/embed/linkfail.rg")) {
        std::cout << "ERROR: load unexpectedly succeeded\n";
        return 1;
    }
    std::cout << "load rejected (as expected):\n" << rt.error;
    return 0;
}
