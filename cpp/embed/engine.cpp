// A tiny "game engine" that embeds RoseGold: it registers native C++ functions
// (the FFI), loads a behavior script once, and ticks its update(dt) each frame.
// The engine owns the entity's position; the script drives it via move_by().
//
// Build: clang++ -std=c++17 -O2 -o cpp/embed/engine cpp/embed/engine.cpp
// Run:   ./cpp/embed/engine [cpp/embed/behavior.rg]
#include "../src/runtime.hpp"
#include <iostream>

int main(int argc, char** argv) {
    Runtime rt;
    const double dt = 0.1;
    double entityX = 0.0;   // host-owned engine state

    // Expose engine functions to scripts (the FFI). Signatures are checked by RoseGold.
    rt.registerNative("engine_log", {"String"}, "Void", [](std::vector<Value>& a) -> Value {
        std::cout << "  [engine] " << (a.empty() ? "" : toStr(a[0])) << "\n"; return Value{};
    });
    rt.registerNative("input_axis", {}, "Float", [](std::vector<Value>&) -> Value {
        return Value{1.0};   // pretend the player is holding "right"
    });
    rt.registerNative("move_by", {"Float", "Float"}, "Void", [&](std::vector<Value>& a) -> Value {
        entityX += asNum(a[0]); return Value{};
    });

    std::string script = argc > 1 ? argv[1] : "cpp/embed/behavior.rg";
    if (!rt.load(script)) { std::cerr << "load error:\n" << rt.error; return 1; }

    std::cout << "== engine boot ==\n";
    if (rt.has("ready")) rt.call("ready");

    for (int frame = 0; frame < 10; frame++) {
        rt.call("update", { Value{dt} });                       // host -> script, once per frame
        std::cout << "frame " << frame << ": entity x = " << toStr(Value{entityX}) << "\n";
    }
    std::cout << "== 10 frames ticked; final x = " << toStr(Value{entityX}) << " ==\n";
    return 0;
}
