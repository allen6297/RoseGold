// Demonstrates `extern func` declarations: the script DECLARES native signatures
// (type-checked on its own), and the host binds implementations BY NAME. This is
// the Rust `extern` model — the interface lives in the script, the code in C++.
//
// Build: clang++ -std=c++17 -O2 -o cpp/embed/externdemo cpp/embed/externdemo.cpp
// Run:   ./cpp/embed/externdemo [cpp/embed/externdemo.rg]
#include "../src/runtime.hpp"
#include <iostream>

int main(int argc, char** argv) {
    Runtime rt;

    // Implementations for the natives the script declares via `extern func`.
    rt.registerNative("host_log", {"String"}, "Void", [](std::vector<Value>& a) -> Value {
        std::cout << "  [host] " << (a.empty() ? "" : toStr(a[0])) << "\n"; return Value{};
    });
    rt.registerNative("host_add", {"Int", "Int"}, "Int", [](std::vector<Value>& a) -> Value {
        return Value{(int64_t)(asNum(a[0]) + asNum(a[1]))};
    });
    rt.registerNative("host_now", {}, "Float", [](std::vector<Value>&) -> Value {
        return Value{42.5};   // fixed value for a deterministic demo
    });

    std::string script = argc > 1 ? argv[1] : "cpp/embed/externdemo.rg";
    if (!rt.load(script)) { std::cerr << "load error:\n" << rt.error; return 1; }
    rt.call("main");
    return 0;
}
