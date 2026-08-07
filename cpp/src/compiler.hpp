#pragma once
#include "value.hpp"
#include "ast.hpp"

// ---------------------------------------------------------------------
// Bytecode + Compiler
// ---------------------------------------------------------------------
enum class Op {
    CONST, PUSHNIL, LOAD, STORE, LOADG, STOREG, POP,
    ADD, SUB, MUL, DIV, MOD, NEG, NOT, LT, LE, GT, GE, EQ, NE,
    JUMP, JFALSE, JTRUE, MAKELIST, IGET, ISET,
    NEWOBJ, GETPROP, SETPROP, INVOKE, MKVARIANT, ISVARIANT, VGET,
    MKCLOSURE, CALLV, SETUP_TRY, POP_TRY, RAISE, CALL, BUILTIN, RET
};
struct Instr { Op op; int a = 0; int b = 0; };
struct CFunc { std::string name; int nlocals = 0; std::vector<Instr> code; };
struct ClassDesc { std::string name; std::vector<std::string> fieldNames; int newFunc = -1; std::map<std::string, int> methods; };
struct VDesc { std::string enumName, name; int arity; };
struct Sym { int kind; int index; };   // kind: 0 FUNC, 1 CLASS, 2 VARIANT, 3 GLOBAL
struct Program {
    std::vector<CFunc> funcs; std::vector<Value> consts; int nglobals = 0;
    std::vector<ClassDesc> classes; std::vector<VDesc> variants;
    std::map<std::string, std::map<std::string, Sym>> syms;   // module -> name -> sym
    std::map<std::string, std::map<std::string, int>> extensions;   // type tag (Int/Float/String/Bool/List/Map) -> method -> funcIndex
};
struct ModuleCtx {
    std::string name; std::map<std::string, Sym>* sym = nullptr;
    std::map<std::string, std::string> qual;                       // qualifier -> target module
    std::map<std::string, std::pair<std::string, std::string>> sel; // name -> (module, name)
};
struct CompileError : std::runtime_error { using std::runtime_error::runtime_error; };
struct Loop { std::vector<int> breaks, continues; };
struct Pending { int fi; std::vector<std::string> caps; std::vector<std::string> params; Expr* body; };

struct Compiler {
    Program& prog; ModuleCtx* mc = nullptr;
    int curFi = -1; std::map<std::string, int> locals; int nextSlot = 0, uid = 0;
    std::vector<Loop> loops; std::vector<Pending> pending;
    Compiler(Program& p) : prog(p) {}

    CFunc& CF() { return prog.funcs[curFi]; }
    int addConst(Value v) { prog.consts.push_back(std::move(v)); return (int)prog.consts.size() - 1; }
    int nc(const std::string& s) { return addConst(Value{s}); }
    int emit(Op op, int a = 0, int b = 0) { CF().code.push_back({op, a, b}); return (int)CF().code.size() - 1; }
    int here() { return (int)CF().code.size(); }
    void patch(int at, int t) { CF().code[at].a = t; }
    int declare(const std::string& n) { int s = nextSlot++; locals[n] = s; return s; }

    bool resolveUn(const std::string& name, Sym& out) {
        auto it = mc->sym->find(name); if (it != mc->sym->end()) { out = it->second; return true; }
        auto s = mc->sel.find(name); if (s != mc->sel.end()) { auto& t = prog.syms[s->second.first]; auto j = t.find(s->second.second); if (j != t.end()) { out = j->second; return true; } }
        return false;
    }
    void emitStore(const std::string& name) {
        auto it = locals.find(name); if (it != locals.end()) { emit(Op::STORE, it->second); return; }
        Sym s; if (resolveUn(name, s) && s.kind == 3) { emit(Op::STOREG, s.index); return; }
        throw CompileError("cannot assign to '" + name + "'");
    }

    void beginFunc(int fi, const std::vector<std::string>& ps) { curFi = fi; locals.clear(); nextSlot = 0; loops.clear(); for (auto& p : ps) declare(p); }
    void endFunc() { emit(Op::PUSHNIL); emit(Op::RET); CF().nlocals = nextSlot; }

    void compileFunc(int fi, Func& f) { beginFunc(fi, f.params); for (auto& s : f.body) stmt(s); endFunc(); }
    void compileMethod(int fi, Func& f) { beginFunc(fi, f.params); for (auto& s : f.body) stmt(s); endFunc(); }
    void compileClass(ClassAst& C, int ci) {
        for (auto& m : C.methods) compileMethod(prog.classes[ci].methods[m.name], m);
        beginFunc(prog.classes[ci].newFunc, C.ctorParams);
        int self = declare("self"); emit(Op::NEWOBJ, ci); emit(Op::STORE, self);
        for (auto& f : C.fields) { emit(Op::LOAD, self); if (f.hasInit) expr(*f.init); else emit(Op::PUSHNIL); emit(Op::SETPROP, nc(f.name)); }
        for (auto& s : C.ctorBody) stmt(s);
        emit(Op::LOAD, self); emit(Op::RET); CF().nlocals = nextSlot;
    }
    void compileGlobals(int fi, std::vector<Stmt>& gs) { beginFunc(fi, {}); for (auto& s : gs) { if (s.hasExpr) expr(*s.expr); else emit(Op::PUSHNIL); Sym sy = (*mc->sym)[s.name]; emit(Op::STOREG, sy.index); } emit(Op::PUSHNIL); emit(Op::RET); CF().nlocals = nextSlot; }

    void drainPending() {
        for (size_t k = 0; k < pending.size(); k++) {
            Pending pc = pending[k];
            std::vector<std::string> ps = pc.caps; for (auto& p : pc.params) ps.push_back(p);
            beginFunc(pc.fi, ps);
            expr(*pc.body); emit(Op::RET); CF().nlocals = nextSlot;
        }
    }

    void block(std::vector<Stmt>& b) { for (auto& s : b) stmt(s); }
    void stmt(Stmt& s) {
        switch (s.k) {
            case Stmt::VAR: if (s.hasExpr) expr(*s.expr); else emit(Op::PUSHNIL); emit(Op::STORE, declare(s.name)); break;
            case Stmt::ASSIGN:
                if (s.target->k == Expr::NAME) { expr(*s.expr); emitStore(s.target->sval); }
                else if (s.target->k == Expr::INDEX) { expr(*s.target->lhs); expr(*s.target->rhs); expr(*s.expr); emit(Op::ISET); }
                else { expr(*s.target->lhs); expr(*s.expr); emit(Op::SETPROP, nc(s.target->sval)); }
                break;
            case Stmt::EXPR: expr(*s.expr); emit(Op::POP); break;
            case Stmt::RET: if (s.hasExpr) expr(*s.expr); else emit(Op::PUSHNIL); emit(Op::RET); break;
            case Stmt::RAISE: expr(*s.expr); emit(Op::RAISE); break;
            case Stmt::PASS: break;
            case Stmt::BREAK: if (loops.empty()) throw CompileError("'break' outside a loop"); loops.back().breaks.push_back(emit(Op::JUMP)); break;
            case Stmt::CONTINUE: if (loops.empty()) throw CompileError("'continue' outside a loop"); loops.back().continues.push_back(emit(Op::JUMP)); break;
            case Stmt::WHILE: { int cond = here(); expr(*s.expr); int jf = emit(Op::JFALSE); loops.push_back({}); block(s.body); emit(Op::JUMP, cond); int end = here(); patch(jf, end); for (int b : loops.back().breaks) patch(b, end); for (int c : loops.back().continues) patch(c, cond); loops.pop_back(); break; }
            case Stmt::FOR: {
                int u = uid++; expr(*s.expr); int it = declare("$it" + std::to_string(u)); emit(Op::STORE, it);
                emit(Op::CONST, addConst(Value{(int64_t)0})); int ix = declare("$ix" + std::to_string(u)); emit(Op::STORE, ix);
                int vr = declare(s.name); int cond = here();
                emit(Op::LOAD, ix); emit(Op::LOAD, it); emit(Op::BUILTIN, 1, 1); emit(Op::LT); int jf = emit(Op::JFALSE);
                emit(Op::LOAD, it); emit(Op::LOAD, ix); emit(Op::IGET); emit(Op::STORE, vr);
                loops.push_back({}); block(s.body); int cont = here();
                emit(Op::LOAD, ix); emit(Op::CONST, addConst(Value{(int64_t)1})); emit(Op::ADD); emit(Op::STORE, ix); emit(Op::JUMP, cond);
                int end = here(); patch(jf, end); for (int b : loops.back().breaks) patch(b, end); for (int c : loops.back().continues) patch(c, cont); loops.pop_back(); break;
            }
            case Stmt::TRY: { int cs = declare(s.name); int setup = emit(Op::SETUP_TRY); block(s.body); emit(Op::POP_TRY); int jend = emit(Op::JUMP); patch(setup, here()); emit(Op::STORE, cs); block(s.elseBody); patch(jend, here()); break; }
            case Stmt::IF: { std::vector<int> ends; expr(*s.expr); int jf = emit(Op::JFALSE); block(s.body); ends.push_back(emit(Op::JUMP)); patch(jf, here()); for (auto& br : s.elifs) { expr(*br.first); int j = emit(Op::JFALSE); block(br.second); ends.push_back(emit(Op::JUMP)); patch(j, here()); } if (s.hasElse) block(s.elseBody); for (int e : ends) patch(e, here()); break; }
        }
    }

    void expr(Expr& e) {
        switch (e.k) {
            case Expr::INT: emit(Op::CONST, addConst(Value{(int64_t)e.ival})); break;
            case Expr::FLT: emit(Op::CONST, addConst(Value{(double)e.dval})); break;
            case Expr::STR: emit(Op::CONST, addConst(Value{e.sval})); break;
            case Expr::BOOL: emit(Op::CONST, addConst(Value{e.bval})); break;
            case Expr::NAME: {
                auto it = locals.find(e.sval); if (it != locals.end()) { emit(Op::LOAD, it->second); break; }
                Sym s; if (resolveUn(e.sval, s)) {
                    if (s.kind == 3) emit(Op::LOADG, s.index);
                    else if (s.kind == 0) emit(Op::MKCLOSURE, s.index, 0);          // function value
                    else if (s.kind == 2 && prog.variants[s.index].arity == 0) emit(Op::MKVARIANT, s.index, 0);
                    else throw CompileError("'" + e.sval + "' cannot be used as a value");
                    break;
                }
                throw CompileError("undefined name '" + e.sval + "'");
            }
            case Expr::UNARY: expr(*e.lhs); emit(e.op == "!" ? Op::NOT : Op::NEG); break;
            case Expr::BINARY: binary(e); break;
            case Expr::LIST: for (auto& a : e.args) expr(*a); emit(Op::MAKELIST, (int)e.args.size()); break;
            case Expr::INDEX: expr(*e.lhs); expr(*e.rhs); emit(Op::IGET); break;
            case Expr::MEMBER: member(e); break;
            case Expr::MATCH: match(e); break;
            case Expr::CLOSURE: closure(e); break;
            case Expr::CALL: call(e); break;
        }
    }

    void member(Expr& e) {  // read q.name (module member) or obj.field (instance)
        if (e.lhs->k == Expr::NAME) {
            auto q = mc->qual.find(e.lhs->sval);
            if (q != mc->qual.end()) {
                auto& t = prog.syms[q->second]; auto it = t.find(e.sval);
                if (it == t.end()) throw CompileError("module '" + q->second + "' has no member '" + e.sval + "'");
                Sym s = it->second;
                if (s.kind == 3) emit(Op::LOADG, s.index);
                else if (s.kind == 0) emit(Op::MKCLOSURE, s.index, 0);
                else if (s.kind == 2 && prog.variants[s.index].arity == 0) emit(Op::MKVARIANT, s.index, 0);
                else throw CompileError("cannot use '" + q->second + "." + e.sval + "' as a value");
                return;
            }
        }
        expr(*e.lhs); emit(Op::GETPROP, nc(e.sval));
    }

    void call(Expr& e) {
        Expr* callee = e.lhs.get();
        if (callee->k == Expr::MEMBER) {
            // module function call q.f(args) ?
            if (callee->lhs->k == Expr::NAME) {
                auto q = mc->qual.find(callee->lhs->sval);
                if (q != mc->qual.end()) {
                    auto& t = prog.syms[q->second]; auto it = t.find(callee->sval);
                    if (it == t.end()) throw CompileError("module '" + q->second + "' has no member '" + callee->sval + "'");
                    Sym s = it->second; for (auto& a : e.args) expr(*a); int argc = (int)e.args.size();
                    if (s.kind == 0) emit(Op::CALL, s.index, argc);
                    else if (s.kind == 1) emit(Op::CALL, prog.classes[s.index].newFunc, argc);
                    else if (s.kind == 2) emit(Op::MKVARIANT, s.index, argc);
                    else throw CompileError("'" + q->second + "." + callee->sval + "' is not callable");
                    return;
                }
            }
            // method call obj.m(args)
            expr(*callee->lhs); for (auto& a : e.args) expr(*a); emit(Op::INVOKE, nc(callee->sval), (int)e.args.size());
            return;
        }
        if (callee->k != Expr::NAME) { expr(*callee); for (auto& a : e.args) expr(*a); emit(Op::CALLV, (int)e.args.size()); return; }
        std::string name = callee->sval; int argc = (int)e.args.size();
        auto lit = locals.find(name);
        if (lit != locals.end()) { emit(Op::LOAD, lit->second); for (auto& a : e.args) expr(*a); emit(Op::CALLV, argc); return; }
        static const std::map<std::string, int> BI = { {"print", 0}, {"len", 1}, {"range", 2}, {"push", 3}, {"pop", 4}, {"str", 5}, {"ord", 6}, {"chr", 7}, {"substr", 8}, {"split", 9}, {"int", 10}, {"readFile", 11}, {"writeFile", 12}, {"map", 13}, {"set", 14}, {"get", 15}, {"has", 16}, {"keys", 17}, {"remove", 18} };
        auto bi = BI.find(name); if (bi != BI.end()) { for (auto& a : e.args) expr(*a); emit(Op::BUILTIN, bi->second, argc); return; }
        Sym s;
        if (resolveUn(name, s)) {
            if (s.kind == 3) { emit(Op::LOADG, s.index); for (auto& a : e.args) expr(*a); emit(Op::CALLV, argc); return; }
            for (auto& a : e.args) expr(*a);
            if (s.kind == 0) emit(Op::CALL, s.index, argc);
            else if (s.kind == 1) emit(Op::CALL, prog.classes[s.index].newFunc, argc);
            else emit(Op::MKVARIANT, s.index, argc);
            return;
        }
        throw CompileError("unknown function '" + name + "'");
    }

    void closure(Expr& e) {
        std::set<std::string> ps(e.params.begin(), e.params.end());
        std::vector<std::string> caps; std::set<std::string> seen;
        collectFree(e.lhs.get(), ps, caps, seen);
        for (auto& c : caps) emit(Op::LOAD, locals[c]);
        int fi = (int)prog.funcs.size(); prog.funcs.push_back({"$cl" + std::to_string(uid++)});
        emit(Op::MKCLOSURE, fi, (int)caps.size());
        pending.push_back({fi, caps, e.params, e.lhs.get()});
    }
    void collectFree(Expr* e, std::set<std::string> bound, std::vector<std::string>& caps, std::set<std::string>& seen) {
        if (!e) return;
        switch (e->k) {
            case Expr::NAME:
                if (!bound.count(e->sval) && !seen.count(e->sval) && locals.count(e->sval)) { caps.push_back(e->sval); seen.insert(e->sval); }
                break;
            case Expr::MEMBER: collectFree(e->lhs.get(), bound, caps, seen); break;
            case Expr::UNARY: collectFree(e->lhs.get(), bound, caps, seen); break;
            case Expr::BINARY: collectFree(e->lhs.get(), bound, caps, seen); collectFree(e->rhs.get(), bound, caps, seen); break;
            case Expr::INDEX: collectFree(e->lhs.get(), bound, caps, seen); collectFree(e->rhs.get(), bound, caps, seen); break;
            case Expr::CALL: collectFree(e->lhs.get(), bound, caps, seen); for (auto& a : e->args) collectFree(a.get(), bound, caps, seen); break;
            case Expr::LIST: for (auto& a : e->args) collectFree(a.get(), bound, caps, seen); break;
            case Expr::MATCH: { collectFree(e->lhs.get(), bound, caps, seen); for (auto& arm : e->arms) { auto b2 = bound; for (auto& p : arm.pats) for (auto& bd : p.binds) b2.insert(bd); collectFree(arm.body.get(), b2, caps, seen); } break; }
            case Expr::CLOSURE: { auto b2 = bound; for (auto& p : e->params) b2.insert(p); collectFree(e->lhs.get(), b2, caps, seen); break; }
            default: break;
        }
    }

    void match(Expr& e) {
        expr(*e.lhs); int ms = declare("$m" + std::to_string(uid++)); emit(Op::STORE, ms);
        std::vector<int> ends;
        for (auto& arm : e.arms) {
            std::vector<int> toBody;
            for (auto& p : arm.pats) {
                if (p.k == 0) toBody.push_back(emit(Op::JUMP));
                else if (p.k == 1) { emit(Op::LOAD, ms); expr(*p.lit); emit(Op::EQ); toBody.push_back(emit(Op::JTRUE)); }
                else { emit(Op::LOAD, ms); emit(Op::ISVARIANT, nc(p.name)); int miss = emit(Op::JFALSE); for (size_t bi = 0; bi < p.binds.size(); bi++) { emit(Op::LOAD, ms); emit(Op::VGET, (int)bi); emit(Op::STORE, declare(p.binds[bi])); } toBody.push_back(emit(Op::JUMP)); patch(miss, here()); }
            }
            int lnext = emit(Op::JUMP); int lbody = here(); for (int j : toBody) patch(j, lbody);
            expr(*arm.body); ends.push_back(emit(Op::JUMP)); patch(lnext, here());
        }
        emit(Op::PUSHNIL); int lend = here(); for (int j : ends) patch(j, lend);
    }

    void binary(Expr& e) {
        if (e.op == "&&") { expr(*e.lhs); int jf = emit(Op::JFALSE); expr(*e.rhs); int je = emit(Op::JUMP); patch(jf, here()); emit(Op::CONST, addConst(Value{false})); patch(je, here()); return; }
        if (e.op == "||") { expr(*e.lhs); int jt = emit(Op::JTRUE); expr(*e.rhs); int je = emit(Op::JUMP); patch(jt, here()); emit(Op::CONST, addConst(Value{true})); patch(je, here()); return; }
        expr(*e.lhs); expr(*e.rhs);
        static const std::map<std::string, Op> M = {{"+", Op::ADD}, {"-", Op::SUB}, {"*", Op::MUL}, {"/", Op::DIV}, {"%", Op::MOD}, {"<", Op::LT}, {"<=", Op::LE}, {">", Op::GT}, {">=", Op::GE}, {"==", Op::EQ}, {"!=", Op::NE}};
        emit(M.at(e.op));
    }
};

