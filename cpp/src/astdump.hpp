#pragma once
#include "parser.hpp"
#include "doc.hpp"   // docTy

// ---------------------------------------------------------------------
// AST dump  (rosegoldc --ast)
// ---------------------------------------------------------------------
// A flat S-expression rendering of the parsed AST — the ground truth the
// self-hosted parser (examples/rgparser.rg) is diffed against, mirroring how
// --tokens grounds the self-hosted lexer. Canonical order: module, globals,
// funcs. Literals with awkward-to-match payloads (floats, strings) print their
// tag only. Covers the parser subset rgparser targets; match/closure/yield show
// a tag placeholder.
// ---------------------------------------------------------------------
static std::string adTy(const TyNodeP& t) { return t ? docTy(t) : "Void"; }

static void adExpr(const Expr* e, std::string& o) {
    if (!e) { o += "nil"; return; }
    switch (e->k) {
        case Expr::INT:    o += "(int " + std::to_string(e->ival) + ")"; break;
        case Expr::FLT:    o += "(flt)"; break;
        case Expr::STR:    o += "(str)"; break;
        case Expr::BOOL:   o += e->bval ? "(bool true)" : "(bool false)"; break;
        case Expr::NAME:   o += "(name " + e->sval + ")"; break;
        case Expr::UNARY:  o += "(unary " + e->op + " "; adExpr(e->lhs.get(), o); o += ")"; break;
        case Expr::BINARY: o += "(binop " + e->op + " "; adExpr(e->lhs.get(), o); o += " "; adExpr(e->rhs.get(), o); o += ")"; break;
        case Expr::CALL:   o += "(call "; adExpr(e->lhs.get(), o); for (auto& a : e->args) { o += " "; adExpr(a.get(), o); } o += ")"; break;
        case Expr::MEMBER: o += "(member "; adExpr(e->lhs.get(), o); o += " " + e->sval + ")"; break;
        case Expr::INDEX:  o += "(index "; adExpr(e->lhs.get(), o); o += " "; adExpr(e->rhs.get(), o); o += ")"; break;
        case Expr::LIST:   o += "(list"; for (auto& a : e->args) { o += " "; adExpr(a.get(), o); } o += ")"; break;
        case Expr::MATCH:  o += "(match)"; break;
        case Expr::CLOSURE:o += "(closure)"; break;
        case Expr::YIELD:  o += "(yield "; adExpr(e->lhs.get(), o); o += ")"; break;
    }
}
static void adStmt(const Stmt& s, std::string& o);
static void adBlock(const std::vector<Stmt>& b, std::string& o) { for (auto& s : b) { o += " "; adStmt(s, o); } }
static void adStmt(const Stmt& s, std::string& o) {
    switch (s.k) {
        case Stmt::VAR:    o += "(var " + s.name; if (s.hasExpr) { o += " "; adExpr(s.expr.get(), o); } o += ")"; break;
        case Stmt::ASSIGN: o += "(assign "; adExpr(s.target.get(), o); o += " "; adExpr(s.expr.get(), o); o += ")"; break;
        case Stmt::EXPR:   o += "(expr "; adExpr(s.expr.get(), o); o += ")"; break;
        case Stmt::RET:    o += "(return"; if (s.hasExpr) { o += " "; adExpr(s.expr.get(), o); } o += ")"; break;
        case Stmt::IF:     o += "(if "; adExpr(s.expr.get(), o); o += " (then"; adBlock(s.body, o); o += ")";
                           for (auto& el : s.elifs) { o += " (elif "; adExpr(el.first.get(), o); adBlock(el.second, o); o += ")"; }
                           if (s.hasElse) { o += " (else"; adBlock(s.elseBody, o); o += ")"; } o += ")"; break;
        case Stmt::WHILE:  o += "(while "; adExpr(s.expr.get(), o); adBlock(s.body, o); o += ")"; break;
        case Stmt::FOR:    o += "(for " + s.name + " "; adExpr(s.expr.get(), o); adBlock(s.body, o); o += ")"; break;
        case Stmt::PASS:   o += "(pass)"; break;
        case Stmt::BREAK:  o += "(break)"; break;
        case Stmt::CONTINUE:o += "(continue)"; break;
        case Stmt::TRY:    o += "(try"; adBlock(s.body, o); o += " (catch " + s.name; adBlock(s.elseBody, o); o += "))"; break;
        case Stmt::RAISE:  o += "(raise "; adExpr(s.expr.get(), o); o += ")"; break;
    }
}
static std::string dumpAst(const std::string& mod, Parsed& P) {
    std::string o = "(module " + mod + ")\n";
    for (auto& g : P.globals) { adStmt(g, o); o += "\n"; }
    for (auto& f : P.funcs) {
        o += "(func " + f.name + " (params";
        for (size_t k = 0; k < f.params.size(); k++) o += " (" + f.params[k] + " " + (k < f.ptypes.size() && f.ptypes[k] ? adTy(f.ptypes[k]) : "Any") + ")";
        o += ") " + (f.retType ? adTy(f.retType) : "Void");
        adBlock(f.body, o);
        o += ")\n";
    }
    return o;
}
