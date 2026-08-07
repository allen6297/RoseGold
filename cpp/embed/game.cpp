// Opaque host handles + the component model. The engine owns Node objects and
// hands scripts OPAQUE handles to them; script `Mover` components (instantiated
// and ticked from C++) drive those nodes via the node_move native.
//
// Build: clang++ -std=c++17 -O2 -o cpp/embed/game cpp/embed/game.cpp
// Run:   ./cpp/embed/game [cpp/embed/entities.rg]
#include "../src/runtime.hpp"
#include <iostream>
#include <vector>

struct Node { std::string name; double x = 0, y = 0; };   // engine-owned scene object

int main(int argc, char** argv) {
    Runtime rt;
    std::vector<std::shared_ptr<Node>> scene;

    // node_move(node, dx, dy): the one native the scripts call. It recovers the
    // real engine Node from the opaque handle and moves it.
    rt.registerNative("node_move", {"Node", "Float", "Float"}, "Void", [](std::vector<Value>& a) -> Value {
        if (auto n = asHandle<Node>(a[0], "Node")) { n->x += asNum(a[1]); n->y += asNum(a[2]); }
        return Value{};
    });

    std::string script = argc > 1 ? argv[1] : "cpp/embed/entities.rg";
    if (!rt.load(script)) { std::cerr << "load error:\n" << rt.error; return 1; }

    auto spawn = [&](const std::string& name) {
        auto n = std::make_shared<Node>(); n->name = name; scene.push_back(n);
        return makeHandle("Node", n);   // hand the script an opaque handle
    };

    // Instantiate two script components, each bound to an engine node, different speeds.
    Value hero  = rt.newInstance("Mover", { spawn("hero"),  Value{3.0} });
    Value enemy = rt.newInstance("Mover", { spawn("enemy"), Value{-1.5} });

    std::cout << "== ticking 5 frames ==\n";
    for (int f = 0; f < 5; f++) {
        rt.callMethod(hero,  "update", { Value{0.1} });
        rt.callMethod(enemy, "update", { Value{0.1} });
    }
    for (auto& n : scene)
        std::cout << "  " << n->name << " moved to x = " << toStr(Value{n->x}) << "\n";
    return 0;
}
