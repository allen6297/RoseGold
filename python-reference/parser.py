#!/usr/bin/env python3
"""
Reference parser for RoseGold (colon + indentation), following grammar.ebnf.

Usage:
    python3 parser.py [source_file]      # defaults to demo/prog.rg

It tokenizes the source, builds an AST via recursive descent, and either
prints the AST and "OK", or reports the first syntax error with location.
This exists to PROVE the grammar accepts real programs (and to surface
design bugs) -- it is not a full compiler.

Blocks use the OFFSIDE RULE (like Python / Go). The lexer emits three
synthetic tokens the parser consumes:
    NEWLINE  ends a logical line
    INDENT   a deeper indentation opens a block
    DEDENT   a block closes (one per level)
NEWLINE/INDENT/DEDENT are suppressed inside '(' or '[', so long
expressions and argument lists can span lines by wrapping in parens.
"""

import sys

# ----------------------------------------------------------------------
# Lexer
# ----------------------------------------------------------------------

KEYWORDS = {
    "module", "import", "as", "pub", "internal", "private", "static",
    "class", "trait", "enum", "extends", "uses", "func", "init",
    "var", "const", "return", "pass",
    "if", "elif", "else", "while", "for", "in", "match",
    "break", "continue", "try", "catch", "raise",
    "true", "false",
}

TWO_CHAR = ("->", "=>", "==", "!=", "<=", ">=", "&&", "||")
ONE_CHAR = set("()[]<>=!+-*/%.,:")


class Token:
    __slots__ = ("type", "value", "line", "col")

    def __init__(self, type_, value, line, col):
        self.type = type_   # IDENT INT FLOAT STRING KW OP NEWLINE INDENT DEDENT EOF
        self.value = value
        self.line = line
        self.col = col

    def __repr__(self):
        if self.type in ("NEWLINE", "INDENT", "DEDENT", "EOF"):
            return self.type
        return f"{self.type}({self.value!r})"


class LexError(Exception):
    pass


def strip_comments(src):
    """Remove '#' line comments and '#/ ... /#' block comments, preserving
    line structure (newlines kept, other chars -> spaces) and skipping
    comment markers that appear inside string literals."""
    out = []
    i, n, in_str = 0, len(src), False
    while i < n:
        c = src[i]
        if in_str:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(src[i + 1])
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if c == '"':
            in_str = True
            out.append(c)
            i += 1
            continue
        if c == "#" and i + 1 < n and src[i + 1] == "/":   # block comment
            out.append("  ")
            i += 2
            while i < n and src[i:i + 2] != "/#":
                out.append("\n" if src[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append("  ")
                i += 2
            continue
        if c == "#":                                        # line comment
            while i < n and src[i] != "\n":
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def lex(src):
    src = strip_comments(src)
    n = len(src)
    i, line, col = 0, 1, 1
    toks = []
    indents = [0]
    paren = 0            # depth of () and [] -- suppresses layout tokens
    line_start = True

    def emit(type_, value, l, c):
        toks.append(Token(type_, value, l, c))

    def advance():
        nonlocal i, line, col
        if src[i] == "\n":
            line += 1
            col = 1
        else:
            col += 1
        i += 1

    while i < n:
        # measure indentation at the start of a logical line
        if line_start and paren == 0:
            width = 0
            while i < n and src[i] in " \t":
                width += 8 if src[i] == "\t" else 1
                advance()
            if i >= n:
                break
            if src[i] == "\n":          # blank line -> ignore for layout
                advance()
                continue
            if width > indents[-1]:
                indents.append(width)
                emit("INDENT", None, line, col)
            elif width < indents[-1]:
                while width < indents[-1]:
                    indents.pop()
                    emit("DEDENT", None, line, col)
                if width != indents[-1]:
                    raise LexError(f"inconsistent indentation at line {line}")
            line_start = False
            continue

        c = src[i]

        if c == "\n":
            if paren == 0:
                if toks and toks[-1].type not in ("NEWLINE", "INDENT", "DEDENT"):
                    emit("NEWLINE", "\\n", line, col)
                advance()
                line_start = True
            else:
                advance()               # inside () or [] : join lines
            continue

        if c in " \t\r":
            advance()
            continue

        sl, sc = line, col

        if c == '"':                    # string
            advance()
            buf = []
            while i < n and src[i] != '"':
                if src[i] == "\\" and i + 1 < n:
                    buf.append(src[i:i + 2])
                    advance(); advance()
                else:
                    buf.append(src[i])
                    advance()
            if i >= n:
                raise LexError(f"unterminated string at line {sl} col {sc}")
            advance()
            emit("STRING", "".join(buf), sl, sc)
            continue

        if c.isdigit():                 # number
            start = i
            while i < n and src[i].isdigit():
                advance()
            is_float = False
            if i < n and src[i] == "." and i + 1 < n and src[i + 1].isdigit():
                is_float = True
                advance()
                while i < n and src[i].isdigit():
                    advance()
            emit("FLOAT" if is_float else "INT", src[start:i], sl, sc)
            continue

        if c.isalpha() or c == "_":     # identifier / keyword
            start = i
            while i < n and (src[i].isalnum() or src[i] == "_"):
                advance()
            text = src[start:i]
            emit("KW" if text in KEYWORDS else "IDENT", text, sl, sc)
            continue

        two = src[i:i + 2]              # operators
        if two in TWO_CHAR:
            advance(); advance()
            emit("OP", two, sl, sc)
            continue
        if c in ONE_CHAR:
            advance()
            emit("OP", c, sl, sc)
            if c in "([":
                paren += 1
            elif c in ")]":
                if paren > 0:
                    paren -= 1
            continue

        raise LexError(f"unexpected character {c!r} at line {sl} col {sc}")

    # end of input: close the last line and any open blocks
    if toks and toks[-1].type not in ("NEWLINE", "INDENT", "DEDENT"):
        emit("NEWLINE", "\\n", line, col)
    while len(indents) > 1:
        indents.pop()
        emit("DEDENT", None, line, col)
    emit("EOF", None, line, col)
    return toks


# ----------------------------------------------------------------------
# Parser
# ----------------------------------------------------------------------

class ParseError(Exception):
    pass


def node(kind, **fields):
    d = {"kind": kind}
    d.update(fields)
    return d


class Parser:
    def __init__(self, toks):
        self.toks = toks
        self.i = 0

    # -- token helpers --
    def peek(self, k=0):
        return self.toks[self.i + k]

    def kw(self, v):
        t = self.peek()
        return t.type == "KW" and t.value == v

    def op(self, v):
        t = self.peek()
        return t.type == "OP" and t.value == v

    def is_(self, type_):
        return self.peek().type == type_

    def next(self):
        t = self.toks[self.i]
        self.i += 1
        return t

    def err(self, msg):
        t = self.peek()
        raise ParseError(f"line {t.line} col {t.col}: {msg} (got {t!r})")

    def eat_kw(self, v):
        if not self.kw(v):
            self.err(f"expected keyword '{v}'")
        return self.next()

    def eat_op(self, v):
        if not self.op(v):
            self.err(f"expected '{v}'")
        return self.next()

    def eat_ident(self):
        if not self.is_("IDENT"):
            self.err("expected identifier")
        return self.next().value

    def expect(self, type_):
        if not self.is_(type_):
            self.err(f"expected {type_}")
        return self.next()

    def skip_nl(self):
        while self.is_("NEWLINE"):
            self.i += 1

    def parse_dotted(self):
        """A dotted path: Ident ('.' Ident)*, stopping before a '.(' selector."""
        parts = [self.eat_ident()]
        while self.op(".") and self.peek(1).type == "IDENT":
            self.eat_op(".")
            parts.append(self.eat_ident())
        return parts

    # -- an indented block of `parse_item`, incl. the leading ':' --
    def parse_indented(self, parse_item):
        self.eat_op(":")
        self.expect("NEWLINE")
        self.expect("INDENT")
        items = []
        self.skip_nl()
        while not self.is_("DEDENT") and not self.is_("EOF"):
            items.append(parse_item())
            self.skip_nl()
        self.expect("DEDENT")
        return items

    # -- Program --
    def parse_program(self):
        self.skip_nl()
        mod = None
        if self.kw("module"):
            self.eat_kw("module")
            mod = ".".join(self.parse_dotted())
            self.skip_nl()
        imports = []
        while True:
            is_pub = False
            if self.kw("pub") and self.peek(1).type == "KW" and self.peek(1).value == "import":
                self.eat_kw("pub")
                is_pub = True
            if not self.kw("import"):
                break
            self.eat_kw("import")
            parts = self.parse_dotted()
            alias, names = None, None
            if self.op("."):                       # selective: .(a, b, c)
                self.eat_op(".")
                self.eat_op("(")
                names = [self.eat_ident()]
                while self.op(","):
                    self.eat_op(",")
                    names.append(self.eat_ident())
                self.eat_op(")")
            elif self.kw("as"):                    # alias
                self.eat_kw("as")
                alias = self.eat_ident()
            imports.append(node("Import", path=".".join(parts),
                                alias=alias, names=names, pub=is_pub))
            self.skip_nl()
        decls = []
        while not self.is_("EOF"):
            decls.append(self.parse_declaration())
            self.skip_nl()
        return node("Program", module=mod, imports=imports, decls=decls)

    # -- Modifiers --
    def parse_modifiers(self):
        vis, static = None, False
        while True:
            if self.kw("pub") or self.kw("internal") or self.kw("private"):
                if vis is not None:
                    self.err("duplicate visibility modifier")
                vis = self.next().value
            elif self.kw("static"):
                self.next()
                static = True
            else:
                break
        return {"vis": vis, "static": static}

    # -- Declarations --
    def parse_declaration(self):
        mods = self.parse_modifiers()
        if self.kw("class"):
            return self.parse_class(mods)
        if self.kw("trait"):
            return self.parse_trait(mods)
        if self.kw("enum"):
            return self.parse_enum(mods)
        if self.kw("func"):
            return self.parse_func(mods)
        if self.kw("init"):
            return self.parse_module_init()
        if self.kw("var") or self.kw("const"):
            return self.parse_field(mods)
        self.err("expected a declaration (class/trait/enum/func/init/var/const)")

    def parse_generic_params(self):
        self.eat_op("<")
        params = [self.parse_generic_param()]
        while self.op(","):
            self.eat_op(",")
            params.append(self.parse_generic_param())
        self.eat_op(">")
        return params

    def parse_generic_param(self):
        name = self.eat_ident()
        bounds = []
        if self.op(":"):
            self.eat_op(":")
            bounds.append(self.parse_type())
            while self.op("+"):
                self.eat_op("+")
                bounds.append(self.parse_type())
        return node("GenericParam", name=name, bounds=bounds)

    def parse_class(self, mods):
        self.eat_kw("class")
        name = self.eat_ident()
        generics = self.parse_generic_params() if self.op("<") else []
        extends = None
        if self.kw("extends"):
            self.eat_kw("extends")
            extends = self.parse_type()
        uses = []
        if self.kw("uses"):
            self.eat_kw("uses")
            uses = self.parse_type_list()
        members = self.parse_indented(self.parse_member)
        return node("Class", mods=mods, name=name, generics=generics,
                    extends=extends, uses=uses, members=members)

    def parse_trait(self, mods):
        self.eat_kw("trait")
        name = self.eat_ident()
        generics = self.parse_generic_params() if self.op("<") else []
        uses = []
        if self.kw("uses"):
            self.eat_kw("uses")
            uses = self.parse_type_list()
        members = self.parse_indented(self.parse_trait_method)
        return node("Trait", mods=mods, name=name, generics=generics,
                    uses=uses, members=members)

    def parse_trait_method(self):
        sig = self.parse_func_sig()
        if self.op(":"):
            body = node("Block", stmts=self.parse_indented(self.parse_statement))
        else:
            body = None
        return node("TraitMethod", sig=sig, body=body)

    def parse_enum(self, mods):
        self.eat_kw("enum")
        name = self.eat_ident()
        generics = self.parse_generic_params() if self.op("<") else []
        variants = self.parse_indented(self.parse_variant)
        return node("Enum", mods=mods, name=name, generics=generics, variants=variants)

    def parse_variant(self):
        name = self.eat_ident()
        fields = []
        if self.op("("):
            self.eat_op("(")
            fields.append(self.parse_field_pair())
            while self.op(","):
                self.eat_op(",")
                fields.append(self.parse_field_pair())
            self.eat_op(")")
        return node("Variant", name=name, fields=fields)

    def parse_field_pair(self):
        name = self.eat_ident()
        self.eat_op(":")
        return node("Field", name=name, type=self.parse_type())

    def parse_member(self):
        mods = self.parse_modifiers()
        if self.kw("var") or self.kw("const"):
            return self.parse_field(mods)
        if self.kw("func"):
            return self.parse_func(mods)
        if self.kw("init"):
            return self.parse_ctor(mods)
        if self.kw("class"):
            return self.parse_class(mods)
        if self.kw("enum"):
            return self.parse_enum(mods)
        self.err("expected a class member")

    def parse_func_sig(self):
        self.eat_kw("func")
        name = self.eat_ident()
        generics = self.parse_generic_params() if self.op("<") else []
        self.eat_op("(")
        params = self.parse_params()
        self.eat_op(")")
        ret = None
        if self.op("->"):
            self.eat_op("->")
            ret = self.parse_type()
        return node("FuncSig", name=name, generics=generics, params=params, ret=ret)

    def parse_func(self, mods):
        sig = self.parse_func_sig()
        body = node("Block", stmts=self.parse_indented(self.parse_statement))
        return node("Func", mods=mods, sig=sig, body=body)

    def parse_module_init(self):
        self.eat_kw("init")
        body = node("Block", stmts=self.parse_indented(self.parse_statement))
        return node("ModuleInit", body=body)

    def parse_ctor(self, mods):
        self.eat_kw("init")
        self.eat_op("(")
        params = self.parse_params()
        self.eat_op(")")
        body = node("Block", stmts=self.parse_indented(self.parse_statement))
        return node("Ctor", mods=mods, params=params, body=body)

    def parse_params(self):
        params = []
        if not self.op(")"):
            params.append(self.parse_param())
            while self.op(","):
                self.eat_op(",")
                params.append(self.parse_param())
        return params

    def parse_param(self):
        name = self.eat_ident()
        ty = None
        if self.op(":"):
            self.eat_op(":")
            ty = self.parse_type()
        return node("Param", name=name, type=ty)

    def parse_field(self, mods):
        if self.kw("var"):
            return node("Field", mods=mods, **self.parse_var_decl())
        return node("Field", mods=mods, **self.parse_const_decl())

    def parse_var_decl(self):
        self.eat_kw("var")
        name = self.eat_ident()
        ty = None
        if self.op(":"):
            self.eat_op(":")
            ty = self.parse_type()
        init = None
        if self.op("="):
            self.eat_op("=")
            init = self.parse_expr()
        return {"binding": "var", "name": name, "type": ty, "init": init}

    def parse_const_decl(self):
        self.eat_kw("const")
        name = self.eat_ident()
        ty = None
        if self.op(":"):
            self.eat_op(":")
            ty = self.parse_type()
        self.eat_op("=")  # const MUST be initialized
        init = self.parse_expr()
        return {"binding": "const", "name": name, "type": ty, "init": init}

    # -- Types --
    def parse_type(self):
        if self.kw("func"):
            self.eat_kw("func")
            self.eat_op("(")
            params = []
            if not self.op(")"):
                params.append(self.parse_type())
                while self.op(","):
                    self.eat_op(",")
                    params.append(self.parse_type())
            self.eat_op(")")
            self.eat_op("->")
            return node("FuncType", params=params, ret=self.parse_type())
        name = self.eat_ident()
        args = []
        if self.op("<"):
            self.eat_op("<")
            args.append(self.parse_type())
            while self.op(","):
                self.eat_op(",")
                args.append(self.parse_type())
            self.eat_op(">")
        return node("Type", name=name, args=args)

    def parse_type_list(self):
        types = [self.parse_type()]
        while self.op(","):
            self.eat_op(",")
            types.append(self.parse_type())
        return types

    # -- Statements --
    def parse_statement(self):
        if self.kw("if"):
            return self.parse_if()
        if self.kw("while"):
            return self.parse_while()
        if self.kw("for"):
            return self.parse_for()
        if self.kw("match"):
            return self.parse_match()          # compound; ends with DEDENT
        if self.kw("try"):
            return self.parse_try()
        # simple statements: caller's skip_nl consumes the trailing NEWLINE
        if self.kw("var"):
            return node("VarStmt", **self.parse_var_decl())
        if self.kw("const"):
            return node("ConstStmt", **self.parse_const_decl())
        if self.kw("return"):
            return self.parse_return()
        if self.kw("break"):
            return node("Break", line=self.eat_kw("break").line)
        if self.kw("continue"):
            return node("Continue", line=self.eat_kw("continue").line)
        if self.kw("raise"):
            t = self.eat_kw("raise")
            return node("Raise", value=self.parse_expr(), line=t.line)
        if self.kw("pass"):
            self.next()
            return node("Pass")
        expr = self.parse_expr()
        if self.op("="):
            self.eat_op("=")
            if expr["kind"] not in ("Ident", "Member", "Index"):
                self.err("left side of assignment is not assignable")
            return node("Assign", target=expr, value=self.parse_expr())
        return node("ExprStmt", expr=expr)

    def parse_if(self):
        self.eat_kw("if")
        cond = self.parse_expr()
        then = node("Block", stmts=self.parse_indented(self.parse_statement))
        elifs = []
        while self.kw("elif"):
            self.eat_kw("elif")
            c = self.parse_expr()
            b = node("Block", stmts=self.parse_indented(self.parse_statement))
            elifs.append((c, b))
        els = None
        if self.kw("else"):
            self.eat_kw("else")
            els = node("Block", stmts=self.parse_indented(self.parse_statement))
        return node("If", cond=cond, then=then, elifs=elifs, els=els)

    def parse_while(self):
        self.eat_kw("while")
        cond = self.parse_expr()
        body = node("Block", stmts=self.parse_indented(self.parse_statement))
        return node("While", cond=cond, body=body)

    def parse_for(self):
        self.eat_kw("for")
        var = self.eat_ident()
        self.eat_kw("in")
        it = self.parse_expr()
        body = node("Block", stmts=self.parse_indented(self.parse_statement))
        return node("For", var=var, iter=it, body=body)

    def parse_try(self):
        self.eat_kw("try")
        body = node("Block", stmts=self.parse_indented(self.parse_statement))
        self.eat_kw("catch")
        name = self.eat_ident()
        handler = node("Block", stmts=self.parse_indented(self.parse_statement))
        return node("Try", body=body, name=name, handler=handler)

    def parse_return(self):
        self.eat_kw("return")
        val = self.parse_expr() if self.can_start_expr() else None
        return node("Return", value=val)

    def can_start_expr(self):
        t = self.peek()
        if t.type in ("INT", "FLOAT", "STRING", "IDENT"):
            return True
        if t.type == "KW" and t.value in ("true", "false", "match", "func"):
            return True
        if t.type == "OP" and t.value in ("(", "[", "!", "-"):
            return True
        return False

    # -- match --
    def parse_match(self):
        self.eat_kw("match")
        subject = self.parse_expr()
        arms = self.parse_indented(self.parse_arm)
        return node("Match", subject=subject, arms=arms)

    def parse_arm(self):
        pats = [self.parse_pattern()]
        while self.op(","):
            self.eat_op(",")
            pats.append(self.parse_pattern())
        self.eat_op(":")
        if self.is_("NEWLINE"):            # indented multi-statement body
            self.next()
            self.expect("INDENT")
            stmts = []
            self.skip_nl()
            while not self.is_("DEDENT") and not self.is_("EOF"):
                stmts.append(self.parse_statement())
                self.skip_nl()
            self.expect("DEDENT")
            body = node("Block", stmts=stmts)
        else:                              # inline expression body
            body = self.parse_expr()
        return node("Arm", patterns=pats, body=body)

    def parse_pattern(self):
        t = self.peek()
        if t.type == "IDENT" and t.value == "_":
            self.next()
            return node("Wildcard")
        if t.type in ("INT", "FLOAT", "STRING") or (t.type == "KW" and t.value in ("true", "false")):
            self.next()
            return node("LitPattern", type=t.type, value=t.value)
        if t.type == "IDENT":
            name = self.eat_ident()
            bindings = []
            if self.op("("):
                self.eat_op("(")
                bindings.append(self.eat_ident())
                while self.op(","):
                    self.eat_op(",")
                    bindings.append(self.eat_ident())
                self.eat_op(")")
            return node("VariantPattern", name=name, bindings=bindings)
        self.err("expected a pattern")

    # -- Expressions (precedence-layered) --
    def parse_expr(self):
        return self.parse_or()

    def _binary(self, sub, ops):
        left = sub()
        while self.peek().type == "OP" and self.peek().value in ops:
            opv = self.next().value
            left = node("Binary", op=opv, left=left, right=sub())
        return left

    def parse_or(self):
        return self._binary(self.parse_and, {"||"})

    def parse_and(self):
        return self._binary(self.parse_eq, {"&&"})

    def parse_eq(self):
        return self._binary(self.parse_cmp, {"==", "!="})

    def parse_cmp(self):
        return self._binary(self.parse_add, {"<", "<=", ">", ">="})

    def parse_add(self):
        return self._binary(self.parse_mul, {"+", "-"})

    def parse_mul(self):
        return self._binary(self.parse_unary, {"*", "/", "%"})

    def parse_unary(self):
        if self.op("!") or self.op("-"):
            opv = self.next().value
            return node("Unary", op=opv, operand=self.parse_unary())
        return self.parse_postfix()

    def parse_postfix(self):
        e = self.parse_primary()
        while True:
            if self.op("("):
                lp = self.eat_op("(")
                args = []
                if not self.op(")"):
                    args.append(self.parse_expr())
                    while self.op(","):
                        self.eat_op(",")
                        args.append(self.parse_expr())
                self.eat_op(")")
                e = node("Call", callee=e, args=args, line=lp.line)
            elif self.op("."):
                dot = self.eat_op(".")
                e = node("Member", obj=e, field=self.eat_ident(), line=dot.line)
            elif self.op("["):
                lb = self.eat_op("[")
                idx = self.parse_expr()
                self.eat_op("]")
                e = node("Index", obj=e, index=idx, line=lb.line)
            else:
                return e

    def parse_primary(self):
        t = self.peek()
        if t.type in ("INT", "FLOAT", "STRING"):
            self.next()
            return node("Lit", type=t.type, value=t.value, line=t.line)
        if t.type == "KW" and t.value in ("true", "false"):
            self.next()
            return node("Lit", type="BOOL", value=t.value, line=t.line)
        if self.kw("match"):
            return self.parse_match()
        if self.kw("func"):
            return self.parse_closure()
        if self.op("("):
            self.eat_op("(")
            e = self.parse_expr()
            self.eat_op(")")
            return e
        if self.op("["):
            self.eat_op("[")
            elems = []
            if not self.op("]"):
                elems.append(self.parse_expr())
                while self.op(","):
                    self.eat_op(",")
                    if self.op("]"):
                        break
                    elems.append(self.parse_expr())
            self.eat_op("]")
            return node("List", elems=elems)
        if t.type == "IDENT":
            return node("Ident", name=self.next().value, line=t.line)
        self.err("expected an expression")

    def parse_closure(self):
        self.eat_kw("func")
        self.eat_op("(")
        params = self.parse_params()
        self.eat_op(")")
        ret = None
        if self.op("->"):
            self.eat_op("->")
            ret = self.parse_type()
        self.eat_op("=>")
        return node("Closure", params=params, ret=ret, body=self.parse_expr())


# ----------------------------------------------------------------------
# AST pretty-printer
# ----------------------------------------------------------------------

def dump(n, indent=0):
    pad = "  " * indent
    if isinstance(n, dict):
        kind = n.get("kind", "?")
        scalars, children = [], []
        for k, v in n.items():
            if k in ("kind", "line"):
                continue
            if isinstance(v, dict) or (isinstance(v, list) and any(isinstance(x, (dict, list, tuple)) for x in v)):
                children.append((k, v))
            elif isinstance(v, tuple):
                children.append((k, list(v)))
            else:
                scalars.append(f"{k}={v!r}")
        print(pad + kind + (("  " + " ".join(scalars)) if scalars else ""))
        for k, v in children:
            print(pad + "  ." + k + ":")
            dump(v, indent + 2)
    elif isinstance(n, (list, tuple)):
        if not n:
            print(pad + "[]")
        for x in n:
            dump(x, indent)
    else:
        print(pad + repr(n))


# ----------------------------------------------------------------------
# main
# ----------------------------------------------------------------------

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "demo/prog.rg"
    with open(path, "r") as f:
        src = f.read()

    try:
        toks = lex(src)
    except LexError as e:
        print(f"LEX ERROR in {path}: {e}")
        return 1

    parser = Parser(toks)
    try:
        ast = parser.parse_program()
    except ParseError as e:
        print(f"PARSE ERROR in {path}: {e}")
        return 1

    if not parser.is_("EOF"):
        t = parser.peek()
        print(f"PARSE ERROR in {path}: line {t.line} col {t.col}: trailing tokens ({t!r})")
        return 1

    print(f"AST for {path}:\n")
    dump(ast)
    print(f"\nOK  parsed {len(toks)} tokens; grammar accepts {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
