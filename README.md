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
  embed/             C++ host-embedding demos (engine/game/hotreload/externdemo) + their scripts
  test/              run_tests.py (golden test harness) · golden/ (committed
                     snapshots) · LSP/DAP protocol test drivers (Python)
examples/            .rg programs, organized into subdirectories:
  (top level)        the guided tour.rg, flagship prog.rg, shared util.rg
  features/          one feature per file (traits, generics, coroutines, signals, …)
  selfhost/          RoseGold's own lexer/parser/checker/compiler + their fixtures
  modules/           multi-file module-system fixtures (with core/, graphics/)
  errors/            deliberate-error fixtures the front end must reject
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
./cpp/rosegoldc --ast    <file.rg>   # dump the parsed AST as S-expressions (self-hosting ground truth)
./cpp/rosegoldc --bytecode <file.rg> # disassemble the compiled bytecode per function (self-hosting ground truth)
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

- **golden-snapshots** every runnable example under `examples/` (searched
  recursively) — its stdout is diffed against a committed snapshot under
  `cpp/test/golden/`, so any change in behavior is caught, not just crashes;
- asserts the **error fixtures** (`errors/typeerrors`, `errors/trait_errors`,
  `modules/broken`, `modules/client`, `modules/consumer`, `selfhost/check_sample`)
  are still rejected by the front end, snapshotting their diagnostics;
- cross-checks **C++/Python parity** on every example the Python oracle supports;
- snapshots the **LSP drivers** and the **embedding demos**;
- runs a **builtin-consistency guard** that verifies each builtin is wired
  identically across all three places it must appear — the compiler's id map,
  the type-checker's signatures, and the VM's dispatch — so the three can't
  drift out of sync.

After a deliberate behavior change, run `make update-golden` and review the diff
before committing.

All `.rg` programs live in `examples/`: the runnable flagship `prog.rg` and
guided `tour.rg` at the top, feature showcases under `features/` (traits,
coroutines, `game.rg`, …), the self-hosted pipeline under `selfhost/`, multi-file
module-system fixtures under `modules/` (`app.rg`, `core/`, `graphics/`), and
deliberate-error fixtures under `errors/` (`typeerrors.rg`, …).

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
(verified on `examples/errors/typeerrors.rg`).

## Editor support

`editors/vscode/` is a VS Code extension: syntax highlighting, comment/bracket/
indent config, and **live diagnostics** that run `typecheck.py` on save and show
real name/visibility/type errors inline. See its README to load it.

## Self-hosting

RoseGold's own front-and-back-end pipeline, **written in RoseGold** and run
natively — each stage proven **byte-identical to the canonical C++ stage** it
mirrors (the test harness diffs the two). They live in `examples/selfhost/`:

- `rglexer.rg` — a **faithful RoseGold lexer** (a real port of the C++ lexer, not a toy): it strips line/block comments, implements the **offside rule** (INDENT/DEDENT/NEWLINE via an indentation stack, with `(`/`[` suppressing newlines), and handles two-char operators, string escapes, and floats. Its token stream is **byte-identical to `rosegoldc --tokens`** — the lexer stage of RoseGold, self-hosted.
- `rgparser.rg` — a **RoseGold parser written in RoseGold** (rung 2): it lexes (via the ported offside lexer) and recursive-descent parses a core subset — module + globals + functions; `var`/`assign`/`return`/`if`-`elif`-`else`/`while`/`for`/`expr`/`break`/`continue`; and expressions across the full precedence ladder with calls, indexing, member access, and list literals — emitting an AST **byte-identical to `rosegoldc --ast`** (`selfhost/parse_sample.rg` is the fixture).
- `rgchecker.rg` — a **RoseGold type checker written in RoseGold** (rung 3): lexes + parses a functions-only subset into an AST, then does a two-pass check (collect signatures, then check bodies) reporting **name-resolution and call errors** (undefined name, call arity, argument-type mismatch) **byte-identical to `rosegoldc --check`** (`selfhost/check_sample.rg` is the fixture).
- `rgcompiler.rg` — a **RoseGold bytecode compiler written in RoseGold** (rung 4, the last): lexes + parses a functions-only subset, then compiles each function to stack-VM bytecode — replicating local-slot assignment, the monotonic constant pool, operator/builtin/`CALL` emission, and if/while jump patching — producing a disassembly **byte-identical to `rosegoldc --bytecode`** (`selfhost/compile_sample.rg` is the fixture).

**The entire pipeline — lex → parse → check → compile — now exists in RoseGold, each stage proven identical to the canonical C++ implementation.**

```bash
./cpp/rosegoldc examples/selfhost/rglexer.rg     # tokenizes examples/prog.rg; matches `--tokens` byte-for-byte
```

### Standard library

Foundational builtins live in both runtimes *and* both type checkers, so they
stay in lockstep:

- **collections** — `push` / `pop` (growable lists); `Map<K, V>` via `map` / `set` / `get` / `has` / `keys` / `remove`
- **strings** — `str`, `ord`, `chr`, `substr`, `split`, `int`
- **file I/O** — `readFile`, `writeFile`

`examples/features/stdlib.rg` and `examples/features/maps.rg` exercise them. This clears
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
- ✅ coroutines — `yield` + `coroutine`/`resume`/`done` for frame-spanning logic (game scripting); `examples/features/coroutine.rg`, `features/game.rg`
- ✅ embeddable runtime + native FFI — a C++ host registers native functions scripts can call, loads a script, and ticks its functions each frame with persistent state (`cpp/src/runtime.hpp`; demo engine `cpp/embed/engine.cpp` driving `cpp/embed/behavior.rg`). Scripts declare the host's functions with **`extern func`** — type-checked standalone, bound by name at runtime (`cpp/embed/externdemo.cpp` + `externdemo.rg`)
- ✅ game-engine embedding kit — opaque **host handles** (natives hand scripts real engine objects), a **component model** (`newInstance`/`callMethod` — instantiate a script class per entity and tick it; `cpp/embed/game.cpp`), built-in **value-type vectors** (`vec2`/`vec3`, `.x/.y/.z`, `+ - *`, `dot`/`vlen`/`norm`), **hot reload** (`Runtime.reload()` keeps state; `cpp/embed/hotreload.cpp`), and **signals** (a first-class language feature — see below; `examples/features/signals.rg`)
- ✅ C++ split into modular `cpp/src/` (one header per stage)
- ✅ self-hosting — the **whole pipeline written in RoseGold**, each stage **byte-identical to the canonical C++ stage**: **lexer** (`rglexer.rg` vs `--tokens`), **parser** (`rgparser.rg` vs `--ast`), **type checker** (`rgchecker.rg` vs `--check`), and **bytecode compiler** (`rgcompiler.rg` vs `--bytecode`) — lex → parse → check → compile, all self-hosted (over a functions-focused subset) and living in `examples/selfhost/`
- ✅ documentation comments — Javadoc-style `##` line / `#/ ... /#` block docs attach to the following declaration; `rosegoldc --doc` renders a Markdown page (with `@param`/`@return`), and the language server shows them on hover; `examples/features/documented.rg`
- ✅ test harness + CI — `make test` golden-snapshots every example, asserts the error fixtures fail, cross-checks C++/Python parity, snapshots the LSP + embedding demos, and guards the builtin tables against drift; runs on every push via GitHub Actions
- ⏳ future: port the full compiler to RoseGold (true self-hosting); trait default-method bodies; primitives implementing built-in traits
