// Hot reload: recompile the script while the "game" keeps running, preserving
// module state. (Edit counter.rg between runs to see new code pick up the old
// state.)  Build: clang++ -std=c++17 -O2 -o cpp/embed/hotreload cpp/embed/hotreload.cpp
#include "../src/runtime.hpp"
#include <iostream>
int main(int argc, char** argv) {
    Runtime rt;
    std::string script = argc > 1 ? argv[1] : "cpp/embed/counter.rg";
    if (!rt.load(script)) { std::cerr << "load error:\n" << rt.error; return 1; }
    std::cout << "== running ==\n";
    rt.call("tick"); rt.call("tick"); rt.call("tick");
    rt.call("show");                          // counter = 3
    std::cout << "== hot reload (recompile, keep state) ==\n";
    if (!rt.reload()) { std::cerr << "reload error:\n" << rt.error; return 1; }
    rt.call("show");                          // counter = 3  (state preserved)
    rt.call("tick");
    rt.call("show");                          // counter = 4
    return 0;
}
