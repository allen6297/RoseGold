#pragma once
#include "lexer.hpp"
#include "ast.hpp"

// ---------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------
struct Parser {
    std::vector<Token> toks; size_t i = 0;
    Token& peek(int k = 0) { return toks[i + k]; }
    bool isKw(const char* v) { return peek().t == Tk::KW && peek().s == v; }
    bool isOp(const char* v) { return peek().t == Tk::OP && peek().s == v; }
    bool is(Tk t) { return peek().t == t; }
    Token next() { return toks[i++]; }
    [[noreturn]] void err(const std::string& m) { throw ParseError("line " + std::to_string(peek().line) + ": " + m); }
    void eatOp(const char* v) { if (!isOp(v)) err(std::string("expected '") + v + "'"); i++; }
    void eatKw(const char* v) { if (!isKw(v)) err(std::string("expected '") + v + "'"); i++; }
    std::string eatIdent() { if (!is(Tk::IDENT)) err("expected identifier"); return next().s; }
    void skipNL() { while (is(Tk::NEWLINE)) i++; }
    void skipVis() { while (isKw("pub") || isKw("internal") || isKw("private") || isKw("static")) i++; }
    void beginBlock() { eatOp(":"); if (!is(Tk::NEWLINE)) err("expected newline before block"); i++; if (!is(Tk::INDENT)) err("expected an indented block"); i++; skipNL(); }

    std::vector<std::string> dotted() { std::vector<std::string> ps{eatIdent()}; while (isOp(".") && peek(1).t == Tk::IDENT) { i++; ps.push_back(eatIdent()); } return ps; }

    std::vector<Stmt> suite() { beginBlock(); std::vector<Stmt> out; while (!is(Tk::DEDENT) && !is(Tk::END)) { out.push_back(statement()); skipNL(); } if (is(Tk::DEDENT)) i++; return out; }
    TyNodeP parseType() {
        auto t = std::make_shared<TyNode>();
        if (isKw("func")) { t->isFunc = true; i++; eatOp("("); if (!isOp(")")) { t->fparams.push_back(parseType()); while (isOp(",")) { i++; t->fparams.push_back(parseType()); } } eatOp(")"); eatOp("->"); t->fret = parseType(); return t; }
        t->name = eatIdent();
        if (isOp("<")) { i++; t->args.push_back(parseType()); while (isOp(",")) { i++; t->args.push_back(parseType()); } eatOp(">"); }
        return t;
    }
    std::vector<std::string> genericNames(Bounds& b) {
        std::vector<std::string> g;
        if (isOp("<")) { i++; genericParam(g, b); while (isOp(",")) { i++; genericParam(g, b); } eatOp(">"); }
        return g;
    }
    void genericParam(std::vector<std::string>& g, Bounds& b) {
        std::string n = eatIdent(); g.push_back(n);
        if (isOp(":")) { i++; b[n].push_back(parseType()->name); while (isOp("+")) { i++; b[n].push_back(parseType()->name); } }
    }
    struct PL { std::vector<std::string> names; std::vector<std::pair<int, int>> pos; std::vector<TyNodeP> types; };
    PL params() { PL pl; eatOp("("); if (!isOp(")")) { param(pl); while (isOp(",")) { i++; param(pl); } } eatOp(")"); return pl; }
    void param(PL& pl) { Token nt = peek(); pl.names.push_back(eatIdent()); pl.pos.push_back({nt.line, nt.col}); if (isOp(":")) { i++; pl.types.push_back(parseType()); } else pl.types.push_back(nullptr); }
    int parseVis() { int v = 0; while (isKw("pub") || isKw("internal") || isKw("private") || isKw("static")) { if (isKw("pub")) v = 1; else if (isKw("private")) v = 2; else if (isKw("internal") && v == 0) v = 3; i++; } return v; }

    Func func() { eatKw("func"); Func f; Token nt = peek(); f.name = eatIdent(); f.nameLine = nt.line; f.nameCol = nt.col; f.generics = genericNames(f.bounds); auto pl = params(); f.params = pl.names; f.paramPos = pl.pos; f.ptypes = pl.types; if (isOp("->")) { i++; f.retType = parseType(); } f.body = suite(); return f; }
    // trait method: an abstract signature (ends at NEWLINE), or a default implementation (has a `:` body).
    Func funcSig() { eatKw("func"); Func f; f.isSig = true; Token nt = peek(); f.name = eatIdent(); f.nameLine = nt.line; f.nameCol = nt.col; f.generics = genericNames(f.bounds); auto pl = params(); f.params = pl.names; f.paramPos = pl.pos; f.ptypes = pl.types; if (isOp("->")) { i++; f.retType = parseType(); } if (isOp(":")) f.body = suite(); return f; }
    TraitAst traitDecl() {
        eatKw("trait"); TraitAst Tr; Token nt = peek(); Tr.name = eatIdent(); Tr.nameLine = nt.line; Tr.nameCol = nt.col; Tr.generics = genericNames(Tr.bounds);
        if (isKw("uses")) { i++; Tr.uses.push_back(parseType()->name); while (isOp(",")) { i++; Tr.uses.push_back(parseType()->name); } }
        beginBlock();
        while (!is(Tk::DEDENT) && !is(Tk::END)) {
            int mv = parseVis();
            if (isKw("func")) { Func m = funcSig(); m.vis = mv; Tr.methods.push_back(std::move(m)); }
            else err("expected a trait method signature");
            skipNL();
        }
        if (is(Tk::DEDENT)) i++;
        return Tr;
    }

    ClassAst classDecl() {
        eatKw("class"); ClassAst C; Token nt = peek(); C.name = eatIdent(); C.nameLine = nt.line; C.nameCol = nt.col; C.generics = genericNames(C.bounds);
        if (isKw("extends")) { i++; C.extends = parseType()->name; }
        if (isKw("uses")) { i++; C.uses.push_back(parseType()->name); while (isOp(",")) { i++; C.uses.push_back(parseType()->name); } }
        beginBlock();
        while (!is(Tk::DEDENT) && !is(Tk::END)) {
            int mv = parseVis();
            if (isKw("var") || isKw("const")) { i++; Field f; f.vis = mv; Token nt = peek(); f.name = eatIdent(); f.nameLine = nt.line; f.nameCol = nt.col; if (isOp(":")) { i++; f.type = parseType(); } if (isOp("=")) { i++; f.init = expr(); f.hasInit = true; } C.fields.push_back(std::move(f)); }
            else if (isKw("init")) { i++; C.hasCtor = true; auto pl = params(); C.ctorParams = pl.names; C.ctorParamPos = pl.pos; C.ctorPtypes = pl.types; C.ctorBody = suite(); }
            else if (isKw("func")) { Func m = func(); m.vis = mv; C.methods.push_back(std::move(m)); }
            else err("expected a class member");
            skipNL();
        }
        if (is(Tk::DEDENT)) i++;
        return C;
    }
    ExtendAst extendDecl() {
        eatKw("extend"); ExtendAst X; Token nt = peek(); X.typeName = eatIdent(); X.nameLine = nt.line; X.nameCol = nt.col;
        if (isKw("uses")) { i++; X.uses.push_back(parseType()->name); while (isOp(",")) { i++; X.uses.push_back(parseType()->name); } }
        beginBlock();
        while (!is(Tk::DEDENT) && !is(Tk::END)) {
            parseVis();
            if (isKw("func")) X.methods.push_back(func());
            else err("expected a method in the extension");
            skipNL();
        }
        if (is(Tk::DEDENT)) i++;
        return X;
    }
    EnumAst enumDecl() {
        eatKw("enum"); EnumAst E; Token nt = peek(); E.name = eatIdent(); E.nameLine = nt.line; E.nameCol = nt.col; Bounds _eb; E.generics = genericNames(_eb); beginBlock();
        while (!is(Tk::DEDENT) && !is(Tk::END)) {
            std::string vn = eatIdent(); std::vector<TyNodeP> fts;
            if (isOp("(")) { i++; eatIdent(); eatOp(":"); fts.push_back(parseType()); while (isOp(",")) { i++; eatIdent(); eatOp(":"); fts.push_back(parseType()); } eatOp(")"); }
            E.variants.emplace_back(vn, std::move(fts)); skipNL();
        }
        if (is(Tk::DEDENT)) i++;
        return E;
    }

    Stmt statement() {
        if (isKw("if")) return ifStmt();
        if (isKw("while")) return whileStmt();
        if (isKw("for")) return forStmt();
        if (isKw("try")) return tryStmt();
        if (isKw("var") || isKw("const")) return varStmt();
        if (isKw("return")) return retStmt();
        if (isKw("raise")) { i++; Stmt s; s.k = Stmt::RAISE; s.expr = expr(); s.hasExpr = true; return s; }
        if (isKw("break")) { i++; Stmt s; s.k = Stmt::BREAK; return s; }
        if (isKw("continue")) { i++; Stmt s; s.k = Stmt::CONTINUE; return s; }
        if (isKw("pass")) { i++; Stmt s; s.k = Stmt::PASS; return s; }
        ExprP e = expr();
        if (isOp("=")) { i++; if (e->k != Expr::NAME && e->k != Expr::INDEX && e->k != Expr::MEMBER) err("left side of assignment is not assignable"); Stmt s; s.k = Stmt::ASSIGN; s.target = std::move(e); s.expr = expr(); s.hasExpr = true; return s; }
        Stmt s; s.k = Stmt::EXPR; s.expr = std::move(e); s.hasExpr = true; return s;
    }
    Stmt varStmt() { Stmt s; s.k = Stmt::VAR; bool c = isKw("const"); i++; Token nt = peek(); s.name = eatIdent(); s.nameLine = nt.line; s.nameCol = nt.col; if (isOp(":")) { i++; s.vtype = parseType(); } if (isOp("=")) { i++; s.expr = expr(); s.hasExpr = true; } else if (c) err("const must be initialized"); return s; }
    Stmt retStmt() { eatKw("return"); Stmt s; s.k = Stmt::RET; if (!is(Tk::NEWLINE) && !is(Tk::DEDENT) && !is(Tk::END)) { s.expr = expr(); s.hasExpr = true; } return s; }
    Stmt ifStmt() { eatKw("if"); Stmt s; s.k = Stmt::IF; s.expr = expr(); s.hasExpr = true; s.body = suite(); while (isKw("elif")) { i++; ExprP c = expr(); auto b = suite(); s.elifs.emplace_back(std::move(c), std::move(b)); } if (isKw("else")) { i++; s.elseBody = suite(); s.hasElse = true; } return s; }
    Stmt whileStmt() { eatKw("while"); Stmt s; s.k = Stmt::WHILE; s.expr = expr(); s.hasExpr = true; s.body = suite(); return s; }
    Stmt forStmt() { eatKw("for"); Stmt s; s.k = Stmt::FOR; Token nt = peek(); s.name = eatIdent(); s.nameLine = nt.line; s.nameCol = nt.col; eatKw("in"); s.expr = expr(); s.hasExpr = true; s.body = suite(); return s; }
    Stmt tryStmt() { eatKw("try"); Stmt s; s.k = Stmt::TRY; s.body = suite(); eatKw("catch"); Token nt = peek(); s.name = eatIdent(); s.nameLine = nt.line; s.nameCol = nt.col; s.elseBody = suite(); return s; }

    ExprP expr() { return orE(); }
    ExprP binL(const std::vector<std::string>& ops, ExprP (Parser::*sub)()) {
        ExprP left = (this->*sub)();
        while (peek().t == Tk::OP) { bool m = false; for (auto& o : ops) if (peek().s == o) { m = true; break; } if (!m) break; std::string op = next().s; ExprP right = (this->*sub)(); auto e = std::make_unique<Expr>(); e->k = Expr::BINARY; e->op = op; e->lhs = std::move(left); e->line = e->lhs->line; e->rhs = std::move(right); left = std::move(e); }
        return left;
    }
    ExprP orE() { return binL({"||"}, &Parser::andE); }
    ExprP andE() { return binL({"&&"}, &Parser::eqE); }
    ExprP eqE() { return binL({"==", "!="}, &Parser::cmpE); }
    ExprP cmpE() { return binL({"<", "<=", ">", ">="}, &Parser::addE); }
    ExprP addE() { return binL({"+", "-"}, &Parser::mulE); }
    ExprP mulE() { return binL({"*", "/", "%"}, &Parser::unaryE); }
    ExprP unaryE() { if (isOp("!") || isOp("-")) { std::string op = next().s; auto e = std::make_unique<Expr>(); e->k = Expr::UNARY; e->op = op; e->lhs = unaryE(); return e; } return postfixE(); }
    ExprP postfixE() {
        ExprP e = primaryE();
        while (true) {
            if (isOp("(")) { i++; auto call = std::make_unique<Expr>(); call->k = Expr::CALL; call->line = e->line; call->lhs = std::move(e); if (!isOp(")")) { call->args.push_back(expr()); while (isOp(",")) { i++; call->args.push_back(expr()); } } eatOp(")"); e = std::move(call); }
            else if (isOp("[")) { i++; auto idx = std::make_unique<Expr>(); idx->k = Expr::INDEX; idx->lhs = std::move(e); idx->line = idx->lhs->line; idx->rhs = expr(); eatOp("]"); e = std::move(idx); }
            else if (isOp(".")) { i++; auto m = std::make_unique<Expr>(); m->k = Expr::MEMBER; m->lhs = std::move(e); Token ft = peek(); m->sval = eatIdent(); m->line = ft.line; m->col = ft.col; e = std::move(m); }
            else break;
        }
        return e;
    }
    ExprP litExpr() {
        Token t = peek(); auto e = std::make_unique<Expr>(); e->line = t.line;
        if (t.t == Tk::INT) { i++; e->k = Expr::INT; e->ival = std::stoll(t.s); return e; }
        if (t.t == Tk::FLT) { i++; e->k = Expr::FLT; e->dval = std::stod(t.s); return e; }
        if (t.t == Tk::STR) { i++; e->k = Expr::STR; e->sval = t.s; return e; }
        if (isKw("true")) { i++; e->k = Expr::BOOL; e->bval = true; return e; }
        if (isKw("false")) { i++; e->k = Expr::BOOL; e->bval = false; return e; }
        err("expected a literal");
    }
    ExprP closureExpr() {
        eatKw("func"); auto e = std::make_unique<Expr>(); e->k = Expr::CLOSURE; auto pl = params(); e->params = pl.names; e->ptypes = pl.types;
        if (isOp("->")) { i++; e->retType = parseType(); }
        eatOp("=>"); e->lhs = expr(); return e;
    }
    ExprP primaryE() {
        Token t = peek();
        if (t.t == Tk::INT || t.t == Tk::FLT || t.t == Tk::STR || isKw("true") || isKw("false")) return litExpr();
        if (isKw("match")) return matchExpr();
        if (isKw("func")) return closureExpr();
        if (t.t == Tk::IDENT) { auto e = std::make_unique<Expr>(); e->line = t.line; e->col = t.col; i++; e->k = Expr::NAME; e->sval = t.s; return e; }
        if (isOp("(")) { i++; ExprP inner = expr(); eatOp(")"); return inner; }
        if (isOp("[")) { i++; auto e2 = std::make_unique<Expr>(); e2->k = Expr::LIST; if (!isOp("]")) { e2->args.push_back(expr()); while (isOp(",")) { i++; if (isOp("]")) break; e2->args.push_back(expr()); } } eatOp("]"); return e2; }
        err("expected an expression");
    }
    Pattern pattern() {
        Token t = peek();
        if (t.t == Tk::IDENT && t.s == "_") { i++; Pattern p; p.k = 0; return p; }
        if (t.t == Tk::INT || t.t == Tk::FLT || t.t == Tk::STR || isKw("true") || isKw("false")) { Pattern p; p.k = 1; p.lit = litExpr(); return p; }
        if (t.t == Tk::IDENT) { Pattern p; p.k = 2; p.name = eatIdent(); if (isOp("(")) { i++; p.binds.push_back(eatIdent()); while (isOp(",")) { i++; p.binds.push_back(eatIdent()); } eatOp(")"); } return p; }
        err("expected a pattern");
    }
    ExprP matchExpr() {
        eatKw("match"); auto e = std::make_unique<Expr>(); e->k = Expr::MATCH; e->lhs = expr(); beginBlock();
        while (!is(Tk::DEDENT) && !is(Tk::END)) { Arm arm; arm.pats.push_back(pattern()); while (isOp(",")) { i++; arm.pats.push_back(pattern()); } eatOp(":"); if (is(Tk::NEWLINE)) err("block match arms are not supported in this build"); arm.body = expr(); e->arms.push_back(std::move(arm)); skipNL(); }
        if (is(Tk::DEDENT)) i++;
        return e;
    }
    Import importDecl(bool pub) {
        eatKw("import"); Import im; im.pub = pub; auto ps = dotted(); im.path = ps[0]; for (size_t k = 1; k < ps.size(); k++) im.path += "." + ps[k];
        if (isOp(".")) { i++; eatOp("("); im.names.push_back(eatIdent()); while (isOp(",")) { i++; im.names.push_back(eatIdent()); } eatOp(")"); }
        else if (isKw("as")) { i++; im.alias = eatIdent(); }
        return im;
    }
    Parsed program() {
        Parsed p; skipNL();
        if (isKw("module")) { i++; auto ps = dotted(); p.module = ps[0]; for (size_t k = 1; k < ps.size(); k++) p.module += "." + ps[k]; skipNL(); }
        while (!is(Tk::END)) {
            skipNL(); if (is(Tk::END)) break;
            bool pub = false;
            if (isKw("pub") && peek(1).t == Tk::KW && peek(1).s == "import") { i++; pub = true; }
            if (isKw("import")) { p.imports.push_back(importDecl(pub)); skipNL(); continue; }
            int tv = parseVis();
            if (isKw("func")) { Func f = func(); f.vis = tv; p.funcs.push_back(std::move(f)); }
            else if (isKw("class")) { ClassAst c = classDecl(); c.vis = tv; p.classes.push_back(std::move(c)); }
            else if (isKw("trait")) { TraitAst t = traitDecl(); t.vis = tv; p.traits.push_back(std::move(t)); }
            else if (isKw("extend")) { p.extensions.push_back(extendDecl()); }
            else if (isKw("enum")) { EnumAst e = enumDecl(); e.vis = tv; p.enums.push_back(std::move(e)); }
            else if (isKw("var") || isKw("const")) { Stmt g = varStmt(); g.vis = tv; p.globals.push_back(std::move(g)); }
            else err("unexpected top-level construct");
            skipNL();
        }
        return p;
    }
};

