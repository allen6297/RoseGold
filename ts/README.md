# rosegold-ts

A **TypeScript port of RoseGold's compiler front-and-back-end**, built
stage-by-stage and held to the same discipline as the self-hosted `.rg`
pipeline: every stage's output must be **byte-identical to the canonical C++
implementation's** matching dump flag. The C++ compiler (`cpp/rosegoldc`) is the
ground truth; this port is "correct" exactly when it matches it. The full
pipeline — **lex → parse → type-check → compile to bytecode** — now runs in
TypeScript, each stage proven identical to the canonical implementation.

Why a second port, in TypeScript? Readability and hackability — a garbage-
collected language with sum types, closures, and a huge editor/tooling ecosystem
is a comfortable place to prototype language changes before mirroring them into
the canonical C++.

## Layout (mirrors `cpp/src/`)

```
ts/
  src/
    lexer.ts     port of cpp/src/lexer.hpp — offside-rule tokenizer
    ast.ts       port of cpp/src/ast.hpp — node types + factories
    parser.ts    port of cpp/src/parser.hpp — recursive-descent parser
    astdump.ts   port of cpp/src/astdump.hpp — --ast S-expression dump
    modules.ts   port of loadModules() (cpp/src/runtime.hpp) — module graph
    types.ts     port of cpp/src/types.hpp — the static type checker
    compiler.ts  port of cpp/src/compiler.hpp + buildProgram() — bytecode compiler
    bytecodedump.ts  port of cpp/src/bytecodedump.hpp — --bytecode disassembly
    main.ts      CLI driver (flag dispatch, mirrors cpp/src/main.cpp)
  test/
    parity.mjs   diffs each stage against the matching `rosegoldc` flag
                 (stdout, stderr, and exit code)
  package.json
```

## Requirements

Node **≥ 23.6** — runs `.ts` directly via built-in type-stripping, no build step,
no dependencies (only Node's standard library). Verified on Node 25.

## Run

```bash
clang++ -std=c++17 -O2 -o cpp/rosegoldc cpp/src/main.cpp   # build the ground truth once
node ts/src/main.ts --tokens examples/prog.rg              # dump tokens (matches `rosegoldc --tokens`)
node ts/src/main.ts --ast    examples/prog.rg              # dump AST    (matches `rosegoldc --ast`)
node ts/src/main.ts --check  examples/errors/typeerrors.rg # type-check  (matches `rosegoldc --check`)
node ts/src/main.ts --bytecode examples/prog.rg            # disassemble (matches `rosegoldc --bytecode`)
node ts/test/parity.mjs                                    # verify every stage across every example
```

## Status — the whole compiler, self-checked

| Stage    | Files                       | Verified against       | State |
|----------|-----------------------------|------------------------|-------|
| Lexer    | `src/lexer.ts`              | `rosegoldc --tokens`   | ✅ byte-identical on every example (43 files) |
| Parser   | `src/parser.ts` · `ast.ts` · `astdump.ts` | `rosegoldc --ast` | ✅ byte-identical on every example (43 files) |
| Checker  | `src/types.ts` · `modules.ts` | `rosegoldc --check`  | ✅ byte-identical on every example (43 files) |
| Compiler | `src/compiler.ts` · `bytecodedump.ts` | `rosegoldc --bytecode` | ✅ byte-identical on every example (43 files) |

The **lexer** is a faithful port: the offside rule (INDENT/DEDENT/NEWLINE off an
indentation stack, with `(`/`[` suppressing newlines), string escapes, floats,
`#` line and `#/ … /#` block comments, and one-/two-character operators.

The **parser** ports the full recursive-descent grammar — module, imports,
globals, funcs, classes, traits, enums, `extern`, `init`, `match`, closures, the
whole precedence ladder — so it consumes every construct in the examples, even
though `--ast` only dumps the module/globals/funcs subset.

The **checker** ports the full static type system — multi-file module loading,
generics with unification, traits + trait-bounded generics, retroactive
`extend`, operator overloading, enums/`match`, signals, and visibility — and
reproduces the C++ diagnostics verbatim: the same messages, the same sort order,
and `error: cannot open module file: …` on a bad import. (The LSP occurrence
index and the compiler-only AST rewrites are omitted, since neither affects
`--check` output.)

The **compiler** ports the reservation + compile passes (`buildProgram`) and the
whole code generator: local-slot allocation, the monotonic constant pool,
if/while/for/try lowering, `match` dispatch, closures with free-variable capture,
method/module/variant/builtin/native call forms, and class construction. Like
`--bytecode` itself, it runs with **no type check**, so it compiles the raw
(un-desugared) AST — matching the canonical disassembly instruction-for-instruction.

Every stage reproduces the C++ behavior verbatim — `unterminated string`,
`line N: expected …`, each `--check` diagnostic, and each function's bytecode —
matching stdout, stderr, and exit code.
