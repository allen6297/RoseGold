// Module loader — a faithful port of loadModules() in cpp/src/runtime.hpp.
// Parses the entry file and its transitive imports, producing a dependency-first
// order. Imports resolve to `<root>/<dotted.path>.rg` relative to the entry's
// directory; a missing file throws "cannot open module file: <path>" (which the
// --check driver reports as `error: …`, matching the C++ CLI).

import { readFileSync } from "node:fs";
import { lex } from "./lexer.ts";
import { Parser } from "./parser.ts";
import type { Parsed } from "./ast.ts";

export class LoadError extends Error {}

function rgReadFile(path: string): string {
  try { return readFileSync(path, "utf8"); }
  catch { throw new LoadError("cannot open module file: " + path); }
}
const rgDirOf = (p: string): string => { const s = p.lastIndexOf("/"); return s === -1 ? "." : p.slice(0, s); };
const rgModToFile = (root: string, mod: string): string => root + "/" + mod.replace(/\./g, "/") + ".rg";

export interface Loaded { entryName: string; mods: Map<string, Parsed>; order: string[]; }

export function loadModules(entryPath: string): Loaded {
  const root = rgDirOf(entryPath);
  const mods = new Map<string, Parsed>();
  const entry = new Parser(lex(rgReadFile(entryPath))).program();
  const entryName = entry.module === "" ? "$entry" : entry.module;
  mods.set(entryName, entry);

  const work: string[] = [entryName];
  while (work.length) {
    const m = work.pop()!;
    for (const imp of mods.get(m)!.imports) {
      if (mods.has(imp.path)) continue;
      const f = rgModToFile(root, imp.path);
      mods.set(imp.path, new Parser(lex(rgReadFile(f))).program());
      work.push(imp.path);
    }
  }

  const done = new Set<string>();
  const active = new Set<string>();
  const order: string[] = [];
  const dfs = (m: string): void => {
    if (done.has(m) || active.has(m)) return;
    active.add(m);
    for (const imp of mods.get(m)!.imports) if (mods.has(imp.path)) dfs(imp.path);
    active.delete(m); done.add(m); order.push(m);
  };
  dfs(entryName);
  return { entryName, mods, order };
}
