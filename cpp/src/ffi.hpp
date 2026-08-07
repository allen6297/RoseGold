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

// Wrap a host object as an opaque handle Value (use a no-op deleter for a non-owning ref to engine-owned memory).
static Value makeHandle(const std::string& kind, std::shared_ptr<void> obj) {
    auto h = std::make_shared<Handle>(); h->kind = kind; h->obj = std::move(obj); return Value{h};
}
// Recover the host object from a handle Value (null if it isn't a handle of that kind).
template <class T> static std::shared_ptr<T> asHandle(const Value& v, const std::string& kind) {
    auto p = std::get_if<std::shared_ptr<Handle>>(&v);
    if (!p || !*p || (*p)->kind != kind) return nullptr;
    return std::static_pointer_cast<T>((*p)->obj);
}
