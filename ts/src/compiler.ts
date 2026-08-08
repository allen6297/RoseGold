// Bytecode compiler — a faithful port of cpp/src/compiler.hpp plus buildProgram()
// from cpp/src/runtime.hpp. `rosegoldc --bytecode` runs loadModules + buildProgram
// with NO type check, so the compiler sees the *raw* AST (no operator/signal
// desugaring); this port replicates that exactly and is diffed against the
// canonical disassembly by ts/test/parity.mjs.
//
// The constant pool never appears in the --bytecode dump (only const-pool
// *indices* do), so it is modelled as a single monotonic counter rather than a
// real Value array — matching addConst()'s no-dedup, ever-increasing indices.

import type { Func, Stmt, Expr, ClassAst, Parsed } from "./ast.ts";

export interface Instr { op: string; a: number; b: number; line: number; }
export interface CFunc { name: string; nlocals: number; code: Instr[]; localNames: string[]; }
export interface ClassDesc { name: string; fieldNames: string[]; newFunc: number; methods: Map<string, number>; }
export interface VDesc { enumName: string; name: string; arity: number; }
export interface Sym { kind: number; index: number; }   // kind: 0 FUNC, 1 CLASS, 2 VARIANT, 3 GLOBAL, 4 EXTERN
export interface Program {
  funcs: CFunc[]; constN: number; nglobals: number;
  classes: ClassDesc[]; variants: VDesc[];
  syms: Map<string, Map<string, Sym>>;
  extensions: Map<string, Map<string, number>>;
}
export const newProgram = (): Program => ({ funcs: [], constN: 0, nglobals: 0, classes: [], variants: [], syms: new Map(), extensions: new Map() });
const newCFunc = (name: string): CFunc => ({ name, nlocals: 0, code: [], localNames: [] });
const symsOf = (prog: Program, m: string): Map<string, Sym> => { let x = prog.syms.get(m); if (!x) { x = new Map(); prog.syms.set(m, x); } return x; };
const extsOf = (prog: Program, t: string): Map<string, number> => { let x = prog.extensions.get(t); if (!x) { x = new Map(); prog.extensions.set(t, x); } return x; };

export class CompileError extends Error {}

interface ModuleCtx { name: string; sym: Map<string, Sym>; qual: Map<string, string>; sel: Map<string, [string, string]>; }
interface Loop { breaks: number[]; continues: number[]; }
interface Pending { fi: number; caps: string[]; params: string[]; body: Expr; }

// Builtin id table — must match compiler.hpp's BI map (and the VM dispatch).
const BI: Record<string, number> = {
  print: 0, len: 1, range: 2, push: 3, pop: 4, str: 5, ord: 6, chr: 7, substr: 8, split: 9, int: 10, readFile: 11, writeFile: 12, map: 13, set: 14, get: 15, has: 16, keys: 17, remove: 18,
  sqrt: 19, sin: 20, cos: 21, tan: 22, atan2: 23, floor: 24, ceil: 25, round: 26, pow: 27, abs: 28, min: 29, max: 30, lerp: 31, clamp: 32, random: 33, randint: 34, srandom: 35,
  coroutine: 36, resume: 37, done: 38,
  vec2: 39, vec3: 40, dot: 41, vlen: 42, norm: 43, __emit: 44,
};
const BINOP: Record<string, string> = { "+": "ADD", "-": "SUB", "*": "MUL", "/": "DIV", "%": "MOD", "<": "LT", "<=": "LE", ">": "GT", ">=": "GE", "==": "EQ", "!=": "NE" };

export class Compiler {
  prog: Program;
  mc: ModuleCtx | null = null;
  curFi = -1;
  locals = new Map<string, number>();
  nextSlot = 0;
  uid = 0;
  curLine = 0;
  loops: Loop[] = [];
  pending: Pending[] = [];
  constructor(prog: Program) { this.prog = prog; }

  CF(): CFunc { return this.prog.funcs[this.curFi]; }
  addConst(): number { return this.prog.constN++; }
  nc(_s: string): number { return this.addConst(); }   // const value is irrelevant to the dump; only the index is
  emit(op: string, a = 0, b = 0): number { this.CF().code.push({ op, a, b, line: this.curLine }); return this.CF().code.length - 1; }
  here(): number { return this.CF().code.length; }
  patch(at: number, t: number): void { this.CF().code[at].a = t; }
  declare(n: string): number { const s = this.nextSlot++; this.locals.set(n, s); const ln = this.CF().localNames; while (ln.length <= s) ln.push(""); ln[s] = n; return s; }

  resolveUn(name: string): Sym | null {
    const it = this.mc!.sym.get(name); if (it) return it;
    const s = this.mc!.sel.get(name); if (s) { const t = symsOf(this.prog, s[0]); const j = t.get(s[1]); if (j) return j; }
    return null;
  }
  emitStore(name: string): void {
    const it = this.locals.get(name); if (it !== undefined) { this.emit("STORE", it); return; }
    const s = this.resolveUn(name); if (s && s.kind === 3) { this.emit("STOREG", s.index); return; }
    throw new CompileError("cannot assign to '" + name + "'");
  }

  beginFunc(fi: number, ps: string[]): void { this.curFi = fi; this.locals = new Map(); this.nextSlot = 0; this.loops = []; this.curLine = 0; for (const p of ps) this.declare(p); }
  endFunc(): void { this.emit("PUSHNIL"); this.emit("RET"); this.CF().nlocals = this.nextSlot; }

  compileFunc(fi: number, f: Func): void { this.beginFunc(fi, f.params); for (const s of f.body) this.stmt(s); this.endFunc(); }
  compileStmtList(fi: number, body: Stmt[]): void { this.beginFunc(fi, []); for (const s of body) this.stmt(s); this.endFunc(); }
  compileMethod(fi: number, f: Func): void { this.beginFunc(fi, f.params); for (const s of f.body) this.stmt(s); this.endFunc(); }
  compileClass(C: ClassAst, ci: number): void {
    for (const m of C.methods) this.compileMethod(this.prog.classes[ci].methods.get(m.name)!, m);
    this.beginFunc(this.prog.classes[ci].newFunc, C.ctorParams);
    const self = this.declare("self"); this.emit("NEWOBJ", ci); this.emit("STORE", self);
    for (const f of C.fields) { this.emit("LOAD", self); if (f.hasInit) this.expr(f.init!); else this.emit("PUSHNIL"); this.emit("SETPROP", this.nc(f.name)); }
    for (const sg of C.signals) { this.emit("LOAD", self); this.emit("MAKELIST", 0); this.emit("SETPROP", this.nc(sg.name)); }
    for (const s of C.ctorBody) this.stmt(s);
    this.emit("LOAD", self); this.emit("RET"); this.CF().nlocals = this.nextSlot;
  }
  compileGlobals(fi: number, gs: Stmt[]): void {
    this.beginFunc(fi, []);
    for (const s of gs) { this.curLine = s.nameLine ? s.nameLine : (s.expr ? s.expr.line : 0); if (s.hasExpr) this.expr(s.expr!); else this.emit("PUSHNIL"); const sy = this.mc!.sym.get(s.name)!; this.emit("STOREG", sy.index); }
    this.emit("PUSHNIL"); this.emit("RET"); this.CF().nlocals = this.nextSlot;
  }

  drainPending(): void {
    for (let k = 0; k < this.pending.length; k++) {
      const pc = this.pending[k];
      const ps = [...pc.caps]; for (const p of pc.params) ps.push(p);
      this.beginFunc(pc.fi, ps);
      this.expr(pc.body); this.emit("RET"); this.CF().nlocals = this.nextSlot;
    }
  }

  block(b: Stmt[]): void { for (const s of b) this.stmt(s); }
  stmt(s: Stmt): void {
    const sl = s.nameLine ? s.nameLine : (s.expr ? s.expr.line : (s.target ? s.target.line : 0));
    if (sl) this.curLine = sl;
    switch (s.k) {
      case "VAR": if (s.hasExpr) this.expr(s.expr!); else this.emit("PUSHNIL"); this.emit("STORE", this.declare(s.name)); break;
      case "ASSIGN":
        if (s.target!.k === "NAME") { this.expr(s.expr!); this.emitStore(s.target!.sval); }
        else if (s.target!.k === "INDEX") { this.expr(s.target!.lhs!); this.expr(s.target!.rhs!); this.expr(s.expr!); this.emit("ISET"); }
        else { this.expr(s.target!.lhs!); this.expr(s.expr!); this.emit("SETPROP", this.nc(s.target!.sval)); }
        break;
      case "EXPR": this.expr(s.expr!); this.emit("POP"); break;
      case "RET": if (s.hasExpr) this.expr(s.expr!); else this.emit("PUSHNIL"); this.emit("RET"); break;
      case "RAISE": this.expr(s.expr!); this.emit("RAISE"); break;
      case "PASS": break;
      case "BREAK": if (!this.loops.length) throw new CompileError("'break' outside a loop"); this.loops[this.loops.length - 1].breaks.push(this.emit("JUMP")); break;
      case "CONTINUE": if (!this.loops.length) throw new CompileError("'continue' outside a loop"); this.loops[this.loops.length - 1].continues.push(this.emit("JUMP")); break;
      case "WHILE": {
        const cond = this.here(); this.expr(s.expr!); const jf = this.emit("JFALSE"); this.loops.push({ breaks: [], continues: [] }); this.block(s.body); this.emit("JUMP", cond); const end = this.here(); this.patch(jf, end);
        const L = this.loops[this.loops.length - 1]; for (const b of L.breaks) this.patch(b, end); for (const c of L.continues) this.patch(c, cond); this.loops.pop(); break;
      }
      case "FOR": {
        const u = this.uid++; this.expr(s.expr!); const it = this.declare("$it" + u); this.emit("STORE", it);
        this.emit("CONST", this.addConst()); const ix = this.declare("$ix" + u); this.emit("STORE", ix);
        const vr = this.declare(s.name); const cond = this.here();
        this.emit("LOAD", ix); this.emit("LOAD", it); this.emit("BUILTIN", 1, 1); this.emit("LT"); const jf = this.emit("JFALSE");
        this.emit("LOAD", it); this.emit("LOAD", ix); this.emit("IGET"); this.emit("STORE", vr);
        this.loops.push({ breaks: [], continues: [] }); this.block(s.body); const cont = this.here();
        this.emit("LOAD", ix); this.emit("CONST", this.addConst()); this.emit("ADD"); this.emit("STORE", ix); this.emit("JUMP", cond);
        const end = this.here(); this.patch(jf, end); const L = this.loops[this.loops.length - 1]; for (const b of L.breaks) this.patch(b, end); for (const c of L.continues) this.patch(c, cont); this.loops.pop(); break;
      }
      case "TRY": { const cs = this.declare(s.name); const setup = this.emit("SETUP_TRY"); this.block(s.body); this.emit("POP_TRY"); const jend = this.emit("JUMP"); this.patch(setup, this.here()); this.emit("STORE", cs); this.block(s.elseBody); this.patch(jend, this.here()); break; }
      case "IF": {
        const ends: number[] = []; this.expr(s.expr!); const jf = this.emit("JFALSE"); this.block(s.body); ends.push(this.emit("JUMP")); this.patch(jf, this.here());
        for (const [cond, body] of s.elifs) { this.expr(cond); const j = this.emit("JFALSE"); this.block(body); ends.push(this.emit("JUMP")); this.patch(j, this.here()); }
        if (s.hasElse) this.block(s.elseBody);
        for (const e of ends) this.patch(e, this.here()); break;
      }
    }
  }

  expr(e: Expr): void {
    switch (e.k) {
      case "INT": this.emit("CONST", this.addConst()); break;
      case "FLT": this.emit("CONST", this.addConst()); break;
      case "STR": this.emit("CONST", this.addConst()); break;
      case "BOOL": this.emit("CONST", this.addConst()); break;
      case "NAME": {
        const it = this.locals.get(e.sval); if (it !== undefined) { this.emit("LOAD", it); break; }
        const s = this.resolveUn(e.sval);
        if (s) {
          if (s.kind === 3) this.emit("LOADG", s.index);
          else if (s.kind === 0) this.emit("MKCLOSURE", s.index, 0);
          else if (s.kind === 2 && this.prog.variants[s.index].arity === 0) this.emit("MKVARIANT", s.index, 0);
          else throw new CompileError("'" + e.sval + "' cannot be used as a value");
          break;
        }
        throw new CompileError("undefined name '" + e.sval + "'");
      }
      case "UNARY": this.expr(e.lhs!); this.emit(e.op === "!" ? "NOT" : "NEG"); break;
      case "BINARY": this.binary(e); break;
      case "LIST": for (const a of e.args) this.expr(a); this.emit("MAKELIST", e.args.length); break;
      case "INDEX": this.expr(e.lhs!); this.expr(e.rhs!); this.emit("IGET"); break;
      case "MEMBER": this.member(e); break;
      case "MATCH": this.match(e); break;
      case "CLOSURE": this.closure(e); break;
      case "CALL": this.call(e); break;
      case "YIELD": this.expr(e.lhs!); this.emit("YIELD"); break;
    }
  }

  member(e: Expr): void {
    if (e.lhs!.k === "NAME") {
      const q = this.mc!.qual.get(e.lhs!.sval);
      if (q !== undefined) {
        const t = symsOf(this.prog, q); const it = t.get(e.sval);
        if (!it) throw new CompileError("module '" + q + "' has no member '" + e.sval + "'");
        const s = it;
        if (s.kind === 3) this.emit("LOADG", s.index);
        else if (s.kind === 0) this.emit("MKCLOSURE", s.index, 0);
        else if (s.kind === 2 && this.prog.variants[s.index].arity === 0) this.emit("MKVARIANT", s.index, 0);
        else throw new CompileError("cannot use '" + q + "." + e.sval + "' as a value");
        return;
      }
    }
    this.expr(e.lhs!); this.emit("GETPROP", this.nc(e.sval));
  }

  call(e: Expr): void {
    const callee = e.lhs!;
    if (callee.k === "MEMBER") {
      if (callee.lhs!.k === "NAME") {
        const q = this.mc!.qual.get(callee.lhs!.sval);
        if (q !== undefined) {
          const t = symsOf(this.prog, q); const it = t.get(callee.sval);
          if (!it) throw new CompileError("module '" + q + "' has no member '" + callee.sval + "'");
          const s = it; for (const a of e.args) this.expr(a); const argc = e.args.length;
          if (s.kind === 0) this.emit("CALL", s.index, argc);
          else if (s.kind === 1) this.emit("CALL", this.prog.classes[s.index].newFunc, argc);
          else if (s.kind === 2) this.emit("MKVARIANT", s.index, argc);
          else throw new CompileError("'" + q + "." + callee.sval + "' is not callable");
          return;
        }
      }
      this.expr(callee.lhs!); for (const a of e.args) this.expr(a); this.emit("INVOKE", this.nc(callee.sval), e.args.length);
      return;
    }
    if (callee.k !== "NAME") { this.expr(callee); for (const a of e.args) this.expr(a); this.emit("CALLV", e.args.length); return; }
    const name = callee.sval; const argc = e.args.length;
    const lit = this.locals.get(name);
    if (lit !== undefined) { this.emit("LOAD", lit); for (const a of e.args) this.expr(a); this.emit("CALLV", argc); return; }
    if (name in BI) { for (const a of e.args) this.expr(a); this.emit("BUILTIN", BI[name], argc); return; }
    const ns = this.resolveUn(name);
    if (ns && ns.kind === 4) { for (const a of e.args) this.expr(a); this.emit("NATIVE", this.nc(name), argc); return; }
    const s = this.resolveUn(name);
    if (s) {
      if (s.kind === 3) { this.emit("LOADG", s.index); for (const a of e.args) this.expr(a); this.emit("CALLV", argc); return; }
      for (const a of e.args) this.expr(a);
      if (s.kind === 0) this.emit("CALL", s.index, argc);
      else if (s.kind === 1) this.emit("CALL", this.prog.classes[s.index].newFunc, argc);
      else this.emit("MKVARIANT", s.index, argc);
      return;
    }
    throw new CompileError("unknown function '" + name + "'");
  }

  closure(e: Expr): void {
    const ps = new Set<string>(e.params);
    const caps: string[] = []; const seen = new Set<string>();
    this.collectFree(e.lhs!, ps, caps, seen);
    for (const c of caps) this.emit("LOAD", this.locals.get(c)!);
    const fi = this.prog.funcs.length; this.prog.funcs.push(newCFunc("$cl" + this.uid++));
    this.emit("MKCLOSURE", fi, caps.length);
    this.pending.push({ fi, caps, params: e.params, body: e.lhs! });
  }
  collectFree(e: Expr | null, bound: Set<string>, caps: string[], seen: Set<string>): void {
    if (!e) return;
    switch (e.k) {
      case "NAME": if (!bound.has(e.sval) && !seen.has(e.sval) && this.locals.has(e.sval)) { caps.push(e.sval); seen.add(e.sval); } break;
      case "MEMBER": this.collectFree(e.lhs, bound, caps, seen); break;
      case "UNARY": this.collectFree(e.lhs, bound, caps, seen); break;
      case "BINARY": this.collectFree(e.lhs, bound, caps, seen); this.collectFree(e.rhs, bound, caps, seen); break;
      case "INDEX": this.collectFree(e.lhs, bound, caps, seen); this.collectFree(e.rhs, bound, caps, seen); break;
      case "CALL": this.collectFree(e.lhs, bound, caps, seen); for (const a of e.args) this.collectFree(a, bound, caps, seen); break;
      case "LIST": for (const a of e.args) this.collectFree(a, bound, caps, seen); break;
      case "MATCH": { this.collectFree(e.lhs, bound, caps, seen); for (const arm of e.arms) { const b2 = new Set(bound); for (const p of arm.pats) for (const bd of p.binds) b2.add(bd); this.collectFree(arm.body, b2, caps, seen); } break; }
      case "CLOSURE": { const b2 = new Set(bound); for (const p of e.params) b2.add(p); this.collectFree(e.lhs, b2, caps, seen); break; }
      default: break;
    }
  }

  match(e: Expr): void {
    this.expr(e.lhs!); const ms = this.declare("$m" + this.uid++); this.emit("STORE", ms);
    const ends: number[] = [];
    for (const arm of e.arms) {
      const toBody: number[] = [];
      for (const p of arm.pats) {
        if (p.k === 0) toBody.push(this.emit("JUMP"));
        else if (p.k === 1) { this.emit("LOAD", ms); this.expr(p.lit!); this.emit("EQ"); toBody.push(this.emit("JTRUE")); }
        else { this.emit("LOAD", ms); this.emit("ISVARIANT", this.nc(p.name)); const miss = this.emit("JFALSE"); for (let bi = 0; bi < p.binds.length; bi++) { this.emit("LOAD", ms); this.emit("VGET", bi); this.emit("STORE", this.declare(p.binds[bi])); } toBody.push(this.emit("JUMP")); this.patch(miss, this.here()); }
      }
      const lnext = this.emit("JUMP"); const lbody = this.here(); for (const j of toBody) this.patch(j, lbody);
      this.expr(arm.body!); ends.push(this.emit("JUMP")); this.patch(lnext, this.here());
    }
    this.emit("PUSHNIL"); const lend = this.here(); for (const j of ends) this.patch(j, lend);
  }

  binary(e: Expr): void {
    if (e.op === "&&") { this.expr(e.lhs!); const jf = this.emit("JFALSE"); this.expr(e.rhs!); const je = this.emit("JUMP"); this.patch(jf, this.here()); this.emit("CONST", this.addConst()); this.patch(je, this.here()); return; }
    if (e.op === "||") { this.expr(e.lhs!); const jt = this.emit("JTRUE"); this.expr(e.rhs!); const je = this.emit("JUMP"); this.patch(jt, this.here()); this.emit("CONST", this.addConst()); this.patch(je, this.here()); return; }
    this.expr(e.lhs!); this.expr(e.rhs!);
    this.emit(BINOP[e.op]);
  }
}

// --- Reserve symbols + compile every module (port of buildProgram, runtime.hpp) --
function classDefaults(P: Parsed, C: ClassAst, out: Array<[string, Func]>): void {
  const own = new Set<string>(); for (const mth of C.methods) own.add(mth.name);
  const added = new Set<string>();
  const walk = (tn: string): void => {
    for (const Tr of P.traits) if (Tr.name === tn) {
      for (const mth of Tr.methods) if (mth.body.length && !own.has(mth.name) && !added.has(mth.name)) { added.add(mth.name); out.push([mth.name, mth]); }
      for (const u of Tr.uses) walk(u);
      return;
    }
  };
  for (const u of C.uses) walk(u);
}
function extMethods(P: Parsed, X: { uses: string[]; methods: Func[] }, out: Array<[string, Func]>): void {
  const seen = new Set<string>(); for (const mth of X.methods) { seen.add(mth.name); out.push([mth.name, mth]); }
  const walk = (tn: string): void => {
    for (const Tr of P.traits) if (Tr.name === tn) {
      for (const mth of Tr.methods) if (mth.body.length && !seen.has(mth.name)) { seen.add(mth.name); out.push([mth.name, mth]); }
      for (const u of Tr.uses) walk(u);
      return;
    }
  };
  for (const u of X.uses) walk(u);
}

export function buildProgram(mods: Map<string, Parsed>, order: string[]): Program {
  const prog = newProgram();
  const globalsFunc = new Map<string, number>();
  const initFunc = new Map<string, number>();
  const modDefaults = new Map<string, Array<[number, Func]>>();

  for (const m of order) {   // reservation pass (dependency order)
    const P = mods.get(m)!; const S = symsOf(prog, m);
    for (const g of P.globals) if (!S.has(g.name)) S.set(g.name, { kind: 3, index: prog.nglobals++ });
    for (const C of P.classes) { const ci = prog.classes.length; prog.classes.push({ name: C.name, fieldNames: [], newFunc: -1, methods: new Map() }); for (const f of C.fields) prog.classes[ci].fieldNames.push(f.name); for (const sg of C.signals) prog.classes[ci].fieldNames.push(sg.name); S.set(C.name, { kind: 1, index: ci }); }
    for (const E of P.enums) for (const [vn, vts] of E.variants) { const vi = prog.variants.length; prog.variants.push({ enumName: E.name, name: vn, arity: vts.length }); S.set(vn, { kind: 2, index: vi }); }
    for (const f of P.funcs) { const fi = prog.funcs.length; prog.funcs.push(newCFunc(m + "::" + f.name)); S.set(f.name, { kind: 0, index: fi }); }
    for (const ex of P.externs) if (!S.has(ex.name)) S.set(ex.name, { kind: 4, index: 0 });
    for (let ci0 = 0; ci0 < P.classes.length; ci0++) {
      const C = P.classes[ci0]; const ci = S.get(C.name)!.index;
      for (const mth of C.methods) { const idx = prog.funcs.length; prog.funcs.push(newCFunc(m + "::" + C.name + "." + mth.name)); prog.classes[ci].methods.set(mth.name, idx); }
      const defs: Array<[string, Func]> = []; classDefaults(P, C, defs);
      for (const [dn, df] of defs) { const idx = prog.funcs.length; prog.funcs.push(newCFunc(m + "::" + C.name + "." + dn + " (default)")); prog.classes[ci].methods.set(dn, idx); if (!modDefaults.has(m)) modDefaults.set(m, []); modDefaults.get(m)!.push([idx, df]); }
      const ni = prog.funcs.length; prog.funcs.push(newCFunc(m + "::new " + C.name)); prog.classes[ci].newFunc = ni;
    }
    for (const X of P.extensions) {
      const ms: Array<[string, Func]> = []; extMethods(P, X, ms);
      const xsym = S.get(X.typeName); const isClass = !!xsym && xsym.kind === 1; const ci = isClass ? xsym!.index : -1;
      for (const [dn, df] of ms) {
        if (isClass && prog.classes[ci].methods.has(dn)) continue;
        const idx = prog.funcs.length; prog.funcs.push(newCFunc(m + "::extend " + X.typeName + "." + dn));
        if (isClass) prog.classes[ci].methods.set(dn, idx); else extsOf(prog, X.typeName).set(dn, idx);
        if (!modDefaults.has(m)) modDefaults.set(m, []); modDefaults.get(m)!.push([idx, df]);
      }
    }
    if (P.globals.length) { globalsFunc.set(m, prog.funcs.length); prog.funcs.push(newCFunc(m + "::$globals")); }
    if (P.hasInit) { initFunc.set(m, prog.funcs.length); prog.funcs.push(newCFunc(m + "::$init")); }
  }

  const comp = new Compiler(prog);   // compile pass
  for (const m of order) {
    const P = mods.get(m)!;
    const ctx: ModuleCtx = { name: m, sym: symsOf(prog, m), qual: new Map(), sel: new Map() };
    for (const imp of P.imports) {
      if (imp.names.length) { for (const n of imp.names) ctx.sel.set(n, [imp.path, n]); }
      else if (imp.alias !== "") ctx.qual.set(imp.alias, imp.path);
      else { const d = imp.path.lastIndexOf("."); ctx.qual.set(d === -1 ? imp.path : imp.path.slice(d + 1), imp.path); }
    }
    comp.mc = ctx;
    for (const f of P.funcs) comp.compileFunc(ctx.sym.get(f.name)!.index, f);
    for (let ci0 = 0; ci0 < P.classes.length; ci0++) comp.compileClass(P.classes[ci0], ctx.sym.get(P.classes[ci0].name)!.index);
    for (const [idx, df] of (modDefaults.get(m) ?? [])) comp.compileMethod(idx, df);
    if (globalsFunc.has(m)) comp.compileGlobals(globalsFunc.get(m)!, P.globals);
    if (initFunc.has(m)) comp.compileStmtList(initFunc.get(m)!, P.initBody);
    comp.drainPending();
  }
  return prog;
}
