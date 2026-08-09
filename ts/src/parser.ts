// RoseGold parser — a faithful port of cpp/src/parser.hpp. Recursive-descent
// over the lexer's token stream, producing the typed AST in ast.ts. The full
// grammar is parsed (module, imports, globals, funcs, classes, traits, enums,
// extern, init, match, closures); `rosegoldc --ast` dumps the module/globals/
// funcs subset, and ts/test/parity.mjs verifies that dump is byte-identical.

import type { Token, Kind } from "./lexer.ts";
import {
  type TyNode, type Pattern, type Arm, type Expr, type Stmt, type Bounds,
  type Func, type Field, type SignalDecl, type ClassAst, type TraitAst,
  type ExtendAst, type EnumAst, type Import, type Parsed,
  mkTy, mkExpr, mkStmt, mkFunc, mkField, mkSignal, mkClass, mkTrait, mkExtend, mkEnum, mkParsed,
  ParseError,
} from "./ast.ts";

interface PL { names: string[]; lines: number[]; types: (TyNode | null)[]; }

export class Parser {
  toks: Token[];
  i = 0;
  constructor(toks: Token[]) { this.toks = toks; }

  // END is always the last token, so an out-of-range peek clamps to it (sentinel).
  peek(k = 0): Token { return this.toks[Math.min(this.i + k, this.toks.length - 1)]; }
  isKw(v: string): boolean { return this.peek().kind === "KW" && this.peek().value === v; }
  isOp(v: string): boolean { return this.peek().kind === "OP" && this.peek().value === v; }
  is(t: Kind): boolean { return this.peek().kind === t; }
  next(): Token { return this.toks[this.i++]; }
  err(m: string): never { throw new ParseError("line " + this.peek().line + ": " + m); }
  eatOp(v: string): void { if (!this.isOp(v)) this.err(`expected '${v}'`); this.i++; }
  eatKw(v: string): void { if (!this.isKw(v)) this.err(`expected '${v}'`); this.i++; }
  eatIdent(): string { if (!this.is("IDENT")) this.err("expected identifier"); return this.next().value; }
  skipNL(): void { while (this.is("NEWLINE")) this.i++; }
  beginBlock(): void {
    this.eatOp(":");
    if (!this.is("NEWLINE")) this.err("expected newline before block");
    this.i++;
    if (!this.is("INDENT")) this.err("expected an indented block");
    this.i++;
    this.skipNL();
  }

  dotted(): string[] {
    const ps: string[] = [this.eatIdent()];
    while (this.isOp(".") && this.peek(1).kind === "IDENT") { this.i++; ps.push(this.eatIdent()); }
    return ps;
  }

  suite(): Stmt[] {
    this.beginBlock();
    const out: Stmt[] = [];
    while (!this.is("DEDENT") && !this.is("END")) { out.push(this.statement()); this.skipNL(); }
    if (this.is("DEDENT")) this.i++;
    return out;
  }

  parseType(): TyNode {
    const t = mkTy();
    if (this.isKw("fn")) {
      t.isFunc = true; this.i++;
      this.eatOp("(");
      if (!this.isOp(")")) { t.fparams.push(this.parseType()); while (this.isOp(",")) { this.i++; t.fparams.push(this.parseType()); } }
      this.eatOp(")"); this.eatOp("->"); t.fret = this.parseType();
      return t;
    }
    t.name = this.eatIdent();
    if (this.isOp("<")) { this.i++; t.args.push(this.parseType()); while (this.isOp(",")) { this.i++; t.args.push(this.parseType()); } this.eatOp(">"); }
    return t;
  }

  genericNames(b: Bounds): string[] {
    const g: string[] = [];
    if (this.isOp("<")) { this.i++; this.genericParam(g, b); while (this.isOp(",")) { this.i++; this.genericParam(g, b); } this.eatOp(">"); }
    return g;
  }
  genericParam(g: string[], b: Bounds): void {
    const n = this.eatIdent(); g.push(n);
    if (this.isOp(":")) {
      this.i++;
      const add = (tn: string) => { if (!b.has(n)) b.set(n, []); b.get(n)!.push(tn); };
      add(this.parseType().name);
      while (this.isOp("+")) { this.i++; add(this.parseType().name); }
    }
  }

  params(): PL {
    const pl: PL = { names: [], lines: [], types: [] };
    this.eatOp("(");
    if (!this.isOp(")")) { this.param(pl); while (this.isOp(",")) { this.i++; this.param(pl); } }
    this.eatOp(")");
    return pl;
  }
  param(pl: PL): void {
    const nt = this.peek();
    pl.names.push(this.eatIdent());
    pl.lines.push(nt.line);
    if (this.isOp(":")) { this.i++; pl.types.push(this.parseType()); } else pl.types.push(null);
  }

  parseVis(): number {
    let v = 0;
    while (this.isKw("pub") || this.isKw("internal") || this.isKw("private") || this.isKw("static")) {
      if (this.isKw("pub")) v = 1;
      else if (this.isKw("private")) v = 2;
      else if (this.isKw("internal") && v === 0) v = 3;
      this.i++;
    }
    return v;
  }

  func(): Func {
    this.eatKw("fn"); const f = mkFunc(); const nt = this.peek();
    f.name = this.eatIdent(); f.nameLine = nt.line;
    f.generics = this.genericNames(f.bounds);
    const pl = this.params(); f.params = pl.names; f.paramLines = pl.lines; f.ptypes = pl.types;
    if (this.isOp("->")) { this.i++; f.retType = this.parseType(); }
    f.body = this.suite();
    return f;
  }
  // trait method / extern: an abstract signature, or a default impl with a `:` body.
  funcSig(): Func {
    this.eatKw("fn"); const f = mkFunc(); f.isSig = true; const nt = this.peek();
    f.name = this.eatIdent(); f.nameLine = nt.line;
    f.generics = this.genericNames(f.bounds);
    const pl = this.params(); f.params = pl.names; f.paramLines = pl.lines; f.ptypes = pl.types;
    if (this.isOp("->")) { this.i++; f.retType = this.parseType(); }
    if (this.isOp(":")) f.body = this.suite();
    return f;
  }
  // `extern fn f(...)` (one-liner) or `extern:` block of `fn`/`type` members, each bound to a
  // host native by name. `type Name` declares an opaque foreign type (its only value is a host handle).
  externBlock(vis: number, p: Parsed): void {
    this.eatKw("extern");
    let tag = ""; if (this.is("STR")) tag = this.next().value;   // optional library tag: extern "audio" ...
    if (this.isOp(":")) {                                       // extern ["tag"]: <block>
      this.beginBlock();
      while (!this.is("DEDENT") && !this.is("END")) {
        if (this.isKw("fn")) { const f = this.funcSig(); f.vis = vis; f.tag = tag; p.externs.push(f); }
        else if (this.is("IDENT") && this.peek().value === "type") { this.i++; p.externTypes.push(this.eatIdent()); }
        else this.err("expected 'fn' or 'type' in extern block");
        this.skipNL();
      }
      if (this.is("DEDENT")) this.i++;
    } else if (this.is("IDENT") && this.peek().value === "type") {  // extern type Name (one-liner)
      this.i++; p.externTypes.push(this.eatIdent());
    } else {                                                    // extern ["tag"] fn name(...) -> Ret (one-liner)
      const f = this.funcSig(); f.vis = vis; f.tag = tag; p.externs.push(f);
    }
  }

  traitDecl(): TraitAst {
    this.eatKw("trait"); const Tr = mkTrait(); const nt = this.peek();
    Tr.name = this.eatIdent(); Tr.nameLine = nt.line;
    Tr.generics = this.genericNames(Tr.bounds);
    if (this.isKw("uses")) { this.i++; Tr.uses.push(this.parseType().name); while (this.isOp(",")) { this.i++; Tr.uses.push(this.parseType().name); } }
    this.beginBlock();
    while (!this.is("DEDENT") && !this.is("END")) {
      const mv = this.parseVis();
      if (this.isKw("fn")) { const m = this.funcSig(); m.vis = mv; Tr.methods.push(m); }
      else this.err("expected a trait method signature");
      this.skipNL();
    }
    if (this.is("DEDENT")) this.i++;
    return Tr;
  }

  classDecl(isValue: boolean): ClassAst {
    this.eatKw(isValue ? "struct" : "class"); const C = mkClass(); C.isValue = isValue; const nt = this.peek();
    C.name = this.eatIdent(); C.nameLine = nt.line;
    C.generics = this.genericNames(C.bounds);
    if (this.isKw("extends")) { if (isValue) this.err("a struct cannot extend a base type"); this.i++; C.extends = this.parseType().name; }
    if (this.isKw("uses")) { this.i++; C.uses.push(this.parseType().name); while (this.isOp(",")) { this.i++; C.uses.push(this.parseType().name); } }
    this.beginBlock();
    while (!this.is("DEDENT") && !this.is("END")) {
      const mv = this.parseVis();
      if (this.isKw("var") || this.isKw("const")) {
        this.i++; const f = mkField(); f.vis = mv; const fnt = this.peek();
        f.name = this.eatIdent(); f.nameLine = fnt.line;
        if (this.isOp(":")) { this.i++; f.type = this.parseType(); }
        if (this.isOp("=")) { this.i++; f.init = this.expr(); f.hasInit = true; }
        C.fields.push(f);
      } else if (this.isKw("init")) {
        this.i++; C.hasCtor = true; const pl = this.params();
        C.ctorParams = pl.names; C.ctorParamLines = pl.lines; C.ctorPtypes = pl.types; C.ctorBody = this.suite();
      } else if (this.isKw("signal")) {
        this.i++; const sg = mkSignal(); const snt = this.peek();
        sg.name = this.eatIdent(); sg.nameLine = snt.line;
        const pl = this.params(); sg.params = pl.names; sg.paramLines = pl.lines; sg.ptypes = pl.types;
        C.signals.push(sg);
      } else if (this.isKw("fn")) {
        const m = this.func(); m.vis = mv; C.methods.push(m);
      } else this.err("expected a class member");
      this.skipNL();
    }
    if (this.is("DEDENT")) this.i++;
    return C;
  }

  extendDecl(): ExtendAst {
    this.eatKw("extend"); const X = mkExtend(); const nt = this.peek();
    X.typeName = this.eatIdent(); X.nameLine = nt.line;
    if (this.isKw("uses")) { this.i++; X.uses.push(this.parseType().name); while (this.isOp(",")) { this.i++; X.uses.push(this.parseType().name); } }
    this.beginBlock();
    while (!this.is("DEDENT") && !this.is("END")) {
      this.parseVis();
      if (this.isKw("fn")) X.methods.push(this.func());
      else this.err("expected a method in the extension");
      this.skipNL();
    }
    if (this.is("DEDENT")) this.i++;
    return X;
  }

  enumDecl(): EnumAst {
    this.eatKw("enum"); const E = mkEnum(); const nt = this.peek();
    E.name = this.eatIdent(); E.nameLine = nt.line;
    const eb: Bounds = new Map(); E.generics = this.genericNames(eb);
    this.beginBlock();
    while (!this.is("DEDENT") && !this.is("END")) {
      const vn = this.eatIdent(); const fts: TyNode[] = [];
      if (this.isOp("(")) {
        this.i++; this.eatIdent(); this.eatOp(":"); fts.push(this.parseType());
        while (this.isOp(",")) { this.i++; this.eatIdent(); this.eatOp(":"); fts.push(this.parseType()); }
        this.eatOp(")");
      }
      E.variants.push([vn, fts]); this.skipNL();
    }
    if (this.is("DEDENT")) this.i++;
    return E;
  }

  statement(): Stmt {
    if (this.isKw("if")) return this.ifStmt();
    if (this.isKw("while")) return this.whileStmt();
    if (this.isKw("for")) return this.forStmt();
    if (this.isKw("try")) return this.tryStmt();
    if (this.isKw("var") || this.isKw("const")) return this.varStmt();
    if (this.isKw("return")) return this.retStmt();
    if (this.isKw("raise")) { this.i++; const s = mkStmt("RAISE"); s.expr = this.expr(); s.hasExpr = true; return s; }
    if (this.isKw("break")) { this.i++; return mkStmt("BREAK"); }
    if (this.isKw("continue")) { this.i++; return mkStmt("CONTINUE"); }
    if (this.isKw("pass")) { this.i++; return mkStmt("PASS"); }
    const e = this.expr();
    if (this.isOp("=")) {
      this.i++;
      if (e.k !== "NAME" && e.k !== "INDEX" && e.k !== "MEMBER") this.err("left side of assignment is not assignable");
      const s = mkStmt("ASSIGN"); s.target = e; s.expr = this.expr(); s.hasExpr = true; return s;
    }
    const s = mkStmt("EXPR"); s.expr = e; s.hasExpr = true; return s;
  }

  varStmt(): Stmt {
    const s = mkStmt("VAR"); const c = this.isKw("const"); this.i++; const nt = this.peek();
    s.name = this.eatIdent(); s.nameLine = nt.line;
    if (this.isOp(":")) { this.i++; s.vtype = this.parseType(); }
    if (this.isOp("=")) { this.i++; s.expr = this.expr(); s.hasExpr = true; }
    else if (c) this.err("const must be initialized");
    return s;
  }
  retStmt(): Stmt {
    this.eatKw("return"); const s = mkStmt("RET");
    if (!this.is("NEWLINE") && !this.is("DEDENT") && !this.is("END")) { s.expr = this.expr(); s.hasExpr = true; }
    return s;
  }
  ifStmt(): Stmt {
    this.eatKw("if"); const s = mkStmt("IF"); s.expr = this.expr(); s.hasExpr = true; s.body = this.suite();
    while (this.isKw("elif")) { this.i++; const c = this.expr(); const b = this.suite(); s.elifs.push([c, b]); }
    if (this.isKw("else")) { this.i++; s.elseBody = this.suite(); s.hasElse = true; }
    return s;
  }
  whileStmt(): Stmt {
    this.eatKw("while"); const s = mkStmt("WHILE"); s.expr = this.expr(); s.hasExpr = true; s.body = this.suite(); return s;
  }
  forStmt(): Stmt {
    this.eatKw("for"); const s = mkStmt("FOR"); const nt = this.peek();
    s.name = this.eatIdent(); s.nameLine = nt.line;
    this.eatKw("in"); s.expr = this.expr(); s.hasExpr = true; s.body = this.suite(); return s;
  }
  tryStmt(): Stmt {
    this.eatKw("try"); const s = mkStmt("TRY"); s.body = this.suite(); this.eatKw("catch"); const nt = this.peek();
    s.name = this.eatIdent(); s.nameLine = nt.line; s.elseBody = this.suite(); return s;
  }

  expr(): Expr { return this.orE(); }
  binL(ops: string[], sub: () => Expr): Expr {
    let left = sub();
    while (this.peek().kind === "OP") {
      let m = false; for (const o of ops) if (this.peek().value === o) { m = true; break; }
      if (!m) break;
      const op = this.next().value; const right = sub();
      const e = mkExpr("BINARY"); e.op = op; e.lhs = left; e.line = e.lhs.line; e.rhs = right; left = e;
    }
    return left;
  }
  orE(): Expr { return this.binL(["||"], () => this.andE()); }
  andE(): Expr { return this.binL(["&&"], () => this.eqE()); }
  eqE(): Expr { return this.binL(["==", "!="], () => this.cmpE()); }
  cmpE(): Expr { return this.binL(["<", "<=", ">", ">="], () => this.addE()); }
  addE(): Expr { return this.binL(["+", "-"], () => this.mulE()); }
  mulE(): Expr { return this.binL(["*", "/", "%"], () => this.unaryE()); }
  unaryE(): Expr {
    if (this.isKw("yield")) { const t = this.peek(); this.i++; const y = mkExpr("YIELD"); y.line = t.line; y.lhs = this.expr(); return y; }
    if (this.isOp("!") || this.isOp("-")) { const op = this.next().value; const e = mkExpr("UNARY"); e.op = op; e.lhs = this.unaryE(); return e; }
    return this.postfixE();
  }
  postfixE(): Expr {
    let e = this.primaryE();
    while (true) {
      if (this.isOp("(")) {
        this.i++; const call = mkExpr("CALL"); call.line = e.line; call.lhs = e;
        if (!this.isOp(")")) { call.args.push(this.expr()); while (this.isOp(",")) { this.i++; call.args.push(this.expr()); } }
        this.eatOp(")"); e = call;
      } else if (this.isOp("[")) {
        this.i++; const idx = mkExpr("INDEX"); idx.lhs = e; idx.line = idx.lhs.line; idx.rhs = this.expr(); this.eatOp("]"); e = idx;
      } else if (this.isOp(".")) {
        this.i++; const m = mkExpr("MEMBER"); m.lhs = e; const ft = this.peek(); m.sval = this.eatIdent(); m.line = ft.line; e = m;
      } else break;
    }
    return e;
  }
  litExpr(): Expr {
    const t = this.peek(); const e = mkExpr("INT"); e.line = t.line;
    if (t.kind === "INT") { this.i++; e.k = "INT"; e.ival = Number(t.value); return e; }
    if (t.kind === "FLT") { this.i++; e.k = "FLT"; e.dval = Number(t.value); return e; }
    if (t.kind === "STR") { this.i++; e.k = "STR"; e.sval = t.value; return e; }
    if (this.isKw("true")) { this.i++; e.k = "BOOL"; e.bval = true; return e; }
    if (this.isKw("false")) { this.i++; e.k = "BOOL"; e.bval = false; return e; }
    this.err("expected a literal");
  }
  closureExpr(): Expr {
    this.eatKw("fn"); const e = mkExpr("CLOSURE"); const pl = this.params(); e.params = pl.names; e.ptypes = pl.types;
    if (this.isOp("->")) { this.i++; e.retType = this.parseType(); }
    this.eatOp("=>"); e.lhs = this.expr(); return e;
  }
  primaryE(): Expr {
    const t = this.peek();
    if (t.kind === "INT" || t.kind === "FLT" || t.kind === "STR" || this.isKw("true") || this.isKw("false")) return this.litExpr();
    if (this.isKw("match")) return this.matchExpr();
    if (this.isKw("fn")) return this.closureExpr();
    if (t.kind === "IDENT") { const e = mkExpr("NAME"); e.line = t.line; this.i++; e.sval = t.value; return e; }
    if (this.isOp("(")) { this.i++; const inner = this.expr(); this.eatOp(")"); return inner; }
    if (this.isOp("[")) {
      this.i++; const e2 = mkExpr("LIST");
      if (!this.isOp("]")) { e2.args.push(this.expr()); while (this.isOp(",")) { this.i++; if (this.isOp("]")) break; e2.args.push(this.expr()); } }
      this.eatOp("]"); return e2;
    }
    this.err("expected an expression");
  }
  pattern(): Pattern {
    const t = this.peek();
    if (t.kind === "IDENT" && t.value === "_") { this.i++; return { k: 0, lit: null, name: "", binds: [] }; }
    if (t.kind === "INT" || t.kind === "FLT" || t.kind === "STR" || this.isKw("true") || this.isKw("false")) {
      return { k: 1, lit: this.litExpr(), name: "", binds: [] };
    }
    if (t.kind === "IDENT") {
      const p: Pattern = { k: 2, lit: null, name: this.eatIdent(), binds: [] };
      if (this.isOp("(")) { this.i++; p.binds.push(this.eatIdent()); while (this.isOp(",")) { this.i++; p.binds.push(this.eatIdent()); } this.eatOp(")"); }
      return p;
    }
    this.err("expected a pattern");
  }
  matchExpr(): Expr {
    this.eatKw("match"); const e = mkExpr("MATCH"); e.lhs = this.expr(); this.beginBlock();
    while (!this.is("DEDENT") && !this.is("END")) {
      const arm: Arm = { pats: [this.pattern()], body: null };
      while (this.isOp(",")) { this.i++; arm.pats.push(this.pattern()); }
      this.eatOp(":");
      if (this.is("NEWLINE")) this.err("block match arms are not supported in this build");
      arm.body = this.expr(); e.arms.push(arm); this.skipNL();
    }
    if (this.is("DEDENT")) this.i++;
    return e;
  }
  importDecl(pub: boolean): Import {
    this.eatKw("import"); const im: Import = { path: "", alias: "", names: [], pub };
    const ps = this.dotted(); im.path = ps.join(".");
    if (this.isOp(".")) { this.i++; this.eatOp("("); im.names.push(this.eatIdent()); while (this.isOp(",")) { this.i++; im.names.push(this.eatIdent()); } this.eatOp(")"); }
    else if (this.isKw("as")) { this.i++; im.alias = this.eatIdent(); }
    return im;
  }

  program(): Parsed {
    const p = mkParsed(); this.skipNL();
    if (this.isKw("module")) { this.i++; p.module = this.dotted().join("."); this.skipNL(); }
    while (!this.is("END")) {
      this.skipNL(); if (this.is("END")) break;
      let pub = false;
      if (this.isKw("pub") && this.peek(1).kind === "KW" && this.peek(1).value === "import") { this.i++; pub = true; }
      if (this.isKw("import")) { p.imports.push(this.importDecl(pub)); this.skipNL(); continue; }
      const tv = this.parseVis();
      if (this.isKw("fn")) { const f = this.func(); f.vis = tv; p.funcs.push(f); }
      else if (this.isKw("class")) { const c = this.classDecl(false); c.vis = tv; p.classes.push(c); }
      else if (this.isKw("struct")) { const c = this.classDecl(true); c.vis = tv; p.classes.push(c); }
      else if (this.isKw("trait")) { const t = this.traitDecl(); t.vis = tv; p.traits.push(t); }
      else if (this.isKw("extend")) { p.extensions.push(this.extendDecl()); }
      else if (this.isKw("extern")) this.externBlock(tv, p);
      else if (this.isKw("enum")) { const e = this.enumDecl(); e.vis = tv; p.enums.push(e); }
      else if (this.isKw("var") || this.isKw("const")) { const g = this.varStmt(); g.vis = tv; p.globals.push(g); }
      else if (this.isKw("init")) { this.i++; const body = this.suite(); for (const s of body) p.initBody.push(s); p.hasInit = true; }
      else this.err("unexpected top-level construct");
      this.skipNL();
    }
    return p;
  }
}
