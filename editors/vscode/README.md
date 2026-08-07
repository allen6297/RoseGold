# RoseGold — VS Code extension

Language support for [RoseGold](../../README.md) `.rg` files.

## What it provides
- **Syntax highlighting** — keywords, types, functions, strings, comments (`#` and `#/ … /#`), numbers, operators.
- **Editor config** — `#` line / `#/ /#` block comments, `(`/`[` bracket matching & auto-close, and auto-indent after a `:` (the offside rule).
- **Language server** — the extension launches the native `rosegoldc --lsp` and talks to it over LSP, so all of the following come straight from the canonical C++ implementation (real inference, not pattern matching):
  - **Diagnostics** — parse and type errors as you type.
  - **Hover** — the type of the identifier under the cursor (e.g. `counts: Map<String, Int>`).
  - **Go-to-definition** (F12) — jump to the declaration of a local variable, parameter, or loop/catch binding, plus top-level funcs/classes/enums/globals and class fields/methods (across files); shadowing is resolved correctly.
  - **Completion** — members offered after `.` on a class instance or an imported module.
  - **Find all references** (⇧F12) — every use of the symbol under the cursor, plus its declaration, **across the whole workspace**.
  - **Rename** (F2) — safely rename a symbol everywhere it's used, across every `.rg` file in the workspace (grouped by definition, so same-named-but-different symbols aren't touched). Enum/variant names are excluded for now.
  - **Outline / breadcrumbs** (⌘⇧O) — document symbols: classes with their fields & methods, funcs, enums, traits, globals.
  - **Signature help** — as you type a call `foo(`, the signature and active parameter are shown.
  - **Semantic highlighting** — type-aware token colors (classes vs functions vs methods vs parameters vs variables), layered over the TextMate grammar.
  - **Document highlight** — all occurrences of the symbol under the cursor.
  - **Folding** and **inlay hints** — collapse blocks; inferred types shown after un-annotated `var`s (e.g. `var c = Counter(10)` → `: Counter`).

## Prerequisites
1. **Build the compiler** (it *is* the language server):
   ```bash
   clang++ -std=c++17 -O2 -o cpp/rosegoldc cpp/src/main.cpp
   ```
2. **Install the client dependency** (`vscode-languageclient`):
   ```bash
   cd editors/vscode && npm install
   ```

## Try it (Extension Development Host)
1. Do the two steps above.
2. Open this folder (`editors/vscode`) in VS Code.
3. Press **F5** → a second "Extension Development Host" window opens with the extension loaded.
4. Open any `.rg` file (e.g. `demo/lsp_demo.rg`): hover a variable, F12 a call, type `.` after an instance, and edit to see live diagnostics.

## Install locally
Copy or symlink this folder into your extensions directory (after `npm install`), then reload VS Code:
```bash
ln -s "$(pwd)" ~/.vscode/extensions/rosegold-0.2.0
```
(or package it with `npx vsce package` to produce a `.vsix` and `code --install-extension rosegold-0.2.0.vsix`.)

## Settings
- `rosegold.compilerPath` — path to the `rosegoldc` binary. If empty, the extension searches your workspace folders for `cpp/rosegoldc` or `rosegoldc`.
- `rosegold.trace.server` — `off` | `messages` | `verbose`; log the JSON-RPC traffic to the "RoseGold Language Server" output channel for debugging.

Highlighting and editor config work with no setup; the language-server features need the built `rosegoldc` and `npm install`.

## How it works
`rosegoldc --lsp` is a JSON-RPC-over-stdio server built into the compiler. It reuses the real lexer, parser, and type checker in-process: on each edit it re-checks the file (and its imports) and, with an occurrence index over the typed AST, answers hover / definition / completion. See [`cpp/src/lsp.hpp`](../../cpp/src/lsp.hpp).
