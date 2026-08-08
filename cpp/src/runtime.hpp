#pragma once
#include "parser.hpp"
#include "types.hpp"
#include "vm.hpp"

// =====================================================================
// Embeddable RoseGold runtime + shared load/compile pipeline.
//
// The CLI (main.cpp) and the host-embedding Runtime share loadModules() and
// buildProgram() so they never drift. Runtime adds native FFI registration and
// the ability to call script functions from C++ (drive `update(dt)` per frame).
// =====================================================================

static std::string rgReadFile(const std::string& path) { std::ifstream f(path); if (!f) throw std::runtime_error("cannot open module file: " + path); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static std::string rgDirOf(const std::string& p) { auto s = p.find_last_of('/'); return s == std::string::npos ? "." : p.substr(0, s); }
static std::string rgModToFile(const std::string& root, const std::string& mod) { std::string r = root + "/"; for (char c : mod) r += (c == '.') ? '/' : c; return r + ".rg"; }
static std::string rgLastSeg(const std::string& p) { auto s = p.find_last_of('.'); return s == std::string::npos ? p : p.substr(s + 1); }

// Parse the entry file and its transitive imports; produce a dependency-first order. Returns the entry module name.
static std::string loadModules(const std::string& entryPath, std::map<std::string, Parsed>& mods, std::vector<std::string>& order,
                               std::map<std::string, std::string>* modFile = nullptr) {
    std::string root = rgDirOf(entryPath);
    Parsed entry = Parser{lex(rgReadFile(entryPath))}.program();
    std::string entryName = entry.module.empty() ? "$entry" : entry.module;
    if (modFile) (*modFile)[entryName] = entryPath;
    mods[entryName] = std::move(entry);
    std::vector<std::string> work = {entryName};
    while (!work.empty()) {
        std::string m = work.back(); work.pop_back();
        for (auto& imp : mods[m].imports) {
            if (mods.count(imp.path)) continue;
            std::string f = rgModToFile(root, imp.path);
            if (modFile) (*modFile)[imp.path] = f;
            mods[imp.path] = Parser{lex(rgReadFile(f))}.program();
            work.push_back(imp.path);
        }
    }
    std::set<std::string> done, active;
    std::function<void(const std::string&)> dfs = [&](const std::string& m) {
        if (done.count(m) || active.count(m)) return; active.insert(m);
        for (auto& imp : mods[m].imports) if (mods.count(imp.path)) dfs(imp.path);
        active.erase(m); done.insert(m); order.push_back(m);
    };
    dfs(entryName);
    return entryName;
}

// Reserve symbols and compile every module into `prog` (which may already have prog.natives set for FFI).
static void buildProgram(std::map<std::string, Parsed>& mods, std::vector<std::string>& order, Program& prog,
                         std::map<std::string, int>& globalsFunc, std::map<std::string, int>& initFunc) {
    std::map<std::string, std::vector<std::pair<int, Func*>>> modDefaults;
    auto classDefaults = [](Parsed& P, ClassAst& C, std::vector<std::pair<std::string, Func*>>& out) {
        std::set<std::string> own; for (auto& mth : C.methods) own.insert(mth.name);
        std::set<std::string> added;
        std::function<void(const std::string&)> walk = [&](const std::string& tn) {
            for (auto& Tr : P.traits) if (Tr.name == tn) {
                for (auto& mth : Tr.methods) if (!mth.body.empty() && !own.count(mth.name) && added.insert(mth.name).second) out.push_back({mth.name, &mth});
                for (auto& u : Tr.uses) walk(u);
                return;
            }
        };
        for (auto& u : C.uses) walk(u);
    };
    auto extMethods = [](Parsed& P, ExtendAst& X, std::vector<std::pair<std::string, Func*>>& out) {
        std::set<std::string> seen; for (auto& mth : X.methods) { seen.insert(mth.name); out.push_back({mth.name, &mth}); }
        std::function<void(const std::string&)> walk = [&](const std::string& tn) {
            for (auto& Tr : P.traits) if (Tr.name == tn) {
                for (auto& mth : Tr.methods) if (!mth.body.empty() && seen.insert(mth.name).second) out.push_back({mth.name, &mth});
                for (auto& u : Tr.uses) walk(u);
                return;
            }
        };
        for (auto& u : X.uses) walk(u);
    };
    for (auto& m : order) {   // reservation pass (dependency order)
        Parsed& P = mods[m]; auto& S = prog.syms[m];
        for (auto& g : P.globals) if (!S.count(g.name)) S[g.name] = {3, prog.nglobals++};
        for (auto& C : P.classes) { int ci = (int)prog.classes.size(); prog.classes.push_back({C.name}); for (auto& f : C.fields) prog.classes.back().fieldNames.push_back(f.name); S[C.name] = {1, ci}; }
        for (auto& E : P.enums) for (auto& v : E.variants) { int vi = (int)prog.variants.size(); prog.variants.push_back({E.name, v.first, (int)v.second.size()}); S[v.first] = {2, vi}; }
        for (auto& f : P.funcs) { int fi = (int)prog.funcs.size(); prog.funcs.push_back({m + "::" + f.name}); S[f.name] = {0, fi}; }
        for (size_t ci0 = 0; ci0 < P.classes.size(); ci0++) {
            ClassAst& C = P.classes[ci0]; int ci = S[C.name].index;
            for (auto& mth : C.methods) { int idx = (int)prog.funcs.size(); prog.funcs.push_back({m + "::" + C.name + "." + mth.name}); prog.classes[ci].methods[mth.name] = idx; }
            std::vector<std::pair<std::string, Func*>> defs; classDefaults(P, C, defs);
            for (auto& d : defs) { int idx = (int)prog.funcs.size(); prog.funcs.push_back({m + "::" + C.name + "." + d.first + " (default)"}); prog.classes[ci].methods[d.first] = idx; modDefaults[m].push_back({idx, d.second}); }
            int ni = (int)prog.funcs.size(); prog.funcs.push_back({m + "::new " + C.name}); prog.classes[ci].newFunc = ni;
        }
        for (auto& X : P.extensions) {
            std::vector<std::pair<std::string, Func*>> ms; extMethods(P, X, ms);
            bool isClass = S.count(X.typeName) && S[X.typeName].kind == 1; int ci = isClass ? S[X.typeName].index : -1;
            for (auto& d : ms) {
                if (isClass && prog.classes[ci].methods.count(d.first)) continue;
                int idx = (int)prog.funcs.size(); prog.funcs.push_back({m + "::extend " + X.typeName + "." + d.first});
                if (isClass) prog.classes[ci].methods[d.first] = idx; else prog.extensions[X.typeName][d.first] = idx;
                modDefaults[m].push_back({idx, d.second});
            }
        }
        if (!P.globals.empty()) { globalsFunc[m] = (int)prog.funcs.size(); prog.funcs.push_back({m + "::$globals"}); }
        if (P.hasInit) { initFunc[m] = (int)prog.funcs.size(); prog.funcs.push_back({m + "::$init"}); }
    }
    Compiler comp(prog);   // compile pass
    for (auto& m : order) {
        Parsed& P = mods[m];
        ModuleCtx ctx; ctx.name = m; ctx.sym = &prog.syms[m];
        for (auto& imp : P.imports) {
            if (!imp.names.empty()) { for (auto& n : imp.names) ctx.sel[n] = {imp.path, n}; }
            else if (!imp.alias.empty()) ctx.qual[imp.alias] = imp.path;
            else ctx.qual[rgLastSeg(imp.path)] = imp.path;
        }
        comp.mc = &ctx;
        for (auto& f : P.funcs) comp.compileFunc((*ctx.sym)[f.name].index, f);
        for (size_t ci0 = 0; ci0 < P.classes.size(); ci0++) comp.compileClass(P.classes[ci0], (*ctx.sym)[P.classes[ci0].name].index);
        for (auto& d : modDefaults[m]) comp.compileMethod(d.first, *d.second);
        if (globalsFunc.count(m)) comp.compileGlobals(globalsFunc[m], P.globals);
        if (initFunc.count(m)) comp.compileStmtList(initFunc[m], P.initBody);
        comp.drainPending();
    }
}

// Call a script function by index with args, on the persistent globals; returns its result.
static Value callFunc(Program& prog, std::vector<Value>& globals, int funcIndex, std::vector<Value>& args) {
    std::vector<Value> st; std::vector<Frame> frames; std::vector<Handler> handlers;
    CFunc* fn = &prog.funcs[funcIndex];
    for (auto& a : args) st.push_back(a);
    while ((int)st.size() < fn->nlocals) st.push_back(Value{});
    frames.push_back({fn, 0, 0});
    return runLoop(prog, globals, st, frames, handlers);
}

// ---------------------------------------------------------------------
// The embeddable runtime a C++ host (e.g. a game engine) drives.
// ---------------------------------------------------------------------
struct Runtime {
    NativeRegistry natives;
    std::map<std::string, Parsed> mods;
    std::vector<std::string> order;
    Program prog;
    std::vector<Value> globals;
    std::string entryMod, error, entryPath;
    bool loaded = false;

    // Expose a C++ function to scripts. params/ret are type names ("Int","Float","Void","Any",...).
    void registerNative(const std::string& name, std::vector<std::string> params, std::string ret, NativeFn fn, bool variadic = false) {
        natives.add(name, {std::move(params), std::move(ret), variadic}, std::move(fn));
    }
    // Parse, type-check (with the FFI signatures), compile, and run module globals + init once. False on error.
    bool load(const std::string& path) {
        try {
            entryPath = path;
            entryMod = loadModules(path, mods, order);
            TypeChecker tc(mods, order); tc.natives = &natives; tc.build(); tc.check();
            if (!tc.errors.empty()) { error.clear(); for (auto& e : tc.errors) { error += std::get<0>(e); if (std::get<1>(e)) error += ":" + std::to_string(std::get<1>(e)); error += ": " + std::get<2>(e) + "\n"; } return false; }
            prog.natives = &natives;
            std::map<std::string, int> globalsFunc, initFunc;
            buildProgram(mods, order, prog, globalsFunc, initFunc);
            globals.assign(prog.nglobals, Value{});
            for (auto& m : order) if (globalsFunc.count(m)) execTop(prog, globals, globalsFunc[m]);
            for (auto& m : order) if (initFunc.count(m)) execTop(prog, globals, initFunc[m]);
            loaded = true; return true;
        } catch (const std::exception& e) { error = e.what(); return false; }
    }
    // Hot reload: recompile the (possibly edited) script, preserving current module-global values by name.
    bool reload() {
        if (!loaded) return false;
        std::map<std::string, Value> saved;
        for (auto& kv : prog.syms[entryMod]) if (kv.second.kind == 3) saved[kv.first] = globals[kv.second.index];
        std::string path = entryPath;
        mods.clear(); order.clear(); prog = Program{}; globals.clear(); loaded = false;
        if (!load(path)) return false;
        for (auto& kv : saved) { auto it = prog.syms[entryMod].find(kv.first); if (it != prog.syms[entryMod].end() && it->second.kind == 3) globals[it->second.index] = kv.second; }
        return true;
    }
    bool has(const std::string& fn) { return loaded && prog.syms[entryMod].count(fn) && prog.syms[entryMod][fn].kind == 0; }
    // Invoke a script function (state persists in globals across calls -- e.g. tick update(dt) each frame).
    Value call(const std::string& fn, std::vector<Value> args = {}) {
        auto it = prog.syms[entryMod].find(fn);
        if (it == prog.syms[entryMod].end() || it->second.kind != 0) throw std::runtime_error("no script function '" + fn + "'");
        return callFunc(prog, globals, it->second.index, args);
    }
    // --- component model: instantiate a script class and call its methods (from C++) ---
    // Construct an instance of a script class (its state lives in the instance, not globals).
    Value newInstance(const std::string& className, std::vector<Value> args = {}) {
        auto it = prog.syms[entryMod].find(className);
        if (it == prog.syms[entryMod].end() || it->second.kind != 1) throw std::runtime_error("no script class '" + className + "'");
        return callFunc(prog, globals, prog.classes[it->second.index].newFunc, args);
    }
    bool hasMethod(const Value& inst, const std::string& method) {
        auto p = std::get_if<std::shared_ptr<Instance>>(&inst);
        return p && prog.classes[(*p)->clsIndex].methods.count(method);
    }
    // Call a method on an instance (e.g. player.update(dt)); the instance is passed as self.
    Value callMethod(const Value& inst, const std::string& method, std::vector<Value> args = {}) {
        auto p = std::get_if<std::shared_ptr<Instance>>(&inst);
        if (!p) throw std::runtime_error("callMethod: not an instance");
        auto& ms = prog.classes[(*p)->clsIndex].methods; auto it = ms.find(method);
        if (it == ms.end()) throw std::runtime_error("'" + (*p)->cls + "' has no method '" + method + "'");
        std::vector<Value> full; full.push_back(inst); for (auto& a : args) full.push_back(a);
        return callFunc(prog, globals, it->second, full);
    }
};
