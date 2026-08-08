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
./cpp/rosegoldc examples/tour.rg                               # a guided tour of the language
./cpp/rosegoldc examples/prog.rg                               # then run any .rg
```

`examples/tour.rg` is the one-file tour — enums + match, generics, traits with
default methods, operator overloading, closures, collections, error handling,
coroutines, signals, and vectors, each section printing its results.

## Pipeline

```
source → tokens → AST → type check → bytecode → run
```

`cpp/src/` holds the compiler split by concern (`value` · `lexer` · `ast` ·
`parser` · `types` · `compiler` · `vm` headers + `main.cpp`): lexer (offside
rule) → recursive-descent parser (typed AST) → static type checker (generic
inference + visibility) → bytecode compiler → stack VM, with multi-file module
loading. The formal grammar is `docs/grammar.ebnf`, the module spec `docs/resolution.md`.

`python-reference/` holds the earlier tree-walking implementation
(`parser.py` · `resolver.py` · `checker.py` · `typecheck.py` · `interpreter.py`)
— retained as an oracle / spec, byte-identical in behavior.

## Repository layout

```
Makefile             build & test entry points (make · make test · make embed)
cpp/
  src/               the compiler + VM (one header per stage: value · lexer · ast ·
                     parser · types · compiler · vm) + main.cpp, plus ffi.hpp
                     (native FFI), runtime.hpp (embeddable runtime), lsp.hpp
  embed/             C++ host-embedding demos (engine/game/hotreload) + their scripts
  test/              run_tests.py (golden test harness) · golden/ (committed
                     snapshots) · LSP protocol test drivers (Python)
examples/            .rg programs: feature demos, the flagship prog.rg, multi-file
                     module fixtures (core/, graphics/), self-hosting fragments
editors/
  vscode/            VS Code extension (LSP client)
  jetbrains/         IntelliJ / JetBrains plugin (LSP via LSP4IJ)
python-reference/    the original tree-walking implementation, kept as an oracle
docs/                grammar.ebnf (formal grammar) · resolution.md (module spec)
.github/workflows/   ci.yml — builds + runs the suite on every push
```

## Language at a glance

- Blocks by `:` + indentation — no braces
- Comments: `#` line comment. **Doc comments** (Javadoc-style): `## ...` line and `#/ ... /#` block — attach to the next declaration, support `@param`/`@return`, render via `rosegoldc --doc`, and show on hover in the editor
- Visibility: `pub | internal | private` (default `internal`)
- `var` / `const` (const must be initialized), with type inference
- `func name(a: Int) -> Bool:` ; single-expression closures `func(x: Int) => x + 1`
- `class` with `extends` (one base) + `uses` (traits), `init(...)` constructors
- `trait` — abstract method signatures + optional **default bodies** (inherited by conformers, overridable); may use `Self` for the conforming type; a class that `uses` a trait must implement the abstract methods (checked), and a trait is a usable type (dynamic dispatch)
- `extend Type uses Trait` — **retroactive conformance**: make an existing type (including primitives like `Int`/`String`, `List`/`Map`, or a class) conform to a trait after the fact, so it satisfies `<T: Trait>` bounds; methods dispatch dynamically (`3.lessThan(9)`)
- **operator overloading** — `+ - * / %` dispatch to `add`/`sub`/`mul`/`div`/`mod`, `< <= > >=` to `compareTo`, and `== !=` to `equals` on any user type (class method or via `extend`); also works on trait-bounded generics (`a < b` where `T: Ordered`)
- `enum` + `match` with variant destructuring
- Generics with real inference: `Box(41)` infers `Box<Int>`; type args flow through methods
- Trait-bounded generics `<T: Drawable>` — the bound is enforced at each call, and the body may only use the bound's members
- `Int` and `Float` are distinct (no implicit coercion)
- Control flow: `if/elif/else`, `while`, `for..in`, `break`, `continue`
- Lists with indexing `a[i]` (get and assign)
- Error handling: `raise <expr>` / `try: ... catch e: ...`
- Coroutines: `yield <expr>` inside a function; `coroutine(fn, args...)` / `resume(coro[, arg])` / `done(coro)` — pause and continue across frames
- **Signals**: `signal onHit(dmg: Int)` declares a typed event on a class; fire with `sig.emit(args)` and subscribe with `sig.connect(handler)` — both type-checked. A handler may take fewer params than the signal emits (extras dropped, Godot-style)
- Game/math stdlib: `sqrt sin cos tan atan2 floor ceil round pow abs min max lerp clamp random randint srandom`
- Load-time `init:` block per module (runs once, dependencies first); programs start at `main()`
- Modules: `module a.b`, `import m`, `import m as x`, `import m.(a, b)`, `pub import m`
- **Native FFI**: `extern func host_add(a: Int, b: Int) -> Int` declares a function the C++ host provides; calls are type-checked against the declaration and bound by name to the host's registry at runtime (Rust-`extern` style)
- File extension: `.rg`

## Tools

```bash
./cpp/rosegoldc          <file.rg>   # type-check, compile, and run
./cpp/rosegoldc --check  <file.rg>   # type-check only (front-end gate; no execution)
./cpp/rosegoldc --format <file.rg>   # print the file in canonical style (to stdout)
./cpp/rosegoldc --lsp                # run the language server (JSON-RPC over stdio)
./cpp/rosegoldc --dap                # run the debug adapter (DAP over stdio)
./cpp/rosegoldc --tokens <file.rg>   # dump the lexer's token stream (self-hosting ground truth)
./cpp/rosegoldc --doc    <file.rg>   # render a Markdown doc page from doc comments (Javadoc-style)
```

The Python reference (in `python-reference/`) exposes each stage separately if
you want to inspect it: `python3 python-reference/parser.py <file>` (AST),
`.../typecheck.py` (types), `.../interpreter.py` (run), etc.

## Building & testing

```bash
make                 # build cpp/rosegoldc
make test            # build + run the full suite (see below)
make run FILE=examples/prog.rg
make embed           # build the C++ host-embedding demos
make update-golden   # regenerate golden snapshots after an intended change
```

`make test` (also run in CI on every push) is a single self-checking harness
(`cpp/test/run_tests.py`) that:

- **golden-snapshots** every runnable `examples/*.rg` — its stdout is diffed
  against a committed snapshot under `cpp/test/golden/`, so any change in
  behavior is caught, not just crashes;
- asserts the **error fixtures** (`typeerrors`, `trait_errors`, `broken`,
  `client`, `consumer`) are still rejected by the front end, snapshotting their
  diagnostics;
- cross-checks **C++/Python parity** on every example the Python oracle supports;
- snapshots the **LSP drivers** and the **embedding demos**;
- runs a **builtin-consistency guard** that verifies each builtin is wired
  identically across all three places it must appear — the compiler's id map,
  the type-checker's signatures, and the VM's dispatch — so the three can't
  drift out of sync.

After a deliberate behavior change, run `make update-golden` and review the diff
before committing.

All `.rg` programs live in `examples/`: the runnable flagship `prog.rg`, feature
showcases (traits, coroutines, `game.rg`, …), multi-file module-system fixtures
(`app.rg`/`util.rg`, `core/`, `graphics/`), and `typeerrors.rg` (deliberate type errors).

## Native runtime (C++)

A native **bytecode VM** lives in `cpp/` (the "make it real" runtime,
GDScript-style: build once, run scripts with no rebuild). The Python toolchain
stays the reference implementation.

```bash
clang++ -std=c++17 -O2 -o cpp/rosegoldc cpp/src/main.cpp
./cpp/rosegoldc examples/prog.rg      # the flagship: closures + modules + classes + enums
```

The VM now has **runtime parity** with the Python reference: functions +
recursion, global `var`/`const`, arithmetic/logic, `if`/`elif`/`else`, `while`,
`for..in`, `break`/`continue`, lists + `a[i]`, `len`/`range`/`print`,
`raise`/`try`/`catch`, classes (fields/`init`/methods/`self`), enums + `match`,
**closures** (first-class functions + capture), and **modules** (multi-file
loading, `import` / `as` / selective, cross-module calls). The flagship
`examples/prog.rg` and every example under `examples/` run **byte-identical**
under this VM and the Python interpreter. It also includes a **static type
checker** (typed AST + generic inference + visibility) that gates execution, so
`rosegoldc` is a complete, self-contained compiler + runtime with no Python
dependency — and it reports the same type errors as the Python front-end
(verified on `examples/typeerrors.rg`).

## Editor support

`editors/vscode/` is a VS Code extension: syntax highlighting, comment/bracket/
indent config, and **live diagnostics** that run `typecheck.py` on save and show
real name/visibility/type errors inline. See its README to load it.

## Self-hosting

Fragments of RoseGold's own front-end, written in RoseGold and run natively
(output byte-identical to the Python engine):
- `examples/bootstrap.rg` — a lexer that streams tokens from an embedded string.
- `examples/bootstrap2.rg` — a lexer that **reads a real `.rg` file** and builds a **token list**, using the standard library.
- `examples/rglexer.rg` — a **faithful RoseGold lexer** (a real port of the C++ lexer, not a toy): it strips line/block comments, implements the **offside rule** (INDENT/DEDENT/NEWLINE via an indentation stack, with `(`/`[` suppressing newlines), and handles two-char operators, string escapes, and floats. Its token stream is **byte-identical to `rosegoldc --tokens`** (the test harness diffs the two) — the lexer stage of RoseGold, self-hosted.
- `examples/calc.rg` — a full **tokenize → recursive-descent parse → evaluate** pipeline for arithmetic, building a **recursive enum AST** and walking it with `match`. The "parser + AST + eval" milestone — the shape of the real front-end.
- `examples/mini.rg` — the calc pipeline plus **statements and variables**, using `Map<String, Int>` as the environment.
- `examples/interp.rg` — a **Turing-complete** little imperative language (`if` / `while` / comparisons / reassignment, `end`-delimited blocks) interpreted in RoseGold; runs real algorithms (factorial, summation) via nested-block recursion over the AST.

```bash
./cpp/rosegoldc examples/bootstrap2.rg   # tokenizes examples/fib.rg -> 94 tokens
```

### Standard library

Foundational builtins live in both runtimes *and* both type checkers, so they
stay in lockstep:

- **collections** — `push` / `pop` (growable lists); `Map<K, V>` via `map` / `set` / `get` / `has` / `keys` / `remove`
- **strings** — `str`, `ord`, `chr`, `substr`, `split`, `int`
- **file I/O** — `readFile`, `writeFile`

`examples/stdlib.rg` and `examples/maps.rg` exercise them. This clears
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
- ✅ editor support — a native **language server** (`rosegoldc --lsp`): diagnostics, hover, go-to-definition (incl. locals/params), completion, document symbols/outline, signature help, workspace-wide find-references + rename, semantic tokens, document highlight, folding, inlay hints, **formatting** (canonical offside-rule whitespace; also `rosegoldc --format`), and **code actions** (did-you-mean quick fixes for undefined names/types; "add inferred type annotation" refactor). Clients: **VS Code** extension (`editors/vscode/`) and a **JetBrains** plugin (`editors/jetbrains/`, via LSP4IJ) — both drive the same server
- ✅ debugger — a native **Debug Adapter** (`rosegoldc --dap`, DAP over stdio): line breakpoints, step in/over/out, call stack, and local/global variable inspection with **drill-in** (expand objects into fields, lists/maps into elements, enum variants into payloads) + expression evaluation, driven by a debug hook in the VM (per-instruction line table + per-function local-name table). VS Code launches it via a `rosegold` debug type (F5 on a `.rg` file)
- ✅ standard library — growable lists, `Map<K,V>`, string ops, file I/O, a math/game stdlib (`sqrt`/`sin`/`lerp`/`clamp`/`random`/…)
- ✅ coroutines — `yield` + `coroutine`/`resume`/`done` for frame-spanning logic (game scripting); `examples/coroutine.rg`, `game.rg`
- ✅ embeddable runtime + native FFI — a C++ host registers native functions scripts can call, loads a script, and ticks its functions each frame with persistent state (`cpp/src/runtime.hpp`; demo engine `cpp/embed/engine.cpp` driving `cpp/embed/behavior.rg`). Scripts declare the host's functions with **`extern func`** — type-checked standalone, bound by name at runtime (`cpp/embed/externdemo.cpp` + `externdemo.rg`)
- ✅ game-engine embedding kit — opaque **host handles** (natives hand scripts real engine objects), a **component model** (`newInstance`/`callMethod` — instantiate a script class per entity and tick it; `cpp/embed/game.cpp`), built-in **value-type vectors** (`vec2`/`vec3`, `.x/.y/.z`, `+ - *`, `dot`/`vlen`/`norm`), **hot reload** (`Runtime.reload()` keeps state; `cpp/embed/hotreload.cpp`), and **signals** (a first-class language feature — see below; `examples/signals.rg`)
- ✅ C++ split into modular `cpp/src/` (one header per stage)
- ✅ self-hosting fragments — RoseGold front-end pieces written in RoseGold: a **faithful lexer** (`rglexer.rg`, offside rule + comments + escapes, **token-for-token identical to the C++ lexer**), plus a lexer that reads a file into a token list and a full tokenize→parse→eval pipeline over a recursive enum AST
- ✅ documentation comments — Javadoc-style `##` line / `#/ ... /#` block docs attach to the following declaration; `rosegoldc --doc` renders a Markdown page (with `@param`/`@return`), and the language server shows them on hover; `examples/documented.rg`
- ✅ test harness + CI — `make test` golden-snapshots every example, asserts the error fixtures fail, cross-checks C++/Python parity, snapshots the LSP + embedding demos, and guards the builtin tables against drift; runs on every push via GitHub Actions
- ⏳ future: port the full compiler to RoseGold (true self-hosting); trait default-method bodies; primitives implementing built-in traits
