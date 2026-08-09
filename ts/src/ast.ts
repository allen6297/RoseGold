// RoseGold AST — a faithful port of cpp/src/ast.hpp. Nodes are plain objects
// built imperatively by the parser (mirroring the C++ `auto n = make_...; n->f = …`
// style), so each has a small factory that fills in defaults.

export interface TyNode {
  name: string;
  args: TyNode[];
  isFunc: boolean;
  fparams: TyNode[];
  fret: TyNode | null;
}
export const mkTy = (): TyNode => ({ name: "", args: [], isFunc: false, fparams: [], fret: null });

export interface Pattern { k: number; lit: Expr | null; name: string; binds: string[]; }
export interface Arm { pats: Pattern[]; body: Expr | null; }

export type ExprKind =
  | "INT" | "FLT" | "STR" | "BOOL" | "NAME" | "UNARY" | "BINARY"
  | "CALL" | "LIST" | "INDEX" | "MEMBER" | "MATCH" | "CLOSURE" | "YIELD";

export interface Expr {
  k: ExprKind;
  ival: number; dval: number; bval: boolean;
  sval: string; op: string;
  lhs: Expr | null; rhs: Expr | null;
  args: Expr[];
  arms: Arm[];
  params: string[];                    // CLOSURE param names
  ptypes: (TyNode | null)[]; retType: TyNode | null;
  line: number;
}
export const mkExpr = (k: ExprKind): Expr => ({
  k, ival: 0, dval: 0, bval: false, sval: "", op: "", lhs: null, rhs: null,
  args: [], arms: [], params: [], ptypes: [], retType: null, line: 0,
});

export type StmtKind =
  | "VAR" | "ASSIGN" | "EXPR" | "RET" | "IF" | "WHILE" | "FOR"
  | "BREAK" | "CONTINUE" | "TRY" | "RAISE" | "PASS";

export interface Stmt {
  k: StmtKind;
  name: string; nameLine: number;
  target: Expr | null; expr: Expr | null; hasExpr: boolean;
  vtype: TyNode | null; vis: number;
  body: Stmt[]; elifs: Array<[Expr, Stmt[]]>; elseBody: Stmt[]; hasElse: boolean;
}
export const mkStmt = (k: StmtKind): Stmt => ({
  k, name: "", nameLine: 0, target: null, expr: null, hasExpr: false,
  vtype: null, vis: 0, body: [], elifs: [], elseBody: [], hasElse: false,
});

// Generic bounds: parameter name -> the trait names bounding it (`<T: A + B>`).
export type Bounds = Map<string, string[]>;

export interface Func {
  name: string; nameLine: number;
  params: string[]; paramLines: number[]; ptypes: (TyNode | null)[];
  retType: TyNode | null; generics: string[]; bounds: Bounds;
  isSig: boolean; vis: number; body: Stmt[];
  tag: string;   // extern library namespace ("" = global); unused by the dump-only port
}
export const mkFunc = (): Func => ({
  name: "", nameLine: 0, params: [], paramLines: [], ptypes: [],
  retType: null, generics: [], bounds: new Map(), isSig: false, vis: 0, body: [], tag: "",
});

export interface Field {
  name: string; nameLine: number;
  type: TyNode | null; vis: number; init: Expr | null; hasInit: boolean;
}
export const mkField = (): Field =>
  ({ name: "", nameLine: 0, type: null, vis: 0, init: null, hasInit: false });

export interface SignalDecl {
  name: string; nameLine: number;
  params: string[]; paramLines: number[]; ptypes: (TyNode | null)[];
}
export const mkSignal = (): SignalDecl =>
  ({ name: "", nameLine: 0, params: [], paramLines: [], ptypes: [] });

export interface ClassAst {
  name: string; nameLine: number;
  generics: string[]; bounds: Bounds; extends: string; uses: string[]; vis: number;
  fields: Field[]; signals: SignalDecl[];
  hasCtor: boolean; ctorParams: string[]; ctorParamLines: number[];
  ctorPtypes: (TyNode | null)[]; ctorBody: Stmt[]; methods: Func[];
}
export const mkClass = (): ClassAst => ({
  name: "", nameLine: 0, generics: [], bounds: new Map(), extends: "", uses: [], vis: 0,
  fields: [], signals: [], hasCtor: false, ctorParams: [], ctorParamLines: [], ctorPtypes: [], ctorBody: [], methods: [],
});

export interface TraitAst {
  name: string; nameLine: number;
  generics: string[]; bounds: Bounds; uses: string[]; vis: number; methods: Func[];
}
export const mkTrait = (): TraitAst =>
  ({ name: "", nameLine: 0, generics: [], bounds: new Map(), uses: [], vis: 0, methods: [] });

export interface ExtendAst {
  typeName: string; nameLine: number; uses: string[]; methods: Func[];
}
export const mkExtend = (): ExtendAst =>
  ({ typeName: "", nameLine: 0, uses: [], methods: [] });

export interface EnumAst {
  name: string; nameLine: number;
  generics: string[]; vis: number; variants: Array<[string, TyNode[]]>;
}
export const mkEnum = (): EnumAst =>
  ({ name: "", nameLine: 0, generics: [], vis: 0, variants: [] });

export interface Import { path: string; alias: string; names: string[]; pub: boolean; }

export interface Parsed {
  module: string; imports: Import[]; funcs: Func[]; externs: Func[]; externTypes: string[];
  globals: Stmt[]; initBody: Stmt[]; hasInit: boolean;
  classes: ClassAst[]; traits: TraitAst[]; extensions: ExtendAst[]; enums: EnumAst[];
}
export const mkParsed = (): Parsed => ({
  module: "", imports: [], funcs: [], externs: [], externTypes: [], globals: [], initBody: [], hasInit: false,
  classes: [], traits: [], extensions: [], enums: [],
});

export class ParseError extends Error {}
