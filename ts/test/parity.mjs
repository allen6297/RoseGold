// Front-end parity check: each ported stage's dump must be byte-identical to
// the canonical C++ `rosegoldc` dump on every example. Same ground-truth
// discipline the self-hosted .rg pipeline is held to — a stage is "correct"
// exactly when it matches the canonical dump.
//
//   --tokens  lexer  (ts/src/lexer.ts)
//   --ast     parser (ts/src/parser.ts + astdump.ts)
//
//   node ts/test/parity.mjs        (build cpp/rosegoldc first)

import { execFileSync } from "node:child_process";
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

// Run a command, capturing stdout; never throw on a non-zero exit.
function run(cmd, args) {
  try {
    return { code: 0, out: execFileSync(cmd, args, { encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] }) };
  } catch (e) {
    return { code: e.status ?? 1, out: e.stdout ?? "" };
  }
}

const STAGES = [
  { flag: "--tokens", stage: "lexer" },
  { flag: "--ast", stage: "parser" },
];

const files = rgFiles(join(ROOT, "examples")).sort();
let anyFail = false;
for (const { flag, stage } of STAGES) {
  let checked = 0;
  const fails = [];
  for (const f of files) {
    const c = run(BIN, [flag, f]);
    if (c.code !== 0) continue;                         // C++ rejects it at this stage — nothing to compare
    const t = run("node", [MAIN, flag, f]);
    checked++;
    if (t.code !== 0 || t.out !== c.out) fails.push(relative(ROOT, f));
  }
  if (fails.length) {
    anyFail = true;
    console.error(`✗ ts/${stage} (${flag}): ${fails.length}/${checked} mismatch(es) vs \`rosegoldc ${flag}\``);
    for (const f of fails) console.error("    " + f);
  } else {
    console.log(`✓ ts/${stage} (${flag}): byte-identical to \`rosegoldc ${flag}\` on ${checked} files`);
  }
}
if (anyFail) process.exit(1);
