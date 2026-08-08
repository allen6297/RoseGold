#pragma once
#include "compiler.hpp"

// ---------------------------------------------------------------------
// VM
// ---------------------------------------------------------------------
struct VMError : std::runtime_error { using std::runtime_error::runtime_error; };
// Deterministic PRNG (xorshift64) for the game stdlib; seedable via srandom() for reproducible runs.
static uint64_t g_rng = 88172645463325252ULL;
static uint64_t rngNext() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static double rngFloat() { return (double)(rngNext() >> 11) / 9007199254740992.0; }   // [0, 1)
static Value arith(Op op, const Value& a, const Value& b) {
    if (auto pa = std::get_if<Vec>(&a)) {   // vector arithmetic (component-wise; Vec*scalar and Vec/scalar)
        if (auto pb = std::get_if<Vec>(&b)) { Vec r; r.n = pa->n > pb->n ? pa->n : pb->n; switch (op) { case Op::ADD: r.x = pa->x + pb->x; r.y = pa->y + pb->y; r.z = pa->z + pb->z; break; case Op::SUB: r.x = pa->x - pb->x; r.y = pa->y - pb->y; r.z = pa->z - pb->z; break; case Op::MUL: r.x = pa->x * pb->x; r.y = pa->y * pb->y; r.z = pa->z * pb->z; break; default: throw VMError("unsupported vector operator"); } return Value{r}; }
        if (isNum(b)) { double s = asNum(b); Vec r = *pa; if (op == Op::MUL) { r.x *= s; r.y *= s; r.z *= s; } else if (op == Op::DIV) { r.x /= s; r.y /= s; r.z /= s; } else throw VMError("a vector and a scalar support only * and /"); return Value{r}; }
        throw VMError("cannot combine a vector with that value");
    }
    if (isNum(a) && std::holds_alternative<Vec>(b)) { if (op != Op::MUL) throw VMError("scalar and vector support only *"); double s = asNum(a); Vec r = std::get<Vec>(b); r.x *= s; r.y *= s; r.z *= s; return Value{r}; }
    if (op == Op::ADD && (std::holds_alternative<std::string>(a) || std::holds_alternative<std::string>(b))) return Value{toStr(a) + toStr(b)};
    if (!isNum(a) || !isNum(b)) throw VMError("cannot apply arithmetic to non-numbers");
    bool bi = std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b);
    if (bi && op != Op::DIV) { int64_t x = std::get<int64_t>(a), y = std::get<int64_t>(b); switch (op) { case Op::ADD: return Value{x + y}; case Op::SUB: return Value{x - y}; case Op::MUL: return Value{x * y}; case Op::MOD: if (y == 0) throw VMError("mod by zero"); return Value{x % y}; default: break; } }
    double x = asNum(a), y = asNum(b); switch (op) { case Op::ADD: return Value{x + y}; case Op::SUB: return Value{x - y}; case Op::MUL: return Value{x * y}; case Op::DIV: if (y == 0.0) throw VMError("division by zero"); return Value{x / y}; case Op::MOD: return Value{std::fmod(x, y)}; default: throw VMError("bad op"); }
}
static bool valueEq(const Value& a, const Value& b) { if (isNum(a) && isNum(b)) return asNum(a) == asNum(b); return a == b; }
struct Frame { CFunc* fn; int ip; int base; };
struct Handler { int catchIp; size_t frameDepth; size_t stackSize; };
// A coroutine keeps its own value/frame/handler stacks so it can be paused mid-execution and resumed.
struct Coro { Value fn; std::vector<Value> args; std::vector<Value> st; std::vector<Frame> frames; std::vector<Handler> handlers; int status = 0; };  // 0 fresh, 1 suspended, 2 dead
struct YieldSignal { Value value; };   // thrown by `yield`, caught by resume() (unwinds only the coroutine's runLoop)

// Debug hook: when set (by the DAP server in dap.hpp), runLoop calls it before
// executing each instruction, giving the debugger a chance to stop for a
// breakpoint or a step. Null in normal runs — one predictable branch per instr.
static std::function<void(Program&, std::vector<Value>&, std::vector<Value>&, std::vector<Frame>&)> g_dbgHook;

// Runs `frames` (with value stack `st`) to completion, returning the top-of-stack result;
// throws YieldSignal on `yield`. Re-entrant: resume() calls it on a coroutine's own stacks.
static Value runLoop(Program& prog, std::vector<Value>& globals, std::vector<Value>& st, std::vector<Frame>& frames, std::vector<Handler>& handlers) {
    auto call = [&](int fi, int argc) { CFunc* fn = &prog.funcs[fi]; int base = (int)st.size() - argc; while ((int)st.size() < base + fn->nlocals) st.push_back(Value{}); frames.push_back({fn, 0, base}); };
    auto asList = [&](const Value& v) -> ListObj& { if (auto p = std::get_if<std::shared_ptr<ListObj>>(&v)) return **p; throw VMError("expected a list"); };
    auto asInst = [&](const Value& v) -> Instance& { if (auto p = std::get_if<std::shared_ptr<Instance>>(&v)) return **p; throw VMError("expected an object"); };
    auto asMap = [&](const Value& v) -> MapObj& { if (auto p = std::get_if<std::shared_ptr<MapObj>>(&v)) return **p; throw VMError("expected a map"); };
    while (!frames.empty()) {
        Frame& fr = frames.back();
        if (g_dbgHook) g_dbgHook(prog, globals, st, frames);   // debugger stop-point (before the instruction runs)
        Instr in = fr.fn->code[fr.ip++];
        switch (in.op) {
            case Op::CONST: st.push_back(prog.consts[in.a]); break;
            case Op::PUSHNIL: st.push_back(Value{}); break;
            case Op::LOAD: st.push_back(st[fr.base + in.a]); break;
            case Op::STORE: st[fr.base + in.a] = st.back(); st.pop_back(); break;
            case Op::LOADG: st.push_back(globals[in.a]); break;
            case Op::STOREG: globals[in.a] = st.back(); st.pop_back(); break;
            case Op::POP: st.pop_back(); break;
            case Op::NEG: { Value v = st.back(); st.pop_back(); if (std::holds_alternative<int64_t>(v)) st.push_back(Value{-std::get<int64_t>(v)}); else st.push_back(Value{-asNum(v)}); break; }
            case Op::NOT: { Value v = st.back(); st.pop_back(); st.push_back(Value{!truthy(v)}); break; }
            case Op::ADD: case Op::SUB: case Op::MUL: case Op::DIV: case Op::MOD: { Value b = st.back(); st.pop_back(); Value a = st.back(); st.pop_back(); st.push_back(arith(in.op, a, b)); break; }
            case Op::LT: case Op::LE: case Op::GT: case Op::GE: {
                Value b = st.back(); st.pop_back(); Value a = st.back(); st.pop_back(); bool r;
                if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
                    const std::string& x = std::get<std::string>(a); const std::string& y = std::get<std::string>(b);
                    r = in.op == Op::LT ? x < y : in.op == Op::LE ? x <= y : in.op == Op::GT ? x > y : x >= y;
                } else { double x = asNum(a), y = asNum(b); r = in.op == Op::LT ? x < y : in.op == Op::LE ? x <= y : in.op == Op::GT ? x > y : x >= y; }
                st.push_back(Value{r}); break;
            }
            case Op::EQ: case Op::NE: { Value b = st.back(); st.pop_back(); Value a = st.back(); st.pop_back(); bool r = valueEq(a, b); st.push_back(Value{in.op == Op::EQ ? r : !r}); break; }
            case Op::JUMP: fr.ip = in.a; break;
            case Op::JFALSE: { Value v = st.back(); st.pop_back(); if (!truthy(v)) fr.ip = in.a; break; }
            case Op::JTRUE: { Value v = st.back(); st.pop_back(); if (truthy(v)) fr.ip = in.a; break; }
            case Op::MAKELIST: { auto lo = std::make_shared<ListObj>(); lo->items.resize(in.a); for (int k = in.a - 1; k >= 0; k--) { lo->items[k] = st.back(); st.pop_back(); } st.push_back(Value{lo}); break; }
            case Op::IGET: { Value idx = st.back(); st.pop_back(); Value lst = st.back(); st.pop_back(); if (!std::holds_alternative<int64_t>(idx)) throw VMError("index must be an integer"); int64_t k = std::get<int64_t>(idx); if (auto sp = std::get_if<std::string>(&lst)) { if (k < 0 || k >= (int64_t)sp->size()) throw VMError("string index out of range"); st.push_back(Value{std::string(1, (*sp)[k])}); break; } ListObj& L = asList(lst); if (k < 0 || k >= (int64_t)L.items.size()) throw VMError("list index " + std::to_string(k) + " out of range"); st.push_back(L.items[k]); break; }
            case Op::ISET: { Value val = st.back(); st.pop_back(); Value idx = st.back(); st.pop_back(); Value lst = st.back(); st.pop_back(); if (!std::holds_alternative<int64_t>(idx)) throw VMError("index must be an integer"); int64_t k = std::get<int64_t>(idx); ListObj& L = asList(lst); if (k < 0 || k >= (int64_t)L.items.size()) throw VMError("list index " + std::to_string(k) + " out of range"); L.items[k] = val; break; }
            case Op::NEWOBJ: { auto inst = std::make_shared<Instance>(); inst->clsIndex = in.a; inst->cls = prog.classes[in.a].name; for (auto& fn : prog.classes[in.a].fieldNames) inst->fields[fn] = Value{}; st.push_back(Value{inst}); break; }
            case Op::GETPROP: { Value o = st.back(); st.pop_back(); const std::string& name = std::get<std::string>(prog.consts[in.a]); if (auto pv = std::get_if<Vec>(&o)) { st.push_back(Value{name == "x" ? pv->x : name == "y" ? pv->y : name == "z" ? pv->z : 0.0}); break; } Instance& I = asInst(o); auto it = I.fields.find(name); if (it == I.fields.end()) throw VMError("'" + I.cls + "' has no member '" + name + "'"); st.push_back(it->second); break; }
            case Op::SETPROP: { Value val = st.back(); st.pop_back(); Value o = st.back(); st.pop_back(); asInst(o).fields[std::get<std::string>(prog.consts[in.a])] = val; break; }
            case Op::INVOKE: { int argc = in.b; const std::string& name = std::get<std::string>(prog.consts[in.a]); Value o = st[st.size() - argc - 1];
                if (auto p = std::get_if<std::shared_ptr<Instance>>(&o)) { auto& ms = prog.classes[(*p)->clsIndex].methods; auto it = ms.find(name); if (it == ms.end()) throw VMError("'" + (*p)->cls + "' has no method '" + name + "'"); call(it->second, argc + 1); break; }
                std::string tag = typeTag(o); auto te = prog.extensions.find(tag);   // extension method on a primitive/List/Map
                if (te != prog.extensions.end()) { auto it = te->second.find(name); if (it != te->second.end()) { call(it->second, argc + 1); break; } }
                throw VMError("'" + tag + "' has no method '" + name + "'"); }
            case Op::MKVARIANT: { auto vv = std::make_shared<VariantVal>(); vv->enumName = prog.variants[in.a].enumName; vv->name = prog.variants[in.a].name; vv->vals.resize(in.b); for (int k = in.b - 1; k >= 0; k--) { vv->vals[k] = st.back(); st.pop_back(); } st.push_back(Value{vv}); break; }
            case Op::ISVARIANT: { Value v = st.back(); st.pop_back(); const std::string& name = std::get<std::string>(prog.consts[in.a]); bool r = false; if (auto p = std::get_if<std::shared_ptr<VariantVal>>(&v)) r = ((*p)->name == name); st.push_back(Value{r}); break; }
            case Op::VGET: { Value v = st.back(); st.pop_back(); auto p = std::get_if<std::shared_ptr<VariantVal>>(&v); if (!p) throw VMError("not a variant"); st.push_back((*p)->vals[in.a]); break; }
            case Op::MKCLOSURE: { auto cl = std::make_shared<Closure>(); cl->fn = in.a; cl->upvals.resize(in.b); for (int k = in.b - 1; k >= 0; k--) { cl->upvals[k] = st.back(); st.pop_back(); } st.push_back(Value{cl}); break; }
            case Op::CALLV: {
                int argc = in.a; size_t cpos = st.size() - argc - 1; Value cv = st[cpos];
                auto cl = std::get_if<std::shared_ptr<Closure>>(&cv); if (!cl) throw VMError("value is not callable");
                std::vector<Value> args(st.begin() + cpos + 1, st.end()); st.resize(cpos);
                for (auto& u : (*cl)->upvals) st.push_back(u); for (auto& a : args) st.push_back(a);
                CFunc* fn = &prog.funcs[(*cl)->fn]; int nu = (int)(*cl)->upvals.size(); int base = (int)st.size() - (nu + argc);
                while ((int)st.size() < base + fn->nlocals) st.push_back(Value{}); frames.push_back({fn, 0, base}); break;
            }
            case Op::SETUP_TRY: handlers.push_back({in.a, frames.size(), st.size()}); break;
            case Op::POP_TRY: handlers.pop_back(); break;
            case Op::RAISE: { Value v = st.back(); st.pop_back(); if (handlers.empty()) throw VMError("uncaught error: " + toStr(v)); Handler h = handlers.back(); handlers.pop_back(); frames.resize(h.frameDepth); st.resize(h.stackSize); frames.back().ip = h.catchIp; st.push_back(v); break; }
            case Op::BUILTIN: {
                int argc = in.b;
                if (in.a == 0) { std::string out; for (int k = 0; k < argc; k++) { if (k) out += " "; out += toStr(st[st.size() - argc + k]); } st.resize(st.size() - argc); std::cout << out << "\n"; st.push_back(Value{}); }
                else if (in.a == 1) { Value v = st.back(); st.pop_back(); if (auto sp = std::get_if<std::string>(&v)) st.push_back(Value{(int64_t)sp->size()}); else if (auto mp = std::get_if<std::shared_ptr<MapObj>>(&v)) st.push_back(Value{(int64_t)(*mp)->items.size()}); else st.push_back(Value{(int64_t)asList(v).items.size()}); }
                else if (in.a == 2) { Value v = st.back(); st.pop_back(); if (!std::holds_alternative<int64_t>(v)) throw VMError("range() needs an integer"); auto lo = std::make_shared<ListObj>(); for (int64_t k = 0; k < std::get<int64_t>(v); k++) lo->items.push_back(Value{(int64_t)k}); st.push_back(Value{lo}); }
                else if (in.a == 3) { Value x = st.back(); st.pop_back(); Value lv = st.back(); st.pop_back(); asList(lv).items.push_back(x); st.push_back(Value{}); }
                else if (in.a == 4) { Value lv = st.back(); st.pop_back(); ListObj& L = asList(lv); if (L.items.empty()) throw VMError("pop() from empty list"); Value r = L.items.back(); L.items.pop_back(); st.push_back(r); }
                else if (in.a == 5) { Value x = st.back(); st.pop_back(); st.push_back(Value{toStr(x)}); }
                else if (in.a == 6) { Value x = st.back(); st.pop_back(); auto s = std::get_if<std::string>(&x); if (!s || s->empty()) throw VMError("ord() needs a non-empty string"); st.push_back(Value{(int64_t)(unsigned char)(*s)[0]}); }
                else if (in.a == 7) { Value x = st.back(); st.pop_back(); if (!std::holds_alternative<int64_t>(x)) throw VMError("chr() needs an integer"); st.push_back(Value{std::string(1, (char)std::get<int64_t>(x))}); }
                else if (in.a == 8) { Value ev = st.back(); st.pop_back(); Value sv2 = st.back(); st.pop_back(); Value sv = st.back(); st.pop_back(); const std::string& s = std::get<std::string>(sv); int64_t a = std::get<int64_t>(sv2), b = std::get<int64_t>(ev), n = (int64_t)s.size(); if (a < 0) a = 0; if (a > n) a = n; if (b < 0) b = 0; if (b > n) b = n; if (a > b) a = b; st.push_back(Value{s.substr(a, b - a)}); }
                else if (in.a == 9) { Value sepv = st.back(); st.pop_back(); Value sv = st.back(); st.pop_back(); const std::string& s = std::get<std::string>(sv); const std::string& sep = std::get<std::string>(sepv); auto lo = std::make_shared<ListObj>(); if (sep.empty()) lo->items.push_back(Value{s}); else { size_t p = 0; while (true) { size_t q = s.find(sep, p); if (q == std::string::npos) { lo->items.push_back(Value{s.substr(p)}); break; } lo->items.push_back(Value{s.substr(p, q - p)}); p = q + sep.size(); } } st.push_back(Value{lo}); }
                else if (in.a == 10) { Value x = st.back(); st.pop_back(); const std::string& s = std::get<std::string>(x); try { st.push_back(Value{(int64_t)std::stoll(s)}); } catch (...) { throw VMError("int(): cannot parse '" + s + "'"); } }
                else if (in.a == 11) { Value x = st.back(); st.pop_back(); const std::string& p = std::get<std::string>(x); std::ifstream f(p); if (!f) throw VMError("readFile: cannot open " + p); std::stringstream ss; ss << f.rdbuf(); st.push_back(Value{ss.str()}); }
                else if (in.a == 12) { Value d = st.back(); st.pop_back(); Value pv = st.back(); st.pop_back(); const std::string& p = std::get<std::string>(pv); const std::string& data = std::get<std::string>(d); std::ofstream of(p); if (!of) throw VMError("writeFile: cannot open " + p); of << data; st.push_back(Value{}); }
                else if (in.a == 13) { st.push_back(Value{std::make_shared<MapObj>()}); }
                else if (in.a == 14) { Value vv = st.back(); st.pop_back(); Value kk = st.back(); st.pop_back(); Value mv = st.back(); st.pop_back(); MapObj& M = asMap(mv); bool found = false; for (auto& pr : M.items) if (valueEq(pr.first, kk)) { pr.second = vv; found = true; break; } if (!found) M.items.push_back({kk, vv}); st.push_back(Value{}); }
                else if (in.a == 15) { Value kk = st.back(); st.pop_back(); Value mv = st.back(); st.pop_back(); MapObj& M = asMap(mv); Value* found = nullptr; for (auto& pr : M.items) if (valueEq(pr.first, kk)) { found = &pr.second; break; } if (!found) throw VMError("get(): key not found: " + toStr(kk)); st.push_back(*found); }
                else if (in.a == 16) { Value kk = st.back(); st.pop_back(); Value mv = st.back(); st.pop_back(); MapObj& M = asMap(mv); bool r = false; for (auto& pr : M.items) if (valueEq(pr.first, kk)) { r = true; break; } st.push_back(Value{r}); }
                else if (in.a == 17) { Value mv = st.back(); st.pop_back(); MapObj& M = asMap(mv); auto lo = std::make_shared<ListObj>(); for (auto& pr : M.items) lo->items.push_back(pr.first); st.push_back(Value{lo}); }
                else if (in.a == 18) { Value kk = st.back(); st.pop_back(); Value mv = st.back(); st.pop_back(); MapObj& M = asMap(mv); for (size_t i = 0; i < M.items.size(); i++) if (valueEq(M.items[i].first, kk)) { M.items.erase(M.items.begin() + i); break; } st.push_back(Value{}); }
                else if (in.a == 19) { Value x = st.back(); st.pop_back(); st.push_back(Value{std::sqrt(asNum(x))}); }
                else if (in.a == 20) { Value x = st.back(); st.pop_back(); st.push_back(Value{std::sin(asNum(x))}); }
                else if (in.a == 21) { Value x = st.back(); st.pop_back(); st.push_back(Value{std::cos(asNum(x))}); }
                else if (in.a == 22) { Value x = st.back(); st.pop_back(); st.push_back(Value{std::tan(asNum(x))}); }
                else if (in.a == 23) { Value b = st.back(); st.pop_back(); Value a = st.back(); st.pop_back(); st.push_back(Value{std::atan2(asNum(a), asNum(b))}); }
                else if (in.a == 24) { Value x = st.back(); st.pop_back(); st.push_back(Value{(int64_t)std::floor(asNum(x))}); }
                else if (in.a == 25) { Value x = st.back(); st.pop_back(); st.push_back(Value{(int64_t)std::ceil(asNum(x))}); }
                else if (in.a == 26) { Value x = st.back(); st.pop_back(); st.push_back(Value{(int64_t)std::llround(asNum(x))}); }
                else if (in.a == 27) { Value b = st.back(); st.pop_back(); Value a = st.back(); st.pop_back(); st.push_back(Value{std::pow(asNum(a), asNum(b))}); }
                else if (in.a == 28) { Value v = st.back(); st.pop_back(); if (auto p = std::get_if<int64_t>(&v)) st.push_back(Value{(int64_t)std::llabs(*p)}); else st.push_back(Value{std::fabs(asNum(v))}); }
                else if (in.a == 29) { Value b = st.back(); st.pop_back(); Value a = st.back(); st.pop_back(); st.push_back(asNum(a) <= asNum(b) ? a : b); }
                else if (in.a == 30) { Value b = st.back(); st.pop_back(); Value a = st.back(); st.pop_back(); st.push_back(asNum(a) >= asNum(b) ? a : b); }
                else if (in.a == 31) { Value tv = st.back(); st.pop_back(); Value bv = st.back(); st.pop_back(); Value av = st.back(); st.pop_back(); double a = asNum(av), b = asNum(bv), t = asNum(tv); st.push_back(Value{a + (b - a) * t}); }
                else if (in.a == 32) { Value hv = st.back(); st.pop_back(); Value lv = st.back(); st.pop_back(); Value v = st.back(); st.pop_back(); double x = asNum(v); st.push_back(x < asNum(lv) ? lv : (x > asNum(hv) ? hv : v)); }
                else if (in.a == 33) { st.push_back(Value{rngFloat()}); }
                else if (in.a == 34) { Value hv = st.back(); st.pop_back(); Value lv = st.back(); st.pop_back(); int64_t lo = std::get<int64_t>(lv), hi = std::get<int64_t>(hv); if (hi < lo) std::swap(lo, hi); st.push_back(Value{lo + (int64_t)(rngNext() % (uint64_t)(hi - lo + 1))}); }
                else if (in.a == 35) { Value s = st.back(); st.pop_back(); uint64_t seed = (uint64_t)std::get<int64_t>(s); g_rng = seed ? seed : 1; st.push_back(Value{}); }
                else if (in.a == 36) { std::vector<Value> vals(argc); for (int k = argc - 1; k >= 0; k--) { vals[k] = st.back(); st.pop_back(); } auto c = std::make_shared<Coro>(); c->fn = vals.empty() ? Value{} : vals[0]; if (vals.size() > 1) c->args.assign(vals.begin() + 1, vals.end()); st.push_back(Value{c}); }
                else if (in.a == 37) {   // resume(coro [, arg])
                    Value arg{}; if (argc >= 2) { arg = st.back(); st.pop_back(); }
                    Value cv = st.back(); st.pop_back();
                    auto p = std::get_if<std::shared_ptr<Coro>>(&cv); if (!p) throw VMError("resume() expects a coroutine");
                    Coro& C = **p; Value result{};
                    if (C.status != 2) {
                        if (C.status == 0) {   // fresh: start the coroutine function on its own stacks
                            auto cl = std::get_if<std::shared_ptr<Closure>>(&C.fn); if (!cl) throw VMError("coroutine() expects a function");
                            for (auto& u : (*cl)->upvals) C.st.push_back(u);
                            for (auto& a : C.args) C.st.push_back(a);
                            CFunc* f = &prog.funcs[(*cl)->fn]; int cbase = (int)C.st.size() - (int)((*cl)->upvals.size() + C.args.size());
                            while ((int)C.st.size() < cbase + f->nlocals) C.st.push_back(Value{});
                            C.frames.push_back({f, 0, cbase}); C.status = 1;
                        } else C.st.push_back(arg);   // resume: the yield expression evaluates to arg
                        try { result = runLoop(prog, globals, C.st, C.frames, C.handlers); C.status = 2; }
                        catch (YieldSignal& y) { result = y.value; }   // still suspended; C's stacks are preserved
                    }
                    st.push_back(result);
                }
                else if (in.a == 38) { Value cv = st.back(); st.pop_back(); auto p = std::get_if<std::shared_ptr<Coro>>(&cv); st.push_back(Value{p ? ((*p)->status == 2) : true}); }
                else if (in.a == 39) { Value yv = st.back(); st.pop_back(); Value xv = st.back(); st.pop_back(); st.push_back(Value{Vec{asNum(xv), asNum(yv), 0.0, 2}}); }
                else if (in.a == 40) { Value zv = st.back(); st.pop_back(); Value yv = st.back(); st.pop_back(); Value xv = st.back(); st.pop_back(); st.push_back(Value{Vec{asNum(xv), asNum(yv), asNum(zv), 3}}); }
                else if (in.a == 41) { Value bv = st.back(); st.pop_back(); Value av = st.back(); st.pop_back(); Vec& A = std::get<Vec>(av); Vec& B = std::get<Vec>(bv); st.push_back(Value{A.x * B.x + A.y * B.y + A.z * B.z}); }
                else if (in.a == 42) { Value v = st.back(); st.pop_back(); Vec& V = std::get<Vec>(v); st.push_back(Value{std::sqrt(V.x * V.x + V.y * V.y + V.z * V.z)}); }
                else if (in.a == 43) { Value v = st.back(); st.pop_back(); Vec r = std::get<Vec>(v); double L = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z); if (L > 0.0) { r.x /= L; r.y /= L; r.z /= L; } st.push_back(Value{r}); }
                else if (in.a == 44) {   // __emit(listenerList, args...) — fire a signal, calling each listener
                    std::vector<Value> vals(argc); for (int k = argc - 1; k >= 0; k--) { vals[k] = st.back(); st.pop_back(); }
                    if (!vals.empty()) if (auto lp = std::get_if<std::shared_ptr<ListObj>>(&vals[0])) {
                        std::vector<Value> eargs(vals.begin() + 1, vals.end());
                        auto listeners = (*lp)->items;   // copy: a handler may connect/disconnect during emit
                        for (auto& lv : listeners) {
                            auto cl = std::get_if<std::shared_ptr<Closure>>(&lv); if (!cl) continue;
                            std::vector<Value> s2; std::vector<Frame> f2; std::vector<Handler> h2;
                            for (auto& u : (*cl)->upvals) s2.push_back(u);
                            for (auto& a : eargs) s2.push_back(a);
                            CFunc* fn2 = &prog.funcs[(*cl)->fn];
                            int base2 = (int)s2.size() - (int)((*cl)->upvals.size() + eargs.size());
                            while ((int)s2.size() < base2 + fn2->nlocals) s2.push_back(Value{});   // fewer params than args: extras dropped on RET
                            f2.push_back({fn2, 0, base2});
                            runLoop(prog, globals, s2, f2, h2);
                        }
                    }
                    st.push_back(Value{});
                }
                break;
            }
            case Op::YIELD: { Value v = st.back(); st.pop_back(); throw YieldSignal{v}; }
            case Op::NATIVE: { int argc = in.b; std::vector<Value> args(argc); for (int k = argc - 1; k >= 0; k--) { args[k] = st.back(); st.pop_back(); } st.push_back(prog.natives->entries[in.a].fn(args)); break; }
            case Op::CALL: call(in.a, in.b); break;
            case Op::RET: { Value ret = st.back(); st.pop_back(); int base = frames.back().base; frames.pop_back(); st.resize(base); st.push_back(ret); break; }
        }
    }
    return st.empty() ? Value{} : st.back();
}
static void execTop(Program& prog, std::vector<Value>& globals, int startIndex) {
    std::vector<Value> st; std::vector<Frame> frames; std::vector<Handler> handlers;
    CFunc* fn = &prog.funcs[startIndex]; while ((int)st.size() < fn->nlocals) st.push_back(Value{}); frames.push_back({fn, 0, 0});
    runLoop(prog, globals, st, frames, handlers);
}

