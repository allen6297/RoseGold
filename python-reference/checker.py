#!/usr/bin/env python3
"""
Semantic checker for RoseGold -- implements resolution.md sections 5 (name resolution)
and 6 (re-exports), standalone.

Given an entry file, it:
  1. resolves the module graph (via resolver.py),
  2. builds a symbol table for every module (with visibility),
  3. computes each module's exported surface, including 'pub import' re-exports,
  4. walks every function / method / field-initializer and resolves each name,
     enforcing pub / internal visibility across module boundaries.

Usage:
    python3 checker.py <entry-file> [root ...]

Not enforced yet (needs a type checker, since it requires knowing an
expression's type): 'private' member access through an instance, and member
existence on arbitrary receivers. Module-level pub/internal IS enforced.
"""

import importlib.util
import os
import sys


def _load(modname, filename):
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location(modname, os.path.join(here, filename))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


resolver = _load("langresolver", "resolver.py")
lang = resolver.lang                     # the parser module

BUILTINS = {"print", "len", "range", "push", "pop", "str", "ord", "chr",
            "substr", "split", "int", "readFile", "writeFile"}


def vis_of(mods):
    return (mods or {}).get("vis") or "internal"


def class_member_table(class_node):
    table = {}
    for mem in class_node["members"]:
        k = mem["kind"]
        if k == "Field":
            table[mem["name"]] = {"kind": mem["binding"], "vis": vis_of(mem["mods"])}
        elif k == "Func":
            table[mem["sig"]["name"]] = {"kind": "func", "vis": vis_of(mem["mods"])}
        elif k == "Ctor":
            table["init"] = {"kind": "ctor", "vis": vis_of(mem["mods"])}
        elif k in ("Class", "Enum"):
            table[mem["name"]] = {"kind": k.lower(), "vis": vis_of(mem["mods"])}
    return table


def build_symbols(prog):
    """Top-level name -> {kind, vis, members}."""
    table = {}
    for d in prog["decls"]:
        k = d["kind"]
        if k == "Class":
            table[d["name"]] = {"kind": "class", "vis": vis_of(d["mods"]),
                                "members": class_member_table(d)}
        elif k == "Trait":
            members = {m["sig"]["name"]: {"kind": "func", "vis": "pub"} for m in d["members"]}
            table[d["name"]] = {"kind": "trait", "vis": vis_of(d["mods"]), "members": members}
        elif k == "Enum":
            evis = vis_of(d["mods"])
            members = {v["name"]: {"kind": "variant", "vis": "pub"} for v in d["variants"]}
            table[d["name"]] = {"kind": "enum", "vis": evis, "members": members}
            for v in d["variants"]:      # variant constructors are usable as bare names
                table[v["name"]] = {"kind": "variant", "vis": evis, "members": {}}
        elif k == "Func":
            table[d["sig"]["name"]] = {"kind": "func", "vis": vis_of(d["mods"]), "members": {}}
        elif k == "Field":
            table[d["name"]] = {"kind": d["binding"], "vis": vis_of(d["mods"]), "members": {}}
    return table


class Checker:
    def __init__(self, graph):
        self.graph = graph
        self.modules = {}     # module name -> Program AST
        self.symbols = {}     # module name -> symbol table
        self._exports = {}    # module name -> exported surface (cached)
        self.errors = []      # (module, line, message)

    def load(self):
        for m, info in self.graph.items():
            with open(info["file"], "r") as f:
                toks = lang.lex(f.read())
            self.modules[m] = lang.Parser(toks).parse_program()
            self.symbols[m] = build_symbols(self.modules[m])

    def exports(self, m, seen=None):
        """A module's public surface: own pub symbols + 'pub import' re-exports."""
        if m in self._exports:
            return self._exports[m]
        seen = seen or set()
        if m in seen:
            return {}                     # re-export cycle guard
        seen.add(m)
        result = {name: sym for name, sym in self.symbols.get(m, {}).items()
                  if sym["vis"] == "pub"}
        for imp in self.modules.get(m, {"imports": []})["imports"]:
            if not imp["pub"]:
                continue
            target = imp["path"]
            if target not in self.symbols:
                continue
            tex = self.exports(target, seen)
            if imp["names"]:
                for nm in imp["names"]:
                    if nm in tex:
                        result[nm] = tex[nm]
            else:
                result.update(tex)
        self._exports[m] = result
        return result

    def err(self, module, line, msg):
        self.errors.append((module, line, msg))

    # -- driver --
    def check(self):
        for m in self.graph:
            self.check_module(m)
        self.errors.sort(key=lambda e: (e[0], e[1] or 0))

    def check_module(self, m):
        prog = self.modules[m]
        qual, uq = {}, {}                 # qualifier->module ; unqualified name->(module,name)
        for imp in prog["imports"]:
            target = imp["path"]
            if target not in self.symbols:
                continue                  # unresolved: resolver already reported it
            if imp["names"]:
                tex = self.exports(target)
                for nm in imp["names"]:
                    if nm in tex:
                        uq[nm] = (target, nm)
                    else:
                        self.err(m, None, f"selective import: '{nm}' is not exported by '{target}'")
            elif imp["alias"]:
                qual[imp["alias"]] = target
            else:
                qual[target.split(".")[-1]] = target

        ctx = {"m": m, "qual": qual, "uq": uq,
               "mods": set(self.symbols[m].keys()), "class": None}
        for d in prog["decls"]:
            self.walk_decl(d, ctx)

    def walk_decl(self, d, ctx):
        k = d["kind"]
        if k == "Field":
            if d.get("init"):
                self.rexpr(d["init"], [set()], ctx)
        elif k == "Func":
            scope = {p["name"] for p in d["sig"]["params"]}
            self.rblock(d["body"]["stmts"], [scope], ctx)
        elif k == "Class":
            cctx = dict(ctx, **{"class": d})
            for mem in d["members"]:
                mk = mem["kind"]
                if mk == "Field" and mem.get("init"):
                    self.rexpr(mem["init"], [set()], cctx)
                elif mk == "Func":
                    scope = {p["name"] for p in mem["sig"]["params"]}
                    self.rblock(mem["body"]["stmts"], [scope], cctx)
                elif mk == "Ctor":
                    scope = {p["name"] for p in mem["params"]}
                    self.rblock(mem["body"]["stmts"], [scope], cctx)
                elif mk in ("Class", "Enum"):
                    self.walk_decl(mem, cctx)
        elif k == "Trait":
            for mem in d["members"]:
                if mem["body"]:
                    scope = {p["name"] for p in mem["sig"]["params"]}
                    self.rblock(mem["body"]["stmts"], [scope], ctx)
        elif k == "ModuleInit":
            self.rblock(d["body"]["stmts"], [set()], ctx)
        # Enum: nothing to resolve

    # -- statements --
    def rblock(self, stmts, scopes, ctx):
        local = scopes + [set()]
        for s in stmts:
            self.rstmt(s, local, ctx)

    def rstmt(self, s, scopes, ctx):
        k = s["kind"]
        if k in ("VarStmt", "ConstStmt"):
            if s.get("init"):
                self.rexpr(s["init"], scopes, ctx)
            scopes[-1].add(s["name"])
        elif k == "Assign":
            self.rexpr(s["target"], scopes, ctx)
            self.rexpr(s["value"], scopes, ctx)
        elif k == "ExprStmt":
            self.rexpr(s["expr"], scopes, ctx)
        elif k == "Return":
            if s.get("value"):
                self.rexpr(s["value"], scopes, ctx)
        elif k == "If":
            self.rexpr(s["cond"], scopes, ctx)
            self.rblock(s["then"]["stmts"], scopes, ctx)
            for c, b in s["elifs"]:
                self.rexpr(c, scopes, ctx)
                self.rblock(b["stmts"], scopes, ctx)
            if s["els"]:
                self.rblock(s["els"]["stmts"], scopes, ctx)
        elif k == "While":
            self.rexpr(s["cond"], scopes, ctx)
            self.rblock(s["body"]["stmts"], scopes, ctx)
        elif k == "For":
            self.rexpr(s["iter"], scopes, ctx)
            self.rblock(s["body"]["stmts"], scopes + [{s["var"]}], ctx)
        elif k == "Match":
            self.rmatch(s, scopes, ctx)
        elif k == "Raise":
            self.rexpr(s["value"], scopes, ctx)
        elif k == "Try":
            self.rblock(s["body"]["stmts"], scopes, ctx)
            self.rblock(s["handler"]["stmts"], scopes + [{s["name"]}], ctx)
        # Pass / Break / Continue: nothing to resolve

    def rmatch(self, m, scopes, ctx):
        self.rexpr(m["subject"], scopes, ctx)
        for arm in m["arms"]:
            binds = set()
            for p in arm["patterns"]:
                if p["kind"] == "VariantPattern":
                    binds.update(p["bindings"])
            body = arm["body"]
            if body["kind"] == "Block":
                self.rblock(body["stmts"], scopes + [binds], ctx)
            else:
                self.rexpr(body, scopes + [binds], ctx)

    # -- expressions --
    def rexpr(self, e, scopes, ctx):
        k = e["kind"]
        if k == "Lit":
            return
        if k == "Ident":
            name = e["name"]
            if name == "self" or name in BUILTINS:
                return
            if any(name in s for s in scopes):
                return
            if name in ctx["mods"] or name in ctx["uq"] or name in ctx["qual"]:
                return
            self.err(ctx["m"], e.get("line"), f"unknown name '{name}'")
        elif k == "Member":
            obj, field = e["obj"], e["field"]
            if obj["kind"] == "Ident" and obj["name"] in ctx["qual"]:
                target = ctx["qual"][obj["name"]]
                tex = self.exports(target)
                if field in tex:
                    return
                if field in self.symbols.get(target, {}):
                    v = self.symbols[target][field]["vis"]
                    self.err(ctx["m"], e.get("line"),
                             f"'{field}' is {v} in module '{target}', not visible here")
                else:
                    self.err(ctx["m"], e.get("line"),
                             f"module '{target}' has no public member '{field}'")
                return
            if obj["kind"] == "Ident" and obj["name"] == "self":
                cls = ctx.get("class")
                if cls and not cls["extends"] and not cls["uses"]:
                    if field not in class_member_table(cls):
                        self.err(ctx["m"], e.get("line"),
                                 f"class '{cls['name']}' has no member '{field}'")
                return                     # inherited members: lenient (needs type info)
            self.rexpr(obj, scopes, ctx)   # arbitrary receiver: needs types, skip field
        elif k == "Call":
            self.rexpr(e["callee"], scopes, ctx)
            for a in e["args"]:
                self.rexpr(a, scopes, ctx)
        elif k == "Unary":
            self.rexpr(e["operand"], scopes, ctx)
        elif k == "Binary":
            self.rexpr(e["left"], scopes, ctx)
            self.rexpr(e["right"], scopes, ctx)
        elif k == "List":
            for x in e["elems"]:
                self.rexpr(x, scopes, ctx)
        elif k == "Index":
            self.rexpr(e["obj"], scopes, ctx)
            self.rexpr(e["index"], scopes, ctx)
        elif k == "Closure":
            self.rexpr(e["body"], scopes + [{p["name"] for p in e["params"]}], ctx)
        elif k == "Match":
            self.rmatch(e, scopes, ctx)


def main():
    if len(sys.argv) < 2:
        print("usage: python3 checker.py <entry-file> [root ...]")
        return 2
    entry = sys.argv[1]
    roots = sys.argv[2:] or [os.path.dirname(entry) or "."]

    graph, rerrors, cycles = resolver.resolve(entry, roots)

    chk = Checker(graph)
    chk.load()
    chk.check()

    print(f"entry : {entry}")
    print(f"checked modules: {', '.join(sorted(graph))}")

    if rerrors:
        print("\nresolution errors:")
        for e in rerrors:
            print("  " + e)

    if chk.errors:
        print("\nname/visibility errors:")
        for mod, line, msg in chk.errors:
            loc = f"{mod}:{line}" if line else mod
            print(f"  {loc}: {msg}")
        return 1

    if not rerrors:
        print("\nOK  all names resolve; visibility respected")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
