// RoseGold lexer — a faithful TypeScript port of the canonical C++ lexer
// (cpp/src/lexer.hpp). Its token stream is verified byte-identical to
// `rosegoldc --tokens` by ts/test/parity.mjs, the same ground-truth discipline
// the self-hosted rglexer.rg is held to.
//
// RoseGold is an offside-rule language: blocks are `:` + indentation, so the
// lexer emits synthetic INDENT / DEDENT / NEWLINE tokens off an indentation
// stack (with `(` / `[` suppressing newlines), on top of the usual literals,
// identifiers/keywords, and one- and two-character operators.

export type Kind =
  | "INT" | "FLT" | "STR" | "IDENT" | "KW" | "OP"
  | "NEWLINE" | "INDENT" | "DEDENT" | "END";

export interface Token {
  kind: Kind;
  value: string;   // literal text / identifier / operator; "" for layout tokens
  line: number;    // 1-based
  col: number;     // 1-based, in the comment-stripped source
}

export class LexError extends Error {}

const KEYWORDS = new Set([
  "module", "import", "as", "pub", "internal", "private", "static",
  "func", "var", "const", "return", "pass",
  "if", "elif", "else", "while", "for", "in",
  "break", "continue", "try", "catch", "raise", "yield",
  "class", "trait", "enum", "init", "match", "extends", "extend",
  "extern", "uses", "signal", "true", "false",
]);

// ASCII classifiers matching C++ std::isdigit/isalpha/isalnum on ASCII source
// (deliberately not Unicode-aware, to stay byte-identical to the C++ lexer).
const isDigit = (c: string) => c >= "0" && c <= "9";
const isAlpha = (c: string) => (c >= "a" && c <= "z") || (c >= "A" && c <= "Z");
const isAlnum = (c: string) => isAlpha(c) || isDigit(c);

const TWO = new Set(["->", "=>", "==", "!=", "<=", ">=", "&&", "||"]);
const SINGLE = "()[]<>=!+-*/%,:.";

// Replace comments with equivalent whitespace so line/column offsets are
// preserved, while leaving string literals untouched. Handles `#` line comments
// (incl. `##` doc comments) and `#/ ... /#` block comments. Mirrors
// stripComments() in lexer.hpp exactly.
export function stripComments(src: string): string {
  let out = "";
  let i = 0;
  const n = src.length;
  let inStr = false;
  while (i < n) {
    const c = src[i];
    if (inStr) {
      out += c;
      if (c === "\\" && i + 1 < n) { out += src[i + 1]; i += 2; continue; }
      if (c === '"') inStr = false;
      i++; continue;
    }
    if (c === '"') { inStr = true; out += c; i++; continue; }
    if (c === "#" && i + 1 < n && src[i + 1] === "/") {           // #/ block comment /#
      out += "  "; i += 2;
      while (i + 1 < n && !(src[i] === "/" && src[i + 1] === "#")) {
        out += src[i] === "\n" ? "\n" : " "; i++;
      }
      if (i + 1 < n) { out += "  "; i += 2; }
      continue;
    }
    if (c === "#") { while (i < n && src[i] !== "\n") i++; continue; }  // # line comment
    out += c; i++;
  }
  return out;
}

export function lex(raw: string): Token[] {
  const src = stripComments(raw);
  let i = 0;
  const n = src.length;
  let line = 1;
  let lineStartByte = 0;
  const toks: Token[] = [];
  const indents: number[] = [0];
  let paren = 0;
  let lineStart = true;

  const emit = (kind: Kind, value: string, startByte: number) =>
    toks.push({ kind, value, line, col: startByte - lineStartByte + 1 });
  const top = () => indents[indents.length - 1];
  const backKind = (): Kind | undefined =>
    toks.length ? toks[toks.length - 1].kind : undefined;
  const isLayout = (k: Kind | undefined) =>
    k === "NEWLINE" || k === "INDENT" || k === "DEDENT";

  while (i < n) {
    // Start of a logical line (outside brackets): measure indentation and emit
    // INDENT / DEDENT relative to the stack.
    if (lineStart && paren === 0) {
      let width = 0;
      while (i < n && (src[i] === " " || src[i] === "\t")) { width += src[i] === "\t" ? 8 : 1; i++; }
      if (i >= n) break;
      if (src[i] === "\n") { line++; i++; lineStartByte = i; continue; }  // blank line
      if (width > top()) { indents.push(width); emit("INDENT", "", i); }
      else if (width < top()) {
        while (width < top()) { indents.pop(); emit("DEDENT", "", i); }
        if (width !== top()) throw new LexError("inconsistent indentation at line " + line);
      }
      lineStart = false;
      continue;
    }

    const sB = i;
    const c = src[i];

    if (c === "\n") {
      if (paren === 0) {
        if (toks.length && !isLayout(backKind())) emit("NEWLINE", "", sB);
        line++; i++; lineStartByte = i; lineStart = true;
      } else { line++; i++; lineStartByte = i; }         // newline inside ( ) / [ ] is insignificant
      continue;
    }
    if (c === " " || c === "\t" || c === "\r") { i++; continue; }

    if (c === '"') {                                     // string literal (with escapes)
      i++;
      let buf = "";
      while (i < n && src[i] !== '"') {
        if (src[i] === "\\" && i + 1 < n) {
          const e = src[i + 1];
          if (e === "n") buf += "\n";
          else if (e === "t") buf += "\t";
          else if (e === '"') buf += '"';
          else if (e === "\\") buf += "\\";
          else { buf += "\\"; buf += e; }
          i += 2;
        } else buf += src[i++];
      }
      if (i >= n) throw new LexError("unterminated string at line " + line);
      i++;
      emit("STR", buf, sB);
      continue;
    }

    if (isDigit(c)) {                                    // Int / Float
      const st = i;
      while (i < n && isDigit(src[i])) i++;
      let flt = false;
      if (i + 1 < n && src[i] === "." && isDigit(src[i + 1])) {
        flt = true; i++;
        while (i < n && isDigit(src[i])) i++;
      }
      emit(flt ? "FLT" : "INT", src.slice(st, i), sB);
      continue;
    }

    if (isAlpha(c) || c === "_") {                       // identifier or keyword
      const st = i;
      while (i < n && (isAlnum(src[i]) || src[i] === "_")) i++;
      const w = src.slice(st, i);
      emit(KEYWORDS.has(w) ? "KW" : "IDENT", w, sB);
      continue;
    }

    const two = i + 1 < n ? src.slice(i, i + 2) : "";
    if (TWO.has(two)) { emit("OP", two, sB); i += 2; continue; }

    if (SINGLE.includes(c)) {
      emit("OP", c, sB);
      if (c === "(" || c === "[") paren++;
      else if (c === ")" || c === "]") { if (paren > 0) paren--; }
      i++;
      continue;
    }

    throw new LexError("unexpected character '" + c + "' at line " + line);
  }

  // Terminate the final line, close any open indentation, and cap with END.
  if (toks.length && !isLayout(backKind())) emit("NEWLINE", "", i);
  while (indents.length > 1) { indents.pop(); emit("DEDENT", "", i); }
  emit("END", "", i);
  return toks;
}
