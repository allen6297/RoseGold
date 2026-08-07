#!/usr/bin/env python3
"""
Static type checker for RoseGold.

Runs after resolution + name checking. It builds a type for every
declaration, then infers and checks the type of every expression and
statement -- catching type mismatches, bad arguments, non-Bool conditions,
returning the wrong type, accessing missing or `private` members, etc.

Usage:
    python3 typecheck.py <entry-file> [root ...]

Design notes:
- Nominal typing for classes / enums / traits; structural for primitives,
  List<T>, and function types.
- Int and Float are DISTINCT (no implicit coercion).
- Generics are checked leniently: a type parameter behaves like '?' (Any),
  which is compatible with everything, pending full generic inference.
- Type names are single identifiers (the grammar has no dotted type paths),
  so every type resolves within its own module or the built-ins.
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
checker = _load("langchecker", "checker.py")
lang = resolver.lang


# ---------------------------------------------------------------------
# Types
# ---------------------------------------------------------------------

class Ty:
    def __eq__(self, o):
        return isinstance(o, Ty) and str(self) == str(o)

    def __hash__(self):
        return hash(str(self))


class Prim(Ty):
    def __init__(self, name):
        self.name = name

    def __str__(self):
        return self.name


class AnyTy(Ty):
    def __str__(self):
        return "?"


class ListTy(Ty):
    def __init__(self, elem):
        self.elem = elem

    def __str__(self):
        return f"List<{self.elem}>"


class FuncTy(Ty):
    def __init__(self, params, ret, variadic=False):
        self.params = params
        self.ret = ret
        self.variadic = variadic

    def __str__(self):
        return f"func({', '.join(str(p) for p in self.params)}) -> {self.ret}"


class Named(Ty):
    def __init__(self, name, kind, args=None):
        self.name = name
        self.kind = kind        # 'class' | 'enum' | 'trait'
        self.args = args or []  # type arguments, e.g. Box<Int>

    def __str__(self):
        if self.args:
            return f"{self.name}<{', '.join(str(a) for a in self.args)}>"
        return self.name


class TypeVar(Ty):
    def __init__(self, name):
        self.name = name

    def __str__(self):
        return self.name


class ModuleTy(Ty):
    def __init__(self, name):
        self.name = name

    def __str__(self):
        return f"module {self.name}"


INT, FLOAT, STRING, BOOL, VOID = Prim("Int"), Prim("Float"), Prim("String"), Prim("Bool"), Prim("Void")
ANY = AnyTy()
NUMERIC = {"Int", "Float"}


def line_of(n):
    if isinstance(n, dict):
        if "line" in n:
            return n["line"]
        for v in n.values():
            r = line_of(v)
            if r:
                return r
    elif isinstance(n, (list, tuple)):
        for x in n:
            r = line_of(x)
            if r:
                return r
    return None


def vis_of(mods):
    return (mods or {}).get("vis") or "internal"


# ---------------------------------------------------------------------
# Type checker
# ---------------------------------------------------------------------

class TypeChecker:
    def __init__(self, graph):
        self.graph = graph
        self.asts = {}          # module -> Program
        self.mods = {}          # module -> {classes, enums, traits, values, pub}
        self.errors = []        # (module, line, msg)
        self.cur = None         # current module tables
        self.curm = None        # current module name
        self.cur_class = None   # class name when inside a method/ctor
        self.cur_ret = VOID     # enclosing function's declared return type
        self.loop_depth = 0     # for break/continue validation

    # -- loading & signature building --
    def load(self):
        for m, info in self.graph.items():
            with open(info["file"], "r") as f:
                self.asts[m] = lang.Parser(lang.lex(f.read())).parse_program()
            self.mods[m] = {"classes": {}, "enums": {}, "traits": {},
                            "values": {}, "pub": {}}

    def build(self):
        # pass A: register type names so resolve_type can find them
        for m in self.graph:
            for d in self.asts[m]["decls"]:
                if d["kind"] == "Class":
                    self.mods[m]["classes"][d["name"]] = None
                elif d["kind"] == "Enum":
                    self.mods[m]["enums"][d["name"]] = None
                elif d["kind"] == "Trait":
                    self.mods[m]["traits"][d["name"]] = None
        # pass B: fill infos + value types
        for m in self.graph:
            self.build_module(m)
        # pass C: public surfaces (for imports), incl. re-exports
        for m in self.graph:
            self.mods[m]["pub"] = self.public_values(m)

    def build_module(self, m):
        T = self.mods[m]
        for d in self.asts[m]["decls"]:
            k = d["kind"]
            if k == "Class":
                T["classes"][d["name"]] = self.class_info(m, d)
                T["values"][d["name"]] = self.ctor_type(m, d)          # Counter(..) -> Counter
            elif k == "Enum":
                gens = [g["name"] for g in d["generics"]]
                enum_ty = Named(d["name"], "enum", [TypeVar(g) for g in gens])
                variants = {}
                for v in d["variants"]:
                    fts = [self.resolve_type(m, f["type"], set(gens)) for f in v["fields"]]
                    variants[v["name"]] = fts
                    # a variant with fields is a constructor; a nullary one is a value
                    T["values"][v["name"]] = FuncTy(fts, enum_ty) if fts else enum_ty
                T["enums"][d["name"]] = {"generics": gens, "variants": variants}
                T["values"][d["name"]] = enum_ty
            elif k == "Trait":
                T["traits"][d["name"]] = self.trait_info(m, d)
            elif k == "Func":
                T["values"][d["sig"]["name"]] = self.func_type(m, d["sig"])
            elif k == "Field":
                T["values"][d["name"]] = self.field_type(m, d, gens=set())

    def class_info(self, m, d):
        gens = [g["name"] for g in d["generics"]]
        fields, methods, ctor = {}, {}, None
        for mem in d["members"]:
            mk = mem["kind"]
            if mk == "Field":
                fields[mem["name"]] = (self.field_type(m, mem, gens), vis_of(mem["mods"]))
            elif mk == "Func":
                methods[mem["sig"]["name"]] = (self.func_type(m, mem["sig"], gens, drop_self=True),
                                               vis_of(mem["mods"]))
            elif mk == "Ctor":
                ctor = [self.resolve_type(m, p["type"], gens) if p["type"] else ANY
                        for p in mem["params"]]
        return {"generics": gens, "extends": d["extends"]["name"] if d["extends"] else None,
                "uses": [u["name"] for u in d["uses"]],
                "fields": fields, "methods": methods, "ctor": ctor}

    def trait_info(self, m, d):
        gens = {g["name"] for g in d["generics"]}
        methods = {}
        for mem in d["members"]:
            methods[mem["sig"]["name"]] = (self.func_type(m, mem["sig"], gens, drop_self=True), "pub")
        return {"generics": gens, "methods": methods}

    def ctor_type(self, m, d):
        info = self.mods[m]["classes"][d["name"]]
        params = info["ctor"] if info and info["ctor"] is not None else []
        genargs = [TypeVar(g) for g in (info["generics"] if info else [])]
        return FuncTy(params, Named(d["name"], "class", genargs))

    def func_type(self, m, sig, gens=None, drop_self=False):
        gens = set(gens or ()) | {g["name"] for g in sig["generics"]}
        params = []
        for p in sig["params"]:
            if drop_self and p["name"] == "self":
                continue
            params.append(self.resolve_type(m, p["type"], gens) if p["type"] else ANY)
        ret = self.resolve_type(m, sig["ret"], gens) if sig["ret"] else VOID
        return FuncTy(params, ret)

    def field_type(self, m, field, gens):
        if field["type"]:
            return self.resolve_type(m, field["type"], gens)
        if field.get("init"):
            return self.infer(field["init"], [{}])   # best-effort
        return ANY

    def public_values(self, m, seen=None):
        seen = seen or set()
        if m in seen:
            return {}
        seen.add(m)
        T = self.mods[m]
        pubset = set()
        for d in self.asts[m]["decls"]:
            if d["kind"] == "Class" and vis_of(d["mods"]) == "pub":
                pubset.add(d["name"])
            elif d["kind"] == "Enum" and vis_of(d["mods"]) == "pub":
                pubset.add(d["name"])
                pubset.update(v["name"] for v in d["variants"])
            elif d["kind"] == "Func" and vis_of(d["mods"]) == "pub":
                pubset.add(d["sig"]["name"])
            elif d["kind"] == "Field" and vis_of(d["mods"]) == "pub":
                pubset.add(d["name"])
        result = {n: T["values"][n] for n in pubset if n in T["values"]}
        for imp in self.asts[m]["imports"]:
            if imp["pub"] and imp["path"] in self.mods:
                tex = self.public_values(imp["path"], seen)
                if imp["names"]:
                    for nm in imp["names"]:
                        if nm in tex:
                            result[nm] = tex[nm]
                else:
                    result.update(tex)
        return result

    # -- type resolution from annotations --
    def resolve_type(self, m, node, gens):
        if node is None:
            return ANY
        if node["kind"] == "FuncType":
            return FuncTy([self.resolve_type(m, p, gens) for p in node["params"]],
                          self.resolve_type(m, node["ret"], gens))
        name = node["name"]
        if name in ("Int", "Float", "String", "Bool", "Void"):
            return Prim(name)
        if name == "List":
            return ListTy(self.resolve_type(m, node["args"][0], gens) if node["args"] else ANY)
        if name in gens:
            return TypeVar(name)
        T = self.mods[m]
        args = [self.resolve_type(m, a, gens) for a in node.get("args", [])]
        if name in T["classes"]:
            return Named(name, "class", args)
        if name in T["enums"]:
            return Named(name, "enum", args)
        if name in T["traits"]:
            return Named(name, "trait", args)
        self.err(m, line_of(node), f"unknown type '{name}'")
        return ANY

    # -- compatibility --
    def assignable(self, src, dst):
        if isinstance(src, (AnyTy, TypeVar)) or isinstance(dst, (AnyTy, TypeVar)):
            return True
        if isinstance(src, Prim) and isinstance(dst, Prim):
            return src.name == dst.name
        if isinstance(src, ListTy) and isinstance(dst, ListTy):
            return self.assignable(src.elem, dst.elem) and self.assignable(dst.elem, src.elem)
        if isinstance(src, FuncTy) and isinstance(dst, FuncTy):
            return (len(src.params) == len(dst.params)
                    and all(self.assignable(a, b) for a, b in zip(dst.params, src.params))
                    and self.assignable(src.ret, dst.ret))
        if isinstance(src, Named) and isinstance(dst, Named):
            if src.name == dst.name:
                return True
            if src.kind == "class" and dst.kind in ("class", "trait"):
                return self.is_subtype(src.name, dst.name)
            return False
        return False

    def is_subtype(self, a, b):
        if a == b:
            return True
        info = self.cur["classes"].get(a)
        if not info:
            return False
        supers = ([info["extends"]] if info["extends"] else []) + info["uses"]
        return any(s == b or self.is_subtype(s, b) for s in supers)

    def join(self, a, b, line):
        if a is None:
            return b
        if self.assignable(b, a):
            return a
        if self.assignable(a, b):
            return b
        self.err(self.curm, line, f"match arms have incompatible types '{a}' and '{b}'")
        return ANY

    # -- generic inference: unification + substitution --
    def unify(self, param, arg, s):
        if isinstance(param, TypeVar):
            if param.name in s:
                self.unify(s[param.name], arg, s)
            elif not isinstance(arg, (AnyTy, TypeVar)):
                s[param.name] = arg
        elif isinstance(param, ListTy) and isinstance(arg, ListTy):
            self.unify(param.elem, arg.elem, s)
        elif isinstance(param, FuncTy) and isinstance(arg, FuncTy):
            for p, a in zip(param.params, arg.params):
                self.unify(p, a, s)
            self.unify(param.ret, arg.ret, s)
        elif isinstance(param, Named) and isinstance(arg, Named):
            for p, a in zip(param.args, arg.args):
                self.unify(p, a, s)

    def subst_ty(self, ty, s):
        if not s:
            return ty
        if isinstance(ty, TypeVar):
            return s.get(ty.name, ty)
        if isinstance(ty, ListTy):
            return ListTy(self.subst_ty(ty.elem, s))
        if isinstance(ty, FuncTy):
            return FuncTy([self.subst_ty(p, s) for p in ty.params],
                          self.subst_ty(ty.ret, s), ty.variadic)
        if isinstance(ty, Named):
            return Named(ty.name, ty.kind, [self.subst_ty(a, s) for a in ty.args])
        return ty

    # -- driver --
    def err(self, module, line, msg):
        self.errors.append((module, line, msg))

    def check(self):
        for m in self.graph:
            self.cur, self.curm = self.mods[m], m
            for d in self.asts[m]["decls"]:
                self.check_decl(d)
        self.errors.sort(key=lambda e: (e[0], e[1] or 0))

    def base_env(self):
        return [{
            "print": FuncTy([], VOID, variadic=True),
            "len": FuncTy([ANY], INT),
            "range": FuncTy([INT], ListTy(INT)),
            "push": FuncTy([ListTy(TypeVar("T")), TypeVar("T")], VOID),
            "pop": FuncTy([ListTy(TypeVar("T"))], TypeVar("T")),
            "str": FuncTy([ANY], STRING),
            "ord": FuncTy([STRING], INT),
            "chr": FuncTy([INT], STRING),
            "substr": FuncTy([STRING, INT, INT], STRING),
            "split": FuncTy([STRING, STRING], ListTy(STRING)),
            "int": FuncTy([STRING], INT),
            "readFile": FuncTy([STRING], STRING),
            "writeFile": FuncTy([STRING, STRING], VOID),
        }, dict(self.cur["values"])]

    def check_decl(self, d):
        k = d["kind"]
        if k == "Field":
            if d.get("init"):
                self.check_field_init(d, None)
        elif k == "Func":
            self.check_body(d["sig"], d["body"], None)
        elif k == "Class":
            for mem in d["members"]:
                mk = mem["kind"]
                if mk == "Field" and mem.get("init"):
                    self.check_field_init(mem, d["name"])
                elif mk == "Func":
                    self.check_body(mem["sig"], mem["body"], d["name"])
                elif mk == "Ctor":
                    self.check_ctor(mem, d["name"])
                elif mk in ("Class", "Enum"):
                    self.check_decl(mem)
        elif k == "Trait":
            for mem in d["members"]:
                if mem["body"]:
                    self.check_body(mem["sig"], mem["body"], None)
        elif k == "ModuleInit":
            prev_class, prev_ret = self.cur_class, self.cur_ret
            self.cur_class, self.cur_ret = None, VOID
            self.check_stmts(d["body"]["stmts"], self.base_env() + [{}])
            self.cur_class, self.cur_ret = prev_class, prev_ret

    def class_generics(self, cls_name):
        info = self.cur["classes"].get(cls_name) if cls_name else None
        return list(info["generics"]) if info else []

    def method_scope(self, sig, cls_name, gens, cgens):
        scope = {}
        self_ty = Named(cls_name, "class", [TypeVar(g) for g in cgens]) if cls_name else None
        for p in sig["params"]:
            if p["name"] == "self":
                scope["self"] = self_ty
            else:
                scope[p["name"]] = self.resolve_type(self.curm, p["type"], gens) if p["type"] else ANY
        return scope

    def check_body(self, sig, body, cls_name):
        prev_class, prev_ret = self.cur_class, self.cur_ret
        self.cur_class = cls_name
        cgens = self.class_generics(cls_name)
        gens = set(cgens) | {g["name"] for g in sig["generics"]}
        self.cur_ret = self.resolve_type(self.curm, sig["ret"], gens) if sig["ret"] else VOID
        env = self.base_env() + [self.method_scope(sig, cls_name, gens, cgens)]
        self.check_stmts(body["stmts"], env)
        self.cur_class, self.cur_ret = prev_class, prev_ret

    def check_ctor(self, ctor, cls_name):
        prev_class, prev_ret = self.cur_class, self.cur_ret
        self.cur_class, self.cur_ret = cls_name, VOID
        cgens = self.class_generics(cls_name)
        scope = {"self": Named(cls_name, "class", [TypeVar(g) for g in cgens])}
        for p in ctor["params"]:
            scope[p["name"]] = self.resolve_type(self.curm, p["type"], set(cgens)) if p["type"] else ANY
        self.check_stmts(ctor["body"]["stmts"], self.base_env() + [scope])
        self.cur_class, self.cur_ret = prev_class, prev_ret

    def check_field_init(self, field, cls_name):
        prev = self.cur_class
        self.cur_class = cls_name
        cgens = self.class_generics(cls_name)
        self_scope = {"self": Named(cls_name, "class", [TypeVar(g) for g in cgens])} if cls_name else {}
        env = self.base_env() + [self_scope]
        t = self.infer(field["init"], env)
        if field["type"]:
            declared = self.resolve_type(self.curm, field["type"], set(cgens))
            if not self.assignable(t, declared):
                self.err(self.curm, line_of(field["init"]),
                         f"field '{field['name']}': cannot assign '{t}' to '{declared}'")
        self.cur_class = prev

    # -- statements --
    def check_stmts(self, stmts, env):
        scope = {}
        env = env + [scope]
        for s in stmts:
            self.check_stmt(s, env)

    def check_stmt(self, s, env):
        k = s["kind"]
        if k in ("VarStmt", "ConstStmt"):
            t = self.infer(s["init"], env) if s.get("init") else ANY
            if s["type"]:
                declared = self.resolve_type(self.curm, s["type"], set())
                if s.get("init") and not self.assignable(t, declared):
                    self.err(self.curm, line_of(s.get("init")),
                             f"'{s['name']}': cannot assign '{t}' to declared '{declared}'")
                t = declared
            env[-1][s["name"]] = t
        elif k == "Assign":
            lt = self.infer(s["target"], env)
            rt = self.infer(s["value"], env)
            if not self.assignable(rt, lt):
                self.err(self.curm, line_of(s["value"]), f"cannot assign '{rt}' to '{lt}'")
        elif k == "ExprStmt":
            self.infer(s["expr"], env)
        elif k == "Return":
            rt = self.infer(s["value"], env) if s.get("value") else VOID
            if not self.assignable(rt, self.cur_ret):
                self.err(self.curm, line_of(s) or line_of(s.get("value")),
                         f"returning '{rt}' from a function declared '-> {self.cur_ret}'")
        elif k == "If":
            self.expect_bool(s["cond"], env)
            self.check_stmts(s["then"]["stmts"], env)
            for c, b in s["elifs"]:
                self.expect_bool(c, env)
                self.check_stmts(b["stmts"], env)
            if s["els"]:
                self.check_stmts(s["els"]["stmts"], env)
        elif k == "While":
            self.expect_bool(s["cond"], env)
            self.loop_depth += 1
            self.check_stmts(s["body"]["stmts"], env)
            self.loop_depth -= 1
        elif k == "For":
            it = self.infer(s["iter"], env)
            elem = it.elem if isinstance(it, ListTy) else ANY
            if not isinstance(it, (ListTy, AnyTy, TypeVar)):
                self.err(self.curm, line_of(s["iter"]), f"cannot iterate over '{it}'")
            self.loop_depth += 1
            self.check_stmts(s["body"]["stmts"], env + [{s["var"]: elem}])
            self.loop_depth -= 1
        elif k in ("Break", "Continue"):
            if self.loop_depth == 0:
                self.err(self.curm, line_of(s), f"'{k.lower()}' used outside a loop")
        elif k == "Raise":
            self.infer(s["value"], env)
        elif k == "Try":
            self.check_stmts(s["body"]["stmts"], env)
            self.check_stmts(s["handler"]["stmts"], env + [{s["name"]: ANY}])
        elif k == "Match":
            self.infer(s, env)
        # Pass: nothing

    def expect_bool(self, cond, env):
        t = self.infer(cond, env)
        if not self.assignable(t, BOOL):
            self.err(self.curm, line_of(cond), f"condition must be 'Bool', got '{t}'")

    # -- expressions --
    def lookup(self, name, env):
        for scope in reversed(env):
            if name in scope:
                return scope[name]
        return None

    def infer(self, e, env):
        k = e["kind"]
        if k == "Lit":
            return {"INT": INT, "FLOAT": FLOAT, "STRING": STRING, "BOOL": BOOL}[e["type"]]
        if k == "Ident":
            t = self.lookup(e["name"], env)
            return t if t is not None else ANY      # unknown names already reported by checker.py
        if k == "Member":
            return self.infer_member(e, env)
        if k == "Call":
            return self.infer_call(e, env)
        if k == "Unary":
            t = self.infer(e["operand"], env)
            if e["op"] == "!":
                return BOOL
            return t
        if k == "Binary":
            return self.infer_binary(e, env)
        if k == "List":
            if not e["elems"]:
                return ListTy(ANY)
            elem = None
            for x in e["elems"]:
                xt = self.infer(x, env)
                elem = xt if elem is None else self.join(elem, xt, line_of(x))
            return ListTy(elem)
        if k == "Index":
            ot = self.infer(e["obj"], env)
            it = self.infer(e["index"], env)
            if not self.assignable(it, INT):
                self.err(self.curm, line_of(e), f"index must be 'Int', got '{it}'")
            if isinstance(ot, ListTy):
                return ot.elem
            if isinstance(ot, (AnyTy, TypeVar)):
                return ANY
            if ot == STRING:
                return STRING
            self.err(self.curm, line_of(e), f"cannot index '{ot}'")
            return ANY
        if k == "Closure":
            scope = {p["name"]: (self.resolve_type(self.curm, p["type"], set()) if p["type"] else ANY)
                     for p in e["params"]}
            body_t = self.infer(e["body"], env + [scope])
            ret = self.resolve_type(self.curm, e["ret"], set()) if e["ret"] else body_t
            return FuncTy([scope[p["name"]] for p in e["params"]], ret)
        if k == "Match":
            return self.infer_match(e, env)
        return ANY

    def infer_member(self, e, env):
        obj = self.infer(e["obj"], env)
        field = e["field"]
        if isinstance(obj, ModuleTy):
            pub = self.mods.get(obj.name, {}).get("pub", {})
            if field in pub:
                return pub[field]
            self.err(self.curm, line_of(e), f"module '{obj.name}' has no public member '{field}'")
            return ANY
        if isinstance(obj, Named) and obj.kind == "class":
            info = self.cur["classes"].get(obj.name)
            if not info:
                return ANY
            member = self.find_member(obj.name, field)
            if member is None:
                self.err(self.curm, line_of(e), f"'{obj.name}' has no member '{field}'")
                return ANY
            ty, vis = member
            if vis == "private" and self.cur_class != obj.name:
                self.err(self.curm, line_of(e),
                         f"'{field}' is private to '{obj.name}'")
            subst = {g: obj.args[i] for i, g in enumerate(info["generics"]) if i < len(obj.args)}
            return self.subst_ty(ty, subst)
        if isinstance(obj, (AnyTy, TypeVar)):
            return ANY
        self.err(self.curm, line_of(e), f"cannot access '.{field}' on '{obj}'")
        return ANY

    def find_member(self, cls_name, field):
        info = self.cur["classes"].get(cls_name)
        while info:
            if field in info["fields"]:
                return info["fields"][field]
            if field in info["methods"]:
                return info["methods"][field]
            info = self.cur["classes"].get(info["extends"]) if info["extends"] else None
        return None

    def infer_call(self, e, env):
        ct = self.infer(e["callee"], env)
        argts = [self.infer(a, env) for a in e["args"]]
        if isinstance(ct, (AnyTy, TypeVar)):
            return ANY
        if not isinstance(ct, FuncTy):
            self.err(self.curm, line_of(e), f"'{ct}' is not callable")
            return ANY
        if ct.variadic:
            return ct.ret
        if len(argts) != len(ct.params):
            self.err(self.curm, line_of(e),
                     f"expected {len(ct.params)} argument(s), got {len(argts)}")
            return ct.ret
        subst = {}                              # infer generic type parameters
        for pt, at in zip(ct.params, argts):
            self.unify(pt, at, subst)
        for i, (at, pt) in enumerate(zip(argts, ct.params)):
            want = self.subst_ty(pt, subst)
            if not self.assignable(at, want):
                self.err(self.curm, line_of(e["args"][i]),
                         f"argument {i + 1}: '{at}' is not assignable to '{want}'")
        return self.subst_ty(ct.ret, subst)

    def infer_binary(self, e, env):
        op = e["op"]
        lt = self.infer(e["left"], env)
        rt = self.infer(e["right"], env)
        if op in ("&&", "||"):
            for side, t in (("left", lt), ("right", rt)):
                if not self.assignable(t, BOOL):
                    self.err(self.curm, line_of(e[side]), f"'{op}' needs 'Bool', got '{t}'")
            return BOOL
        if op in ("==", "!="):
            return BOOL
        if op in ("<", "<=", ">", ">="):
            if not (self.compatible_num_or_str(lt, rt)):
                self.err(self.curm, line_of(e["right"]), f"cannot compare '{lt}' and '{rt}'")
            return BOOL
        # arithmetic  + - * / %
        if isinstance(lt, (AnyTy, TypeVar)) or isinstance(rt, (AnyTy, TypeVar)):
            return ANY
        if op == "+" and lt == STRING and rt == STRING:
            return STRING
        if lt == rt and isinstance(lt, Prim) and lt.name in NUMERIC:
            return lt
        self.err(self.curm, line_of(e["right"]), f"cannot apply '{op}' to '{lt}' and '{rt}'")
        return ANY

    def compatible_num_or_str(self, a, b):
        if isinstance(a, (AnyTy, TypeVar)) or isinstance(b, (AnyTy, TypeVar)):
            return True
        return a == b and isinstance(a, Prim) and a.name in (NUMERIC | {"String"})

    def infer_match(self, e, env):
        subj = self.infer(e["subject"], env)
        result = None
        for arm in e["arms"]:
            binds = {}
            for p in arm["patterns"]:
                self.check_pattern(p, subj, binds, line_of(arm.get("body")))
            body = arm["body"]
            if body["kind"] == "Block":
                bt = self.block_value_type(body["stmts"], env + [binds])
            else:
                bt = self.infer(body, env + [binds])
            result = self.join(result, bt, line_of(body))
        return result if result is not None else VOID

    def check_pattern(self, p, subj, binds, line):
        k = p["kind"]
        if k == "Wildcard":
            return
        if k == "LitPattern":
            lt = {"INT": INT, "FLOAT": FLOAT, "STRING": STRING, "BOOL": BOOL}[p["type"]]
            if not self.assignable(lt, subj):
                self.err(self.curm, line, f"pattern '{lt}' cannot match subject '{subj}'")
        elif k == "VariantPattern":
            if isinstance(subj, Named) and subj.kind == "enum":
                info = self.cur["enums"].get(subj.name)
                variants = info["variants"] if info else {}
                if p["name"] not in variants:
                    self.err(self.curm, line, f"'{p['name']}' is not a variant of '{subj.name}'")
                    for b in p["bindings"]:
                        binds[b] = ANY
                else:
                    fts = variants[p["name"]]
                    gens = info["generics"] if info else []
                    subst = {g: subj.args[i] for i, g in enumerate(gens) if i < len(subj.args)}
                    for i, b in enumerate(p["bindings"]):
                        binds[b] = self.subst_ty(fts[i], subst) if i < len(fts) else ANY
            else:
                for b in p["bindings"]:
                    binds[b] = ANY

    def block_value_type(self, stmts, env):
        env = env + [{}]
        last = VOID
        for s in stmts:
            if s["kind"] == "ExprStmt":
                last = self.infer(s["expr"], env)
            else:
                self.check_stmt(s, env)
        return last


# ---------------------------------------------------------------------
# main
# ---------------------------------------------------------------------

def main():
    if len(sys.argv) < 2:
        print("usage: python3 typecheck.py <entry-file> [root ...]")
        return 2
    entry = sys.argv[1]
    roots = sys.argv[2:] or [os.path.dirname(entry) or "."]

    graph, rerrors, cycles = resolver.resolve(entry, roots)

    name_check = checker.Checker(graph)
    name_check.load()
    name_check.check()

    tc = TypeChecker(graph)
    tc.load()
    tc.build()
    tc.check()

    print(f"entry : {entry}")
    print(f"checked modules: {', '.join(sorted(graph))}")

    all_errs = ([("resolve", None, e) for e in rerrors]
                + [(m, l, msg) for (m, l, msg) in name_check.errors]
                + tc.errors)
    if all_errs:
        print("\ntype errors:" if tc.errors else "\nerrors:")
        for mod, line, msg in sorted(all_errs, key=lambda e: (e[0], e[1] or 0)):
            loc = f"{mod}:{line}" if line else mod
            print(f"  {loc}: {msg}")
        return 1

    print("\nOK  all types check")
    return 0


if __name__ == "__main__":
    sys.exit(main())
