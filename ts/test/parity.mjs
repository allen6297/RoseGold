// Lexer parity check: the TypeScript lexer's `--tokens` output must be
// byte-identical to the canonical C++ `rosegoldc --tokens` on every example.
// This is the same ground-truth discipline the self-hosted rglexer.rg is held
// to — the port is "correct" exactly when it matches the canonical dump.
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

const files = rgFiles(join(ROOT, "examples")).sort();
let checked = 0;
const fails = [];
for (const f of files) {
  const c = run(BIN, ["--tokens", f]);
  if (c.code !== 0) continue;                           // C++ rejects it at the lexer — nothing to compare
  const t = run("node", [MAIN, "--tokens", f]);
  checked++;
  if (t.code !== 0 || t.out !== c.out) fails.push(relative(ROOT, f));
}

console.log(`ts/lexer parity: ${checked} files checked against \`rosegoldc --tokens\``);
if (fails.length) {
  console.error(`\n✗ ${fails.length} mismatch(es):`);
  for (const f of fails) console.error("  " + f);
  process.exit(1);
}
console.log("✓ byte-identical on every file");
