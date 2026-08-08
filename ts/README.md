# rosegold-ts

A **TypeScript port of RoseGold's front-end**, built stage-by-stage and held to
the same discipline as the self-hosted `.rg` pipeline: every stage's output must
be **byte-identical to the canonical C++ implementation's** matching dump flag.
The C++ compiler (`cpp/rosegoldc`) is the ground truth; this port is "correct"
exactly when it matches it.

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
    main.ts      CLI driver (flag dispatch, mirrors cpp/src/main.cpp)
  test/
    parity.mjs   diffs each stage's dump against the matching `rosegoldc` flag
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
node ts/test/parity.mjs                                    # verify every stage across every example
```

## Status

| Stage   | Files                       | Verified against     | State |
|---------|-----------------------------|----------------------|-------|
| Lexer   | `src/lexer.ts`              | `rosegoldc --tokens` | ✅ byte-identical on every example (43 files) |
| Parser  | `src/parser.ts` · `ast.ts` · `astdump.ts` | `rosegoldc --ast` | ✅ byte-identical on every example (43 files) |
| Checker | —                           | `rosegoldc --check`  | ⏳ next |

The **lexer** is a faithful port: the offside rule (INDENT/DEDENT/NEWLINE off an
indentation stack, with `(`/`[` suppressing newlines), string escapes, floats,
`#` line and `#/ … /#` block comments, and one-/two-character operators.

The **parser** ports the full recursive-descent grammar — module, imports,
globals, funcs, classes, traits, enums, `extern`, `init`, `match`, closures, the
whole precedence ladder — so it consumes every construct in the examples, even
though `--ast` only dumps the module/globals/funcs subset. Both stages reproduce
the C++ lex/parse errors verbatim (`unterminated string`, `line N: expected …`).
