# RoseGold

A small, statically-typed programming language with significant indentation,
a module system, algebraic enums + pattern matching, classes, generics,
closures, and a small standard library. The **canonical implementation** is a
single, self-contained C++ compiler + bytecode VM (`cpp/rosegoldc`) — no runtime
dependency. The original Python implementation is kept as a reference in
`python-reference/`.

## Hello, RoseGold

```rosegold
module hello

func main():
    print("hello, RoseGold")
```

```bash
clang++ -std=c++17 -O2 -o cpp/rosegoldc cpp/src/main.cpp   # build once
./cpp/rosegoldc demo/prog.rg                               # then run any .rg
```

## Pipeline

```
source → tokens → AST → type check → bytecode → run
```

`cpp/src/` holds the compiler split by concern (`value` · `lexer` · `ast` ·
`parser` · `types` · `compiler` · `vm` headers + `main.cpp`): lexer (offside
rule) → recursive-descent parser (typed AST) → static type checker (generic
inference + visibility) → bytecode compiler → stack VM, with multi-file module
loading. The formal grammar is `grammar.ebnf`, the module spec `resolution.md`.

`python-reference/` holds the earlier tree-walking implementation
(`parser.py` · `resolver.py` · `checker.py` · `typecheck.py` · `interpreter.py`)
— retained as an oracle / spec, byte-identical in behavior.

## Language at a glance

- Blocks by `:` + indentation — no braces
- Comments: `#` line, `#/ ... /#` block
- Visibility: `pub | internal | private` (default `internal`)
- `var` / `const` (const must be initialized), with type inference
- `func name(a: Int) -> Bool:` ; single-expression closures `func(x: Int) => x + 1`
- `class` with `extends` (one base) + `uses` (traits), `init(...)` constructors
- `trait` — abstract method signatures + optional **default bodies** (inherited by conformers, overridable); may use `Self` for the conforming type; a class that `uses` a trait must implement the abstract methods (checked), and a trait is a usable type (dynamic dispatch)
- `extend Type uses Trait` — **retroactive conformance**: make an existing type (including primitives like `Int`/`String`, `List`/`Map`, or a class) conform to a trait after the fact, so it satisfies `<T: Trait>` bounds; methods dispatch dynamically (`3.lessThan(9)`)
- `enum` + `match` with variant destructuring
- Generics with real inference: `Box(41)` infers `Box<Int>`; type args flow through methods
- Trait-bounded generics `<T: Drawable>` — the bound is enforced at each call, and the body may only use the bound's members
- `Int` and `Float` are distinct (no implicit coercion)
- Control flow: `if/elif/else`, `while`, `for..in`, `break`, `continue`
- Lists with indexing `a[i]` (get and assign)
- Error handling: `raise <expr>` / `try: ... catch e: ...`
- Load-time `init:` block per module (runs once, dependencies first); programs start at `main()`
- Modules: `module a.b`, `import m`, `import m as x`, `import m.(a, b)`, `pub import m`
- File extension: `.rg`

## Tools

```bash
./cpp/rosegoldc          <file.rg>   # type-check, compile, and run
./cpp/rosegoldc --check  <file.rg>   # type-check only (front-end gate; no execution)
```

The Python reference (in `python-reference/`) exposes each stage separately if
you want to inspect it: `python3 python-reference/parser.py <file>` (AST),
`.../typecheck.py` (types), `.../interpreter.py` (run), etc.

Examples live in `demo/` (`prog.rg` runs; `typeerrors.rg` has deliberate type
errors). `spec-indentation.rg` is the canonical syntax sample.

## Native runtime (C++)

A native **bytecode VM** lives in `cpp/` (the "make it real" runtime,
GDScript-style: build once, run scripts with no rebuild). The Python toolchain
stays the reference implementation.

```bash
clang++ -std=c++17 -O2 -o cpp/rosegoldc cpp/src/main.cpp
./cpp/rosegoldc demo/prog.rg      # the flagship: closures + modules + classes + enums
```

The VM now has **runtime parity** with the Python reference: functions +
recursion, global `var`/`const`, arithmetic/logic, `if`/`elif`/`else`, `while`,
`for..in`, `break`/`continue`, lists + `a[i]`, `len`/`range`/`print`,
`raise`/`try`/`catch`, classes (fields/`init`/methods/`self`), enums + `match`,
**closures** (first-class functions + capture), and **modules** (multi-file
loading, `import` / `as` / selective, cross-module calls). The flagship
`demo/prog.rg` and every example under `cpp/examples/` run **byte-identical**
under this VM and the Python interpreter. It also includes a **static type
checker** (typed AST + generic inference + visibility) that gates execution, so
`rosegoldc` is a complete, self-contained compiler + runtime with no Python
dependency — and it reports the same type errors as the Python front-end
(verified on `demo/typeerrors.rg`).

## Editor support

`editors/vscode/` is a VS Code extension: syntax highlighting, comment/bracket/
indent config, and **live diagnostics** that run `typecheck.py` on save and show
real name/visibility/type errors inline. See its README to load it.

## Self-hosting

Two fragments of RoseGold's own front-end, written in RoseGold and run natively
(output byte-identical to the Python engine):
- `cpp/examples/bootstrap.rg` — a lexer that streams tokens from an embedded string.
- `cpp/examples/bootstrap2.rg` — a lexer that **reads a real `.rg` file** and builds a **token list**, using the standard library.

```bash
./cpp/rosegoldc cpp/examples/bootstrap2.rg   # tokenizes cpp/examples/fib.rg -> 94 tokens
```

### Standard library

Foundational builtins live in both runtimes *and* both type checkers, so they
stay in lockstep:

- **collections** — `push` / `pop` (growable lists); `Map<K, V>` via `map` / `set` / `get` / `has` / `keys` / `remove`
- **strings** — `str`, `ord`, `chr`, `substr`, `split`, `int`
- **file I/O** — `readFile`, `writeFile`

`cpp/examples/stdlib.rg` and `cpp/examples/maps.rg` exercise them. This clears
the blockers that stood in the way of self-hosting — **file I/O**, **growable
lists**, and a **`Map` type** for symbol tables. What remains is "just" the
(large) work of porting the compiler itself to RoseGold on top of the stdlib.

## Status

- ✅ lexer, parser, module resolver, name + visibility checker, type checker, interpreter
- ✅ generic type inference (unification-based)
- ✅ traits — `trait` decls with `Self`-typed requirements and default method bodies, `uses`-conformance checking, trait-bounded generics `<T: Trait>`, and retroactive `extend Type uses Trait` conformance incl. primitives (C++ canonical)
- ✅ `break` / `continue`, list indexing `a[i]`
- ✅ error handling (`raise` / `try` / `catch`)
- ✅ `init:` load-time execution model + `main()` entry
- ✅ native compiler + runtime — self-contained C++ (`cpp/`): typed parser + static type checker + bytecode VM, at **full parity** with Python (byte-identical output; identical type errors)
- ✅ editor support — VS Code extension (`editors/vscode/`): highlighting, config, and a native **language server** (`rosegoldc --lsp`): diagnostics, hover, go-to-definition (incl. locals/params), completion, document symbols/outline, signature help, and workspace-wide find-references + rename
- ✅ standard library — growable lists, `Map<K,V>`, string ops, file I/O
- ✅ C++ split into modular `cpp/src/` (one header per stage)
- ✅ self-hosting fragments — a RoseGold lexer written in RoseGold (streams tokens; reads a file + builds a token list)
- ⏳ future: port the full compiler to RoseGold (true self-hosting); trait default-method bodies; primitives implementing built-in traits
