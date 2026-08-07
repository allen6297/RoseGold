#!/usr/bin/env python3
"""
RoseGold interpreter (tree-walking) -- actually runs a program.

Pipeline: source -> tokens -> AST -> resolved graph -> name/visibility check
-> EVALUATE. It resolves the module graph, gates on the semantic checker
(refuses to run a program with resolution/visibility errors), then executes
the entry module's `main()`.

Usage:
    python3 interpreter.py <entry-file> [root ...]

Supports: functions, recursion, closures (arrow), higher-order calls;
var/const, assignment; if/elif/else, while, for-in over lists; match with
literal / wildcard / variant patterns; classes with fields, init, methods,
self; enums + variant construction; cross-module calls; builtins print/len/range.
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
typecheck = _load("langtypecheck", "typecheck.py")
lang = resolver.lang


# ---------------------------------------------------------------------
# Runtime values & signals
# ---------------------------------------------------------------------

class ReturnSignal(Exception):
    def __init__(self, value):
        self.value = value


class BreakSignal(Exception):
    pass


class ContinueSignal(Exception):
    pass


class RaiseSignal(Exception):
    def __init__(self, value):
        self.value = value


class RuntimeErr(Exception):
    pass


class Env:
    def __init__(self, parent=None):
        self.vars = {}
        self.parent = parent

    def define(self, name, val):
        self.vars[name] = val

    def get(self, name):
        e = self
        while e:
            if name in e.vars:
                return e.vars[name]
            e = e.parent
        raise RuntimeErr(f"name '{name}' is not defined")

    def assign(self, name, val):
        e = self
        while e:
            if name in e.vars:
                e.vars[name] = val
                return
            e = e.parent
        raise RuntimeErr(f"cannot assign to undefined '{name}'")


class Function:
    def __init__(self, name, params, body, env, expr_body=False):
        self.name = name
        self.params = params        # list of parameter names
        self.body = body            # Block node, or Expr node if expr_body
        self.env = env
        self.expr_body = expr_body


class BoundMethod:
    def __init__(self, func, recv):
        self.func = func
        self.recv = recv


class ClassObj:
    def __init__(self, name, node, env):
        self.name = name
        self.env = env
        self.fields = []            # Field nodes with initializers
        self.ctor = None            # Ctor node or None
        self.methods = {}           # name -> Function
        for mem in node["members"]:
            k = mem["kind"]
            if k == "Field":
                self.fields.append(mem)
            elif k == "Ctor":
                self.ctor = mem
            elif k == "Func":
                self.methods[mem["sig"]["name"]] = Function(
                    mem["sig"]["name"],
                    [p["name"] for p in mem["sig"]["params"]],
                    mem["body"], env)


class Instance:
    def __init__(self, cls):
        self.cls = cls
        self.fields = {}


class EnumObj:
    def __init__(self, name, variants):
        self.name = name
        self.variants = variants    # variant name -> [field names]


class VariantCtor:
    def __init__(self, enum_name, name, fieldnames):
        self.enum_name = enum_name
        self.name = name
        self.fieldnames = fieldnames


class VariantValue:
    def __init__(self, enum_name, name, values):
        self.enum_name = enum_name
        self.name = name
        self.values = values        # list, positional


class ModuleObj:
    def __init__(self, name, exports):
        self.name = name
        self.exports = exports      # dict: public name -> value


# ---------------------------------------------------------------------
# Display & truthiness
# ---------------------------------------------------------------------

def to_display(v):
    if v is None:
        return "void"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float):
        return repr(v)
    if isinstance(v, (int, str)):
        return str(v)
    if isinstance(v, list):
        return "[" + ", ".join(to_display(x) for x in v) + "]"
    if isinstance(v, VariantValue):
        return f"{v.name}(" + ", ".join(to_display(x) for x in v.values) + ")"
    if isinstance(v, Instance):
        return f"<{v.cls.name}>"
    if isinstance(v, (Function, BoundMethod)):
        return "<func>"
    if isinstance(v, ClassObj):
        return f"<class {v.name}>"
    if isinstance(v, ModuleObj):
        return f"<module {v.name}>"
    return str(v)


def truthy(v):
    if isinstance(v, bool):
        return v
    if v is None:
        return False
    return True


def unescape(s):
    return (s.replace("\\n", "\n").replace("\\t", "\t")
             .replace('\\"', '"').replace("\\\\", "\\"))


# ---------------------------------------------------------------------
# Interpreter
# ---------------------------------------------------------------------

class Interp:
    def __init__(self, graph):
        self.graph = graph
        self.modules = {}       # module name -> Program
        self.envs = {}          # module name -> Env
        self.pub = {}           # module name -> set of public top-level names
        self._exports = {}      # module name -> {name: value}
        self.builtins = Env()
        self.builtins.define("print", ("builtin", self._print))
        self.builtins.define("len", ("builtin", lambda a: len(a[0])))
        self.builtins.define("range", ("builtin", lambda a: list(range(a[0]))))
        # -- standard library --
        self.builtins.define("push", ("builtin", self._push))
        self.builtins.define("pop", ("builtin", lambda a: a[0].pop()))
        self.builtins.define("str", ("builtin", lambda a: to_display(a[0])))
        self.builtins.define("ord", ("builtin", lambda a: ord(a[0][0])))
        self.builtins.define("chr", ("builtin", lambda a: chr(a[0])))
        self.builtins.define("substr", ("builtin", self._substr))
        self.builtins.define("split", ("builtin", lambda a: a[0].split(a[1])))
        self.builtins.define("int", ("builtin", lambda a: int(a[0])))
        self.builtins.define("readFile", ("builtin", lambda a: open(a[0]).read()))
        self.builtins.define("writeFile", ("builtin", self._writeFile))

    def _print(self, args):
        print(" ".join(to_display(a) for a in args))
        return None

    def _push(self, args):
        args[0].append(args[1])
        return None

    def _substr(self, args):
        s, a, b, n = args[0], args[1], args[2], len(args[0])
        a = 0 if a < 0 else (n if a > n else a)
        b = 0 if b < 0 else (n if b > n else b)
        return s[a:b] if a < b else ""

    def _writeFile(self, args):
        with open(args[0], "w") as f:
            f.write(args[1])
        return None

    # -- setup passes --
    def load(self):
        for m, info in self.graph.items():
            with open(info["file"], "r") as f:
                toks = lang.lex(f.read())
            self.modules[m] = lang.Parser(toks).parse_program()
            self.envs[m] = Env(parent=self.builtins)
            self.pub[m] = set()

    def define_decls(self, m):
        env = self.modules_env(m)
        for d in self.modules[m]["decls"]:
            k = d["kind"]
            vis = checker.vis_of(d.get("mods"))
            if k == "Func":
                name = d["sig"]["name"]
                env.define(name, Function(name, [p["name"] for p in d["sig"]["params"]],
                                          d["body"], env))
                self._maybe_pub(m, name, vis)
            elif k == "Class":
                env.define(d["name"], ClassObj(d["name"], d, env))
                self._maybe_pub(m, d["name"], vis)
            elif k == "Enum":
                variants = {v["name"]: [f["name"] for f in v["fields"]] for v in d["variants"]}
                env.define(d["name"], EnumObj(d["name"], variants))
                self._maybe_pub(m, d["name"], vis)
                for vn, fns in variants.items():             # flattened into module scope
                    # a variant with fields is a constructor; a nullary one is a value
                    env.define(vn, VariantCtor(d["name"], vn, fns) if fns
                               else VariantValue(d["name"], vn, []))
                    self._maybe_pub(m, vn, vis)
            elif k == "Field":
                env.define(d["name"], None)                  # value filled in eval_globals
                self._maybe_pub(m, d["name"], vis)

    def _maybe_pub(self, m, name, vis):
        if vis == "pub":
            self.pub[m].add(name)

    def modules_env(self, m):
        return self.envs[m]

    def exports(self, m, seen=None):
        if m in self._exports:
            return self._exports[m]
        seen = seen or set()
        if m in seen:
            return {}
        seen.add(m)
        env = self.envs[m]
        result = {name: env.vars[name] for name in self.pub[m] if name in env.vars}
        for imp in self.modules[m]["imports"]:
            if not imp["pub"] or imp["path"] not in self.modules:
                continue
            tex = self.exports(imp["path"], seen)
            if imp["names"]:
                for nm in imp["names"]:
                    if nm in tex:
                        result[nm] = tex[nm]
            else:
                result.update(tex)
        self._exports[m] = result
        return result

    def bind_imports(self, m):
        env = self.envs[m]
        for imp in self.modules[m]["imports"]:
            target = imp["path"]
            if target not in self.modules:
                continue
            if imp["names"]:
                tex = self.exports(target)
                for nm in imp["names"]:
                    if nm in tex:
                        env.define(nm, tex[nm])
            else:
                q = imp["alias"] or target.split(".")[-1]
                env.define(q, ModuleObj(target, self.exports(target)))

    def eval_globals(self, m):
        env = self.envs[m]
        for d in self.modules[m]["decls"]:
            if d["kind"] == "Field" and d.get("init"):
                env.vars[d["name"]] = self.eval(d["init"], env)

    def setup(self):
        for m in self.graph:
            self.define_decls(m)
        for m in self.graph:
            self.bind_imports(m)
        for m in self.graph:
            self.eval_globals(m)

    # -- execution --
    def module_init(self, m):
        for d in self.modules[m]["decls"]:
            if d["kind"] == "ModuleInit":
                return d
        return None

    def run_inits(self):
        """Run each module's `init:` block once, dependencies first."""
        order, state = [], {}   # state: 0 = visiting, 1 = done

        def visit(m):
            state[m] = 0
            for imp in self.modules[m]["imports"]:
                t = imp["path"]
                if t not in self.modules:
                    continue
                if state.get(t) == 0:                       # back-edge: cycle
                    if self.module_init(m) or self.module_init(t):
                        raise RuntimeErr(
                            f"module init cycle involving '{m}' and '{t}'")
                elif t not in state:
                    visit(t)
            state[m] = 1
            order.append(m)

        for m in self.graph:
            if m not in state:
                visit(m)
        for m in order:
            init = self.module_init(m)
            if init:
                self.exec_stmts(init["body"]["stmts"], Env(parent=self.envs[m]))

    def run(self, entry_mod):
        self.run_inits()
        env = self.envs[entry_mod]
        if "main" not in env.vars:
            raise RuntimeErr(f"entry module '{entry_mod}' has no func main()")
        self.call(env.vars["main"], [])

    def exec_stmts(self, stmts, env):
        for s in stmts:
            self.exec_stmt(s, env)

    def exec_suite(self, block, env):
        self.exec_stmts(block["stmts"], Env(parent=env))

    def exec_stmt(self, s, env):
        k = s["kind"]
        if k in ("VarStmt", "ConstStmt"):
            env.define(s["name"], self.eval(s["init"], env) if s.get("init") else None)
        elif k == "Assign":
            self.assign(s["target"], self.eval(s["value"], env), env)
        elif k == "ExprStmt":
            self.eval(s["expr"], env)
        elif k == "Return":
            raise ReturnSignal(self.eval(s["value"], env) if s.get("value") else None)
        elif k == "If":
            if truthy(self.eval(s["cond"], env)):
                self.exec_suite(s["then"], env)
                return
            for c, b in s["elifs"]:
                if truthy(self.eval(c, env)):
                    self.exec_suite(b, env)
                    return
            if s["els"]:
                self.exec_suite(s["els"], env)
        elif k == "While":
            while truthy(self.eval(s["cond"], env)):
                try:
                    self.exec_suite(s["body"], env)
                except ContinueSignal:
                    continue
                except BreakSignal:
                    break
        elif k == "For":
            seq = self.eval(s["iter"], env)
            if not isinstance(seq, list):
                raise RuntimeErr(f"cannot iterate over {to_display(seq)}")
            for item in seq:
                child = Env(parent=env)
                child.define(s["var"], item)
                try:
                    self.exec_stmts(s["body"]["stmts"], child)
                except ContinueSignal:
                    continue
                except BreakSignal:
                    break
        elif k == "Break":
            raise BreakSignal()
        elif k == "Continue":
            raise ContinueSignal()
        elif k == "Raise":
            raise RaiseSignal(self.eval(s["value"], env))
        elif k == "Try":
            try:
                self.exec_stmts(s["body"]["stmts"], Env(parent=env))
            except RaiseSignal as rs:
                henv = Env(parent=env)
                henv.define(s["name"], rs.value)
                self.exec_stmts(s["handler"]["stmts"], henv)
        elif k == "Match":
            self.eval_match(s, env)
        # Pass: nothing

    def assign(self, target, value, env):
        if target["kind"] == "Ident":
            env.assign(target["name"], value)
        elif target["kind"] == "Member":
            obj = self.eval(target["obj"], env)
            if isinstance(obj, Instance):
                obj.fields[target["field"]] = value
            else:
                raise RuntimeErr(f"cannot assign to member of {to_display(obj)}")
        elif target["kind"] == "Index":
            obj = self.eval(target["obj"], env)
            idx = self.eval(target["index"], env)
            if not isinstance(obj, list):
                raise RuntimeErr(f"cannot index-assign into {to_display(obj)}")
            if not isinstance(idx, int) or idx < 0 or idx >= len(obj):
                raise RuntimeErr(f"list index {idx} out of range (len {len(obj)})")
            obj[idx] = value
        else:
            raise RuntimeErr("invalid assignment target")

    # -- expressions --
    def eval(self, e, env):
        k = e["kind"]
        if k == "Lit":
            return self.literal(e)
        if k == "Ident":
            return env.get(e["name"])
        if k == "Member":
            return self.member(e, env)
        if k == "Call":
            callee = self.eval(e["callee"], env)
            args = [self.eval(a, env) for a in e["args"]]
            return self.call(callee, args)
        if k == "Unary":
            v = self.eval(e["operand"], env)
            return (not truthy(v)) if e["op"] == "!" else -v
        if k == "Binary":
            return self.binary(e, env)
        if k == "List":
            return [self.eval(x, env) for x in e["elems"]]
        if k == "Index":
            obj = self.eval(e["obj"], env)
            idx = self.eval(e["index"], env)
            if isinstance(obj, (list, str)):
                if not isinstance(idx, int) or idx < 0 or idx >= len(obj):
                    raise RuntimeErr(f"index {idx} out of range (len {len(obj)})")
                return obj[idx]
            raise RuntimeErr(f"cannot index {to_display(obj)}")
        if k == "Closure":
            return Function("<closure>", [p["name"] for p in e["params"]],
                            e["body"], env, expr_body=True)
        if k == "Match":
            return self.eval_match(e, env)
        raise RuntimeErr(f"cannot evaluate node {k}")

    def literal(self, e):
        t, v = e["type"], e["value"]
        if t == "INT":
            return int(v)
        if t == "FLOAT":
            return float(v)
        if t == "STRING":
            return unescape(v)
        if t == "BOOL":
            return v == "true"
        raise RuntimeErr(f"bad literal {t}")

    def member(self, e, env):
        obj = self.eval(e["obj"], env)
        field = e["field"]
        if isinstance(obj, ModuleObj):
            if field in obj.exports:
                return obj.exports[field]
            raise RuntimeErr(f"module '{obj.name}' has no public member '{field}'")
        if isinstance(obj, Instance):
            if field in obj.fields:
                return obj.fields[field]
            if field in obj.cls.methods:
                return BoundMethod(obj.cls.methods[field], obj)
            raise RuntimeErr(f"'{obj.cls.name}' has no member '{field}'")
        if isinstance(obj, EnumObj):
            if field in obj.variants:
                fns = obj.variants[field]
                return (VariantCtor(obj.name, field, fns) if fns
                        else VariantValue(obj.name, field, []))
            raise RuntimeErr(f"enum '{obj.name}' has no variant '{field}'")
        raise RuntimeErr(f"cannot access .{field} on {to_display(obj)}")

    def binary(self, e, env):
        op = e["op"]
        if op == "&&":
            return truthy(self.eval(e["left"], env)) and truthy(self.eval(e["right"], env))
        if op == "||":
            return truthy(self.eval(e["left"], env)) or truthy(self.eval(e["right"], env))
        l = self.eval(e["left"], env)
        r = self.eval(e["right"], env)
        if op == "+":
            if isinstance(l, str) or isinstance(r, str):
                return to_display(l) + to_display(r)
            return l + r
        if op == "-":
            return l - r
        if op == "*":
            return l * r
        if op == "/":
            return l / r
        if op == "%":
            return l % r
        if op == "<":
            return l < r
        if op == "<=":
            return l <= r
        if op == ">":
            return l > r
        if op == ">=":
            return l >= r
        if op == "==":
            return l == r
        if op == "!=":
            return l != r
        raise RuntimeErr(f"unknown operator {op}")

    def call(self, callee, args):
        if isinstance(callee, tuple) and callee[0] == "builtin":
            return callee[1](args)
        if isinstance(callee, ClassObj):
            return self.instantiate(callee, args)
        if isinstance(callee, VariantCtor):
            return VariantValue(callee.enum_name, callee.name, args)
        if isinstance(callee, BoundMethod):
            return self.call_function(callee.func, args, self_val=callee.recv)
        if isinstance(callee, Function):
            return self.call_function(callee, args)
        raise RuntimeErr(f"{to_display(callee)} is not callable")

    def call_function(self, fn, args, self_val=None):
        env = Env(parent=fn.env)
        params = fn.params
        i = 0
        if self_val is not None:
            env.define("self", self_val)
            if params and params[0] == "self":
                i = 1
        for j, pname in enumerate(params[i:]):
            env.define(pname, args[j] if j < len(args) else None)
        if fn.expr_body:
            return self.eval(fn.body, env)
        try:
            self.exec_stmts(fn.body["stmts"], env)
        except ReturnSignal as rs:
            return rs.value
        return None

    def instantiate(self, cls, args):
        inst = Instance(cls)
        fenv = Env(parent=cls.env)
        fenv.define("self", inst)
        for f in cls.fields:
            inst.fields[f["name"]] = self.eval(f["init"], fenv) if f.get("init") else None
        if cls.ctor:
            cenv = Env(parent=cls.env)
            cenv.define("self", inst)
            for j, p in enumerate(cls.ctor["params"]):
                cenv.define(p["name"], args[j] if j < len(args) else None)
            try:
                self.exec_stmts(cls.ctor["body"]["stmts"], cenv)
            except ReturnSignal:
                pass
        return inst

    # -- match --
    def eval_match(self, node, env):
        subj = self.eval(node["subject"], env)
        for arm in node["arms"]:
            for pat in arm["patterns"]:
                binds = self.match_pattern(pat, subj)
                if binds is not None:
                    aenv = Env(parent=env)
                    for name, val in binds.items():
                        aenv.define(name, val)
                    body = arm["body"]
                    if body["kind"] == "Block":
                        return self.block_value(body["stmts"], aenv)
                    return self.eval(body, aenv)
        return None

    def match_pattern(self, pat, subj):
        k = pat["kind"]
        if k == "Wildcard":
            return {}
        if k == "LitPattern":
            lit = self.literal({"type": pat["type"], "value": pat["value"]})
            return {} if subj == lit else None
        if k == "VariantPattern":
            if isinstance(subj, VariantValue) and subj.name == pat["name"]:
                return {b: subj.values[i] for i, b in enumerate(pat["bindings"])}
            return None
        return None

    def block_value(self, stmts, env):
        last = None
        child = Env(parent=env)
        for s in stmts:
            if s["kind"] == "ExprStmt":
                last = self.eval(s["expr"], child)
            else:
                self.exec_stmt(s, child)
        return last


# ---------------------------------------------------------------------
# main
# ---------------------------------------------------------------------

def main():
    if len(sys.argv) < 2:
        print("usage: python3 interpreter.py <entry-file> [root ...]")
        return 2
    entry = sys.argv[1]
    roots = sys.argv[2:] or [os.path.dirname(entry) or "."]

    graph, rerrors, cycles = resolver.resolve(entry, roots)

    # gate: run the full front-end (resolve -> name/visibility -> types)
    chk = checker.Checker(graph)
    chk.load()
    chk.check()
    tc = typecheck.TypeChecker(graph)
    tc.load()
    tc.build()
    tc.check()
    if rerrors or chk.errors or tc.errors:
        print("cannot run -- front-end errors:")
        for e in rerrors:
            print("  " + e)
        for mod, line, msg in chk.errors + tc.errors:
            loc = f"{mod}:{line}" if line else mod
            print(f"  {loc}: {msg}")
        return 1

    entry_mod, _ = resolver.parse_module(entry)
    interp = Interp(graph)
    interp.load()
    interp.setup()
    try:
        interp.run(entry_mod)
    except RaiseSignal as rs:
        print(f"uncaught error: {to_display(rs.value)}")
        return 1
    except RuntimeErr as e:
        print(f"runtime error: {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
