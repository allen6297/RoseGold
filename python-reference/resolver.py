#!/usr/bin/env python3
"""
Module resolver for RoseGold -- implements resolution.md.

Given an entry file, it reuses parser.py to read each module's `module`
declaration and imports, maps every import path to a file across the source
roots, verifies the declared name matches the file's location, detects import
cycles, and prints the resulting module graph.

Usage:
    python3 resolver.py <entry-file> [root ...]

If no roots are given, the entry file's directory is used as the sole root.
"""

import importlib.util
import os
import sys

# RoseGold source-file extension. Change this one constant (and rename the
# source files to match) to switch extensions.
SOURCE_EXT = ".rg"

# Load the sibling parser.py explicitly (avoids clashing with any stdlib
# module named "parser" on older Pythons).
_here = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("langparser", os.path.join(_here, "parser.py"))
lang = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lang)


def module_relpath(modpath):
    """graphics.geometry -> graphics/geometry.rg"""
    return os.path.join(*modpath.split(".")) + SOURCE_EXT


def find_module_file(modpath, roots):
    """First root whose R/<relpath> exists wins. Returns (file, root, tried)."""
    tried = []
    rel = module_relpath(modpath)
    for r in roots:
        cand = os.path.join(r, rel)
        tried.append(cand)
        if os.path.isfile(cand):
            return cand, r, tried
    return None, None, tried


def parse_module(path):
    """Return (declared_module_name, [Import nodes]) for a source file."""
    with open(path, "r") as f:
        src = f.read()
    toks = lang.lex(src)
    prog = lang.Parser(toks).parse_program()
    return prog["module"], prog["imports"]


def resolve(entry, roots):
    graph = {}       # modname -> {file, root, imports:[Import], import_paths:[str]}
    errors = []
    cycles = []
    visiting = []    # DFS stack of module names
    done = set()

    def visit(modname, file, root):
        if modname in done:
            return
        if modname in visiting:                       # back-edge => cycle
            cyc = visiting[visiting.index(modname):] + [modname]
            cycles.append(cyc)
            return
        visiting.append(modname)

        try:
            declared, imports = parse_module(file)
        except (lang.LexError, lang.ParseError) as e:
            errors.append(f"{file}: parse error: {e}")
            visiting.pop()
            done.add(modname)
            return

        if declared != modname:
            errors.append(
                f"{file}: declares 'module {declared}' but resolves as '{modname}'"
            )

        graph[modname] = {
            "file": file, "root": root,
            "imports": imports,
            "import_paths": [imp["path"] for imp in imports],
        }

        for imp in imports:
            target = imp["path"]
            tfile, troot, tried = find_module_file(target, roots)
            if tfile is None:
                errors.append(
                    f"{file}: cannot resolve 'import {target}' "
                    f"(searched: {', '.join(tried)})"
                )
            else:
                visit(target, tfile, troot)

        visiting.pop()
        done.add(modname)

    # Entry: read its declared name, then check it sits where a root implies.
    try:
        entry_mod, _ = parse_module(entry)
    except (lang.LexError, lang.ParseError) as e:
        return graph, [f"{entry}: parse error: {e}"], cycles

    entry_root = None
    for r in roots:
        if os.path.normpath(os.path.join(r, module_relpath(entry_mod))) == os.path.normpath(entry):
            entry_root = r
            break
    if entry_root is None:
        errors.append(
            f"{entry}: 'module {entry_mod}' does not match its location under any root {roots}"
        )
        entry_root = roots[0]

    visit(entry_mod, entry, entry_root)
    return graph, errors, cycles


def import_label(imp):
    if imp["names"]:
        return f"{imp['path']}.({', '.join(imp['names'])})"
    if imp["alias"]:
        return f"{imp['path']} as {imp['alias']}"
    if imp["pub"]:
        return f"pub {imp['path']}"
    return imp["path"]


def main():
    if len(sys.argv) < 2:
        print("usage: python3 resolver.py <entry-file> [root ...]")
        return 2
    entry = sys.argv[1]
    roots = sys.argv[2:] or [os.path.dirname(entry) or "."]

    graph, errors, cycles = resolve(entry, roots)

    print(f"entry : {entry}")
    print(f"roots : {roots}")
    print("\nmodule graph:")
    for m in sorted(graph):
        info = graph[m]
        print(f"  {m:22} -> {info['file']}")
        for imp in info["imports"]:
            print(f"      import {import_label(imp)}")

    if cycles:
        print("\ncycles:")
        for c in cycles:
            print("  " + " -> ".join(c))

    if errors:
        print("\nERRORS:")
        for e in errors:
            print("  " + e)
        return 1

    print(f"\nOK  resolved {len(graph)} modules, no errors")
    return 0


if __name__ == "__main__":
    sys.exit(main())
