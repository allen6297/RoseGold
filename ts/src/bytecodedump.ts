// Bytecode dump (rosegoldc --bytecode) — a faithful port of cpp/src/bytecodedump.hpp.
// Per-function disassembly: `func <name> nlocals=N` then one `  <i>: OP a b` line
// per instruction (operands always shown). The function name has its `mod::`
// prefix stripped. This is the ground truth the compiler stage is diffed against.

import type { Program } from "./compiler.ts";

export function dumpBytecode(prog: Program): string {
  let o = "";
  for (const f of prog.funcs) {
    let nm = f.name; const p = nm.indexOf("::"); if (p !== -1) nm = nm.slice(p + 2);
    o += "fn " + nm + " nlocals=" + f.nlocals + "\n";
    for (let i = 0; i < f.code.length; i++) {
      const inx = f.code[i];
      o += "  " + i + ": " + inx.op + " " + inx.a + " " + inx.b + "\n";
    }
  }
  return o;
}
