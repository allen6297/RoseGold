// Static type checker (front-end gate) — a faithful port of cpp/src/types.hpp,
// scoped to what `rosegoldc --check` needs. Two parts of the C++ checker are
// deliberately omitted because they don't affect --check output:
//   * the LSP occurrence index (recordOcc / occs / inlays / recordDecl / …) is
//     inert unless recordOcc is set, which the CLI never does; and
//   * the AST rewrites that desugar operator/signal calls for the compiler —
//     for type-checking we only need the same errors and the same result types,
//     so the rewrites are dropped and the result type returned directly.
// Everything else — generics/unification, traits, classes, enums, visibility,
// operator overloading, signals, module resolution — is ported verbatim.

import type {
  TyNode, Pattern, Expr, Stmt, Bounds, Func, ClassAst, TraitAst, ExtendAst, Parsed,
} from "./ast.ts";

// --- Types -------------------------------------------------------------------
export type TyKind = "ANY" | "PRIM" | "LIST" | "FUNC" | "NAMED" | "TVAR" | "MODULE" | "MAP";
// nkind: 0 = class, 1 = enum, 2 = trait, 3 = signal. bounds: FUNC generic
// constraints. tbounds: a bounded TVAR's trait names.
export interface Ty {
  k: TyKind; name: string; nkind: number;
  args: Ty[]; elem: Ty | null; ret: Ty | null; variadic: boolean;
  bounds: Bounds; tbounds: string[];
}
const mk = (k: TyKind): Ty =>
  ({ k, name: "", nkind: 0, args: [], elem: null, ret: null, variadic: false, bounds: new Map(), tbounds: [] });
export const tAny = (): Ty => mk("ANY");
export const tPrim = (n: string): Ty => { const t = mk("PRIM"); t.name = n; return t; };
export const tList = (e: Ty | null): Ty => { const t = mk("LIST"); t.elem = e; return t; };
export const tFunc = (ps: Ty[], r: Ty | null, va = false): Ty => { const t = mk("FUNC"); t.args = ps; t.ret = r; t.variadic = va; return t; };
export const tNamed = (n: string, kind: number, a: Ty[] = []): Ty => { const t = mk("NAMED"); t.name = n; t.nkind = kind; t.args = a; return t; };
export const tVar = (n: string): Ty => { const t = mk("TVAR"); t.name = n; return t; };
export const tMod = (n: string): Ty => { const t = mk("MODULE"); t.name = n; return t; };
export const tMap = (k: Ty, v: Ty): Ty => { const t = mk("MAP"); t.args = [k, v]; return t; };

export function tStr(t: Ty | null): string {
  if (!t) return "?";
  switch (t.k) {
    case "ANY": return "?";
    case "PRIM": case "TVAR": return t.name;
    case "MODULE": return "module " + t.name;
    case "LIST": return "List<" + tStr(t.elem) + ">";
    case "MAP": return "Map<" + tStr(t.args[0]) + ", " + tStr(t.args[1]) + ">";
    case "FUNC": { let s = "fn("; for (let i = 0; i < t.args.length; i++) { if (i) s += ", "; s += tStr(t.args[i]); } return s + ") -> " + tStr(t.ret); }
    case "NAMED": { if (!t.args.length) return t.name; let s = t.name + "<"; for (let i = 0; i < t.args.length; i++) { if (i) s += ", "; s += tStr(t.args[i]); } return s + ">"; }
  }
  return "?";
}

// --- Per-module resolved type tables -----------------------------------------
interface ClassInfoT { generics: string[]; bounds: Bounds; extends: string; uses: string[]; fields: Map<string, [Ty, number]>; methods: Map<string, [Ty, number]>; ctorParams: Ty[]; }
interface EnumInfoT { generics: string[]; variants: Map<string, Ty[]>; }
interface TraitInfoT { generics: string[]; uses: string[]; methods: Map<string, Ty>; defaulted: Set<string>; }
interface ExtInfoT { uses: string[]; methods: Map<string, [Ty, number]>; }
interface ModTypes { classes: Map<string, ClassInfoT>; enums: Map<string, EnumInfoT>; traits: Map<string, TraitInfoT>; exts: Map<string, ExtInfoT>; foreign: Set<string>; values: Map<string, Ty>; pub: Map<string, Ty>; }   // foreign: opaque `extern type` names (nkind 4)
const emptyClassInfo = (): ClassInfoT => ({ generics: [], bounds: new Map(), extends: "", uses: [], fields: new Map(), methods: new Map(), ctorParams: [] });
const emptyEnumInfo = (): EnumInfoT => ({ generics: [], variants: new Map() });
const emptyTraitInfo = (): TraitInfoT => ({ generics: [], uses: [], methods: new Map(), defaulted: new Set() });
const emptyModTypes = (): ModTypes => ({ classes: new Map(), enums: new Map(), traits: new Map(), exts: new Map(), foreign: new Set(), values: new Map(), pub: new Map() });

// An environment binding: a value's type (plus a declaration site, unused here).
interface Binding { ty: Ty | null; line: number; }
const bind = (ty: Ty | null, line = 0): Binding => ({ ty, line });
type Scope = Map<string, Binding>;
type Env = Scope[];

type ErrTuple = [string, number, string];

export class TypeChecker {
  mods: Map<string, Parsed>;
  order: string[];
  T = new Map<string, ModTypes>();
  errors: ErrTuple[] = [];
  curm = "";
  curClass = "";
  curRet: Ty | null = null;
  loopDepth = 0;
  curBounds: Bounds | null = null;   // generic bounds in scope, so resolveType tags TVARs with their trait bounds
  curSelf: Ty | null = null;         // what `Self` resolves to here
  qual = new Map<string, string>();
  sel = new Map<string, [string, string]>();

  constructor(mods: Map<string, Parsed>, order: string[]) { this.mods = mods; this.order = order; }

  // std::map operator[] auto-creates; mirror that for the module type table.
  private Tm(m: string): ModTypes { let x = this.T.get(m); if (!x) { x = emptyModTypes(); this.T.set(m, x); } return x; }
  err(line: number, msg: string): void { this.errors.push([this.curm, line, msg]); }
  lineOf(e: Expr | null): number { return e ? e.line : 0; }

  resolveType(n: TyNode | null, gens: Set<string>, m: string): Ty {
    if (!n) return tAny();
    if (n.isFunc) { const ps: Ty[] = []; for (const p of n.fparams) ps.push(this.resolveType(p, gens, m)); return tFunc(ps, this.resolveType(n.fret, gens, m)); }
    const name = n.name;
    if (name === "Self") return this.curSelf ? this.curSelf : tVar("Self");
    if (name === "Int" || name === "Float" || name === "String" || name === "Bool" || name === "Void") return tPrim(name);
    if (name === "List") return tList(n.args.length === 0 ? tAny() : this.resolveType(n.args[0], gens, m));
    if (name === "Map") return tMap(n.args.length > 0 ? this.resolveType(n.args[0], gens, m) : tAny(), n.args.length > 1 ? this.resolveType(n.args[1], gens, m) : tAny());
    if (gens.has(name)) { const tv = tVar(name); if (this.curBounds) { const b = this.curBounds.get(name); if (b) tv.tbounds = b; } return tv; }
    const args: Ty[] = []; for (const a of n.args) args.push(this.resolveType(a, gens, m));
    const MT = this.Tm(m);
    if (MT.classes.has(name)) return tNamed(name, 0, args);
    if (MT.enums.has(name)) return tNamed(name, 1, args);
    if (MT.traits.has(name)) return tNamed(name, 2, args);
    if (MT.foreign.has(name)) return tNamed(name, 4, args);   // opaque foreign (extern type)
    if (name === "Vec2" || name === "Vec3" || name === "Vec") return tPrim("Vec");
    this.err(0, "unknown type '" + name + "'"); return tAny();
  }
  extSelfType(n: string): Ty | null {
    if (n === "Int" || n === "Float" || n === "String" || n === "Bool" || n === "Void") return tPrim(n);
    if (n === "List") return tList(tAny());
    if (n === "Map") return tMap(tAny(), tAny());
    if (this.Tm(this.curm).classes.has(n)) return tNamed(n, 0);
    return null;
  }
  isSub(a: string, b: string): boolean {
    if (a === b) return true;
    const sup: string[] = [];
    const MT = this.Tm(this.curm);
    const xe = MT.exts.get(a); if (xe) for (const u of xe.uses) sup.push(u);
    const ci = MT.classes.get(a);
    if (ci) { if (ci.extends !== "") sup.push(ci.extends); for (const u of ci.uses) sup.push(u); }
    else { const ti = MT.traits.get(a); if (ti) for (const u of ti.uses) sup.push(u); }
    for (const s of sup) if (s === b || this.isSub(s, b)) return true;
    return false;
  }
  satisfies(t: Ty | null, trait: string): boolean {
    if (!t) return false;
    if (t.k === "NAMED" || t.k === "PRIM") return this.isSub(t.name, trait);
    if (t.k === "LIST") return this.isSub("List", trait);
    if (t.k === "MAP") return this.isSub("Map", trait);
    return false;
  }
  collectSupers(n: string, order: string[], seen: Set<string>): void {
    if (seen.has(n)) return; seen.add(n); order.push(n);
    const MT = this.Tm(this.curm);
    const ci = MT.classes.get(n);
    if (ci) { if (ci.extends !== "") this.collectSupers(ci.extends, order, seen); for (const u of ci.uses) this.collectSupers(u, order, seen); }
    else { const ti = MT.traits.get(n); if (ti) for (const u of ti.uses) this.collectSupers(u, order, seen); }
  }
  commonSuper(a: Ty | null, b: Ty | null): Ty | null {
    if (!a || !b || a.k !== "NAMED" || b.k !== "NAMED") return null;
    const ao: string[] = [], bo: string[] = []; const as = new Set<string>(), bs = new Set<string>();
    this.collectSupers(a.name, ao, as); this.collectSupers(b.name, bo, bs);
    const MT = this.Tm(this.curm);
    for (const n of bo) if (as.has(n)) { const kind = MT.classes.has(n) ? 0 : (MT.traits.has(n) ? 2 : 1); return tNamed(n, kind); }
    return null;
  }
  assignable(s: Ty | null, d: Ty | null): boolean {
    if (!s || !d) return true;
    if (s.k === "ANY" || d.k === "ANY" || s.k === "TVAR" || d.k === "TVAR") return true;
    if (d.k === "NAMED" && d.nkind === 2 && (s.k === "PRIM" || s.k === "LIST" || s.k === "MAP")) return this.satisfies(s, d.name);
    if (s.k === "PRIM" && d.k === "PRIM") return s.name === d.name;
    if (s.k === "LIST" && d.k === "LIST") return this.assignable(s.elem, d.elem) && this.assignable(d.elem, s.elem);
    if (s.k === "MAP" && d.k === "MAP") return this.assignable(s.args[0], d.args[0]) && this.assignable(d.args[0], s.args[0]) && this.assignable(s.args[1], d.args[1]) && this.assignable(d.args[1], s.args[1]);
    if (s.k === "FUNC" && d.k === "FUNC") { if (s.args.length !== d.args.length) return false; for (let i = 0; i < s.args.length; i++) if (!this.assignable(d.args[i], s.args[i])) return false; return this.assignable(s.ret, d.ret); }
    if (s.k === "NAMED" && d.k === "NAMED") { if (s.name === d.name) return true; if (d.nkind === 2) return this.isSub(s.name, d.name); if (s.nkind === 0 && d.nkind === 0) return this.isSub(s.name, d.name); return false; }
    return false;
  }
  unify(p: Ty | null, a: Ty | null, s: Map<string, Ty>): void {
    if (!p || !a) return;
    if (p.k === "TVAR") { if (s.has(p.name)) this.unify(s.get(p.name)!, a, s); else if (a.k !== "ANY" && a.k !== "TVAR") s.set(p.name, a); return; }
    if (p.k === "LIST" && a.k === "LIST") this.unify(p.elem, a.elem, s);
    else if (p.k === "MAP" && a.k === "MAP") { for (let i = 0; i < 2 && i < a.args.length; i++) this.unify(p.args[i], a.args[i], s); }
    else if (p.k === "FUNC" && a.k === "FUNC") { for (let i = 0; i < p.args.length && i < a.args.length; i++) this.unify(p.args[i], a.args[i], s); this.unify(p.ret, a.ret, s); }
    else if (p.k === "NAMED" && a.k === "NAMED") for (let i = 0; i < p.args.length && i < a.args.length; i++) this.unify(p.args[i], a.args[i], s);
  }
  subst(t: Ty | null, s: Map<string, Ty>): Ty | null {
    if (!t || s.size === 0) return t;
    if (t.k === "TVAR") { const it = s.get(t.name); return it !== undefined ? it : t; }
    if (t.k === "LIST") return tList(this.subst(t.elem, s));
    if (t.k === "MAP") return tMap(this.subst(t.args[0], s)!, this.subst(t.args[1], s)!);
    if (t.k === "FUNC") { const ps: Ty[] = []; for (const p of t.args) ps.push(this.subst(p, s)!); const r = tFunc(ps, this.subst(t.ret, s), t.variadic); return r; }
    if (t.k === "NAMED") { const a: Ty[] = []; for (const x of t.args) a.push(this.subst(x, s)!); return tNamed(t.name, t.nkind, a); }
    return t;
  }
  funcType(f: Func, m: string, extra: string[]): Ty {
    const g = new Set<string>(f.generics); for (const x of extra) g.add(x);
    const ps: Ty[] = [];
    for (let k = 0; k < f.params.length; k++) { if (f.params[k] === "self") continue; ps.push(f.ptypes[k] ? this.resolveType(f.ptypes[k], g, m) : tAny()); }
    const ft = tFunc(ps, f.retType ? this.resolveType(f.retType, g, m) : tPrim("Void")); ft.bounds = f.bounds; return ft;
  }

  build(): void {
    for (const m of this.order) this.T.set(m, emptyModTypes());
    for (const m of this.order) {
      const P = this.mods.get(m)!; const MT = this.Tm(m);
      for (const C of P.classes) if (!MT.classes.has(C.name)) MT.classes.set(C.name, emptyClassInfo());
      for (const Tr of P.traits) if (!MT.traits.has(Tr.name)) MT.traits.set(Tr.name, emptyTraitInfo());
      for (const E of P.enums) if (!MT.enums.has(E.name)) MT.enums.set(E.name, emptyEnumInfo());
      for (const t of P.externTypes) MT.foreign.add(t);
    }
    for (const m of this.order) this.buildModule(m);
    for (const m of this.order) this.Tm(m).pub = this.pubValues(m, new Set());
  }
  buildModule(m: string): void {
    this.curm = m; const P = this.mods.get(m)!; const MT = this.Tm(m);
    for (const Tr of P.traits) {
      const ti: TraitInfoT = { generics: Tr.generics, uses: Tr.uses, methods: new Map(), defaulted: new Set() };
      const g = new Set<string>(Tr.generics);
      for (const mth of Tr.methods) {
        const ps: Ty[] = []; for (let k = 0; k < mth.params.length; k++) { if (mth.params[k] === "self") continue; ps.push(mth.ptypes[k] ? this.resolveType(mth.ptypes[k], g, m) : tAny()); }
        ti.methods.set(mth.name, tFunc(ps, mth.retType ? this.resolveType(mth.retType, g, m) : tPrim("Void")));
        if (mth.body.length) ti.defaulted.add(mth.name);
      }
      MT.traits.set(Tr.name, ti);
    }
    for (const C of P.classes) {
      const ga: Ty[] = []; for (const gn of C.generics) ga.push(tVar(gn));
      const pcs = this.curSelf; this.curSelf = tNamed(C.name, 0, ga);
      const ci: ClassInfoT = { generics: C.generics, bounds: C.bounds, extends: C.extends, uses: C.uses, fields: new Map(), methods: new Map(), ctorParams: [] };
      const g = new Set<string>(C.generics);
      for (const f of C.fields) ci.fields.set(f.name, [f.type ? this.resolveType(f.type, g, m) : tAny(), f.vis]);
      for (const sg of C.signals) { const ps: Ty[] = []; for (let k = 0; k < sg.params.length; k++) ps.push(sg.ptypes[k] ? this.resolveType(sg.ptypes[k], g, m) : tAny()); ci.fields.set(sg.name, [tNamed("Signal", 3, ps), 0]); }
      for (const mth of C.methods) {
        const g2 = new Set<string>(g); for (const x of mth.generics) g2.add(x);
        const ps: Ty[] = []; for (let k = 0; k < mth.params.length; k++) { if (mth.params[k] === "self") continue; ps.push(mth.ptypes[k] ? this.resolveType(mth.ptypes[k], g2, m) : tAny()); }
        const mft = tFunc(ps, mth.retType ? this.resolveType(mth.retType, g2, m) : tPrim("Void")); mft.bounds = mth.bounds;
        ci.methods.set(mth.name, [mft, mth.vis]);
      }
      if (C.hasCtor) for (let k = 0; k < C.ctorParams.length; k++) ci.ctorParams.push(C.ctorPtypes[k] ? this.resolveType(C.ctorPtypes[k], g, m) : tAny());
      const selfSub = new Map<string, Ty>(); selfSub.set("Self", this.curSelf);
      const inheritDefaults = (tn: string): void => {
        const ti = MT.traits.get(tn); if (!ti) return;
        for (const dn of ti.defaulted) if (!ci.methods.has(dn)) ci.methods.set(dn, [this.subst(ti.methods.get(dn)!, selfSub)!, 1]);
        for (const u of ti.uses) inheritDefaults(u);
      };
      for (const u of C.uses) inheritDefaults(u);
      MT.classes.set(C.name, ci);
      const ctor = tFunc(ci.ctorParams, tNamed(C.name, 0, ga)); ctor.bounds = C.bounds;
      MT.values.set(C.name, ctor);
      this.curSelf = pcs;
    }
    for (const X of P.extensions) {
      let self = this.extSelfType(X.typeName); if (!self) self = tAny();
      const pcs = this.curSelf; this.curSelf = self;
      const xi: ExtInfoT = { uses: [], methods: new Map() }; for (const u of X.uses) xi.uses.push(u);
      for (const mth of X.methods) {
        const g = new Set<string>(mth.generics);
        const ps: Ty[] = []; for (let k = 0; k < mth.params.length; k++) { if (mth.params[k] === "self") continue; ps.push(mth.ptypes[k] ? this.resolveType(mth.ptypes[k], g, m) : tAny()); }
        const mft = tFunc(ps, mth.retType ? this.resolveType(mth.retType, g, m) : tPrim("Void")); mft.bounds = mth.bounds;
        xi.methods.set(mth.name, [mft, mth.vis]);
      }
      const selfSub = new Map<string, Ty>(); selfSub.set("Self", self);
      const inh = (tn: string): void => { const ti = MT.traits.get(tn); if (!ti) return; for (const dn of ti.defaulted) if (!xi.methods.has(dn)) xi.methods.set(dn, [this.subst(ti.methods.get(dn)!, selfSub)!, 1]); for (const u of ti.uses) inh(u); };
      for (const u of X.uses) inh(u);
      this.curSelf = pcs;
      let e = MT.exts.get(X.typeName); if (!e) { e = { uses: [], methods: new Map() }; MT.exts.set(X.typeName, e); }
      for (const u of xi.uses) e.uses.push(u); for (const [kk, vv] of xi.methods) e.methods.set(kk, vv);
      const cit = MT.classes.get(X.typeName);
      if (cit) for (const [kk, vv] of xi.methods) if (!cit.methods.has(kk)) cit.methods.set(kk, vv);
    }
    for (const E of P.enums) {
      const ei: EnumInfoT = { generics: E.generics, variants: new Map() };
      const g = new Set<string>(E.generics);
      const ga: Ty[] = []; for (const gn of E.generics) ga.push(tVar(gn)); const enumTy = tNamed(E.name, 1, ga);
      for (const [vn, vts] of E.variants) { const fts: Ty[] = []; for (const ft of vts) fts.push(this.resolveType(ft, g, m)); ei.variants.set(vn, fts); MT.values.set(vn, fts.length === 0 ? enumTy : tFunc(fts, enumTy)); }
      MT.enums.set(E.name, ei); MT.values.set(E.name, enumTy);
    }
    for (const f of P.funcs) MT.values.set(f.name, this.funcType(f, m, []));
    for (const f of P.externs) MT.values.set(f.name, this.funcType(f, m, []));
    for (const g of P.globals) MT.values.set(g.name, g.vtype ? this.resolveType(g.vtype, new Set(), m) : tAny());
  }
  pubValues(m: string, seen: Set<string>): Map<string, Ty> {
    if (seen.has(m)) return new Map(); seen.add(m);
    const P = this.mods.get(m)!; const MT = this.Tm(m); const r = new Map<string, Ty>();
    for (const C of P.classes) if (C.vis === 1) r.set(C.name, MT.values.get(C.name)!);
    for (const E of P.enums) if (E.vis === 1) { r.set(E.name, MT.values.get(E.name)!); for (const [vn] of E.variants) r.set(vn, MT.values.get(vn)!); }
    for (const f of P.funcs) if (f.vis === 1) r.set(f.name, MT.values.get(f.name)!);
    for (const f of P.externs) if (f.vis === 1) r.set(f.name, MT.values.get(f.name)!);
    for (const g of P.globals) if (g.vis === 1) r.set(g.name, MT.values.get(g.name)!);
    for (const im of P.imports) if (im.pub && this.mods.has(im.path)) { const tex = this.pubValues(im.path, seen); if (im.names.length) { for (const n of im.names) if (tex.has(n)) r.set(n, tex.get(n)!); } else for (const [kk, vv] of tex) r.set(kk, vv); }
    return r;
  }

  builtins(): Scope {
    const b: Scope = new Map();
    const set = (n: string, t: Ty) => b.set(n, bind(t));
    set("print", tFunc([], tPrim("Void"), true)); set("len", tFunc([tAny()], tPrim("Int"))); set("range", tFunc([tPrim("Int")], tList(tPrim("Int"))));
    set("push", tFunc([tList(tVar("T")), tVar("T")], tPrim("Void"))); set("pop", tFunc([tList(tVar("T"))], tVar("T")));
    set("str", tFunc([tAny()], tPrim("String"))); set("ord", tFunc([tPrim("String")], tPrim("Int"))); set("chr", tFunc([tPrim("Int")], tPrim("String")));
    set("substr", tFunc([tPrim("String"), tPrim("Int"), tPrim("Int")], tPrim("String"))); set("split", tFunc([tPrim("String"), tPrim("String")], tList(tPrim("String"))));
    set("int", tFunc([tPrim("String")], tPrim("Int"))); set("readFile", tFunc([tPrim("String")], tPrim("String"))); set("writeFile", tFunc([tPrim("String"), tPrim("String")], tPrim("Void")));
    set("map", tFunc([], tMap(tVar("K"), tVar("V"))));
    set("set", tFunc([tMap(tVar("K"), tVar("V")), tVar("K"), tVar("V")], tPrim("Void")));
    set("get", tFunc([tMap(tVar("K"), tVar("V")), tVar("K")], tVar("V")));
    set("has", tFunc([tMap(tVar("K"), tVar("V")), tVar("K")], tPrim("Bool")));
    set("keys", tFunc([tMap(tVar("K"), tVar("V"))], tList(tVar("K"))));
    set("remove", tFunc([tMap(tVar("K"), tVar("V")), tVar("K")], tPrim("Void")));
    set("sqrt", tFunc([tVar("N")], tPrim("Float"))); set("sin", tFunc([tVar("N")], tPrim("Float"))); set("cos", tFunc([tVar("N")], tPrim("Float"))); set("tan", tFunc([tVar("N")], tPrim("Float")));
    set("atan2", tFunc([tVar("N"), tVar("N")], tPrim("Float"))); set("pow", tFunc([tVar("N"), tVar("N")], tPrim("Float")));
    set("floor", tFunc([tVar("N")], tPrim("Int"))); set("ceil", tFunc([tVar("N")], tPrim("Int"))); set("round", tFunc([tVar("N")], tPrim("Int")));
    set("abs", tFunc([tVar("N")], tVar("N"))); set("min", tFunc([tVar("N"), tVar("N")], tVar("N"))); set("max", tFunc([tVar("N"), tVar("N")], tVar("N")));
    set("lerp", tFunc([tVar("N"), tVar("N"), tVar("N")], tPrim("Float"))); set("clamp", tFunc([tVar("N"), tVar("N"), tVar("N")], tVar("N")));
    set("random", tFunc([], tPrim("Float"))); set("randint", tFunc([tPrim("Int"), tPrim("Int")], tPrim("Int"))); set("srandom", tFunc([tPrim("Int")], tPrim("Void")));
    set("vec2", tFunc([tVar("N"), tVar("N")], tPrim("Vec"))); set("vec3", tFunc([tVar("N"), tVar("N"), tVar("N")], tPrim("Vec")));
    set("dot", tFunc([tPrim("Vec"), tPrim("Vec")], tPrim("Float"))); set("vlen", tFunc([tPrim("Vec")], tPrim("Float"))); set("norm", tFunc([tPrim("Vec")], tPrim("Vec")));
    set("coroutine", tFunc([tAny()], tAny(), true)); set("resume", tFunc([tAny()], tAny(), true)); set("done", tFunc([tAny()], tPrim("Bool")));
    set("__emit", tFunc([tAny()], tPrim("Void"), true));
    return b;
  }
  lookup(name: string, env: Env): Binding {
    for (let i = env.length - 1; i >= 0; i--) { const f = env[i].get(name); if (f) return f; }
    const v = this.Tm(this.curm).values.get(name); if (v) return bind(v);
    const s = this.sel.get(name); if (s) { const tv = this.Tm(s[0]).pub; const j = tv.get(s[1]); if (j) return bind(j); }
    const q = this.qual.get(name); if (q) return bind(tMod(q));
    return bind(null);
  }
  findMember(cls: string, field: string): [Ty | null, number] {
    let c = cls;
    while (c !== "") { const it = this.Tm(this.curm).classes.get(c); if (!it) break; const f = it.fields.get(field); if (f) return f; const mm = it.methods.get(field); if (mm) return mm; c = it.extends; }
    return [null, 0];
  }
  traitMethod(tr: string, field: string): Ty | null {
    const it = this.Tm(this.curm).traits.get(tr); if (!it) return null;
    const mm = it.methods.get(field); if (mm) return mm;
    for (const u of it.uses) { const r = this.traitMethod(u, field); if (r) return r; }
    return null;
  }
  litType(e: Expr): Ty { switch (e.k) { case "INT": return tPrim("Int"); case "FLT": return tPrim("Float"); case "STR": return tPrim("String"); case "BOOL": return tPrim("Bool"); default: return tAny(); } }

  infer(e: Expr, env: Env): Ty {
    switch (e.k) {
      case "INT": return tPrim("Int");
      case "FLT": return tPrim("Float");
      case "STR": return tPrim("String");
      case "BOOL": return tPrim("Bool");
      case "NAME": { const b = this.lookup(e.sval, env); if (b.ty) return b.ty; this.err(this.lineOf(e), "undefined name '" + e.sval + "'"); return tAny(); }
      case "MEMBER": return this.inferMember(e, env);
      case "CALL": return this.inferCall(e, env);
      case "UNARY": { const t = this.infer(e.lhs!, env); return e.op === "!" ? tPrim("Bool") : t; }
      case "BINARY": return this.inferBinary(e, env);
      case "LIST": {
        if (e.args.length === 0) return tList(tAny());
        let el: Ty | null = null;
        for (const a of e.args) { const t = this.infer(a, env); if (!el) el = t; else if (this.assignable(t, el)) { /* ok */ } else if (this.assignable(el, t)) el = t; else { const cs = this.commonSuper(el, t); if (cs) el = cs; else { this.err(this.lineOf(a), "list elements have differing types"); el = tAny(); } } }
        return tList(el);
      }
      case "INDEX": { const ot = this.infer(e.lhs!, env); const it = this.infer(e.rhs!, env); if (!this.assignable(it, tPrim("Int"))) this.err(this.lineOf(e), "index must be 'Int', got '" + tStr(it) + "'"); if (ot.k === "LIST") return ot.elem!; if (ot.k === "ANY" || ot.k === "TVAR") return tAny(); if (ot.k === "PRIM" && ot.name === "String") return tPrim("String"); this.err(this.lineOf(e), "cannot index '" + tStr(ot) + "'"); return tAny(); }
      case "MATCH": return this.inferMatch(e, env);
      case "CLOSURE": return this.inferClosure(e, env);
      case "YIELD": { this.infer(e.lhs!, env); return tAny(); }
    }
    return tAny();
  }
  inferMember(e: Expr, env: Env): Ty {
    const o = this.infer(e.lhs!, env); const field = e.sval;
    let result: Ty = tAny();
    const MT = this.Tm(this.curm);
    if (o.k === "MODULE") {
      const pv = this.Tm(o.name).pub; const it = pv.get(field);
      if (it) result = it; else this.err(this.lineOf(e), "module '" + o.name + "' has no public member '" + field + "'");
    } else if (o.k === "NAMED" && o.nkind === 0) {
      if (MT.classes.has(o.name)) {
        const mem = this.findMember(o.name, field);
        if (!mem[0]) this.err(this.lineOf(e), "'" + o.name + "' has no member '" + field + "'");
        else {
          if (mem[1] === 2 && this.curClass !== o.name) this.err(this.lineOf(e), "'" + field + "' is private to '" + o.name + "'");
          const s = new Map<string, Ty>(); const g = MT.classes.get(o.name)!.generics; for (let i = 0; i < g.length && i < o.args.length; i++) s.set(g[i], o.args[i]);
          result = this.subst(mem[0], s)!;
        }
      }
    } else if (o.k === "NAMED" && o.nkind === 2) {
      const mt = this.traitMethod(o.name, field);
      if (mt) { const ss = new Map<string, Ty>(); ss.set("Self", o); result = this.subst(mt, ss)!; } else this.err(this.lineOf(e), "trait '" + o.name + "' has no method '" + field + "'");
    } else if (o.k === "TVAR" && o.tbounds.length) {
      for (const tb of o.tbounds) { const mt = this.traitMethod(tb, field); if (mt) { const ss = new Map<string, Ty>(); ss.set("Self", o); result = this.subst(mt, ss)!; break; } }
      if (result.k === "ANY") { const bs = o.tbounds.join(" + "); this.err(this.lineOf(e), "trait bound (" + bs + ") has no member '" + field + "'"); }
    } else if (o.k === "PRIM" && o.name === "Vec") {
      if (field === "x" || field === "y" || field === "z") result = tPrim("Float"); else this.err(this.lineOf(e), "a vector has no member '" + field + "'");
    } else if (o.k === "PRIM" || o.k === "LIST" || o.k === "MAP") {
      const tn = o.k === "PRIM" ? o.name : (o.k === "LIST" ? "List" : "Map");
      const xe = MT.exts.get(tn);
      if (xe && xe.methods.has(field)) { const ss = new Map<string, Ty>(); ss.set("Self", o); result = this.subst(xe.methods.get(field)![0], ss)!; }
      else this.err(this.lineOf(e), "cannot access '." + field + "' on '" + tStr(o) + "'");
    } else if (o.k === "ANY" || o.k === "TVAR") {
      // dynamic / unbounded receiver: result stays Any
    } else this.err(this.lineOf(e), "cannot access '." + field + "' on '" + tStr(o) + "'");
    return result;
  }
  isSignal(t: Ty | null): boolean { return !!t && t.k === "NAMED" && t.nkind === 3; }
  signalCall(e: Expr, sig: Ty, env: Env): Ty {
    const method = e.lhs!.sval; const ps = sig.args;
    if (method === "emit") {
      if (e.args.length !== ps.length) this.err(this.lineOf(e), "signal expects " + ps.length + " argument(s), got " + e.args.length);
      else for (let k = 0; k < e.args.length; k++) { const at = this.infer(e.args[k], env); if (!this.assignable(at, ps[k])) this.err(this.lineOf(e.args[k]), "signal argument " + (k + 1) + ": '" + tStr(at) + "' is not assignable to '" + tStr(ps[k]) + "'"); }
      return tPrim("Void");
    }
    if (e.args.length !== 1) { this.err(this.lineOf(e), "connect expects a single handler"); return tPrim("Void"); }
    const h = this.infer(e.args[0], env);
    if (h.k === "FUNC") {
      if (h.args.length > ps.length) this.err(this.lineOf(e), "handler takes " + h.args.length + " params but the signal emits " + ps.length);
      else for (let k = 0; k < h.args.length; k++) if (!this.assignable(ps[k], h.args[k])) this.err(this.lineOf(e.args[0]), "handler param " + (k + 1) + ": expected '" + tStr(ps[k]) + "', got '" + tStr(h.args[k]) + "'");
    } else if (h.k !== "ANY" && h.k !== "TVAR") this.err(this.lineOf(e.args[0]), "connect expects a function, got '" + tStr(h) + "'");
    return tPrim("Void");
  }
  inferCall(e: Expr, env: Env): Ty {
    if (e.lhs!.k === "MEMBER" && (e.lhs!.sval === "emit" || e.lhs!.sval === "connect")) {
      const recvT = this.infer(e.lhs!.lhs!, env);
      if (this.isSignal(recvT)) return this.signalCall(e, recvT, env);
    }
    const ct = this.infer(e.lhs!, env); const at: Ty[] = []; for (const a of e.args) at.push(this.infer(a, env));
    if (ct.k === "ANY" || ct.k === "TVAR") return tAny();
    if (ct.k !== "FUNC") { this.err(this.lineOf(e), "'" + tStr(ct) + "' is not callable"); return tAny(); }
    if (ct.variadic) return ct.ret!;
    if (at.length !== ct.args.length) { this.err(this.lineOf(e), "expected " + ct.args.length + " argument(s), got " + at.length); return ct.ret!; }
    const s = new Map<string, Ty>(); for (let i = 0; i < at.length; i++) this.unify(ct.args[i], at[i], s);
    for (let i = 0; i < at.length; i++) { const want = this.subst(ct.args[i], s); if (!this.assignable(at[i], want)) this.err(this.lineOf(e.args[i]), "argument " + (i + 1) + ": '" + tStr(at[i]) + "' is not assignable to '" + tStr(want) + "'"); }
    for (const [kk, vv] of ct.bounds) { const sit = s.get(kk); if (!sit) continue; for (const tb of vv) if (!this.satisfies(sit, tb)) this.err(this.lineOf(e), "type '" + tStr(sit) + "' does not satisfy bound '" + tb + "' on '" + kk + "'"); }
    return this.subst(ct.ret, s)!;
  }
  operatorMethod(t: Ty, name: string): Ty | null {
    let m: Ty | null = null;
    if (t.k === "NAMED" && t.nkind === 0) m = this.findMember(t.name, name)[0];
    else if (t.k === "TVAR") { for (const tb of t.tbounds) { const x = this.traitMethod(tb, name); if (x) { m = x; break; } } }
    else if (t.k === "PRIM" || t.k === "LIST" || t.k === "MAP") { const tn = t.k === "PRIM" ? t.name : (t.k === "LIST" ? "List" : "Map"); const xe = this.Tm(this.curm).exts.get(tn); if (xe) { const it = xe.methods.get(name); if (it) m = it[0]; } }
    return (m && m.k === "FUNC") ? m : null;
  }
  inferBinary(e: Expr, env: Env): Ty {
    const op = e.op; const lt = this.infer(e.lhs!, env), rt = this.infer(e.rhs!, env);
    const anyv = (t: Ty) => t.k === "ANY" || t.k === "TVAR";
    if (op === "&&" || op === "||") {
      if (!this.assignable(lt, tPrim("Bool"))) this.err(this.lineOf(e.lhs), "'" + op + "' needs 'Bool', got '" + tStr(lt) + "'");
      if (!this.assignable(rt, tPrim("Bool"))) this.err(this.lineOf(e.rhs), "'" + op + "' needs 'Bool', got '" + tStr(rt) + "'");
      return tPrim("Bool");
    }
    const isVec = (t: Ty) => t.k === "PRIM" && t.name === "Vec";
    if (isVec(lt) || isVec(rt)) {
      const numOrAny = (t: Ty) => anyv(t) || (t.k === "PRIM" && (t.name === "Float" || t.name === "Int"));
      if (op === "==" || op === "!=") return tPrim("Bool");
      if ((op === "+" || op === "-" || op === "*") && isVec(lt) && isVec(rt)) return tPrim("Vec");
      if ((op === "*" || op === "/") && isVec(lt) && numOrAny(rt)) return tPrim("Vec");
      if (op === "*" && isVec(rt) && numOrAny(lt)) return tPrim("Vec");
      this.err(this.lineOf(e.rhs), "cannot apply '" + op + "' to '" + tStr(lt) + "' and '" + tStr(rt) + "'"); return tAny();
    }
    const arith = op === "+" || op === "-" || op === "*" || op === "/" || op === "%";
    const cmp = op === "<" || op === "<=" || op === ">" || op === ">=";
    const nativeNum = lt.k === "PRIM" && rt.k === "PRIM" && lt.name === rt.name && (lt.name === "Int" || lt.name === "Float");
    const nativeStr = op === "+" && lt.k === "PRIM" && lt.name === "String" && rt.k === "PRIM" && rt.name === "String";
    const nativeCmp = lt.k === "PRIM" && rt.k === "PRIM" && lt.name === rt.name && (lt.name === "Int" || lt.name === "Float" || lt.name === "String");
    if (arith && !nativeNum && !nativeStr) {
      const mn = op === "+" ? "add" : op === "-" ? "sub" : op === "*" ? "mul" : op === "/" ? "div" : "mod";
      const m = this.operatorMethod(lt, mn);
      if (m) { if (m.args.length && !this.assignable(rt, m.args[0])) this.err(this.lineOf(e.rhs), "operator '" + op + "' on '" + tStr(lt) + "' expects '" + tStr(m.args[0]) + "', got '" + tStr(rt) + "'"); return m.ret!; }
    }
    if (cmp && !nativeCmp) {
      const m = this.operatorMethod(lt, "compareTo");
      if (m) { if (m.args.length && !this.assignable(rt, m.args[0])) this.err(this.lineOf(e.rhs), "'" + op + "' on '" + tStr(lt) + "' expects '" + tStr(m.args[0]) + "', got '" + tStr(rt) + "'"); return tPrim("Bool"); }
    }
    if (op === "==" || op === "!=") return tPrim("Bool");   // native or user `equals`; result is Bool either way
    if (cmp) { if (!(anyv(lt) || anyv(rt) || nativeCmp)) this.err(this.lineOf(e.rhs), "cannot compare '" + tStr(lt) + "' and '" + tStr(rt) + "'"); return tPrim("Bool"); }
    if (anyv(lt) || anyv(rt)) return tAny();
    if (nativeStr) return tPrim("String");
    if (nativeNum) return lt;
    this.err(this.lineOf(e.rhs), "cannot apply '" + op + "' to '" + tStr(lt) + "' and '" + tStr(rt) + "'"); return tAny();
  }
  inferClosure(e: Expr, env: Env): Ty {
    const sc: Scope = new Map(); const ps: Ty[] = [];
    for (let k = 0; k < e.params.length; k++) { const pt = (k < e.ptypes.length && e.ptypes[k]) ? this.resolveType(e.ptypes[k], new Set(), this.curm) : tAny(); sc.set(e.params[k], bind(pt)); ps.push(pt); }
    env.push(sc); const bt = this.infer(e.lhs!, env); env.pop();
    return tFunc(ps, e.retType ? this.resolveType(e.retType, new Set(), this.curm) : bt);
  }
  inferMatch(e: Expr, env: Env): Ty {
    const subj = this.infer(e.lhs!, env); let result: Ty | null = null;
    for (const arm of e.arms) {
      const binds: Scope = new Map(); for (const p of arm.pats) this.checkPattern(p, subj, binds, this.lineOf(arm.body));
      env.push(binds); const bt = this.infer(arm.body!, env); env.pop();
      if (!result) result = bt; else if (this.assignable(bt, result)) { /* ok */ } else if (this.assignable(result, bt)) result = bt; else { this.err(this.lineOf(arm.body), "match arms have incompatible types '" + tStr(result) + "' and '" + tStr(bt) + "'"); result = tAny(); }
    }
    return result ? result : tPrim("Void");
  }
  checkPattern(p: Pattern, subj: Ty, binds: Scope, line: number): void {
    if (p.k === 0) return;
    if (p.k === 1) { const lt = this.litType(p.lit!); if (!this.assignable(lt, subj)) this.err(line, "pattern '" + tStr(lt) + "' cannot match subject '" + tStr(subj) + "'"); return; }
    if (subj.k === "NAMED" && subj.nkind === 1) {
      const ei = this.Tm(this.curm).enums.get(subj.name); if (!ei) { for (const b of p.binds) binds.set(b, bind(tAny())); return; }
      const v = ei.variants.get(p.name); if (!v) { this.err(line, "'" + p.name + "' is not a variant of '" + subj.name + "'"); for (const b of p.binds) binds.set(b, bind(tAny())); return; }
      const s = new Map<string, Ty>(); const g = ei.generics; for (let i = 0; i < g.length && i < subj.args.length; i++) s.set(g[i], subj.args[i]);
      for (let i = 0; i < p.binds.length; i++) binds.set(p.binds[i], bind(i < v.length ? this.subst(v[i], s) : tAny()));
    } else for (const b of p.binds) binds.set(b, bind(tAny()));
  }
  expectBool(c: Expr, env: Env): void { const t = this.infer(c, env); if (!this.assignable(t, tPrim("Bool"))) this.err(this.lineOf(c), "condition must be 'Bool', got '" + tStr(t) + "'"); }
  checkStmts(ss: Stmt[], env: Env): void { env.push(new Map()); for (const s of ss) this.checkStmt(s, env); env.pop(); }
  checkStmt(s: Stmt, env: Env): void {
    switch (s.k) {
      case "VAR": { let t = s.hasExpr ? this.infer(s.expr!, env) : tAny(); if (s.vtype) { const d = this.resolveType(s.vtype, new Set(), this.curm); if (s.hasExpr && !this.assignable(t, d)) this.err(this.lineOf(s.expr), "'" + s.name + "': cannot assign '" + tStr(t) + "' to declared '" + tStr(d) + "'"); t = d; } env[env.length - 1].set(s.name, bind(t, s.nameLine)); break; }
      case "ASSIGN": { const lt = this.infer(s.target!, env); const rt = this.infer(s.expr!, env); if (!this.assignable(rt, lt)) this.err(this.lineOf(s.expr), "cannot assign '" + tStr(rt) + "' to '" + tStr(lt) + "'"); break; }
      case "EXPR": this.infer(s.expr!, env); break;
      case "RET": { const rt = s.hasExpr ? this.infer(s.expr!, env) : tPrim("Void"); if (!this.assignable(rt, this.curRet)) this.err(s.hasExpr ? this.lineOf(s.expr) : 0, "returning '" + tStr(rt) + "' from a function declared '-> " + tStr(this.curRet) + "'"); break; }
      case "IF": { this.expectBool(s.expr!, env); this.checkStmts(s.body, env); for (const [cond, body] of s.elifs) { this.expectBool(cond, env); this.checkStmts(body, env); } if (s.hasElse) this.checkStmts(s.elseBody, env); break; }
      case "WHILE": { this.expectBool(s.expr!, env); this.loopDepth++; this.checkStmts(s.body, env); this.loopDepth--; break; }
      case "FOR": { const it = this.infer(s.expr!, env); const el = it.k === "LIST" ? it.elem! : tAny(); if (!(it.k === "LIST" || it.k === "ANY" || it.k === "TVAR")) this.err(this.lineOf(s.expr), "cannot iterate over '" + tStr(it) + "'"); env.push(new Map([[s.name, bind(el, s.nameLine)]])); this.loopDepth++; this.checkStmts(s.body, env); this.loopDepth--; env.pop(); break; }
      case "BREAK": case "CONTINUE": if (this.loopDepth === 0) this.err(0, (s.k === "BREAK" ? "'break'" : "'continue'") + " used outside a loop"); break;
      case "RAISE": this.infer(s.expr!, env); break;
      case "TRY": { this.checkStmts(s.body, env); env.push(new Map([[s.name, bind(tAny(), s.nameLine)]])); this.checkStmts(s.elseBody, env); env.pop(); break; }
      case "PASS": break;
    }
  }
  checkFunc(f: Func, cls: string): void {
    const pc = this.curClass, pr = this.curRet, pl = this.loopDepth, pb = this.curBounds;
    this.loopDepth = 0; this.curClass = cls;
    const g = new Set<string>(f.generics); let cg: string[] = [];
    if (cls !== "") cg = this.Tm(this.curm).classes.get(cls)!.generics; for (const x of cg) g.add(x);
    const mb: Bounds = new Map(f.bounds); if (cls !== "") for (const [kk, vv] of this.Tm(this.curm).classes.get(cls)!.bounds) mb.set(kk, vv); this.curBounds = mb;
    this.curRet = f.retType ? this.resolveType(f.retType, g, this.curm) : tPrim("Void");
    const env: Env = [this.builtins()]; const sc: Scope = new Map(); const selfArgs: Ty[] = []; for (const gn of cg) selfArgs.push(tVar(gn));
    for (let k = 0; k < f.params.length; k++) {
      if (f.params[k] === "self") sc.set("self", bind(tNamed(cls, 0, selfArgs)));
      else { const pt = f.ptypes[k] ? this.resolveType(f.ptypes[k], g, this.curm) : tAny(); const pl = k < f.paramLines.length ? f.paramLines[k] : 0; sc.set(f.params[k], bind(pt, pl)); }
    }
    env.push(sc); this.checkStmts(f.body, env);
    this.curClass = pc; this.curRet = pr; this.loopDepth = pl; this.curBounds = pb;
  }
  checkClass(C: ClassAst): void {
    const pbc = this.curBounds; this.curBounds = C.bounds; const pcs = this.curSelf;
    const g = new Set<string>(C.generics); const selfArgs: Ty[] = []; for (const gn of C.generics) selfArgs.push(tVar(gn));
    this.curSelf = tNamed(C.name, 0, selfArgs);
    for (const f of C.fields) if (f.hasInit) { const env: Env = [this.builtins()]; const sc: Scope = new Map(); sc.set("self", bind(tNamed(C.name, 0, selfArgs))); env.push(sc); const pc = this.curClass; this.curClass = C.name; const t = this.infer(f.init!, env); if (f.type) { const d = this.resolveType(f.type, g, this.curm); if (!this.assignable(t, d)) this.err(this.lineOf(f.init), "field '" + f.name + "': cannot assign '" + tStr(t) + "' to '" + tStr(d) + "'"); } this.curClass = pc; }
    for (const m of C.methods) this.checkFunc(m, C.name);
    if (C.hasCtor) {
      const pc = this.curClass, pr = this.curRet, pl = this.loopDepth; this.loopDepth = 0; this.curClass = C.name; this.curRet = tPrim("Void");
      const env: Env = [this.builtins()]; const sc: Scope = new Map(); sc.set("self", bind(tNamed(C.name, 0, selfArgs)));
      for (let k = 0; k < C.ctorParams.length; k++) { const pt = C.ctorPtypes[k] ? this.resolveType(C.ctorPtypes[k], g, this.curm) : tAny(); const pl = k < C.ctorParamLines.length ? C.ctorParamLines[k] : 0; sc.set(C.ctorParams[k], bind(pt, pl)); }
      env.push(sc); this.checkStmts(C.ctorBody, env); this.curClass = pc; this.curRet = pr; this.loopDepth = pl;
    }
    this.curBounds = pbc; this.curSelf = pcs;
  }
  checkExtensions(X: ExtendAst): void {
    let self = this.extSelfType(X.typeName);
    if (!self) { this.err(X.nameLine, "cannot extend unknown type '" + X.typeName + "'"); self = tAny(); }
    for (const mth of X.methods) {
      const pc = this.curClass, pr = this.curRet, pcs = this.curSelf, pl = this.loopDepth, pb = this.curBounds;
      this.loopDepth = 0; this.curClass = ""; this.curSelf = self; this.curBounds = mth.bounds;
      const g = new Set<string>(mth.generics);
      this.curRet = mth.retType ? this.resolveType(mth.retType, g, this.curm) : tPrim("Void");
      const env: Env = [this.builtins()]; const sc: Scope = new Map();
      for (let k = 0; k < mth.params.length; k++) { if (mth.params[k] === "self") sc.set("self", bind(self)); else sc.set(mth.params[k], bind(mth.ptypes[k] ? this.resolveType(mth.ptypes[k], g, this.curm) : tAny())); }
      env.push(sc); this.checkStmts(mth.body, env);
      this.curClass = pc; this.curRet = pr; this.curSelf = pcs; this.loopDepth = pl; this.curBounds = pb;
    }
  }
  checkTraitDefaults(Tr: TraitAst): void {
    for (const mth of Tr.methods) {
      if (mth.body.length === 0) continue;
      const pc = this.curClass, pr = this.curRet, pcs = this.curSelf, pl = this.loopDepth, pb = this.curBounds;
      this.loopDepth = 0; this.curClass = ""; this.curSelf = tNamed(Tr.name, 2); this.curBounds = mth.bounds;
      const g = new Set<string>(mth.generics);
      this.curRet = mth.retType ? this.resolveType(mth.retType, g, this.curm) : tPrim("Void");
      const env: Env = [this.builtins()]; const sc: Scope = new Map();
      for (let k = 0; k < mth.params.length; k++) { if (mth.params[k] === "self") sc.set("self", bind(tNamed(Tr.name, 2))); else sc.set(mth.params[k], bind(mth.ptypes[k] ? this.resolveType(mth.ptypes[k], g, this.curm) : tAny())); }
      env.push(sc); this.checkStmts(mth.body, env);
      this.curClass = pc; this.curRet = pr; this.curSelf = pcs; this.loopDepth = pl; this.curBounds = pb;
    }
  }
  checkConformance(): void {
    for (const m of this.order) {
      this.curm = m; const MT = this.Tm(m);
      for (const C of this.mods.get(m)!.classes) {
        for (const tn of C.uses) {
          const ti = MT.traits.get(tn);
          if (!ti) { this.err(C.nameLine, "class '" + C.name + "' uses '" + tn + "', which is not a trait"); continue; }
          const ga: Ty[] = []; for (const gn of C.generics) ga.push(tVar(gn));
          const selfSub = new Map<string, Ty>(); selfSub.set("Self", tNamed(C.name, 0, ga));
          for (const [reqName, reqTy] of ti.methods) {
            const want = this.subst(reqTy, selfSub);
            const have = this.findMember(C.name, reqName);
            if (!have[0]) { this.err(C.nameLine, "class '" + C.name + "' does not implement '" + tn + "." + reqName + "' required as '" + tStr(want) + "'"); continue; }
            if (!this.assignable(have[0], want)) this.err(C.nameLine, "class '" + C.name + "': method '" + reqName + "' is '" + tStr(have[0]) + "' but trait '" + tn + "' requires '" + tStr(want) + "'");
          }
        }
      }
      for (const X of this.mods.get(m)!.extensions) {
        const self = this.extSelfType(X.typeName); const selfSub = new Map<string, Ty>(); selfSub.set("Self", self ? self : tAny());
        const xi = MT.exts.get(X.typeName)!;
        for (const tn of X.uses) {
          const ti = MT.traits.get(tn);
          if (!ti) { this.err(X.nameLine, "extension of '" + X.typeName + "' uses '" + tn + "', which is not a trait"); continue; }
          for (const [reqName, reqTy] of ti.methods) {
            const want = this.subst(reqTy, selfSub);
            const mit = xi.methods.get(reqName);
            if (!mit) { this.err(X.nameLine, "extension of '" + X.typeName + "' does not implement '" + tn + "." + reqName + "' required as '" + tStr(want) + "'"); continue; }
            if (!this.assignable(mit[0], want)) this.err(X.nameLine, "extension of '" + X.typeName + "': method '" + reqName + "' is '" + tStr(mit[0]) + "' but trait '" + tn + "' requires '" + tStr(want) + "'");
          }
        }
      }
    }
  }
  check(): void {
    this.checkConformance();
    for (const m of this.order) {
      this.curm = m; this.qual = new Map(); this.sel = new Map();
      for (const im of this.mods.get(m)!.imports) {
        if (im.names.length) { for (const n of im.names) this.sel.set(n, [im.path, n]); }
        else if (im.alias !== "") this.qual.set(im.alias, im.path);
        else { const d = im.path.lastIndexOf("."); this.qual.set(d === -1 ? im.path : im.path.slice(d + 1), im.path); }
      }
      const P = this.mods.get(m)!;
      for (const f of P.funcs) this.checkFunc(f, "");
      for (const Tr of P.traits) this.checkTraitDefaults(Tr);
      for (const X of P.extensions) this.checkExtensions(X);
      for (const C of P.classes) this.checkClass(C);
      for (const g of P.globals) if (g.hasExpr) { const env: Env = [this.builtins()]; const t = this.infer(g.expr!, env); if (g.vtype) { const d = this.resolveType(g.vtype, new Set(), this.curm); if (!this.assignable(t, d)) this.err(this.lineOf(g.expr), "'" + g.name + "': cannot assign '" + tStr(t) + "' to '" + tStr(d) + "'"); } }
      if (P.hasInit) { const pc = this.curClass, pr = this.curRet; this.curClass = ""; this.curRet = tPrim("Void"); const env: Env = [this.builtins()]; this.checkStmts(P.initBody, env); this.curClass = pc; this.curRet = pr; }
    }
    this.errors.sort((a, b) => (a[0] !== b[0] ? (a[0] < b[0] ? -1 : 1) : a[1] !== b[1] ? a[1] - b[1] : a[2] !== b[2] ? (a[2] < b[2] ? -1 : 1) : 0));
  }
}
