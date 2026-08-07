#pragma once
#include "ast.hpp"

// ---------------------------------------------------------------------
// Static type checker (front-end gate)
// ---------------------------------------------------------------------
struct Ty; using TyP = std::shared_ptr<Ty>;
// nkind: 0 = class, 1 = enum, 2 = trait.  bounds: FUNC generic constraints.  tbounds: a bounded TVAR's trait names.
struct Ty { enum K { ANY, PRIM, LIST, FUNC, NAMED, TVAR, MODULE, MAP } k; std::string name; int nkind = 0; std::vector<TyP> args; TyP elem, ret; bool variadic = false; Bounds bounds; std::vector<std::string> tbounds; };
static TyP tAny() { auto t = std::make_shared<Ty>(); t->k = Ty::ANY; return t; }
static TyP tPrim(const std::string& n) { auto t = std::make_shared<Ty>(); t->k = Ty::PRIM; t->name = n; return t; }
static TyP tList(TyP e) { auto t = std::make_shared<Ty>(); t->k = Ty::LIST; t->elem = e; return t; }
static TyP tFunc(std::vector<TyP> ps, TyP r, bool va = false) { auto t = std::make_shared<Ty>(); t->k = Ty::FUNC; t->args = std::move(ps); t->ret = r; t->variadic = va; return t; }
static TyP tNamed(const std::string& n, int kind, std::vector<TyP> a = {}) { auto t = std::make_shared<Ty>(); t->k = Ty::NAMED; t->name = n; t->nkind = kind; t->args = std::move(a); return t; }
static TyP tVar(const std::string& n) { auto t = std::make_shared<Ty>(); t->k = Ty::TVAR; t->name = n; return t; }
static TyP tMod(const std::string& n) { auto t = std::make_shared<Ty>(); t->k = Ty::MODULE; t->name = n; return t; }
static TyP tMap(TyP k, TyP v) { auto t = std::make_shared<Ty>(); t->k = Ty::MAP; t->args = {k, v}; return t; }
static std::string tStr(const TyP& t) {
    if (!t) return "?";
    switch (t->k) {
        case Ty::ANY: return "?"; case Ty::PRIM: case Ty::TVAR: return t->name; case Ty::MODULE: return "module " + t->name;
        case Ty::LIST: return "List<" + tStr(t->elem) + ">";
        case Ty::MAP: return "Map<" + tStr(t->args[0]) + ", " + tStr(t->args[1]) + ">";
        case Ty::FUNC: { std::string s = "func("; for (size_t i = 0; i < t->args.size(); i++) { if (i) s += ", "; s += tStr(t->args[i]); } return s + ") -> " + tStr(t->ret); }
        case Ty::NAMED: { if (t->args.empty()) return t->name; std::string s = t->name + "<"; for (size_t i = 0; i < t->args.size(); i++) { if (i) s += ", "; s += tStr(t->args[i]); } return s + ">"; }
    }
    return "?";
}
struct ClassInfoT { std::vector<std::string> generics; Bounds bounds; std::string extends; std::vector<std::string> uses; std::map<std::string, std::pair<TyP, int>> fields; std::map<std::string, std::pair<TyP, int>> methods; std::vector<TyP> ctorParams; };
struct EnumInfoT { std::vector<std::string> generics; std::map<std::string, std::vector<TyP>> variants; };
struct TraitInfoT { std::vector<std::string> generics; std::vector<std::string> uses; std::map<std::string, TyP> methods; std::set<std::string> defaulted; };
struct ExtInfoT { std::vector<std::string> uses; std::map<std::string, std::pair<TyP, int>> methods; };   // retroactive `extend Type uses Trait`
struct ModTypes { std::map<std::string, ClassInfoT> classes; std::map<std::string, EnumInfoT> enums; std::map<std::string, TraitInfoT> traits; std::map<std::string, ExtInfoT> exts; std::map<std::string, TyP> values; std::map<std::string, TyP> pub; };

// One resolved identifier occurrence — feeds LSP hover / go-to-definition / completion.
struct Occ {
    std::string occMod;                 // module (file) the identifier appears in
    int line = 0, col = 0, len = 0;     // 1-based source span of the identifier
    std::string name; TyP ty;           // identifier text + its resolved type
    std::string defMod; int defLine = 0, defCol = 0;  // definition location (defLine 0 = unknown)
    int recv = 0;                        // 1 if a MEMBER access (its ty is the member's type)
    std::string recvClass, recvMod;     // for MEMBER/completion: receiver's class name or module name
    int sem = -1;                        // semantic-token type index (see SEM_* / legend), -1 = don't emit
};
// Semantic token type indices (must match the legend advertised to the client).
enum { SEM_TYPE = 0, SEM_CLASS = 1, SEM_ENUM = 2, SEM_INTERFACE = 3, SEM_FUNCTION = 4, SEM_METHOD = 5, SEM_PROPERTY = 6, SEM_VARIABLE = 7, SEM_PARAMETER = 8 };
// An inlay hint: the inferred type shown after an un-annotated `var` name.
struct InlayH { std::string mod; int line = 0, col = 0; std::string label; };

struct TypeChecker {
    std::map<std::string, Parsed>& mods; std::vector<std::string>& order;
    std::map<std::string, ModTypes> T;
    std::vector<std::tuple<std::string, int, std::string>> errors;
    std::string curm, curClass; TyP curRet; int loopDepth = 0;
    Bounds* curBounds = nullptr;   // generic bounds in scope, so resolveType can tag TVARs with their trait bounds
    TyP curSelf = nullptr;         // what `Self` resolves to here (a class while building/checking it; else a marker TVAR)
    std::map<std::string, std::string> qual; std::map<std::string, std::pair<std::string, std::string>> sel;
    std::vector<Occ> occs; bool recordOcc = false;   // LSP index (populated only when recordOcc)
    std::vector<InlayH> inlays;                       // inferred-type hints on un-annotated vars
    TypeChecker(std::map<std::string, Parsed>& m, std::vector<std::string>& o) : mods(m), order(o) {}

    // An environment binding: a value's type plus, for locals/params/loop vars, its 1-based declaration site
    // (line 0 = no known site, e.g. builtins, closure params, match binds). Implicitly built from a bare TyP.
    struct Binding { TyP ty; int line = 0, col = 0; Binding() {} Binding(TyP t) : ty(t) {} Binding(TyP t, int l, int c) : ty(t), line(l), col(c) {} };
    using Scope = std::map<std::string, Binding>;
    using Env = std::vector<Scope>;
    void err(int line, const std::string& msg) { errors.emplace_back(curm, line, msg); }
    int lineOf(Expr* e) { return e ? e->line : 0; }

    // --- LSP occurrence index ------------------------------------------------
    ClassAst* classAst(const std::string& m, const std::string& name) { for (auto& C : mods[m].classes) if (C.name == name) return &C; return nullptr; }
    // Resolve a bare top-level name (func/class/enum/variant/global) in module m to its declaration site.
    bool topDef(const std::string& m, const std::string& name, Occ& o) {
        auto& P = mods[m];
        for (auto& f : P.funcs) if (f.name == name) { o.defMod = m; o.defLine = f.nameLine; o.defCol = f.nameCol; return true; }
        for (auto& C : P.classes) if (C.name == name) { o.defMod = m; o.defLine = C.nameLine; o.defCol = C.nameCol; return true; }
        for (auto& E : P.enums) { if (E.name == name) { o.defMod = m; o.defLine = E.nameLine; o.defCol = E.nameCol; return true; }
            for (auto& v : E.variants) if (v.first == name) { o.defMod = m; o.defLine = E.nameLine; o.defCol = E.nameCol; return true; } }
        for (auto& g : P.globals) if (g.k == Stmt::VAR && g.name == name) { o.defMod = m; o.defLine = g.nameLine; o.defCol = g.nameCol; return true; }
        return false;
    }
    // Resolve a member (field/method, walking the extends chain) to its declaration site.
    bool memberDef(const std::string& m, const std::string& cls, const std::string& field, Occ& o) {
        std::string c = cls;
        while (!c.empty()) { ClassAst* C = classAst(m, c); if (!C) break;
            for (auto& f : C->fields) if (f.name == field) { o.defMod = m; o.defLine = f.nameLine; o.defCol = f.nameCol; return true; }
            for (auto& mm : C->methods) if (mm.name == field) { o.defMod = m; o.defLine = mm.nameLine; o.defCol = mm.nameCol; return true; }
            c = C->extends; }
        return false;
    }
    bool inEnv(const std::string& name, Env& env) { for (auto& sc : env) if (sc.count(name)) return true; return false; }
    // Classify a value reference for semantic highlighting (by name kind, then by type shape).
    int classifyValue(const std::string& name, const TyP& ty) {
        if (T[curm].classes.count(name)) return SEM_CLASS;
        if (T[curm].enums.count(name)) return SEM_ENUM;
        if (T[curm].traits.count(name)) return SEM_INTERFACE;
        if (ty && ty->k == Ty::FUNC) { TyP r = ty->ret; if (r && r->k == Ty::NAMED && r->nkind == 1) return SEM_ENUM; if (r && r->k == Ty::NAMED && r->nkind == 0 && r->name == name) return SEM_CLASS; return SEM_FUNCTION; }
        if (ty && ty->k == Ty::NAMED && ty->nkind == 1) return SEM_ENUM;
        return SEM_VARIABLE;
    }
    // A declaration site, recorded as an occurrence that is its own definition (so find-references / rename
    // can group a symbol's declaration together with all its uses, and the cursor may sit on either).
    void recordDecl(const std::string& name, int line, int col, const TyP& ty, int sem = -1) {
        if (!recordOcc || line <= 0) return;
        Occ o; o.occMod = curm; o.line = line; o.col = col; o.len = (int)name.size(); o.name = name; o.ty = ty; o.sem = sem;
        o.defMod = curm; o.defLine = line; o.defCol = col;
        occs.push_back(std::move(o));
    }
    // Structural declarations of a module: funcs, globals, enums/traits/classes and class members.
    void recordDecls(const std::string& m) {
        if (!recordOcc) return;
        auto& P = mods[m]; auto& MT = T[m];
        for (auto& f : P.funcs) recordDecl(f.name, f.nameLine, f.nameCol, MT.values.count(f.name) ? MT.values[f.name] : tAny(), SEM_FUNCTION);
        for (auto& g : P.globals) if (g.k == Stmt::VAR) recordDecl(g.name, g.nameLine, g.nameCol, MT.values.count(g.name) ? MT.values[g.name] : tAny(), SEM_VARIABLE);
        for (auto& E : P.enums) recordDecl(E.name, E.nameLine, E.nameCol, tNamed(E.name, 1), SEM_ENUM);
        for (auto& Tr : P.traits) recordDecl(Tr.name, Tr.nameLine, Tr.nameCol, tNamed(Tr.name, 2), SEM_INTERFACE);
        for (auto& C : P.classes) {
            recordDecl(C.name, C.nameLine, C.nameCol, MT.values.count(C.name) ? MT.values[C.name] : tNamed(C.name, 0), SEM_CLASS);
            auto& ci = MT.classes[C.name];
            for (auto& fld : C.fields) recordDecl(fld.name, fld.nameLine, fld.nameCol, ci.fields.count(fld.name) ? ci.fields[fld.name].first : tAny(), SEM_PROPERTY);
            for (auto& mth : C.methods) recordDecl(mth.name, mth.nameLine, mth.nameCol, ci.methods.count(mth.name) ? ci.methods[mth.name].first : tAny(), SEM_METHOD);
        }
    }
    void recordName(Expr* e, const Binding& b, Env& env) {
        if (!recordOcc || !e || e->line <= 0) return;
        Occ o; o.occMod = curm; o.line = e->line; o.col = e->col; o.len = (int)e->sval.size(); o.name = e->sval; o.ty = b.ty;
        o.sem = classifyValue(e->sval, b.ty);
        if (b.line > 0) { o.defMod = curm; o.defLine = b.line; o.defCol = b.col; }   // a local/param/loop var with a known declaration site
        else if (!inEnv(e->sval, env)) {                            // not a local -> resolve a top-level or imported definition
            if (!topDef(curm, e->sval, o)) { auto s = sel.find(e->sval); if (s != sel.end()) topDef(s->second.first, s->second.second, o); }
        }
        occs.push_back(std::move(o));
    }

    TyP resolveType(const TyNodeP& n, const std::set<std::string>& gens, const std::string& m) {
        if (!n) return tAny();
        if (n->isFunc) { std::vector<TyP> ps; for (auto& p : n->fparams) ps.push_back(resolveType(p, gens, m)); return tFunc(ps, resolveType(n->fret, gens, m)); }
        const std::string& name = n->name;
        if (name == "Self") return curSelf ? curSelf : tVar("Self");   // marker outside a type context; substituted at conformance/dispatch
        if (name == "Int" || name == "Float" || name == "String" || name == "Bool" || name == "Void") return tPrim(name);
        if (name == "List") return tList(n->args.empty() ? tAny() : resolveType(n->args[0], gens, m));
        if (name == "Map") return tMap(n->args.size() > 0 ? resolveType(n->args[0], gens, m) : tAny(), n->args.size() > 1 ? resolveType(n->args[1], gens, m) : tAny());
        if (gens.count(name)) { TyP tv = tVar(name); if (curBounds) { auto b = curBounds->find(name); if (b != curBounds->end()) tv->tbounds = b->second; } return tv; }
        std::vector<TyP> args; for (auto& a : n->args) args.push_back(resolveType(a, gens, m));
        if (T[m].classes.count(name)) return tNamed(name, 0, args);
        if (T[m].enums.count(name)) return tNamed(name, 1, args);
        if (T[m].traits.count(name)) return tNamed(name, 2, args);
        err(0, "unknown type '" + name + "'"); return tAny();
    }
    // The `Self` type for an extension of `n` (a primitive, List/Map, or a class); null if `n` isn't extendable.
    TyP extSelfType(const std::string& n) {
        if (n == "Int" || n == "Float" || n == "String" || n == "Bool" || n == "Void") return tPrim(n);
        if (n == "List") return tList(tAny());
        if (n == "Map") return tMap(tAny(), tAny());
        if (T[curm].classes.count(n)) return tNamed(n, 0);
        return nullptr;
    }
    // Supertype walk over classes (extends + uses), traits (uses), and retroactive extensions (uses), for X->trait subtyping.
    bool isSub(const std::string& a, const std::string& b) {
        if (a == b) return true;
        std::vector<std::string> sup;
        auto xe = T[curm].exts.find(a); if (xe != T[curm].exts.end()) for (auto& u : xe->second.uses) sup.push_back(u);
        auto ci = T[curm].classes.find(a);
        if (ci != T[curm].classes.end()) { if (!ci->second.extends.empty()) sup.push_back(ci->second.extends); for (auto& u : ci->second.uses) sup.push_back(u); }
        else { auto ti = T[curm].traits.find(a); if (ti != T[curm].traits.end()) for (auto& u : ti->second.uses) sup.push_back(u); }
        for (auto& s : sup) if (s == b || isSub(s, b)) return true; return false;
    }
    bool satisfies(const TyP& t, const std::string& trait) {
        if (!t) return false;
        if (t->k == Ty::NAMED || t->k == Ty::PRIM) return isSub(t->name, trait);
        if (t->k == Ty::LIST) return isSub("List", trait);
        if (t->k == Ty::MAP) return isSub("Map", trait);
        return false;
    }
    void collectSupers(const std::string& n, std::vector<std::string>& order, std::set<std::string>& seen) {
        if (!seen.insert(n).second) return; order.push_back(n);
        auto ci = T[curm].classes.find(n);
        if (ci != T[curm].classes.end()) { if (!ci->second.extends.empty()) collectSupers(ci->second.extends, order, seen); for (auto& u : ci->second.uses) collectSupers(u, order, seen); }
        else { auto ti = T[curm].traits.find(n); if (ti != T[curm].traits.end()) for (auto& u : ti->second.uses) collectSupers(u, order, seen); }
    }
    // Nearest shared class/trait of two named types (for typing a mixed list literal); null if none.
    TyP commonSuper(const TyP& a, const TyP& b) {
        if (!a || !b || a->k != Ty::NAMED || b->k != Ty::NAMED) return nullptr;
        std::vector<std::string> ao, bo; std::set<std::string> as, bs; collectSupers(a->name, ao, as); collectSupers(b->name, bo, bs);
        for (auto& n : bo) if (as.count(n)) { int kind = T[curm].classes.count(n) ? 0 : (T[curm].traits.count(n) ? 2 : 1); return tNamed(n, kind); }
        return nullptr;
    }
    bool assignable(const TyP& s, const TyP& d) {
        if (!s || !d) return true;
        if (s->k == Ty::ANY || d->k == Ty::ANY || s->k == Ty::TVAR || d->k == Ty::TVAR) return true;
        if (d->k == Ty::NAMED && d->nkind == 2 && (s->k == Ty::PRIM || s->k == Ty::LIST || s->k == Ty::MAP)) return satisfies(s, d->name);  // conform via extension
        if (s->k == Ty::PRIM && d->k == Ty::PRIM) return s->name == d->name;
        if (s->k == Ty::LIST && d->k == Ty::LIST) return assignable(s->elem, d->elem) && assignable(d->elem, s->elem);
        if (s->k == Ty::MAP && d->k == Ty::MAP) return assignable(s->args[0], d->args[0]) && assignable(d->args[0], s->args[0]) && assignable(s->args[1], d->args[1]) && assignable(d->args[1], s->args[1]);
        if (s->k == Ty::FUNC && d->k == Ty::FUNC) { if (s->args.size() != d->args.size()) return false; for (size_t i = 0; i < s->args.size(); i++) if (!assignable(d->args[i], s->args[i])) return false; return assignable(s->ret, d->ret); }
        if (s->k == Ty::NAMED && d->k == Ty::NAMED) { if (s->name == d->name) return true; if (d->nkind == 2) return isSub(s->name, d->name); if (s->nkind == 0 && d->nkind == 0) return isSub(s->name, d->name); return false; }
        return false;
    }
    void unify(const TyP& p, const TyP& a, std::map<std::string, TyP>& s) {
        if (!p || !a) return;
        if (p->k == Ty::TVAR) { if (s.count(p->name)) unify(s[p->name], a, s); else if (a->k != Ty::ANY && a->k != Ty::TVAR) s[p->name] = a; return; }
        if (p->k == Ty::LIST && a->k == Ty::LIST) unify(p->elem, a->elem, s);
        else if (p->k == Ty::MAP && a->k == Ty::MAP) { for (size_t i = 0; i < 2 && i < a->args.size(); i++) unify(p->args[i], a->args[i], s); }
        else if (p->k == Ty::FUNC && a->k == Ty::FUNC) { for (size_t i = 0; i < p->args.size() && i < a->args.size(); i++) unify(p->args[i], a->args[i], s); unify(p->ret, a->ret, s); }
        else if (p->k == Ty::NAMED && a->k == Ty::NAMED) for (size_t i = 0; i < p->args.size() && i < a->args.size(); i++) unify(p->args[i], a->args[i], s);
    }
    TyP subst(const TyP& t, std::map<std::string, TyP>& s) {
        if (!t || s.empty()) return t;
        if (t->k == Ty::TVAR) { auto it = s.find(t->name); return it != s.end() ? it->second : t; }
        if (t->k == Ty::LIST) return tList(subst(t->elem, s));
        if (t->k == Ty::MAP) return tMap(subst(t->args[0], s), subst(t->args[1], s));
        if (t->k == Ty::FUNC) { std::vector<TyP> ps; for (auto& p : t->args) ps.push_back(subst(p, s)); return tFunc(ps, subst(t->ret, s), t->variadic); }
        if (t->k == Ty::NAMED) { std::vector<TyP> a; for (auto& x : t->args) a.push_back(subst(x, s)); return tNamed(t->name, t->nkind, a); }
        return t;
    }
    TyP funcType(Func& f, const std::string& m, const std::vector<std::string>& extra) {
        std::set<std::string> g(f.generics.begin(), f.generics.end()); for (auto& x : extra) g.insert(x);
        std::vector<TyP> ps; for (size_t k = 0; k < f.params.size(); k++) { if (f.params[k] == "self") continue; ps.push_back(f.ptypes[k] ? resolveType(f.ptypes[k], g, m) : tAny()); }
        TyP ft = tFunc(ps, f.retType ? resolveType(f.retType, g, m) : tPrim("Void")); ft->bounds = f.bounds; return ft;
    }
    void build() {
        for (auto& m : order) { auto& P = mods[m]; for (auto& C : P.classes) T[m].classes[C.name]; for (auto& Tr : P.traits) T[m].traits[Tr.name]; for (auto& E : P.enums) T[m].enums[E.name]; }
        for (auto& m : order) buildModule(m);
        for (auto& m : order) T[m].pub = pubValues(m, {});
    }
    void buildModule(const std::string& m) {
        curm = m; auto& P = mods[m]; auto& MT = T[m];
        for (auto& Tr : P.traits) {
            TraitInfoT ti; ti.generics = Tr.generics; ti.uses = Tr.uses;
            std::set<std::string> g(Tr.generics.begin(), Tr.generics.end());
            for (auto& mth : Tr.methods) { std::vector<TyP> ps; for (size_t k = 0; k < mth.params.size(); k++) { if (mth.params[k] == "self") continue; ps.push_back(mth.ptypes[k] ? resolveType(mth.ptypes[k], g, m) : tAny()); } ti.methods[mth.name] = tFunc(ps, mth.retType ? resolveType(mth.retType, g, m) : tPrim("Void")); if (!mth.body.empty()) ti.defaulted.insert(mth.name); }
            MT.traits[Tr.name] = ti;
        }
        for (auto& C : P.classes) {
            std::vector<TyP> ga; for (auto& gn : C.generics) ga.push_back(tVar(gn));
            TyP pcs = curSelf; curSelf = tNamed(C.name, 0, ga);       // inside this class, `Self` is the class itself
            ClassInfoT ci; ci.generics = C.generics; ci.bounds = C.bounds; ci.extends = C.extends; ci.uses = C.uses;
            std::set<std::string> g(C.generics.begin(), C.generics.end());
            for (auto& f : C.fields) ci.fields[f.name] = { f.type ? resolveType(f.type, g, m) : tAny(), f.vis };
            for (auto& mth : C.methods) { std::set<std::string> g2 = g; for (auto& x : mth.generics) g2.insert(x); std::vector<TyP> ps; for (size_t k = 0; k < mth.params.size(); k++) { if (mth.params[k] == "self") continue; ps.push_back(mth.ptypes[k] ? resolveType(mth.ptypes[k], g2, m) : tAny()); } TyP mft = tFunc(ps, mth.retType ? resolveType(mth.retType, g2, m) : tPrim("Void")); mft->bounds = mth.bounds; ci.methods[mth.name] = { mft, mth.vis }; }
            if (C.hasCtor) for (size_t k = 0; k < C.ctorParams.size(); k++) ci.ctorParams.push_back(C.ctorPtypes[k] ? resolveType(C.ctorPtypes[k], g, m) : tAny());
            // Inherit default trait methods (Self := this class) unless the class overrides them.
            std::map<std::string, TyP> selfSub; selfSub["Self"] = curSelf;
            std::function<void(const std::string&)> inheritDefaults = [&](const std::string& tn) {
                auto ti = MT.traits.find(tn); if (ti == MT.traits.end()) return;
                for (auto& dn : ti->second.defaulted) if (!ci.methods.count(dn)) ci.methods[dn] = { subst(ti->second.methods[dn], selfSub), 1 };
                for (auto& u : ti->second.uses) inheritDefaults(u);
            };
            for (auto& u : C.uses) inheritDefaults(u);
            MT.classes[C.name] = ci;
            TyP ctor = tFunc(ci.ctorParams, tNamed(C.name, 0, ga)); ctor->bounds = C.bounds;
            MT.values[C.name] = ctor;
            curSelf = pcs;
        }
        for (auto& X : P.extensions) {
            TyP self = extSelfType(X.typeName); if (!self) self = tAny();
            TyP pcs = curSelf; curSelf = self;
            ExtInfoT xi; for (auto& u : X.uses) xi.uses.push_back(u);
            for (auto& mth : X.methods) { std::set<std::string> g(mth.generics.begin(), mth.generics.end()); std::vector<TyP> ps; for (size_t k = 0; k < mth.params.size(); k++) { if (mth.params[k] == "self") continue; ps.push_back(mth.ptypes[k] ? resolveType(mth.ptypes[k], g, m) : tAny()); } TyP mft = tFunc(ps, mth.retType ? resolveType(mth.retType, g, m) : tPrim("Void")); mft->bounds = mth.bounds; xi.methods[mth.name] = { mft, mth.vis }; }
            std::map<std::string, TyP> selfSub; selfSub["Self"] = self;   // inherit trait defaults (Self := extended type)
            std::function<void(const std::string&)> inh = [&](const std::string& tn) { auto ti = MT.traits.find(tn); if (ti == MT.traits.end()) return; for (auto& dn : ti->second.defaulted) if (!xi.methods.count(dn)) xi.methods[dn] = { subst(ti->second.methods[dn], selfSub), 1 }; for (auto& u : ti->second.uses) inh(u); };
            for (auto& u : X.uses) inh(u);
            curSelf = pcs;
            auto& e = MT.exts[X.typeName]; for (auto& u : xi.uses) e.uses.push_back(u); for (auto& kv : xi.methods) e.methods[kv.first] = kv.second;
            auto cit = MT.classes.find(X.typeName);   // extending a class: expose the methods on the class too
            if (cit != MT.classes.end()) for (auto& kv : xi.methods) if (!cit->second.methods.count(kv.first)) cit->second.methods[kv.first] = kv.second;
        }
        for (auto& E : P.enums) {
            EnumInfoT ei; ei.generics = E.generics; std::set<std::string> g(E.generics.begin(), E.generics.end());
            std::vector<TyP> ga; for (auto& gn : E.generics) ga.push_back(tVar(gn)); TyP enumTy = tNamed(E.name, 1, ga);
            for (auto& v : E.variants) { std::vector<TyP> fts; for (auto& ft : v.second) fts.push_back(resolveType(ft, g, m)); ei.variants[v.first] = fts; MT.values[v.first] = fts.empty() ? enumTy : tFunc(fts, enumTy); }
            MT.enums[E.name] = ei; MT.values[E.name] = enumTy;
        }
        for (auto& f : P.funcs) MT.values[f.name] = funcType(f, m, {});
        for (auto& g : P.globals) MT.values[g.name] = g.vtype ? resolveType(g.vtype, {}, m) : tAny();
    }
    std::map<std::string, TyP> pubValues(const std::string& m, std::set<std::string> seen) {
        if (seen.count(m)) return {}; seen.insert(m);
        auto& P = mods[m]; auto& MT = T[m]; std::map<std::string, TyP> r;
        for (auto& C : P.classes) if (C.vis == 1) r[C.name] = MT.values[C.name];
        for (auto& E : P.enums) if (E.vis == 1) { r[E.name] = MT.values[E.name]; for (auto& v : E.variants) r[v.first] = MT.values[v.first]; }
        for (auto& f : P.funcs) if (f.vis == 1) r[f.name] = MT.values[f.name];
        for (auto& g : P.globals) if (g.vis == 1) r[g.name] = MT.values[g.name];
        for (auto& im : P.imports) if (im.pub && mods.count(im.path)) { auto tex = pubValues(im.path, seen); if (!im.names.empty()) { for (auto& n : im.names) if (tex.count(n)) r[n] = tex[n]; } else for (auto& kv : tex) r[kv.first] = kv.second; }
        return r;
    }

    Scope builtins() {
        Scope b;
        b["print"] = tFunc({}, tPrim("Void"), true); b["len"] = tFunc({tAny()}, tPrim("Int")); b["range"] = tFunc({tPrim("Int")}, tList(tPrim("Int")));
        b["push"] = tFunc({tList(tVar("T")), tVar("T")}, tPrim("Void")); b["pop"] = tFunc({tList(tVar("T"))}, tVar("T"));
        b["str"] = tFunc({tAny()}, tPrim("String")); b["ord"] = tFunc({tPrim("String")}, tPrim("Int")); b["chr"] = tFunc({tPrim("Int")}, tPrim("String"));
        b["substr"] = tFunc({tPrim("String"), tPrim("Int"), tPrim("Int")}, tPrim("String")); b["split"] = tFunc({tPrim("String"), tPrim("String")}, tList(tPrim("String")));
        b["int"] = tFunc({tPrim("String")}, tPrim("Int")); b["readFile"] = tFunc({tPrim("String")}, tPrim("String")); b["writeFile"] = tFunc({tPrim("String"), tPrim("String")}, tPrim("Void"));
        b["map"] = tFunc({}, tMap(tVar("K"), tVar("V")));
        b["set"] = tFunc({tMap(tVar("K"), tVar("V")), tVar("K"), tVar("V")}, tPrim("Void"));
        b["get"] = tFunc({tMap(tVar("K"), tVar("V")), tVar("K")}, tVar("V"));
        b["has"] = tFunc({tMap(tVar("K"), tVar("V")), tVar("K")}, tPrim("Bool"));
        b["keys"] = tFunc({tMap(tVar("K"), tVar("V"))}, tList(tVar("K")));
        b["remove"] = tFunc({tMap(tVar("K"), tVar("V")), tVar("K")}, tPrim("Void"));
        return b;
    }
    Binding lookup(const std::string& name, Env& env) {
        for (auto it = env.rbegin(); it != env.rend(); ++it) { auto f = it->find(name); if (f != it->end()) return f->second; }
        auto v = T[curm].values.find(name); if (v != T[curm].values.end()) return v->second;
        auto s = sel.find(name); if (s != sel.end()) { auto& tv = T[s->second.first].pub; auto j = tv.find(s->second.second); if (j != tv.end()) return j->second; }
        auto q = qual.find(name); if (q != qual.end()) return tMod(q->second);
        return {};
    }
    std::pair<TyP, int> findMember(const std::string& cls, const std::string& field) {
        std::string c = cls;
        while (!c.empty()) { auto it = T[curm].classes.find(c); if (it == T[curm].classes.end()) break; auto f = it->second.fields.find(field); if (f != it->second.fields.end()) return f->second; auto m = it->second.methods.find(field); if (m != it->second.methods.end()) return m->second; c = it->second.extends; }
        return { nullptr, 0 };
    }
    // A trait method, searching the trait and the traits it `uses` (transitively).
    TyP traitMethod(const std::string& tr, const std::string& field) {
        auto it = T[curm].traits.find(tr); if (it == T[curm].traits.end()) return nullptr;
        auto m = it->second.methods.find(field); if (m != it->second.methods.end()) return m->second;
        for (auto& u : it->second.uses) { if (TyP r = traitMethod(u, field)) return r; }
        return nullptr;
    }
    TyP litType(Expr* e) { switch (e->k) { case Expr::INT: return tPrim("Int"); case Expr::FLT: return tPrim("Float"); case Expr::STR: return tPrim("String"); case Expr::BOOL: return tPrim("Bool"); default: return tAny(); } }

    TyP infer(Expr* e, Env& env) {
        switch (e->k) {
            case Expr::INT: return tPrim("Int"); case Expr::FLT: return tPrim("Float"); case Expr::STR: return tPrim("String"); case Expr::BOOL: return tPrim("Bool");
            case Expr::NAME: { Binding b = lookup(e->sval, env); if (b.ty) { recordName(e, b, env); return b.ty; } err(lineOf(e), "undefined name '" + e->sval + "'"); return tAny(); }
            case Expr::MEMBER: return inferMember(e, env);
            case Expr::CALL: return inferCall(e, env);
            case Expr::UNARY: { TyP t = infer(e->lhs.get(), env); return e->op == "!" ? tPrim("Bool") : t; }
            case Expr::BINARY: return inferBinary(e, env);
            case Expr::LIST: { if (e->args.empty()) return tList(tAny()); TyP el; for (auto& a : e->args) { TyP t = infer(a.get(), env); if (!el) el = t; else if (assignable(t, el)) {} else if (assignable(el, t)) el = t; else if (TyP cs = commonSuper(el, t)) el = cs; else { err(lineOf(a.get()), "list elements have differing types"); el = tAny(); } } return tList(el); }
            case Expr::INDEX: { TyP ot = infer(e->lhs.get(), env); TyP it = infer(e->rhs.get(), env); if (!assignable(it, tPrim("Int"))) err(lineOf(e), "index must be 'Int', got '" + tStr(it) + "'"); if (ot->k == Ty::LIST) return ot->elem; if (ot->k == Ty::ANY || ot->k == Ty::TVAR) return tAny(); if (ot->k == Ty::PRIM && ot->name == "String") return tPrim("String"); err(lineOf(e), "cannot index '" + tStr(ot) + "'"); return tAny(); }
            case Expr::MATCH: return inferMatch(e, env);
            case Expr::CLOSURE: return inferClosure(e, env);
        }
        return tAny();
    }
    TyP inferMember(Expr* e, Env& env) {
        TyP o = infer(e->lhs.get(), env); const std::string& field = e->sval;
        // LSP: record one occurrence per member access, capturing the receiver even when the member is invalid/partial (drives completion).
        bool rec = recordOcc && e->line > 0; Occ oc;
        if (rec) { oc.occMod = curm; oc.line = e->line; oc.col = e->col; oc.len = (int)field.size(); oc.name = field; oc.recv = 1;
            if (o->k == Ty::MODULE) oc.recvMod = o->name; else if (o->k == Ty::NAMED && o->nkind == 0) oc.recvClass = o->name; }
        TyP result = tAny();
        if (o->k == Ty::MODULE) {
            auto& pv = T[o->name].pub; auto it = pv.find(field);
            if (it != pv.end()) { result = it->second; if (rec) topDef(o->name, field, oc); }
            else err(lineOf(e), "module '" + o->name + "' has no public member '" + field + "'");
        } else if (o->k == Ty::NAMED && o->nkind == 0) {
            if (T[curm].classes.count(o->name)) {
                auto mem = findMember(o->name, field);
                if (!mem.first) err(lineOf(e), "'" + o->name + "' has no member '" + field + "'");
                else { if (mem.second == 2 && curClass != o->name) err(lineOf(e), "'" + field + "' is private to '" + o->name + "'");
                    std::map<std::string, TyP> s; auto& g = T[curm].classes[o->name].generics; for (size_t i = 0; i < g.size() && i < o->args.size(); i++) s[g[i]] = o->args[i];
                    result = subst(mem.first, s); if (rec) memberDef(curm, o->name, field, oc); }
            }   // unknown/unresolved class body: result stays Any (no error, matching prior behavior)
        } else if (o->k == Ty::NAMED && o->nkind == 2) {                 // value typed as a trait: dispatch to a trait method
            TyP mt = traitMethod(o->name, field);
            if (mt) { std::map<std::string, TyP> ss; ss["Self"] = o; result = subst(mt, ss); } else err(lineOf(e), "trait '" + o->name + "' has no method '" + field + "'");
        } else if (o->k == Ty::TVAR && !o->tbounds.empty()) {            // bounded generic: only the bound's members are available (Self = this type param)
            for (auto& tb : o->tbounds) if (TyP mt = traitMethod(tb, field)) { std::map<std::string, TyP> ss; ss["Self"] = o; result = subst(mt, ss); break; }
            if (result->k == Ty::ANY) { std::string bs; for (size_t i = 0; i < o->tbounds.size(); i++) { if (i) bs += " + "; bs += o->tbounds[i]; } err(lineOf(e), "trait bound (" + bs + ") has no member '" + field + "'"); }
        } else if (o->k == Ty::PRIM || o->k == Ty::LIST || o->k == Ty::MAP) {   // extension method on a primitive/List/Map
            std::string tn = o->k == Ty::PRIM ? o->name : (o->k == Ty::LIST ? "List" : "Map");
            auto xe = T[curm].exts.find(tn);
            if (xe != T[curm].exts.end() && xe->second.methods.count(field)) { std::map<std::string, TyP> ss; ss["Self"] = o; result = subst(xe->second.methods[field].first, ss); }
            else err(lineOf(e), "cannot access '." + field + "' on '" + tStr(o) + "'");
        } else if (o->k == Ty::ANY || o->k == Ty::TVAR) {
            // dynamic / unbounded receiver: result stays Any
        } else err(lineOf(e), "cannot access '." + field + "' on '" + tStr(o) + "'");
        if (rec) { oc.ty = result; oc.sem = (result && result->k == Ty::FUNC) ? SEM_METHOD : SEM_PROPERTY; occs.push_back(std::move(oc)); }
        return result;
    }
    TyP inferCall(Expr* e, Env& env) {
        TyP ct = infer(e->lhs.get(), env); std::vector<TyP> at; for (auto& a : e->args) at.push_back(infer(a.get(), env));
        if (ct->k == Ty::ANY || ct->k == Ty::TVAR) return tAny();
        if (ct->k != Ty::FUNC) { err(lineOf(e), "'" + tStr(ct) + "' is not callable"); return tAny(); }
        if (ct->variadic) return ct->ret;
        if (at.size() != ct->args.size()) { err(lineOf(e), "expected " + std::to_string(ct->args.size()) + " argument(s), got " + std::to_string(at.size())); return ct->ret; }
        std::map<std::string, TyP> s; for (size_t i = 0; i < at.size(); i++) unify(ct->args[i], at[i], s);
        for (size_t i = 0; i < at.size(); i++) { TyP want = subst(ct->args[i], s); if (!assignable(at[i], want)) err(lineOf(e->args[i].get()), "argument " + std::to_string(i + 1) + ": '" + tStr(at[i]) + "' is not assignable to '" + tStr(want) + "'"); }
        for (auto& kv : ct->bounds) { auto sit = s.find(kv.first); if (sit == s.end() || !sit->second) continue; for (auto& tb : kv.second) if (!satisfies(sit->second, tb)) err(lineOf(e), "type '" + tStr(sit->second) + "' does not satisfy bound '" + tb + "' on '" + kv.first + "'"); }
        return subst(ct->ret, s);
    }
    // A FUNC-typed operator method reachable on a value: a class method, a bounded generic's trait method, or an extension method.
    TyP operatorMethod(const TyP& t, const std::string& name) {
        TyP m = nullptr;
        if (t->k == Ty::NAMED && t->nkind == 0) m = findMember(t->name, name).first;
        else if (t->k == Ty::TVAR) { for (auto& tb : t->tbounds) if (TyP x = traitMethod(tb, name)) { m = x; break; } }
        else if (t->k == Ty::PRIM || t->k == Ty::LIST || t->k == Ty::MAP) { std::string tn = t->k == Ty::PRIM ? t->name : (t->k == Ty::LIST ? "List" : "Map"); auto xe = T[curm].exts.find(tn); if (xe != T[curm].exts.end()) { auto it = xe->second.methods.find(name); if (it != xe->second.methods.end()) m = it->second.first; } }
        return (m && m->k == Ty::FUNC) ? m : nullptr;
    }
    // Build `recv.method(arg)` (recv/arg moved in) so the untyped compiler emits an INVOKE.
    ExprP makeMethodCall(ExprP recv, const std::string& method, ExprP arg, int line, int col) {
        auto mem = std::make_unique<Expr>(); mem->k = Expr::MEMBER; mem->sval = method; mem->line = line; mem->col = col; mem->lhs = std::move(recv);
        auto call = std::make_unique<Expr>(); call->k = Expr::CALL; call->line = line; call->col = col; call->lhs = std::move(mem); call->args.push_back(std::move(arg));
        return call;
    }
    TyP inferBinary(Expr* e, Env& env) {
        std::string op = e->op; TyP lt = infer(e->lhs.get(), env), rt = infer(e->rhs.get(), env);
        auto anyv = [](const TyP& t) { return t->k == Ty::ANY || t->k == Ty::TVAR; };
        if (op == "&&" || op == "||") { if (!assignable(lt, tPrim("Bool"))) err(lineOf(e->lhs.get()), "'" + op + "' needs 'Bool', got '" + tStr(lt) + "'"); if (!assignable(rt, tPrim("Bool"))) err(lineOf(e->rhs.get()), "'" + op + "' needs 'Bool', got '" + tStr(rt) + "'"); return tPrim("Bool"); }

        bool arith = (op == "+" || op == "-" || op == "*" || op == "/" || op == "%");
        bool cmp = (op == "<" || op == "<=" || op == ">" || op == ">=");
        bool nativeNum = lt->k == Ty::PRIM && rt->k == Ty::PRIM && lt->name == rt->name && (lt->name == "Int" || lt->name == "Float");
        bool nativeStr = op == "+" && lt->k == Ty::PRIM && lt->name == "String" && rt->k == Ty::PRIM && rt->name == "String";
        bool nativeCmp = lt->k == Ty::PRIM && rt->k == Ty::PRIM && lt->name == rt->name && (lt->name == "Int" || lt->name == "Float" || lt->name == "String");

        // --- operator overloading (desugar to a method call on the left operand) ---
        if (arith && !nativeNum && !nativeStr) {
            std::string mn = op == "+" ? "add" : op == "-" ? "sub" : op == "*" ? "mul" : op == "/" ? "div" : "mod";
            if (TyP m = operatorMethod(lt, mn)) { if (!m->args.empty() && !assignable(rt, m->args[0])) err(lineOf(e->rhs.get()), "operator '" + op + "' on '" + tStr(lt) + "' expects '" + tStr(m->args[0]) + "', got '" + tStr(rt) + "'"); ExprP nn = makeMethodCall(std::move(e->lhs), mn, std::move(e->rhs), e->line, e->col); *e = std::move(*nn); return m->ret; }
        }
        if (cmp && !nativeCmp) {
            if (TyP m = operatorMethod(lt, "compareTo")) { if (!m->args.empty() && !assignable(rt, m->args[0])) err(lineOf(e->rhs.get()), "'" + op + "' on '" + tStr(lt) + "' expects '" + tStr(m->args[0]) + "', got '" + tStr(rt) + "'"); ExprP c = makeMethodCall(std::move(e->lhs), "compareTo", std::move(e->rhs), e->line, e->col); e->lhs = std::move(c); auto zero = std::make_unique<Expr>(); zero->k = Expr::INT; zero->ival = 0; zero->line = e->line; e->rhs = std::move(zero); return tPrim("Bool"); }
        }
        if (op == "==" || op == "!=") {
            if (TyP m = operatorMethod(lt, "equals")) { ExprP eq = makeMethodCall(std::move(e->lhs), "equals", std::move(e->rhs), e->line, e->col); if (op == "==") *e = std::move(*eq); else { auto un = std::make_unique<Expr>(); un->k = Expr::UNARY; un->op = "!"; un->line = e->line; un->lhs = std::move(eq); *e = std::move(*un); } }
            return tPrim("Bool");
        }

        // --- native handling + leniency ---
        if (cmp) { if (!(anyv(lt) || anyv(rt) || nativeCmp)) err(lineOf(e->rhs.get()), "cannot compare '" + tStr(lt) + "' and '" + tStr(rt) + "'"); return tPrim("Bool"); }
        if (anyv(lt) || anyv(rt)) return tAny();
        if (nativeStr) return tPrim("String");
        if (nativeNum) return lt;
        err(lineOf(e->rhs.get()), "cannot apply '" + op + "' to '" + tStr(lt) + "' and '" + tStr(rt) + "'"); return tAny();
    }
    TyP inferClosure(Expr* e, Env& env) {
        Scope sc; std::vector<TyP> ps;
        for (size_t k = 0; k < e->params.size(); k++) { TyP pt = (k < e->ptypes.size() && e->ptypes[k]) ? resolveType(e->ptypes[k], {}, curm) : tAny(); sc[e->params[k]] = pt; ps.push_back(pt); }
        env.push_back(sc); TyP bt = infer(e->lhs.get(), env); env.pop_back();
        return tFunc(ps, e->retType ? resolveType(e->retType, {}, curm) : bt);
    }
    TyP inferMatch(Expr* e, Env& env) {
        TyP subj = infer(e->lhs.get(), env); TyP result;
        for (auto& arm : e->arms) {
            Scope binds; for (auto& p : arm.pats) checkPattern(p, subj, binds, lineOf(arm.body.get()));
            env.push_back(binds); TyP bt = infer(arm.body.get(), env); env.pop_back();
            if (!result) result = bt; else if (assignable(bt, result)) {} else if (assignable(result, bt)) result = bt; else { err(lineOf(arm.body.get()), "match arms have incompatible types '" + tStr(result) + "' and '" + tStr(bt) + "'"); result = tAny(); }
        }
        return result ? result : tPrim("Void");
    }
    void checkPattern(Pattern& p, TyP subj, Scope& binds, int line) {
        if (p.k == 0) return;
        if (p.k == 1) { TyP lt = litType(p.lit.get()); if (!assignable(lt, subj)) err(line, "pattern '" + tStr(lt) + "' cannot match subject '" + tStr(subj) + "'"); return; }
        if (subj->k == Ty::NAMED && subj->nkind == 1) {
            auto ei = T[curm].enums.find(subj->name); if (ei == T[curm].enums.end()) { for (auto& b : p.binds) binds[b] = tAny(); return; }
            auto v = ei->second.variants.find(p.name); if (v == ei->second.variants.end()) { err(line, "'" + p.name + "' is not a variant of '" + subj->name + "'"); for (auto& b : p.binds) binds[b] = tAny(); return; }
            std::map<std::string, TyP> s; auto& g = ei->second.generics; for (size_t i = 0; i < g.size() && i < subj->args.size(); i++) s[g[i]] = subj->args[i];
            for (size_t i = 0; i < p.binds.size(); i++) binds[p.binds[i]] = i < v->second.size() ? subst(v->second[i], s) : tAny();
        } else for (auto& b : p.binds) binds[b] = tAny();
    }
    void expectBool(Expr* c, Env& env) { TyP t = infer(c, env); if (!assignable(t, tPrim("Bool"))) err(lineOf(c), "condition must be 'Bool', got '" + tStr(t) + "'"); }
    void checkStmts(std::vector<Stmt>& ss, Env& env) { env.push_back({}); for (auto& s : ss) checkStmt(s, env); env.pop_back(); }
    void checkStmt(Stmt& s, Env& env) {
        switch (s.k) {
            case Stmt::VAR: { TyP t = s.hasExpr ? infer(s.expr.get(), env) : tAny(); if (s.vtype) { TyP d = resolveType(s.vtype, {}, curm); if (s.hasExpr && !assignable(t, d)) err(lineOf(s.expr.get()), "'" + s.name + "': cannot assign '" + tStr(t) + "' to declared '" + tStr(d) + "'"); t = d; } env.back()[s.name] = Binding(t, s.nameLine, s.nameCol); recordDecl(s.name, s.nameLine, s.nameCol, t, SEM_VARIABLE); if (recordOcc && !s.vtype && s.hasExpr && s.nameLine > 0 && tStr(t) != "?") inlays.push_back({curm, s.nameLine, s.nameCol + (int)s.name.size(), ": " + tStr(t)}); break; }
            case Stmt::ASSIGN: { TyP lt = infer(s.target.get(), env); TyP rt = infer(s.expr.get(), env); if (!assignable(rt, lt)) err(lineOf(s.expr.get()), "cannot assign '" + tStr(rt) + "' to '" + tStr(lt) + "'"); break; }
            case Stmt::EXPR: infer(s.expr.get(), env); break;
            case Stmt::RET: { TyP rt = s.hasExpr ? infer(s.expr.get(), env) : tPrim("Void"); if (!assignable(rt, curRet)) err(s.hasExpr ? lineOf(s.expr.get()) : 0, "returning '" + tStr(rt) + "' from a function declared '-> " + tStr(curRet) + "'"); break; }
            case Stmt::IF: { expectBool(s.expr.get(), env); checkStmts(s.body, env); for (auto& br : s.elifs) { expectBool(br.first.get(), env); checkStmts(br.second, env); } if (s.hasElse) checkStmts(s.elseBody, env); break; }
            case Stmt::WHILE: { expectBool(s.expr.get(), env); loopDepth++; checkStmts(s.body, env); loopDepth--; break; }
            case Stmt::FOR: { TyP it = infer(s.expr.get(), env); TyP el = it->k == Ty::LIST ? it->elem : tAny(); if (!(it->k == Ty::LIST || it->k == Ty::ANY || it->k == Ty::TVAR)) err(lineOf(s.expr.get()), "cannot iterate over '" + tStr(it) + "'"); recordDecl(s.name, s.nameLine, s.nameCol, el, SEM_VARIABLE); env.push_back({ {s.name, Binding(el, s.nameLine, s.nameCol)} }); loopDepth++; checkStmts(s.body, env); loopDepth--; env.pop_back(); break; }
            case Stmt::BREAK: case Stmt::CONTINUE: if (loopDepth == 0) err(0, std::string(s.k == Stmt::BREAK ? "'break'" : "'continue'") + " used outside a loop"); break;
            case Stmt::RAISE: infer(s.expr.get(), env); break;
            case Stmt::TRY: { checkStmts(s.body, env); recordDecl(s.name, s.nameLine, s.nameCol, tAny(), SEM_VARIABLE); env.push_back({ {s.name, Binding(tAny(), s.nameLine, s.nameCol)} }); checkStmts(s.elseBody, env); env.pop_back(); break; }
            case Stmt::PASS: break;
        }
    }
    void checkFunc(Func& f, const std::string& cls) {
        std::string pc = curClass; TyP pr = curRet; int pl = loopDepth; Bounds* pb = curBounds; loopDepth = 0; curClass = cls;
        std::set<std::string> g(f.generics.begin(), f.generics.end()); std::vector<std::string> cg;
        if (!cls.empty()) cg = T[curm].classes[cls].generics; for (auto& x : cg) g.insert(x);
        Bounds mb = f.bounds; if (!cls.empty()) for (auto& kv : T[curm].classes[cls].bounds) mb[kv.first] = kv.second; curBounds = &mb;
        curRet = f.retType ? resolveType(f.retType, g, curm) : tPrim("Void");
        Env env; env.push_back(builtins()); Scope sc; std::vector<TyP> selfArgs; for (auto& gn : cg) selfArgs.push_back(tVar(gn));
        for (size_t k = 0; k < f.params.size(); k++) { if (f.params[k] == "self") sc["self"] = tNamed(cls, 0, selfArgs); else { TyP pt = f.ptypes[k] ? resolveType(f.ptypes[k], g, curm) : tAny(); auto pp = k < f.paramPos.size() ? f.paramPos[k] : std::make_pair(0, 0); sc[f.params[k]] = Binding(pt, pp.first, pp.second); recordDecl(f.params[k], pp.first, pp.second, pt, SEM_PARAMETER); } }
        env.push_back(sc); checkStmts(f.body, env);
        curClass = pc; curRet = pr; loopDepth = pl; curBounds = pb;
    }
    void checkClass(ClassAst& C) {
        Bounds* pbc = curBounds; curBounds = &C.bounds; TyP pcs = curSelf;
        std::set<std::string> g(C.generics.begin(), C.generics.end()); std::vector<TyP> selfArgs; for (auto& gn : C.generics) selfArgs.push_back(tVar(gn));
        curSelf = tNamed(C.name, 0, selfArgs);
        for (auto& f : C.fields) if (f.hasInit) { Env env; env.push_back(builtins()); Scope sc; sc["self"] = tNamed(C.name, 0, selfArgs); env.push_back(sc); std::string pc = curClass; curClass = C.name; TyP t = infer(f.init.get(), env); if (f.type) { TyP d = resolveType(f.type, g, curm); if (!assignable(t, d)) err(lineOf(f.init.get()), "field '" + f.name + "': cannot assign '" + tStr(t) + "' to '" + tStr(d) + "'"); } curClass = pc; }
        for (auto& m : C.methods) checkFunc(m, C.name);
        if (C.hasCtor) {
            std::string pc = curClass; TyP pr = curRet; int pl = loopDepth; loopDepth = 0; curClass = C.name; curRet = tPrim("Void");
            Env env; env.push_back(builtins()); Scope sc; sc["self"] = tNamed(C.name, 0, selfArgs);
            for (size_t k = 0; k < C.ctorParams.size(); k++) { TyP pt = C.ctorPtypes[k] ? resolveType(C.ctorPtypes[k], g, curm) : tAny(); auto pp = k < C.ctorParamPos.size() ? C.ctorParamPos[k] : std::make_pair(0, 0); sc[C.ctorParams[k]] = Binding(pt, pp.first, pp.second); recordDecl(C.ctorParams[k], pp.first, pp.second, pt, SEM_PARAMETER); }
            env.push_back(sc); checkStmts(C.ctorBody, env); curClass = pc; curRet = pr; loopDepth = pl;
        }
        curBounds = pbc; curSelf = pcs;
    }
    // Type-check an extension's method bodies (self and Self are the extended type).
    void checkExtensions(ExtendAst& X) {
        TyP self = extSelfType(X.typeName);
        if (!self) { err(X.nameLine, "cannot extend unknown type '" + X.typeName + "'"); self = tAny(); }
        for (auto& mth : X.methods) {
            std::string pc = curClass; TyP pr = curRet, pcs = curSelf; int pl = loopDepth; Bounds* pb = curBounds;
            loopDepth = 0; curClass = ""; curSelf = self; curBounds = &mth.bounds;
            std::set<std::string> g(mth.generics.begin(), mth.generics.end());
            curRet = mth.retType ? resolveType(mth.retType, g, curm) : tPrim("Void");
            Env env; env.push_back(builtins()); Scope sc;
            for (size_t k = 0; k < mth.params.size(); k++) { if (mth.params[k] == "self") sc["self"] = self; else sc[mth.params[k]] = mth.ptypes[k] ? resolveType(mth.ptypes[k], g, curm) : tAny(); }
            env.push_back(sc); checkStmts(mth.body, env);
            curClass = pc; curRet = pr; curSelf = pcs; loopDepth = pl; curBounds = pb;
        }
    }
    // Type-check a trait's default method bodies (self is the trait itself; Self resolves to the trait).
    void checkTraitDefaults(TraitAst& Tr) {
        for (auto& mth : Tr.methods) {
            if (mth.body.empty()) continue;
            std::string pc = curClass; TyP pr = curRet, pcs = curSelf; int pl = loopDepth; Bounds* pb = curBounds;
            loopDepth = 0; curClass = ""; curSelf = tNamed(Tr.name, 2); curBounds = &mth.bounds;
            std::set<std::string> g(mth.generics.begin(), mth.generics.end());
            curRet = mth.retType ? resolveType(mth.retType, g, curm) : tPrim("Void");
            Env env; env.push_back(builtins()); Scope sc;
            for (size_t k = 0; k < mth.params.size(); k++) { if (mth.params[k] == "self") sc["self"] = tNamed(Tr.name, 2); else sc[mth.params[k]] = mth.ptypes[k] ? resolveType(mth.ptypes[k], g, curm) : tAny(); }
            env.push_back(sc); checkStmts(mth.body, env);
            curClass = pc; curRet = pr; curSelf = pcs; loopDepth = pl; curBounds = pb;
        }
    }
    // Every class must implement the methods of each trait it `uses` (checked by signature).
    void checkConformance() {
        for (auto& m : order) {
            curm = m;
            for (auto& C : mods[m].classes) {
                for (auto& tn : C.uses) {
                    auto ti = T[m].traits.find(tn);
                    if (ti == T[m].traits.end()) { err(C.nameLine, "class '" + C.name + "' uses '" + tn + "', which is not a trait"); continue; }
                    std::vector<TyP> ga; for (auto& gn : C.generics) ga.push_back(tVar(gn));
                    std::map<std::string, TyP> selfSub; selfSub["Self"] = tNamed(C.name, 0, ga);   // `Self` in the trait means this class
                    for (auto& req : ti->second.methods) {
                        TyP want = subst(req.second, selfSub);
                        auto have = findMember(C.name, req.first);
                        if (!have.first) { err(C.nameLine, "class '" + C.name + "' does not implement '" + tn + "." + req.first + "' required as '" + tStr(want) + "'"); continue; }
                        if (!assignable(have.first, want)) err(C.nameLine, "class '" + C.name + "': method '" + req.first + "' is '" + tStr(have.first) + "' but trait '" + tn + "' requires '" + tStr(want) + "'");
                    }
                }
            }
            for (auto& X : mods[m].extensions) {
                TyP self = extSelfType(X.typeName); std::map<std::string, TyP> selfSub; selfSub["Self"] = self ? self : tAny();
                auto& xi = T[m].exts[X.typeName];
                for (auto& tn : X.uses) {
                    auto ti = T[m].traits.find(tn);
                    if (ti == T[m].traits.end()) { err(X.nameLine, "extension of '" + X.typeName + "' uses '" + tn + "', which is not a trait"); continue; }
                    for (auto& req : ti->second.methods) {
                        TyP want = subst(req.second, selfSub);
                        auto mit = xi.methods.find(req.first);
                        if (mit == xi.methods.end()) { err(X.nameLine, "extension of '" + X.typeName + "' does not implement '" + tn + "." + req.first + "' required as '" + tStr(want) + "'"); continue; }
                        if (!assignable(mit->second.first, want)) err(X.nameLine, "extension of '" + X.typeName + "': method '" + req.first + "' is '" + tStr(mit->second.first) + "' but trait '" + tn + "' requires '" + tStr(want) + "'");
                    }
                }
            }
        }
    }
    void check() {
        checkConformance();
        for (auto& m : order) {
            curm = m; qual.clear(); sel.clear();
            for (auto& im : mods[m].imports) { if (!im.names.empty()) { for (auto& n : im.names) sel[n] = { im.path, n }; } else if (!im.alias.empty()) qual[im.alias] = im.path; else { auto d = im.path.find_last_of('.'); qual[d == std::string::npos ? im.path : im.path.substr(d + 1)] = im.path; } }
            recordDecls(m);
            auto& P = mods[m];
            for (auto& f : P.funcs) checkFunc(f, "");
            for (auto& Tr : P.traits) checkTraitDefaults(Tr);
            for (auto& X : P.extensions) checkExtensions(X);
            for (auto& C : P.classes) checkClass(C);
            for (auto& g : P.globals) if (g.hasExpr) { Env env; env.push_back(builtins()); TyP t = infer(g.expr.get(), env); if (g.vtype) { TyP d = resolveType(g.vtype, {}, curm); if (!assignable(t, d)) err(lineOf(g.expr.get()), "'" + g.name + "': cannot assign '" + tStr(t) + "' to '" + tStr(d) + "'"); } }
            if (P.hasInit) { std::string pc = curClass; TyP pr = curRet; curClass = ""; curRet = tPrim("Void"); Env env; env.push_back(builtins()); checkStmts(P.initBody, env); curClass = pc; curRet = pr; }
        }
        std::sort(errors.begin(), errors.end());
    }
};

