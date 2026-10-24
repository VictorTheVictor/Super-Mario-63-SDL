"""ActionScript 2 parser (no class syntax) producing a small AST with comments attached
to statements, so the C++ emitter can keep them."""

import re


class ParseError(Exception):
    def __init__(self, msg, line):
        super().__init__(f"line {line}: {msg}")
        self.line = line


# ---------------------------------------------------------------- lexer

class Tok:
    __slots__ = ("kind", "value", "line", "nl", "end_line")

    def __init__(self, kind, value, line, nl, end_line=None):
        self.kind, self.value, self.line, self.nl = kind, value, line, nl
        self.end_line = end_line if end_line is not None else line

    def __repr__(self):
        return f"{self.kind}:{self.value!r}@{self.line}"


PUNCT = sorted("""
>>>= === !== >>> <<= >>= ... == != <= >= && || ++ -- += -= *= /= %= &= |= ^= << >>
{ } ( ) [ ] ; , < > + - * / % & | ^ ! ~ ? : = .
""".split(), key=len, reverse=True)

WORD_OPS = {"and": "&&", "or": "||", "not": "!", "add": "+", "eq": "==", "ne": "!=",
            "lt": "<", "gt": ">", "le": "<=", "ge": ">="}

ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "b": "\b", "f": "\f", "v": "\v", "0": "\0",
           "\\": "\\", '"': '"', "'": "'"}

ID_START = re.compile(r"[A-Za-z_$\u0080-￿]")
ID_CHAR = re.compile(r"[\w$\u0080-￿]")


def lex(src):
    """Returns (tokens, comments). Comments are (line, end_line, text, own_line, blank_before)."""
    src = src.replace("\r\n", "\n").replace("\r", "\n")
    toks, comments = [], []
    i, line, n = 0, 1, len(src)
    nl = True              # newline since the previous token
    line_has_token = False
    blank = False          # blank line since the previous token/comment
    newlines_run = 0
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1
            i += 1
            nl = True
            newlines_run += 1
            if newlines_run >= 2:
                blank = True
            line_has_token = False
            continue
        if c in " \t\f\v﻿ ":
            i += 1
            continue
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            comments.append((line, line, src[i:j].rstrip(), not line_has_token, blank, len(toks)))
            blank = False
            newlines_run = 0
            i = j
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            text = src[i:j]
            start = line
            line += text.count("\n")
            comments.append((start, line, text, not line_has_token, blank, len(toks)))
            blank = False
            newlines_run = 0
            if "\n" in text:
                nl = True
            i = j
            continue
        newlines_run = 0
        start_line = line
        if c.isdigit() or (c == "." and i + 1 < n and src[i + 1].isdigit()):
            m = re.compile(r"0[xX][0-9a-fA-F]+|(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?").match(src, i)
            toks.append(Tok("num", m.group(0), line, nl))
            i = m.end()
        elif c in "\"'":
            q, j, out = c, i + 1, []
            while j < n and src[j] != q:
                if src[j] == "\\" and j + 1 < n:
                    e = src[j + 1]
                    if e == "u" and re.match(r"[0-9a-fA-F]{4}", src[j + 2:j + 6]):
                        out.append(chr(int(src[j + 2:j + 6], 16)))
                        j += 6
                        continue
                    if e == "x" and re.match(r"[0-9a-fA-F]{2}", src[j + 2:j + 4]):
                        out.append(chr(int(src[j + 2:j + 4], 16)))
                        j += 4
                        continue
                    if e == "\n":
                        line += 1
                        j += 2
                        continue
                    out.append(ESCAPES.get(e, e))
                    j += 2
                    continue
                if src[j] == "\n":
                    line += 1
                out.append(src[j])
                j += 1
            toks.append(Tok("str", "".join(out), start_line, nl, line))
            i = j + 1
        elif ID_START.match(c):
            j = i + 1
            while j < n and ID_CHAR.match(src[j]):
                j += 1
            word = src[i:j]
            toks.append(Tok("id", word, line, nl))
            i = j
        elif c == "#":
            j = src.find("\n", i)
            j = n if j < 0 else j
            toks.append(Tok("directive", src[i:j].strip(), line, nl))
            i = j
        else:
            for p in PUNCT:
                if src.startswith(p, i):
                    toks.append(Tok("p", p, line, nl))
                    i += len(p)
                    break
            else:
                raise ParseError(f"unexpected character {c!r}", line)
        nl = False
        blank = False
        line_has_token = True
    toks.append(Tok("eof", None, line, True))
    return toks, comments


# ---------------------------------------------------------------- AST

class Node:
    __slots__ = ("kind", "a", "line")

    def __init__(self, kind, line=0, **a):
        self.kind, self.a, self.line = kind, a, line

    def __getattr__(self, name):
        try:
            return self.a[name]
        except KeyError:
            raise AttributeError(name)

    def __repr__(self):
        return f"{self.kind}({self.a})"


class Stmt(Node):
    """Statement with comments: `before` (own-line comments and blank-line markers
    preceding it) and `trailing` (comment on the same line after it)."""
    __slots__ = ("before", "trailing")

    def __init__(self, kind, line=0, **a):
        super().__init__(kind, line, **a)
        self.before, self.trailing = [], None


KEYWORDS = {"break", "case", "continue", "default", "delete", "do", "else", "for", "function",
            "if", "in", "instanceof", "new", "return", "switch", "this", "typeof", "var",
            "void", "while", "with", "true", "false", "null", "undefined"}

ASSIGN_OPS = {"=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", ">>>=", "&=", "|=", "^="}
BINARY = [  # lowest to highest precedence
    {"||"}, {"&&"}, {"|"}, {"^"}, {"&"}, {"==", "!=", "===", "!=="},
    {"<", ">", "<=", ">=", "instanceof", "in"}, {"<<", ">>", ">>>"}, {"+", "-"}, {"*", "/", "%"},
]


class Parser:
    def __init__(self, src):
        self.toks, self.comments = lex(src)
        self.i = 0
        self.ci = 0  # next unattached comment
        self.no_in = False

    # -- token helpers
    def peek(self, k=0):
        return self.toks[min(self.i + k, len(self.toks) - 1)]

    def next(self):
        t = self.toks[self.i]
        self.i += 1
        return t

    def is_p(self, v, k=0):
        t = self.peek(k)
        return t.kind == "p" and t.value == v

    def is_id(self, v, k=0):
        t = self.peek(k)
        return t.kind == "id" and t.value == v

    def expect_p(self, v):
        t = self.next()
        if t.kind != "p" or t.value != v:
            raise ParseError(f"expected '{v}', got {t.value!r}", t.line)
        return t

    def ident(self):
        t = self.next()
        if t.kind != "id":
            raise ParseError(f"expected identifier, got {t.value!r}", t.line)
        return t.value

    def semicolon(self):
        if self.is_p(";"):
            self.next()
            return
        t = self.peek()
        if t.kind == "eof" or (t.kind == "p" and t.value == "}") or t.nl:
            return  # automatic semicolon insertion
        raise ParseError(f"expected ';', got {t.value!r}", t.line)

    # -- comments
    def take_comments_before(self, tok_index):
        out = []
        while self.ci < len(self.comments) and self.comments[self.ci][5] <= tok_index:
            line, end, text, own, blank, _ = self.comments[self.ci]
            if blank:
                out.append(None)  # blank line marker
            out.append(text)
            self.ci += 1
        return out

    def trailing_comment(self, line):
        if self.ci < len(self.comments):
            cl, end, text, own, blank, _ = self.comments[self.ci]
            if cl == line and not own and "\n" not in text:
                self.ci += 1
                return text
        return None

    # -- program
    def program(self):
        body = self.statements(top=True)
        tail = self.take_comments_before(len(self.toks))
        return Node("program", body=body, tail=tail)

    def statements(self, top=False):
        out = []
        while True:
            t = self.peek()
            if t.kind == "eof" or (t.kind == "p" and t.value == "}"):
                return out
            out.append(self.statement(top))

    def statement(self, top=False):
        start = self.i
        before = self.take_comments_before(start)
        # Blank line before this statement (not preceded by a comment).
        if self.peek().nl and start > 0 and self._blank_before(start) and (not before or before[0] is not None):
            before.insert(0, None)
        s = self._statement(top)
        s.before = before
        last_line = self.toks[self.i - 1].end_line if self.i > 0 else 0
        s.trailing = self.trailing_comment(last_line)
        return s

    def _blank_before(self, idx):
        prev = self.toks[idx - 1]
        return self.toks[idx].line - prev.end_line >= 2 and not any(
            prev.end_line < c[0] < self.toks[idx].line for c in self.comments)

    def block_or_statement(self):
        if self.is_p("{"):
            return self.block()
        s = self.statement()
        return Stmt("block", s.line, body=[s], braces=False)

    def block(self):
        t = self.expect_p("{")
        body = self.statements()
        tail = self.take_comments_before(self.i)
        self.expect_p("}")
        return Stmt("block", t.line, body=body, braces=True, tail=tail)

    def _statement(self, top):
        t = self.peek()
        line = t.line
        if t.kind == "p":
            if t.value == "{":
                return self.block()
            if t.value == ";":
                self.next()
                return Stmt("empty", line)
        if t.kind == "directive":
            self.next()
            return Stmt("directive", line, text=t.value)
        if t.kind == "id":
            v = t.value
            if v == "var":
                self.next()
                decls = self.var_list()
                self.semicolon()
                return Stmt("var", line, decls=decls)
            if v == "function" and self.peek(1).kind == "id":
                self.next()
                name = self.ident()
                params, body = self.function_rest()
                return Stmt("funcdecl", line, name=name, params=params, body=body)
            if v == "if":
                self.next()
                self.expect_p("(")
                cond = self.expression()
                self.expect_p(")")
                then = self.block_or_statement()
                other = None
                if self.is_id("else"):
                    self.next()
                    other = self.block_or_statement() if not self.is_id("if") else self.statement()
                return Stmt("if", line, cond=cond, then=then, other=other)
            if v == "while":
                self.next()
                self.expect_p("(")
                cond = self.expression()
                self.expect_p(")")
                return Stmt("while", line, cond=cond, body=self.block_or_statement())
            if v == "do":
                self.next()
                body = self.block_or_statement()
                if not self.is_id("while"):
                    raise ParseError("expected 'while'", self.peek().line)
                self.next()
                self.expect_p("(")
                cond = self.expression()
                self.expect_p(")")
                if self.is_p(";"):
                    self.next()
                return Stmt("dowhile", line, cond=cond, body=body)
            if v == "for":
                return self.for_statement()
            if v == "switch":
                return self.switch_statement()
            if v in ("break", "continue"):
                self.next()
                self.semicolon()
                return Stmt(v, line)
            if v == "return":
                self.next()
                value = None
                nt = self.peek()
                if not (nt.kind == "eof" or nt.nl or (nt.kind == "p" and nt.value in (";", "}"))):
                    value = self.expression()
                self.semicolon()
                return Stmt("return", line, value=value)
            if v in ("with", "tellTarget"):
                self.next()
                self.expect_p("(")
                target = self.expression()
                self.expect_p(")")
                return Stmt(v, line, target=target, body=self.block_or_statement())
            if v in ("onClipEvent", "on") and self.is_p("(", 1):
                self.next()
                self.expect_p("(")
                events = []
                while not self.is_p(")"):
                    et = self.next()
                    if et.kind == "id" and et.value == "keyPress":
                        key = self.next()
                        events.append(("keyPress", key.value))
                    else:
                        events.append((et.value, None))
                    if self.is_p(","):
                        self.next()
                self.expect_p(")")
                return Stmt("handler", line, which=v, events=events, body=self.block())
            if v == "ifFrameLoaded":
                self.next()
                self.expect_p("(")
                args = self.arguments_until(")")
                return Stmt("ifFrameLoaded", line, args=args, body=self.block_or_statement())
        expr = self.expression()
        self.semicolon()
        return Stmt("expr", line, expr=expr)

    def var_list(self):
        decls = []
        while True:
            name = self.ident()
            if self.is_p(":"):  # type annotation
                self.next()
                self.type_name()
            init = None
            if self.is_p("="):
                self.next()
                init = self.assignment()
            decls.append((name, init))
            if not self.is_p(","):
                return decls
            self.next()

    def type_name(self):
        self.ident()
        while self.is_p("."):
            self.next()
            self.ident()

    def for_statement(self):
        line = self.next().line
        self.expect_p("(")
        init = None
        # for (var x in obj) / for (x in obj)
        save = self.i
        if self.is_id("var") and self.peek(1).kind == "id" and (self.is_id("in", 2) or self.is_p(":", 2)):
            self.next()
            name = self.ident()
            if self.is_p(":"):
                self.next()
                self.type_name()
            if self.is_id("in"):
                self.next()
                obj = self.expression()
                self.expect_p(")")
                return Stmt("forin", line, var=name, declared=True, obj=obj, body=self.block_or_statement())
            self.i = save
        if self.peek().kind == "id" and self.is_id("in", 1) and not self.is_id("var"):
            name = self.ident()
            self.next()
            obj = self.expression()
            self.expect_p(")")
            return Stmt("forin", line, var=name, declared=False, obj=obj, body=self.block_or_statement())
        if not self.is_p(";"):
            if self.is_id("var"):
                self.next()
                self.no_in = True
                init = Stmt("var", line, decls=self.var_list())
                self.no_in = False
            else:
                self.no_in = True
                init = Stmt("expr", line, expr=self.expression())
                self.no_in = False
        self.expect_p(";")
        cond = None if self.is_p(";") else self.expression()
        self.expect_p(";")
        update = None if self.is_p(")") else self.expression()
        self.expect_p(")")
        return Stmt("for", line, init=init, cond=cond, update=update, body=self.block_or_statement())

    def switch_statement(self):
        line = self.next().line
        self.expect_p("(")
        disc = self.expression()
        self.expect_p(")")
        self.expect_p("{")
        cases = []
        while not self.is_p("}"):
            before = self.take_comments_before(self.i)
            if self.is_id("case"):
                self.next()
                test = self.expression()
            elif self.is_id("default"):
                self.next()
                test = None
            else:
                raise ParseError("expected case/default", self.peek().line)
            self.expect_p(":")
            body = []
            while not (self.is_id("case") or self.is_id("default") or self.is_p("}")):
                body.append(self.statement())
            cases.append((test, body, before))
        tail = self.take_comments_before(self.i)
        self.expect_p("}")
        return Stmt("switch", line, disc=disc, cases=cases, tail=tail)

    def function_rest(self):
        self.expect_p("(")
        params = []
        while not self.is_p(")"):
            params.append(self.ident())
            if self.is_p(":"):
                self.next()
                self.type_name()
            if self.is_p(","):
                self.next()
        self.expect_p(")")
        if self.is_p(":"):
            self.next()
            self.type_name()
        return params, self.block()

    def arguments_until(self, close):
        args = []
        while not self.is_p(close):
            args.append(self.assignment())
            if self.is_p(","):
                self.next()
            elif not self.is_p(close):
                raise ParseError(f"expected ',' or '{close}'", self.peek().line)
        self.next()
        return args

    # -- expressions
    def expression(self):
        e = self.assignment()
        if self.is_p(","):
            items = [e]
            while self.is_p(","):
                self.next()
                items.append(self.assignment())
            return Node("comma", e.line, items=items)
        return e

    def assignment(self):
        left = self.conditional()
        t = self.peek()
        if t.kind == "p" and t.value in ASSIGN_OPS:
            if left.kind not in ("name", "member", "index"):
                raise ParseError("invalid assignment target", t.line)
            self.next()
            right = self.assignment()
            return Node("assign", t.line, op=t.value, target=left, value=right)
        return left

    def conditional(self):
        cond = self.binary(0)
        if self.is_p("?"):
            self.next()
            no_in, self.no_in = self.no_in, False
            a = self.assignment()
            self.no_in = no_in
            self.expect_p(":")
            b = self.assignment()
            return Node("cond", cond.line, cond=cond, yes=a, no=b)
        return cond

    def binop(self):
        t = self.peek()
        if t.kind == "p":
            return t.value
        if t.kind == "id":
            if t.value in ("instanceof",) or (t.value == "in" and not self.no_in):
                return t.value
            if t.value in WORD_OPS and WORD_OPS[t.value] not in ("!",):
                return WORD_OPS[t.value]
        return None

    def binary(self, level):
        if level == len(BINARY):
            return self.unary()
        left = self.binary(level + 1)
        while True:
            op = self.binop()
            if op not in BINARY[level]:
                return left
            line = self.next().line
            right = self.binary(level + 1)
            left = Node("binary", line, op=op, left=left, right=right)

    def unary(self):
        t = self.peek()
        if t.kind == "p" and t.value in ("!", "-", "+", "~"):
            self.next()
            return Node("unary", t.line, op=t.value, arg=self.unary())
        if t.kind == "p" and t.value in ("++", "--"):
            self.next()
            return Node("preinc", t.line, op=t.value, arg=self.unary())
        if t.kind == "id" and t.value in ("typeof", "delete", "void", "not"):
            self.next()
            if t.value == "not":
                return Node("unary", t.line, op="!", arg=self.unary())
            return Node(t.value, t.line, arg=self.unary())
        return self.postfix()

    def postfix(self):
        e = self.call_member()
        t = self.peek()
        if t.kind == "p" and t.value in ("++", "--") and not t.nl:
            self.next()
            return Node("postinc", t.line, op=t.value, arg=e)
        return e

    def call_member(self):
        if self.is_id("new"):
            line = self.next().line
            ctor = self.member_only()
            args = []
            if self.is_p("("):
                self.next()
                args = self.arguments_until(")")
            e = Node("new", line, ctor=ctor, args=args)
        else:
            e = self.primary()
        return self.suffixes(e, calls=True)

    def member_only(self):
        if self.is_id("new"):
            return self.call_member()
        return self.suffixes(self.primary(), calls=False)

    def suffixes(self, e, calls):
        while True:
            if self.is_p("."):
                self.next()
                t = self.next()
                if t.kind != "id":
                    raise ParseError("expected property name", t.line)
                e = Node("member", t.line, obj=e, name=t.value)
            elif self.is_p("["):
                line = self.next().line
                no_in, self.no_in = self.no_in, False
                key = self.expression()
                self.no_in = no_in
                self.expect_p("]")
                e = Node("index", line, obj=e, key=key)
            elif calls and self.is_p("("):
                line = self.next().line
                no_in, self.no_in = self.no_in, False
                args = self.arguments_until(")")
                self.no_in = no_in
                e = Node("call", line, fn=e, args=args)
            else:
                return e

    def primary(self):
        t = self.next()
        if t.kind == "num":
            v = t.value
            num = float(int(v, 16)) if v[:2].lower() == "0x" else float(v)
            return Node("num", t.line, value=num, text=v)
        if t.kind == "str":
            return Node("str", t.line, value=t.value)
        if t.kind == "p":
            if t.value == "(":
                no_in, self.no_in = self.no_in, False
                e = self.expression()
                self.no_in = no_in
                self.expect_p(")")
                return Node("paren", t.line, expr=e)
            if t.value == "[":
                return Node("array", t.line, items=self.arguments_until("]"))
            if t.value == "{":
                props = []
                while not self.is_p("}"):
                    k = self.next()
                    if k.kind not in ("id", "str", "num"):
                        raise ParseError("bad object key", k.line)
                    self.expect_p(":")
                    props.append((str(k.value), self.assignment()))
                    if self.is_p(","):
                        self.next()
                self.expect_p("}")
                return Node("object", t.line, props=props)
        if t.kind == "id":
            v = t.value
            if v == "function":
                name = self.ident() if self.peek().kind == "id" else None
                params, body = self.function_rest()
                return Node("function", t.line, name=name, params=params, body=body)
            if v in ("true", "false"):
                return Node("bool", t.line, value=v == "true")
            if v == "null":
                return Node("null", t.line)
            if v == "undefined":
                return Node("undefined", t.line)
            if v == "this":
                return Node("this", t.line)
            return Node("name", t.line, name=v)
        raise ParseError(f"unexpected {t.value!r}", t.line)


def parse(src):
    return Parser(src).program()
