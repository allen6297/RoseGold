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
    main.ts      CLI driver (flag dispatch, mirrors cpp/src/main.cpp)
  test/
    parity.mjs   diffs `--tokens` output against `rosegoldc --tokens`
  package.json
```

## Requirements

Node **≥ 23.6** — runs `.ts` directly via built-in type-stripping, no build step,
no dependencies (only Node's standard library). Verified on Node 25.

## Run

```bash
clang++ -std=c++17 -O2 -o cpp/rosegoldc cpp/src/main.cpp   # build the ground truth once
node ts/src/main.ts --tokens examples/prog.rg              # dump tokens (matches `rosegoldc --tokens`)
node ts/test/parity.mjs                                    # verify parity across every example
```

## Status

| Stage   | File          | Verified against      | State |
|---------|---------------|-----------------------|-------|
| Lexer   | `src/lexer.ts`| `rosegoldc --tokens`  | ✅ byte-identical on every example (43 files) |
| Parser  | —             | `rosegoldc --ast`     | ⏳ next |
| Checker | —             | `rosegoldc --check`   | ⏳ |

The lexer is a faithful port: the offside rule (INDENT/DEDENT/NEWLINE off an
indentation stack, with `(`/`[` suppressing newlines), string escapes, floats,
`#` line and `#/ … /#` block comments, and one-/two-character operators — with
the same lex errors (`unterminated string`, `unexpected character`, …) as the
C++ lexer.
