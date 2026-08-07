# Module Resolution — RoseGold

How an `import a.b.c` becomes a file on disk, how names resolve after that,
and what happens with re-exports and cycles.

---

## 1. One file = one module

Every source file begins with a `module` declaration naming its dotted path:

```
module graphics.geometry
```

A file has exactly one module; a module is exactly one file.

## 2. Path ↔ file mapping

A module `a.b.c` lives at `a/b/c.rg` relative to a **source root**
(`.rg` is RoseGold's file extension, kept in the one constant `SOURCE_EXT`
in resolver.py).

The declared `module` name **must match** the file's location under the root
it was found in. A mismatch is an error. This keeps resolution deterministic
and reversible: given a path you know the file, and given a file you know the
path.

```
graphics.geometry   <->   <root>/graphics/geometry.rg
```

## 3. Source roots (the search path)

Roots are an **ordered** list; resolution tries each in order and the **first
match wins**:

1. Project source root(s)
2. Dependency roots
3. Standard-library root

To resolve `import a.b.c`: for each root `R`, test `R/a/b/c.rg`; the first
file that exists is the module. If none match, it's an error that lists every
path that was tried.

## 4. What resolution produces

A **module graph**: each module mapped to its file and to the modules it
imports, built transitively from the entry module.

The import *form* does **not** affect which file is loaded — `import math`,
`import math as m`, `import math.(sin, cos)`, and `pub import math` all
resolve the **same** file for the path `math`. The form only shapes the
importing module's symbol table (§5) and its re-export surface (§6).

## 5. Name resolution (after files are found)

Resolving an **unqualified** name inside a function, in order:

1. Local / block scope
2. Enclosing function parameters, then enclosing class members
3. Module-level declarations in the current file
4. Names pulled in unqualified by selective imports (`import math.(sin)`)
5. Otherwise: error

A **qualified** name (`math.pi`, or `m.pi` via an alias) skips straight to the
named module and looks the symbol up there.

**Visibility** is enforced at this layer:
- `pub` — visible to other modules
- `internal` — visible only within the same module (the default)
- `private` — visible only within the declaring class

Only `pub` items (and `pub import` re-exports) are reachable across a module
boundary.

## 6. Re-exports

`pub import shapes` makes `shapes` part of *this* module's public surface:
a module that imports this one can then reference `shapes` (and `shapes.X`)
as though it had imported `shapes` itself. Re-export follows `pub`
transitively, so a curated "facade" module can re-expose several internal
modules through one public path.

## 7. Cycles

The resolver builds the import graph and **detects cycles**.

- Cross-cycle *type / declaration* references are allowed — mutual references
  between modules are common and legal.
- If load-time execution is ever added (an `init:` block, or top-level code
  that runs on import), a cycle among modules with load-time side effects has
  no well-defined order and becomes an error. Until such execution exists,
  cycles are **reported but not fatal**.

## 8. Entry module

Compilation / execution starts from a single **entry module** — the file
handed to the compiler. Its imports are resolved transitively to form the
full graph. The entry file must itself sit at the location its `module`
declaration implies, under one of the roots.
