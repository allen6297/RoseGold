// noinspection D

export type Kind =
    | "INTEGER"
    | "FLOAT"
    | "STRING"
    | "IDENTIFIER" //name of x
    | "KEYWORD"
    | "OPERATOR"
    | "NEWLINE" // \n
    | "INDENT"
    | "DEDENT"
    | "END"; //EOF

export interface Token {
    kind: Kind;
    value: string;
    line: number;
}

export class LexerError extends Error {
}

const KEYWORDS = new Set([
    // TOP LEVEL
    "module", "import",

    //DECLARATION DECORATORS

    // DECLARATION LEVEL
    "var", "const", "signal", "enum",

    //METHOD DECORATORS

    // METHOD PREFIX
    "pub", "internal", "private", "static",

    // METHOD LEVEL
    "fn", "class", "struct", "trait",

    // METHOD BODY
    "init",

    // METHOD LOGIC
    "while", "for", "in", "match",
    "if", "elif", "else",
    "break", "continue", "try", "catch", "raise", "yield",
    "return", "pass",

    // METHOD SUFFIX
    "uses", "extend", "extends",

    // HELPERS
    "as", "true", "false",
])

// SYMBOLS
const isDigit = (c: string) => c >= "0" && c <= "9";
const isChar = (c: string) => c >= "a" && c <= "z" || c >= "A" && c <= "Z";
const isAlnum = (c: string) => isDigit(c) || isChar(c);

const TWOCHAR = new Set(["->", "=>", "==", "!=", "<=", ">=", "&&", "||"]);
const SINGLECHAR = "()[]<>=!+-*/%,:.";

export function stripComments(src: string): string {
    let out = "";
    let i = 0;
    const n = src.length;
    let inStr = false;
    while (i < n) {
        const c = src[i];
        if (inStr) {
            out += c;
            if (c === "\\" && i + 1 < n) {
                out += src[i + 1];
                i += 2;
                continue;
            }
            if (c === '"') inStr = false;
            i++;
            continue;
        }
        if (c === '"') {
            inStr = true;
            out += c;
            i++;
            continue;
        }
        if (c === "#" && i + 1 < n && src[i + 1] === "/") {           // #/ block comment /#
            out += "  ";
            i += 2;
            while (i + 1 < n && !(src[i] === "/" && src[i + 1] === "#")) {
                out += src[i] === "\n" ? "\n" : " ";
                i++;
            }
            if (i + 1 < n) {
                out += "  ";
                i += 2;
            }
            continue;
        }
        if (c === "#") {
            while (i < n && src[i] !== "\n") i++;
            continue;
        }  // # line comment
        out += c;
        i++;
    }
    return out;
}

export function lex(raw: string): Token[] {
    const src = stripComments(raw);
    let i = 0;
    const n = src.length;
    let line = 1;
    const toks: Token[] = [];
    const indents: number[] = [0];
    let paren = 0;
    let lineStart = true;

    const emit = (kind: Kind, value: string) => toks.push({kind, value, line});
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
            while (i < n && (src[i] === " " || src[i] === "\t")) {
                width += src[i] === "\t" ? 8 : 1;
                i++;
            }
            if (i >= n) break;
            if (src[i] === "\n") {
                line++;
                i++;
                continue;
            }  // blank line
            if (width > top()) {
                indents.push(width);
                emit("INDENT", "");
            } else if (width < top()) {
                while (width < top()) {
                    indents.pop();
                    emit("DEDENT", "");
                }
                if (width !== top()) throw new LexerError("inconsistent indentation at line " + line);
            }
            lineStart = false;
            continue;
        }

        const c = src[i];

        if (c === "\n") {
            if (paren === 0) {
                if (toks.length && !isLayout(backKind())) emit("NEWLINE", "");
                line++;
                i++;
                lineStart = true;
            } else {
                line++;
                i++;
            }         // newline inside ( ) / [ ] is insignificant
            continue;
        }
        if (c === " " || c === "\t" || c === "\r") {
            i++;
            continue;
        }

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
                    else {
                        buf += "\\";
                        buf += e;
                    }
                    i += 2;
                } else buf += src[i++];
            }
            if (i >= n) throw new LexerError("unterminated string at line " + line);
            i++;
            emit("STRING", buf);
            continue;
        }

        if (isDigit(c)) {                                    // Int / Float
            const st = i;
            while (i < n && isDigit(src[i])) i++;
            let float = false;
            if (i + 1 < n && src[i] === "." && isDigit(src[i + 1])) {
                float = true;
                i++;
                while (i < n && isDigit(src[i])) i++;
            }
            emit(float ? "FLOAT" : "INTEGER", src.slice(st, i));
            continue;
        }

        if (isChar(c) || c === "_") {                       // identifier or keyword
            const st = i;
            while (i < n && (isAlnum(src[i]) || src[i] === "_")) i++;
            const w = src.slice(st, i);
            emit(KEYWORDS.has(w) ? "KEYWORD" : "IDENTIFIER", w);
            continue;
        }

        const two = i + 1 < n ? src.slice(i, i + 2) : "";
        if (TWOCHAR.has(two)) {
            emit("OPERATOR", two);
            i += 2;
            continue;
        }

        if (SINGLECHAR.includes(c)) {
            emit("OPERATOR", c);
            if (c === "(" || c === "[") paren++;
            else if (c === ")" || c === "]") {
                if (paren > 0) paren--;
            }
            i++;
            continue;
        }

        throw new LexerError("unexpected character '" + c + "' at line " + line);
    }

    // Terminate the final line, close any open indentation, and cap with END.
    if (toks.length && !isLayout(backKind())) emit("NEWLINE", "");
    while (indents.length > 1) {
        indents.pop();
        emit("DEDENT", "");
    }
    emit("END", "");
    return toks;
}
