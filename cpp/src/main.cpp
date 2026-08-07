#include "parser.hpp"
#include "types.hpp"
#include "compiler.hpp"
#include "vm.hpp"
#include "lsp.hpp"

// ---------------------------------------------------------------------
// Module loading + driver
// ---------------------------------------------------------------------
static std::string readFile(const std::string& path) { std::ifstream f(path); if (!f) throw std::runtime_error("cannot open module file: " + path); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static std::string dirOf(const std::string& p) { auto s = p.find_last_of('/'); return s == std::string::npos ? "." : p.substr(0, s); }
static std::string modToFile(const std::string& root, const std::string& mod) { std::string r = root + "/"; for (char c : mod) r += (c == '.') ? '/' : c; return r + ".rg"; }
static std::string lastSeg(const std::string& p) { auto s = p.find_last_of('.'); return s == std::string::npos ? p : p.substr(s + 1); }

int main(int argc, char** argv) {
    // `rosegoldc <file.rg>`         type-check, compile, and run
    // `rosegoldc --check <file.rg>` type-check only (front-end gate; no execution)
    // `rosegoldc --lsp`             run the language server (JSON-RPC over stdio)
    if (argc >= 2 && std::string(argv[1]) == "--lsp") return runLsp();
    bool checkOnly = false; int argi = 1;
    if (argc >= 2 && std::string(argv[1]) == "--check") { checkOnly = true; argi = 2; }
    if (argc < argi + 1) { std::cerr << "usage: rosegoldc [--check] <file.rg>\n"; return 2; }
    try {
        std::string entryPath = argv[argi], root = dirOf(entryPath);
        std::map<std::string, Parsed> mods;
        // load entry, then transitive imports
        Parsed entry = Parser{lex(readFile(entryPath))}.program();
        std::string entryName = entry.module.empty() ? "$entry" : entry.module;
        mods[entryName] = std::move(entry);
        std::vector<std::string> work = {entryName};
        while (!work.empty()) {
            std::string m = work.back(); work.pop_back();
            for (auto& imp : mods[m].imports) {
                if (mods.count(imp.path)) continue;
                mods[imp.path] = Parser{lex(readFile(modToFile(root, imp.path)))}.program();
                work.push_back(imp.path);
            }
        }
        // dependency order (deps first)
        std::vector<std::string> order; std::set<std::string> done, active;
        std::function<void(const std::string&)> dfs = [&](const std::string& m) {
            if (done.count(m) || active.count(m)) return; active.insert(m);
            for (auto& imp : mods[m].imports) if (mods.count(imp.path)) dfs(imp.path);
            active.erase(m); done.insert(m); order.push_back(m);
        };
        dfs(entryName);

        // static type check (front-end gate)
        TypeChecker tc(mods, order); tc.build(); tc.check();
        if (!tc.errors.empty()) {
            std::cerr << "type errors:\n";
            for (auto& e : tc.errors) { const std::string& mm = std::get<0>(e); int ln = std::get<1>(e); const std::string& msg = std::get<2>(e); if (ln) std::cerr << "  " << mm << ":" << ln << ": " << msg << "\n"; else std::cerr << "  " << mm << ": " << msg << "\n"; }
            return 1;
        }
        if (checkOnly) return 0;   // front-end gate passed; skip execution

        Program prog; std::map<std::string, int> globalsFunc;
        std::map<std::string, std::vector<std::pair<int, Func*>>> modDefaults;   // module -> (funcIndex, default trait method) to compile as class methods
        // Default trait methods a class inherits (walking `uses` transitively), not overridden by the class.
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
        // Methods an extension contributes: its own, plus inherited trait defaults it doesn't override.
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
        // reservation pass (in dependency order)
        for (auto& m : order) {
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
            for (auto& X : P.extensions) {   // retroactive conformance: register methods on a class table or the primitive-extension table
                std::vector<std::pair<std::string, Func*>> ms; extMethods(P, X, ms);
                bool isClass = S.count(X.typeName) && S[X.typeName].kind == 1; int ci = isClass ? S[X.typeName].index : -1;
                for (auto& d : ms) {
                    if (isClass && prog.classes[ci].methods.count(d.first)) continue;   // class's own method wins
                    int idx = (int)prog.funcs.size(); prog.funcs.push_back({m + "::extend " + X.typeName + "." + d.first});
                    if (isClass) prog.classes[ci].methods[d.first] = idx; else prog.extensions[X.typeName][d.first] = idx;
                    modDefaults[m].push_back({idx, d.second});
                }
            }
            if (!P.globals.empty()) { globalsFunc[m] = (int)prog.funcs.size(); prog.funcs.push_back({m + "::$globals"}); }
        }
        // compile pass
        Compiler comp(prog);
        for (auto& m : order) {
            Parsed& P = mods[m];
            ModuleCtx ctx; ctx.name = m; ctx.sym = &prog.syms[m];
            for (auto& imp : P.imports) {
                if (!imp.names.empty()) { for (auto& n : imp.names) ctx.sel[n] = {imp.path, n}; }
                else if (!imp.alias.empty()) ctx.qual[imp.alias] = imp.path;
                else ctx.qual[lastSeg(imp.path)] = imp.path;
            }
            comp.mc = &ctx;
            for (auto& f : P.funcs) comp.compileFunc((*ctx.sym)[f.name].index, f);
            for (size_t ci0 = 0; ci0 < P.classes.size(); ci0++) comp.compileClass(P.classes[ci0], (*ctx.sym)[P.classes[ci0].name].index);
            for (auto& d : modDefaults[m]) comp.compileMethod(d.first, *d.second);   // inherited default trait method bodies
            if (globalsFunc.count(m)) comp.compileGlobals(globalsFunc[m], P.globals);
            comp.drainPending();
        }
        // run: each module's globals in dep order, then entry main
        std::vector<Value> globals(prog.nglobals);
        for (auto& m : order) if (globalsFunc.count(m)) execTop(prog, globals, globalsFunc[m]);
        auto mainSym = prog.syms[entryName].find("main");
        if (mainSym == prog.syms[entryName].end()) throw VMError("entry module '" + entryName + "' has no func main()");
        execTop(prog, globals, mainSym->second.index);
    } catch (const std::exception& e) { std::cerr << "error: " << e.what() << "\n"; return 1; }
    return 0;
}
