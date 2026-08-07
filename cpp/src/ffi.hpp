#pragma once
#include "value.hpp"
#include <functional>

// ---------------------------------------------------------------------
// Native FFI: functions the C++ host registers so RoseGold scripts can call
// into the engine (getInput, moveTo, playSound, ...). The registry is consulted
// by the type checker (signatures), the compiler (emits Op::NATIVE), and the VM
// (dispatch). Types are given by name: Int/Float/String/Bool/Void/List/Map/Any
// or a class name (checked loosely as Any).
// ---------------------------------------------------------------------
using NativeFn = std::function<Value(std::vector<Value>&)>;

struct NativeSig { std::vector<std::string> params; std::string ret = "Void"; bool variadic = false; };
struct NativeEntry { std::string name; NativeSig sig; NativeFn fn; };

struct NativeRegistry {
    std::vector<NativeEntry> entries;
    std::map<std::string, int> index;
    void add(const std::string& name, NativeSig sig, NativeFn fn) {
        index[name] = (int)entries.size();
        entries.push_back({name, std::move(sig), std::move(fn)});
    }
    int find(const std::string& name) const { auto it = index.find(name); return it == index.end() ? -1 : it->second; }
};
