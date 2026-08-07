#pragma once
#include "common.hpp"

// ---------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------
struct ListObj; struct Instance; struct VariantVal; struct Closure; struct MapObj; struct Coro;
using Value = std::variant<std::monostate, int64_t, double, bool, std::string,
                           std::shared_ptr<ListObj>, std::shared_ptr<Instance>,
                           std::shared_ptr<VariantVal>, std::shared_ptr<Closure>,
                           std::shared_ptr<MapObj>, std::shared_ptr<Coro>>;
struct ListObj { std::vector<Value> items; };
struct Instance { std::string cls; int clsIndex; std::map<std::string, Value> fields; };
struct VariantVal { std::string enumName, name; std::vector<Value> vals; };
struct Closure { int fn; std::vector<Value> upvals; };
struct MapObj { std::vector<std::pair<Value, Value>> items; };   // insertion-ordered

static bool isNum(const Value& v) { return std::holds_alternative<int64_t>(v) || std::holds_alternative<double>(v); }
static double asNum(const Value& v) {
    if (auto p = std::get_if<int64_t>(&v)) return (double)*p;
    if (auto p = std::get_if<double>(&v)) return *p;
    throw std::runtime_error("expected a number");
}
// Runtime type tag used for extension-method dispatch on non-object values.
static std::string typeTag(const Value& v) {
    if (std::holds_alternative<bool>(v)) return "Bool";
    if (std::holds_alternative<int64_t>(v)) return "Int";
    if (std::holds_alternative<double>(v)) return "Float";
    if (std::holds_alternative<std::string>(v)) return "String";
    if (std::holds_alternative<std::shared_ptr<ListObj>>(v)) return "List";
    if (std::holds_alternative<std::shared_ptr<MapObj>>(v)) return "Map";
    return "?";
}
static bool truthy(const Value& v) {
    if (auto p = std::get_if<bool>(&v)) return *p;
    if (std::holds_alternative<std::monostate>(v)) return false;
    if (auto p = std::get_if<int64_t>(&v)) return *p != 0;
    if (auto p = std::get_if<double>(&v)) return *p != 0.0;
    return true;
}
static std::string fmtDouble(double d) {
    if (std::isnan(d)) return "nan";
    if (std::isinf(d)) return d < 0 ? "-inf" : "inf";
    char buf[64]; auto r = std::to_chars(buf, buf + sizeof(buf), d); std::string s(buf, r.ptr);
    bool plain = true; for (char c : s) if (!(std::isdigit((unsigned char)c) || c == '-')) { plain = false; break; }
    if (plain) s += ".0";
    return s;
}
static std::string toStr(const Value& v) {
    if (std::holds_alternative<std::monostate>(v)) return "void";
    if (auto p = std::get_if<bool>(&v)) return *p ? "true" : "false";
    if (auto p = std::get_if<int64_t>(&v)) return std::to_string(*p);
    if (auto p = std::get_if<double>(&v)) return fmtDouble(*p);
    if (auto p = std::get_if<std::string>(&v)) return *p;
    if (auto p = std::get_if<std::shared_ptr<ListObj>>(&v)) {
        std::string s = "["; for (size_t i = 0; i < (*p)->items.size(); i++) { if (i) s += ", "; s += toStr((*p)->items[i]); } return s + "]";
    }
    if (auto p = std::get_if<std::shared_ptr<Instance>>(&v)) return "<" + (*p)->cls + ">";
    if (auto p = std::get_if<std::shared_ptr<VariantVal>>(&v)) {
        std::string s = (*p)->name;
        if (!(*p)->vals.empty()) { s += "("; for (size_t i = 0; i < (*p)->vals.size(); i++) { if (i) s += ", "; s += toStr((*p)->vals[i]); } s += ")"; }
        return s;
    }
    if (std::holds_alternative<std::shared_ptr<Closure>>(v)) return "<func>";
    if (std::holds_alternative<std::shared_ptr<Coro>>(v)) return "<coroutine>";
    if (auto p = std::get_if<std::shared_ptr<MapObj>>(&v)) {
        std::string s = "{";
        for (size_t i = 0; i < (*p)->items.size(); i++) { if (i) s += ", "; s += toStr((*p)->items[i].first) + ": " + toStr((*p)->items[i].second); }
        return s + "}";
    }
    return "?";
}

