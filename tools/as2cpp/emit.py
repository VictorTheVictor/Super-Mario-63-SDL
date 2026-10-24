"""Emits C++ for parsed AS2 scripts, targeting the as:: runtime and game/Prelude.h.

The output aims to read like the original ActionScript:

    _root["gravity"] = _root[a + "Gravity"];      // _root.gravity = _root[a+"Gravity"];
    if (scope["attack"] == true) { ... }          // if (attack == true) { ... }
    scope.self["_x"] += 5;                         // this._x += 5;
"""

import math

CPP_RESERVED = set("""
alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t
char32_t class compl concept const consteval constexpr constinit const_cast continue co_await
co_return co_yield decltype default delete do double dynamic_cast else enum explicit export
extern false float for friend goto if inline int long mutable namespace new noexcept not not_eq
nullptr operator or or_eq private protected public register reinterpret_cast requires return
short signed sizeof static static_assert static_cast struct switch template this thread_local
throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while
xor xor_eq NULL main errno assert
""".split())

# Names the prelude defines; locals with these names get renamed.
PRELUDE_NAMES = {"scope", "self", "undefined", "obj", "arr", "new_", "Value", "Ref", "Args",
                 "Activation", "With", "TellTarget", "forIn", "caseIndex", "typeOf",
                 "logicalAnd", "logicalOr", "strictEquals", "strictNotEquals", "deleteMember",
                 "instanceOf", "int_", "callGlobal"}

# Global objects/functions exposed as prelude objects (resolve through _global).
PRELUDE_GLOBALS = {
    "Math", "Key", "Mouse", "Stage", "System", "SharedObject", "String", "Number", "Boolean",
    "Array", "Object", "Sound", "Color", "LoadVars", "XML", "ContextMenu", "ContextMenuItem",
    "MovieClip", "TextField", "Button", "Function", "trace", "random", "getTimer", "setInterval",
    "clearInterval", "isNaN", "isFinite", "parseInt", "parseFloat", "escape", "unescape",
    "getURL", "fscommand", "targetPath", "chr", "ord", "updateAfterEvent", "getVersion",
    "NaN", "Infinity",
}
RENAMED_GLOBALS = {"int": "int_"}

# Unqualified timeline opcodes: act on the current target (AVM1 ActionStop etc.).
TIMELINE_FUNCS = {"stop", "play", "gotoAndStop", "gotoAndPlay", "nextFrame", "prevFrame",
                  "stopAllSounds"}

CLIP_EVENTS = {
    "load": "Load", "enterFrame": "EnterFrame", "unload": "Unload", "mouseMove": "MouseMove",
    "mouseDown": "MouseDown", "mouseUp": "MouseUp", "keyDown": "KeyDown", "keyUp": "KeyUp",
    "data": "Data", "initialize": "Initialize", "construct": "Construct", "press": "Press",
    "release": "Release", "releaseOutside": "ReleaseOutside", "rollOver": "RollOver",
    "rollOut": "RollOut", "dragOver": "DragOver", "dragOut": "DragOut", "keyPress": "KeyPress",
}
BUTTON_EVENTS = {
    "press": "Press", "release": "Release", "releaseOutside": "ReleaseOutside",
    "rollOver": "RollOver", "rollOut": "RollOut", "dragOver": "DragOver", "dragOut": "DragOut",
}

ARITH = {"+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>", ">>>"}
COMPARE = {"==", "!=", "<", ">", "<=", ">="}


class EmitError(Exception):
    pass


def cpp_string(s):
    out = ['"']
    for ch in s:
        o = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif o < 32 or o == 127:
            out.append("\\%03o" % o)
        elif ch == "?" and out[-1].endswith("?"):
            out.append("\\?")  # never form a trigraph
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def cpp_number(v, text=None):
    if text and text[:2].lower() == "0x" and v < 2 ** 31:
        return text
    if math.isnan(v):
        return "NaN"
    if math.isinf(v):
        return "Infinity" if v > 0 else "-Infinity"
    if v == int(v) and abs(v) < 2 ** 31:
        return str(int(v))
    r = repr(v)
    if "e" not in r and "." not in r:
        r += ".0"
    return r


def sanitize(name):
    n = name.replace("$", "_S_")
    if n in CPP_RESERVED or n in PRELUDE_NAMES or n in PRELUDE_GLOBALS or n in TIMELINE_FUNCS \
            or n.startswith("__"):
        n += "_"
    return n


# ---------------------------------------------------------------- variable analysis

def walk(node, fn, into_functions=False):
    """Calls fn on every AST node under `node` (not descending into nested functions
    unless asked)."""
    if node is None:
        return
    if isinstance(node, list):
        for x in node:
            walk(x, fn, into_functions)
        return
    if isinstance(node, tuple):
        for x in node:
            walk(x, fn, into_functions)
        return
    if not hasattr(node, "kind"):
        return
    fn(node)
    if node.kind in ("function", "funcdecl") and not into_functions:
        return
    for v in node.a.values():
        walk(v, fn, into_functions)


def declared_vars(body):
    """var-declared names and nested function declarations of a function body."""
    names = []

    def visit(n):
        if n.kind == "var":
            names.extend(d[0] for d in n.decls)
        elif n.kind == "forin" and n.declared:
            names.append(n.var)
        elif n.kind == "funcdecl":
            names.append(n.name)
        elif n.kind == "for" and n.init is not None and n.init.kind == "var":
            names.extend(d[0] for d in n.init.decls)
    for s in body:
        walk(s, visit)
    seen, out = set(), []
    for n in names:
        if n not in seen:
            seen.add(n)
            out.append(n)
    return out


def free_names(fn_node):
    """Names referenced inside a function (including nested ones) that it does not
    declare itself."""
    own = set(fn_node.params) | set(declared_vars(fn_node.body.body))
    used = set()

    def visit(n):
        if n.kind == "name":
            used.add(n.name)
        elif n.kind in ("function", "funcdecl") and n is not fn_node:
            used.update(free_names(n))
        elif n.kind == "forin":
            used.add(n.var)
    walk(fn_node.body, visit)
    return used - own


# ---------------------------------------------------------------- emitter

class Context:
    def __init__(self, kind, parent=None):
        self.kind = kind          # 'script' | 'function'
        self.parent = parent
        self.locals = {}          # AS name -> C++ name (plain C++ locals)
        self.captured = set()     # function vars living in the activation object

    def lookup(self, name):
        c = self
        while c:
            if name in c.locals:
                return ("local", c.locals[name]) if c is self else ("outer", name)
            if name in c.captured:
                return ("captured", name)
            c = c.parent
        return (None, None)


class Emitter:
    def __init__(self):
        self.lines = []
        self.indent = 0
        self.unsupported = []
        self.tmp = 0
        self.loops = 0     # enclosing loops in the current function body
        self.switches = 0  # enclosing switch statements

    def w(self, text=""):
        self.lines.append(("    " * self.indent + text) if text else "")

    def fresh(self, base):
        self.tmp += 1
        return f"{base}{self.tmp}"

    # -- comments
    def comments(self, items):
        for c in items:
            if c is None:
                if self.lines and self.lines[-1] != "" and not self.lines[-1].rstrip().endswith("{"):
                    self.w()
            else:
                for i, part in enumerate(c.split("\n")):
                    self.w(part.strip() if i else part)

    # -- expressions: return (code, type); types: num str bool null undef value ref global cbool
    def expr(self, n, ctx, boolean=False):
        k = n.kind
        if k == "num":
            return cpp_number(n.value, n.text), "num"
        if k == "str":
            return cpp_string(n.value), "str"
        if k == "bool":
            return ("true" if n.value else "false"), "bool"
        if k == "null":
            return "Value::null()", "value"
        if k == "undefined":
            return "undefined", "value"
        if k == "this":
            return "scope.self", "value"
        if k == "name":
            return self.name(n.name, ctx)
        if k == "paren":
            code, t = self.expr(n.expr, ctx, boolean)
            return f"({code})", t
        if k == "member":
            obj = self.as_value(n.obj, ctx)
            return f"{obj}[{cpp_string(n.name)}]", "ref"
        if k == "index":
            obj = self.as_value(n.obj, ctx)
            key, kt = self.expr(n.key, ctx)
            return f"{obj}[{key}]", "ref"
        if k == "call":
            return self.call(n, ctx)
        if k == "new":
            ctor, _ = self.expr(n.ctor, ctx)
            args = [self.expr(a, ctx)[0] for a in n.args]
            return f"new_({', '.join([ctor] + args)})", "value"
        if k == "array":
            return f"arr({{{', '.join(self.expr(a, ctx)[0] for a in n.items)}}})", "value"
        if k == "object":
            props = ", ".join(f"{{{cpp_string(key)}, {self.expr(v, ctx)[0]}}}" for key, v in n.props)
            return f"obj({{{props}}})", "value"
        if k == "function":
            return self.function_expr(n, ctx), "value"
        if k == "assign":
            return self.assign(n, ctx), "value"
        if k == "unary":
            return self.unary(n, ctx, boolean)
        if k in ("preinc", "postinc"):
            target = self.lvalue(n.arg, ctx)
            return (f"{n.op}{target}" if k == "preinc" else f"{target}{n.op}"), "value"
        if k == "typeof":
            return f"typeOf({self.expr(n.arg, ctx)[0]})", "value"
        if k == "void":
            return f"((void)({self.expr(n.arg, ctx)[0]}), undefined)", "value"
        if k == "delete":
            a = n.arg
            if a.kind == "member":
                return f"deleteMember({self.as_value(a.obj, ctx)}, {cpp_string(a.name)})", "cbool"
            if a.kind == "index":
                return f"deleteMember({self.as_value(a.obj, ctx)}, {self.expr(a.key, ctx)[0]})", "cbool"
            if a.kind == "name":
                return f"scope.deleteVariable({cpp_string(a.name)})", "cbool"
            return "false", "bool"
        if k == "comma":
            return "(" + ", ".join(self.expr(i, ctx)[0] for i in n.items) + ")", "value"
        if k == "cond":
            c, _ = self.expr(n.cond, ctx, boolean=True)
            a, at = self.expr(n.yes, ctx)
            b, bt = self.expr(n.no, ctx)
            return f"({c} ? Value({a}) : Value({b}))", "value"
        if k == "binary":
            return self.binary(n, ctx, boolean)
        raise EmitError(f"unsupported expression {k}")

    def as_value(self, n, ctx):
        """Code for a member-access base; wraps C++ literals in Value(...)."""
        code, t = self.expr(n, ctx)
        if t in ("value", "ref", "global"):
            return code
        return f"Value({code})"

    def name(self, nm, ctx):
        where, cname = ctx.lookup(nm)
        if where == "local":
            return cname, "value"
        if where in ("captured", "outer"):
            return f"scope[{cpp_string(nm)}]", "ref"
        if nm in ("_root", "_level0"):
            return "_root", "global"
        if nm == "arguments" and ctx.kind == "function":
            return "scope.arguments()", "value"
        if nm in PRELUDE_GLOBALS:
            return nm, "global"
        if nm in RENAMED_GLOBALS:
            return RENAMED_GLOBALS[nm], "global"
        return f"scope[{cpp_string(nm)}]", "ref"

    def lvalue(self, n, ctx):
        if n.kind == "paren":
            return self.lvalue(n.expr, ctx)
        code, t = self.expr(n, ctx)
        if t == "global":  # assigning to a global name: go through the scope chain
            return f"scope[{cpp_string(n.name)}]"
        if t not in ("value", "ref"):
            raise EmitError("assignment to a non-variable")
        return code

    def assign(self, n, ctx):
        target = self.lvalue(n.target, ctx)
        value, vt = self.expr(n.value, ctx)
        op = n.op
        if op in ("=", "+=", "-=", "*=", "/=", "%="):
            return f"{target} {op} {value}"
        base = op[:-1]
        fn = {"<<": "<<", ">>": ">>", "&": "&", "|": "|", "^": "^"}.get(base)
        if fn:
            return f"{target} = {target} {fn} {self.wrap(value, vt)}"
        return f"{target} = Value(double({target}.get().toUint32() >> ({value} & 31).toInt32()))"

    def wrap(self, code, t):
        return code if t in ("value", "ref", "global") else f"Value({code})"

    def unary(self, n, ctx, boolean):
        a, t = self.expr(n.arg, ctx, boolean=(n.op == "!"))
        if n.op == "-" and t == "num":
            return (a[1:] if a.startswith("-") else f"-{a}"), "num"
        if n.op == "!":
            if t in ("cbool", "bool"):
                return f"!{a}", "cbool"
            if boolean:
                return f"!{a}", "cbool"
            return f"!{self.wrap(a, t)}", "value"
        return f"{n.op}{self.wrap(a, t)}", "value"

    def binary(self, n, ctx, boolean):
        op = n.op
        if op in ("&&", "||"):
            if boolean:
                a, _ = self.expr(n.left, ctx, boolean=True)
                b, _ = self.expr(n.right, ctx, boolean=True)
                return f"{a} {op} {b}", "cbool"
            a, at = self.expr(n.left, ctx)
            b, bt = self.expr(n.right, ctx)
            fn = "logicalAnd" if op == "&&" else "logicalOr"
            return f"{fn}({self.wrap(a, at)}, [&] {{ return Value({b}); }})", "value"
        a, at = self.expr(n.left, ctx)
        b, bt = self.expr(n.right, ctx)
        if op in ("===", "!=="):
            fn = "strictEquals" if op == "===" else "strictNotEquals"
            return f"{fn}({a}, {b})", "cbool"
        if op == "instanceof":
            return f"instanceOf({a}, {b})", "cbool"
        if op == "in":
            return f"hasProperty({self.wrap(b, bt)}.object(), Value({a}).toAtom())", "cbool"
        values = ("value", "ref", "global")
        if op in ARITH and at == "num" and bt == "num":
            folded = fold(op, n.left, n.right)
            if folded is not None:
                return folded, "num"
        if at not in values and bt not in values:
            a = f"Value({a})"
        if op == ">>>":
            return f"Value(double(Value({a}).toUint32() >> (Value({b}).toUint32() & 31)))", "value"
        if op in COMPARE:
            return f"{a} {op} {b}", "cbool"
        return f"{a} {op} {b}", "value"

    def call(self, n, ctx):
        args = [self.expr(a, ctx)[0] for a in n.args]
        fn = n.fn
        while fn.kind == "paren":
            fn = fn.expr
        if fn.kind == "name":
            where, _ = ctx.lookup(fn.name)
            if where is None and fn.name in TIMELINE_FUNCS:
                return f"{fn.name}({', '.join(args)})", "value"
        code, t = self.expr(fn, ctx)
        if t not in ("value", "ref", "global"):
            raise EmitError("call of a literal")
        return f"{code}({', '.join(args)})", "value"

    # -- functions
    def function_expr(self, n, ctx):
        inner = self.function_context(n, ctx)
        saved = self.lines, self.indent, self.loops, self.switches
        self.lines, self.indent, self.loops, self.switches = [], saved[1] + 1, 0, 0
        self.function_prologue(n, inner)
        self.body(n.body.body, inner)
        self.comments(n.body.tail)
        body_lines = self.lines
        self.lines, self.indent, self.loops, self.switches = saved
        pad = "    " * self.indent
        text = "scope.function([](Activation& scope) -> Value {\n"
        text += "\n".join(body_lines) + ("\n" if body_lines else "")
        ends_with_return = bool(n.body.body) and n.body.body[-1].kind == "return"
        if not ends_with_return:
            text += pad + "    return undefined;\n"
        text += pad + "})"
        return text

    def function_context(self, n, outer):
        c = Context("function", outer)
        names = list(n.params) + [v for v in declared_vars(n.body.body) if v not in n.params]
        # Locals referenced by nested functions must live in the activation object.
        captured = set()

        def visit(x):
            if x.kind in ("function", "funcdecl"):
                captured.update(free_names(x) & set(names))
        walk(n.body, visit)
        # walk() stops at nested functions, so visit them explicitly.
        for s in n.body.body:
            walk(s, lambda x: captured.update(free_names(x) & set(names))
                 if x.kind in ("function", "funcdecl") else None)
        for v in names:
            if v in captured:
                c.captured.add(v)
            else:
                c.locals[v] = sanitize(v)
        c.params = list(n.params)
        return c

    def function_prologue(self, n, c):
        for i, p in enumerate(n.params):
            if p in c.captured:
                self.w(f"scope.var({cpp_string(p)}) = scope.args[{i}];")
            else:
                self.w(f"Value {c.locals[p]} = scope.args[{i}];")
        # Hoist locals whose C++ declaration would not cover all their uses.
        c.inline_decl = self.inline_declarations(n.body.body, c)
        for v in sorted(c.captured - set(n.params) - c.inline_decl):
            self.w(f"scope.var({cpp_string(v)});")
        hoisted = [c.locals[v] for v in c.locals if v not in n.params and v not in c.inline_decl]
        if hoisted:
            self.w(f"Value {', '.join(hoisted)};")

    @staticmethod
    def inline_declarations(body, c):
        """Locals that can be declared where their (single, top-level) `var` is."""
        counts = {}

        def count(x):
            if x.kind == "var":
                for d in x.decls:
                    counts[d[0]] = counts.get(d[0], 0) + 1
            elif x.kind == "forin" and x.declared:
                counts[x.var] = counts.get(x.var, 0) + 2  # never inline
            elif x.kind == "funcdecl":
                counts[x.name] = counts.get(x.name, 0) + 1
            elif x.kind == "for" and x.init is not None and x.init.kind == "var":
                for d in x.init.decls:
                    counts[d[0]] = counts.get(d[0], 0) + 2
        for s in body:
            walk(s, count)
        ok = set()
        seen = set()
        for s in body:
            names_here = set()
            if s.kind == "var":
                names_here = {d[0] for d in s.decls}
            elif s.kind == "funcdecl":
                names_here = {s.name}
            for nm in names_here:
                if (nm in c.locals or nm in c.captured) and counts.get(nm) == 1 and nm not in seen:
                    ok.add(nm)
            walk(s, lambda x: seen.add(x.name) if x.kind == "name" else None, into_functions=True)
        return ok

    # -- statements
    def body(self, stmts, ctx):
        # Function declarations used before they appear are hoisted (Flash does this).
        used_before = set()
        hoist = []
        seen_names = set()
        for s in stmts:
            if s.kind == "funcdecl" and s.name in seen_names:
                hoist.append(s)
            walk(s, lambda x: seen_names.add(x.name) if x.kind == "name" else None, into_functions=True)
        for s in hoist:
            self.stmt(s, ctx)
        for s in stmts:
            if s in hoist:
                continue
            self.stmt(s, ctx)

    def block_body(self, block, ctx):
        self.body(block.body, ctx)
        self.comments(block.a.get("tail", []))

    def open_block(self, head, block, ctx, trailing=None, loop=False):
        self.w(head + " {" + (f" {trailing}" if trailing else ""))
        self.indent += 1
        self.loops += loop
        self.block_body(block, ctx)
        self.loops -= loop
        self.indent -= 1

    def stmt(self, s, ctx):
        self.comments(s.before)
        start = len(self.lines)
        self._stmt(s, ctx)
        if s.trailing and len(self.lines) > start:
            self.lines[-1] += " " + s.trailing

    def var_target(self, name, ctx):
        where, cname = ctx.lookup(name)
        if where == "local":
            return cname
        return f"scope.var({cpp_string(name)})"

    def _stmt(self, s, ctx):
        k = s.kind
        if k == "expr":
            code, _ = self.expr(s.expr, ctx)
            self.w(code + ";")
        elif k == "var":
            parts = []
            inline = getattr(ctx, "inline_decl", set())
            for name, init in s.decls:
                where, cname = ctx.lookup(name)
                if where == "local" and name in inline:
                    parts.append(f"Value {cname}" + (f" = {self.expr(init, ctx)[0]}" if init else ""))
                elif where == "local":
                    if init is not None:
                        parts.append(f"{cname} = {self.expr(init, ctx)[0]}")
                else:
                    tgt = f"scope.var({cpp_string(name)})"
                    parts.append(f"{tgt} = {self.expr(init, ctx)[0]}" if init is not None else tgt)
            for p in parts:
                self.w(p + ";")
        elif k == "funcdecl":
            fn = self.function_expr(s, ctx)
            where, cname = ctx.lookup(s.name)
            inline = getattr(ctx, "inline_decl", set())
            if where == "local":
                lhs = f"Value {cname}" if s.name in inline else cname
            else:
                lhs = f"scope.var({cpp_string(s.name)})"
            self.w(f"{lhs} = {fn};")
        elif k == "block":
            if s.braces:
                self.open_block("", s, ctx)
                self.w("}")
            else:
                self.block_body(s, ctx)
        elif k == "empty":
            pass
        elif k == "if":
            self.if_chain(s, ctx, "if")
        elif k == "while":
            cond, _ = self.expr(s.cond, ctx, boolean=True)
            self.open_block(f"while ({cond})", s.body, ctx, loop=True)
            self.w("}")
        elif k == "dowhile":
            self.open_block("do", s.body, ctx, loop=True)
            cond, _ = self.expr(s.cond, ctx, boolean=True)
            self.w(f"}} while ({cond});")
        elif k == "for":
            init = ""
            if s.init is not None:
                if s.init.kind == "var":
                    parts = []
                    for name, iv in s.init.decls:
                        tgt = self.var_target(name, ctx)
                        if iv is not None:
                            parts.append(f"{tgt} = {self.expr(iv, ctx)[0]}")
                    init = ", ".join(parts)
                else:
                    init = self.expr(s.init.expr, ctx)[0]
            cond = self.expr(s.cond, ctx, boolean=True)[0] if s.cond else ""
            update = self.expr(s.update, ctx)[0] if s.update else ""
            self.open_block(f"for ({init}; {cond}; {update})", s.body, ctx, loop=True)
            self.w("}")
        elif k == "forin":
            obj = self.expr(s.obj, ctx)[0]
            key = self.fresh("key")
            self.w(f"for (Value {key} : forIn({obj})) {{")
            self.indent += 1
            where, cname = ctx.lookup(s.var)
            target = cname if where == "local" else (
                f"scope.var({cpp_string(s.var)})" if s.declared else f"scope[{cpp_string(s.var)}]")
            self.w(f"{target} = {key};")
            self.loops += 1
            self.block_body(s.body, ctx)
            self.loops -= 1
            self.indent -= 1
            self.w("}")
        elif k == "switch":
            disc = self.expr(s.disc, ctx)[0]
            tests = [self.expr(t, ctx)[0] for t, _, _ in s.cases if t is not None]
            self.w(f"switch (caseIndex({disc}, {{{', '.join(tests)}}})) {{")
            i = 0
            for test, body, before in s.cases:
                self.comments(before)
                if test is None:
                    self.w("default:")
                else:
                    self.w(f"case {i}:")
                    i += 1
                self.indent += 1
                self.switches += 1
                self.body(body, ctx)
                self.switches -= 1
                self.indent -= 1
            self.comments(s.tail)
            self.w("}")
        elif k in ("break", "continue"):
            if self.loops or (k == "break" and self.switches):
                self.w(k + ";")
            else:
                # Flash compiles a break/continue outside any loop as a jump to the end
                # of the function or script.
                ret = "return undefined;" if ctx.kind == "function" else "return;"
                self.w(f"{ret}  // was '{k}' outside a loop")
        elif k == "return":
            if ctx.kind == "function":
                self.w(f"return {self.expr(s.value, ctx)[0]};" if s.value is not None else "return undefined;")
            elif s.value is not None:
                self.w(f"{self.expr(s.value, ctx)[0]};")
                self.w("return;")
            else:
                self.w("return;")
        elif k == "with":
            target = self.expr(s.target, ctx)[0]
            self.w("{")
            self.indent += 1
            self.w(f"With {self.fresh('with')}(scope, {target});")
            self.block_body(s.body, ctx)
            self.indent -= 1
            self.w("}")
        elif k == "tellTarget":
            target = self.expr(s.target, ctx)[0]
            self.w("{")
            self.indent += 1
            self.w(f"TellTarget {self.fresh('tellTarget')}(scope, {target});")
            self.block_body(s.body, ctx)
            self.indent -= 1
            self.w("}")
        elif k == "ifFrameLoaded":
            self.w("{  // ifFrameLoaded: everything is always loaded")
            self.indent += 1
            self.block_body(s.body, ctx)
            self.indent -= 1
            self.w("}")
        elif k == "directive":
            self.unsupported.append(s.text)
            self.w(f"// {s.text}  (unsupported directive, ignored)")
        elif k == "handler":
            raise EmitError("event handler outside an instance script")
        else:
            raise EmitError(f"unsupported statement {k}")

    def if_chain(self, s, ctx, keyword):
        cond, _ = self.expr(s.cond, ctx, boolean=True)
        if s.then.braces:
            self.open_block(f"{keyword} ({cond})", s.then, ctx)
            closer = "}"
        else:
            self.w(f"{keyword} ({cond}) {{")
            self.indent += 1
            self.block_body(s.then, ctx)
            self.indent -= 1
            closer = "}"
        other = s.other
        if other is None:
            self.w(closer)
            return
        if other.kind == "if":
            self.comments(other.before)
            self.lines.append("    " * self.indent + closer + " ")
            # Continue on the same line: "} else if (...) {"
            mark = len(self.lines) - 1
            self.if_chain(other, ctx, "else if")
            self.lines[mark] = self.lines[mark] + self.lines.pop(mark + 1).lstrip()
            return
        self.w(closer + " else {")
        self.indent += 1
        self.block_body(other, ctx)
        self.indent -= 1
        self.w("}")


def literal_value(n):
    """Numeric value of a literal expression (5, -5, (2), -(3)), else None."""
    if n.kind == "num":
        return n.value
    if n.kind == "paren":
        return literal_value(n.expr)
    if n.kind == "unary" and n.op in ("-", "+"):
        v = literal_value(n.arg)
        return None if v is None else (-v if n.op == "-" else v)
    if n.kind == "binary" and n.op in ("+", "-", "*", "/", "%"):
        a, b = literal_value(n.left), literal_value(n.right)
        if a is None or b is None:
            return None
        r = fold(n.op, n.left, n.right)
        return None if r is None else float(r) if r not in ("NaN", "Infinity", "-Infinity") else None
    return None


def fold(op, left, right):
    a, b = literal_value(left), literal_value(right)
    if a is None or b is None:
        return None
    try:
        if op == "+":
            v = a + b
        elif op == "-":
            v = a - b
        elif op == "*":
            v = a * b
        elif op == "/":
            v = a / b if b != 0 else (math.copysign(math.inf, a) if a else math.nan)
        elif op == "%":
            v = math.fmod(a, b) if b != 0 else math.nan
        else:
            return None
    except OverflowError:
        return None
    return cpp_number(v)


# ---------------------------------------------------------------- scripts

def emit_script(program, fn_name, origin):
    """A frame script as `static void fn(Activation& scope)`. Returns (lines, unsupported)."""
    e = Emitter()
    e.w(f"// {origin}")
    e.w(f"void {fn_name}(Activation& scope) {{")
    e.indent = 1
    ctx = Context("script")
    e.body(program.body, ctx)
    e.comments(program.tail)
    e.indent = 0
    e.w("}")
    return e.lines, e.unsupported


def emit_handlers(program, base_name, origin, button):
    """Instance scripts: one function per onClipEvent()/on() block.
    Returns [(fn_name, mask_expression, lines)], unsupported."""
    out = []
    unsupported = []
    pending = []
    for i, s in enumerate(program.body):
        if s.kind != "handler":
            if s.kind == "empty":
                continue
            raise EmitError(f"statement outside an event handler ({s.kind})")
        e = Emitter()
        e.comments(pending + s.before)
        events = []
        table = BUTTON_EVENTS if button else CLIP_EVENTS
        prefix = "buttoncond::" if button else "clipevent::"
        for name, key in s.events:
            if name not in table:
                unsupported.append(f"{s.which}({name})")
                continue
            if name == "keyPress":
                unsupported.append(f"keyPress {key}")
            events.append(prefix + table[name])
        if not events:
            continue
        label = ", ".join(n if k is None else f"{n} {k!r}" for n, k in s.events)
        fn_name = f"{base_name}_{len(out)}"
        e.w(f"// {origin}")
        e.w(f"// {s.which}({label})" + (f" {s.trailing}" if s.trailing else ""))
        e.w(f"void {fn_name}(Activation& scope) {{")
        e.indent = 1
        e.block_body(s.body, Context("script"))
        e.indent = 0
        e.w("}")
        unsupported += e.unsupported
        out.append((fn_name, " | ".join(events), e.lines))
    return out, unsupported
