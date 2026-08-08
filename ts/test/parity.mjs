// Front-end parity check: each ported stage's output must be byte-identical to
// the canonical C++ `rosegoldc` on every example — same stdout, same stderr, and
// same exit code. Same ground-truth discipline the self-hosted .rg pipeline is
// held to: a stage is "correct" exactly when it matches the canonical tool.
//
//   --tokens  lexer   (ts/src/lexer.ts)
//   --ast     parser  (ts/src/parser.ts + astdump.ts)
//   --check   checker (ts/src/types.ts + modules.ts)   [diagnostics on stderr]
//
//   node ts/test/parity.mjs        (build cpp/rosegoldc first)

import { spawnSync } from "node:child_process";
import { readdirSync } from "node:fs";
import { join, dirname, resolve, relative } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, "..", "..");                 // repo root
const BIN = join(ROOT, "cpp", "rosegoldc");
const MAIN = join(ROOT, "ts", "src", "main.ts");

function rgFiles(dir) {
  const out = [];
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    const p = join(dir, e.name);
    if (e.isDirectory()) out.push(...rgFiles(p));
    else if (e.name.endsWith(".rg")) out.push(p);
  }
  return out;
}

// Run a command from the repo root, capturing stdout/stderr/exit without throwing.
// Paths are passed relative to ROOT so diagnostics read the same on both sides.
function run(cmd, args, extraEnv) {
  const r = spawnSync(cmd, args, { cwd: ROOT, encoding: "utf8", env: extraEnv ? { ...process.env, ...extraEnv } : process.env });
  return { code: r.status ?? 1, out: r.stdout ?? "", err: r.stderr ?? "" };
}
const same = (a, b) => a.code === b.code && a.out === b.out && a.err === b.err;

const STAGES = [
  { flag: "--tokens", stage: "lexer" },
  { flag: "--ast", stage: "parser" },
  { flag: "--check", stage: "checker" },
];

const files = rgFiles(join(ROOT, "examples")).sort().map((f) => relative(ROOT, f));
let anyFail = false;
for (const { flag, stage } of STAGES) {
  const fails = [];
  for (const rel of files) {
    const c = run(BIN, [flag, rel]);
    const t = run("node", [MAIN, flag, rel], { NODE_NO_WARNINGS: "1" });
    if (!same(c, t)) fails.push(rel);
  }
  if (fails.length) {
    anyFail = true;
    console.error(`✗ ts/${stage} (${flag}): ${fails.length}/${files.length} mismatch(es) vs \`rosegoldc ${flag}\``);
    for (const f of fails) console.error("    " + f);
  } else {
    console.log(`✓ ts/${stage} (${flag}): byte-identical to \`rosegoldc ${flag}\` on ${files.length} files`);
  }
}
if (anyFail) process.exit(1);
