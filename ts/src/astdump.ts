// AST dump (rosegoldc --ast) — a faithful port of cpp/src/astdump.hpp. A flat
// S-expression rendering of the module/globals/funcs subset of the parsed AST,
// the ground truth the parser stage is diffed against (mirroring how --tokens
// grounds the lexer). Literals with awkward-to-match payloads (floats, strings)
// print their tag only; match/closure show a tag placeholder.

import type { TyNode, Expr, Stmt, Parsed } from "./ast.ts";

// docTy — render a type node (port of docTy in doc.hpp).
function docTy(t: TyNode | null): string {
  if (!t) return "";
  if (t.isFunc) return "fn(" + t.fparams.map(docTy).join(", ") + ") -> " + docTy(t.fret);
  let s = t.name;
  if (t.args.length) s += "<" + t.args.map(docTy).join(", ") + ">";
  return s;
}
const adTy = (t: TyNode | null): string => (t ? docTy(t) : "Void");

function adExpr(e: Expr | null): string {
  if (!e) return "nil";
  switch (e.k) {
    case "INT": return "(int " + e.ival + ")";
    case "FLT": return "(flt)";
    case "STR": return "(str)";
    case "BOOL": return e.bval ? "(bool true)" : "(bool false)";
    case "NAME": return "(name " + e.sval + ")";
    case "UNARY": return "(unary " + e.op + " " + adExpr(e.lhs) + ")";
    case "BINARY": return "(binop " + e.op + " " + adExpr(e.lhs) + " " + adExpr(e.rhs) + ")";
    case "CALL": return "(call " + adExpr(e.lhs) + e.args.map((a) => " " + adExpr(a)).join("") + ")";
    case "MEMBER": return "(member " + adExpr(e.lhs) + " " + e.sval + ")";
    case "INDEX": return "(index " + adExpr(e.lhs) + " " + adExpr(e.rhs) + ")";
    case "LIST": return "(list" + e.args.map((a) => " " + adExpr(a)).join("") + ")";
    case "MATCH": return "(match)";
    case "CLOSURE": return "(closure)";
    case "YIELD": return "(yield " + adExpr(e.lhs) + ")";
  }
}

const adBlock = (b: Stmt[]): string => b.map((s) => " " + adStmt(s)).join("");

function adStmt(s: Stmt): string {
  switch (s.k) {
    case "VAR": return "(var " + s.name + (s.hasExpr ? " " + adExpr(s.expr) : "") + ")";
    case "ASSIGN": return "(assign " + adExpr(s.target) + " " + adExpr(s.expr) + ")";
    case "EXPR": return "(expr " + adExpr(s.expr) + ")";
    case "RET": return "(return" + (s.hasExpr ? " " + adExpr(s.expr) : "") + ")";
    case "IF": {
      let o = "(if " + adExpr(s.expr) + " (then" + adBlock(s.body) + ")";
      for (const [cond, body] of s.elifs) o += " (elif " + adExpr(cond) + adBlock(body) + ")";
      if (s.hasElse) o += " (else" + adBlock(s.elseBody) + ")";
      return o + ")";
    }
    case "WHILE": return "(while " + adExpr(s.expr) + adBlock(s.body) + ")";
    case "FOR": return "(for " + s.name + " " + adExpr(s.expr) + adBlock(s.body) + ")";
    case "PASS": return "(pass)";
    case "BREAK": return "(break)";
    case "CONTINUE": return "(continue)";
    case "TRY": return "(try" + adBlock(s.body) + " (catch " + s.name + adBlock(s.elseBody) + "))";
    case "RAISE": return "(raise " + adExpr(s.expr) + ")";
  }
}

export function dumpAst(mod: string, P: Parsed): string {
  let o = "(module " + mod + ")\n";
  for (const g of P.globals) o += adStmt(g) + "\n";
  for (const f of P.funcs) {
    o += "(fn " + f.name + " (params";
    for (let k = 0; k < f.params.length; k++) {
      const ty = k < f.ptypes.length && f.ptypes[k] ? adTy(f.ptypes[k]) : "Any";
      o += " (" + f.params[k] + " " + ty + ")";
    }
    o += ") " + (f.retType ? adTy(f.retType) : "Void");
    o += adBlock(f.body);
    o += ")\n";
  }
  return o;
}
