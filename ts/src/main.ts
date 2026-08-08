// rosegold-ts — the TypeScript front-end's CLI driver, mirroring the flag
// dispatch in cpp/src/main.cpp. Each dump flag is verified against the canonical
// C++ implementation's matching flag (see ts/test/). Stages land here as they
// are ported: --tokens (done), then --ast, --check.
//
// Run directly with Node's built-in TypeScript support:  node ts/src/main.ts --tokens file.rg

import { readFileSync } from "node:fs";
import { lex } from "./lexer.ts";
import { Parser } from "./parser.ts";
import { dumpAst } from "./astdump.ts";
import { loadModules } from "./modules.ts";
import { TypeChecker } from "./types.ts";
import { buildProgram } from "./compiler.ts";
import { dumpBytecode } from "./bytecodedump.ts";

// Token kinds that carry a value in `--tokens` output; the rest print bare.
// (STR is intentionally omitted — its decoded value can hold arbitrary bytes,
// so the C++ dump prints the bare kind. Must match main.cpp.)
const WITH_VALUE = new Set(["INT", "FLT", "IDENT", "KW", "OP"]);

// `rosegold-ts --tokens <file>` — dump the token stream, one per line:
//   "KIND value" for INT/FLT/IDENT/KW/OP; bare "KIND" for STR/NEWLINE/INDENT/DEDENT/END.
function tokensCmd(path: string): number {
  let src: string;
  try { src = readFileSync(path, "utf8"); }
  catch { process.stderr.write(`cannot open ${path}\n`); return 2; }
  try {
    const out: string[] = [];
    for (const t of lex(src)) {
      out.push(WITH_VALUE.has(t.kind) ? `${t.kind} ${t.value}\n` : `${t.kind}\n`);
    }
    process.stdout.write(out.join(""));
  } catch (e) {
    process.stderr.write("error: " + (e instanceof Error ? e.message : String(e)) + "\n");
    return 1;
  }
  return 0;
}

// `rosegold-ts --ast <file>` — dump the parsed AST as flat S-expressions,
// module defaulting to "$entry" when the file omits a `module` decl (matches main.cpp).
function astCmd(path: string): number {
  let src: string;
  try { src = readFileSync(path, "utf8"); }
  catch { process.stderr.write(`cannot open ${path}\n`); return 2; }
  try {
    const P = new Parser(lex(src)).program();
    process.stdout.write(dumpAst(P.module === "" ? "$entry" : P.module, P));
  } catch (e) {
    process.stderr.write("error: " + (e instanceof Error ? e.message : String(e)) + "\n");
    return 1;
  }
  return 0;
}

// `rosegold-ts --check <file>` — type-check the entry module + its imports.
// On failure, print the sorted diagnostics to stderr and exit 1, matching the
// C++ CLI: `type errors:` then `  <module>[:<line>]: <message>` per error; a
// module-load failure prints `error: <message>` instead.
function checkCmd(path: string): number {
  try {
    const { mods, order } = loadModules(path);
    const tc = new TypeChecker(mods, order);
    tc.build();
    tc.check();
    if (tc.errors.length) {
      let out = "type errors:\n";
      for (const [mm, ln, msg] of tc.errors) out += ln ? `  ${mm}:${ln}: ${msg}\n` : `  ${mm}: ${msg}\n`;
      process.stderr.write(out);
      return 1;
    }
  } catch (e) {
    process.stderr.write("error: " + (e instanceof Error ? e.message : String(e)) + "\n");
    return 1;
  }
  return 0;
}

// `rosegold-ts --bytecode <file>` — compile the entry module + imports (no type
// check, matching main.cpp) and disassemble the program per function.
function bytecodeCmd(path: string): number {
  try {
    const { mods, order } = loadModules(path);
    process.stdout.write(dumpBytecode(buildProgram(mods, order)));
  } catch (e) {
    process.stderr.write("error: " + (e instanceof Error ? e.message : String(e)) + "\n");
    return 1;
  }
  return 0;
}

function main(argv: string[]): number {
  if (argv[0] === "--tokens" && argv[1]) return tokensCmd(argv[1]);
  if (argv[0] === "--ast" && argv[1]) return astCmd(argv[1]);
  if (argv[0] === "--check" && argv[1]) return checkCmd(argv[1]);
  if (argv[0] === "--bytecode" && argv[1]) return bytecodeCmd(argv[1]);
  process.stderr.write("usage: rosegold-ts (--tokens | --ast | --check | --bytecode) <file.rg>\n");
  return 2;
}

process.exit(main(process.argv.slice(2)));
