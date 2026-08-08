#pragma once
#include "common.hpp"

// ---------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------
enum class Tk { INT, FLT, STR, IDENT, KW, OP, NEWLINE, INDENT, DEDENT, END };
struct Token { Tk t; std::string s; int line; int col = 1; };   // col: 1-based
static const std::vector<std::string> KEYWORDS = {
    "module", "import", "as", "pub", "internal", "private", "static",
    "func", "var", "const", "return", "pass",
    "if", "elif", "else", "while", "for", "in",
    "break", "continue", "try", "catch", "raise", "yield",
    "class", "trait", "enum", "init", "match", "extends", "extend", "uses", "signal", "true", "false"};
static bool isKw(const std::string& s) { for (auto& k : KEYWORDS) if (k == s) return true; return false; }
struct LexError : std::runtime_error { using std::runtime_error::runtime_error; };

static std::string stripComments(const std::string& src) {
    std::string out; size_t i = 0, n = src.size(); bool inStr = false;
    while (i < n) {
        char c = src[i];
        if (inStr) { out += c; if (c == '\\' && i + 1 < n) { out += src[i + 1]; i += 2; continue; } if (c == '"') inStr = false; i++; continue; }
        if (c == '"') { inStr = true; out += c; i++; continue; }
        if (c == '#' && i + 1 < n && src[i + 1] == '/') { out += "  "; i += 2; while (i + 1 < n && !(src[i] == '/' && src[i + 1] == '#')) { out += (src[i] == '\n') ? '\n' : ' '; i++; } if (i + 1 < n) { out += "  "; i += 2; } continue; }
        if (c == '#') { while (i < n && src[i] != '\n') i++; continue; }
        out += c; i++;
    }
    return out;
}
static std::vector<Token> lex(const std::string& raw) {
    std::string src = stripComments(raw);
    size_t i = 0, n = src.size(); int line = 1; size_t lineStartByte = 0;
    std::vector<Token> toks; std::vector<int> indents = {0}; int paren = 0; bool lineStart = true;
    auto emit = [&](Tk t, const std::string& s, size_t startByte) { toks.push_back({t, s, line, (int)(startByte - lineStartByte) + 1}); };
    while (i < n) {
        if (lineStart && paren == 0) {
            int width = 0; while (i < n && (src[i] == ' ' || src[i] == '\t')) { width += (src[i] == '\t') ? 8 : 1; i++; }
            if (i >= n) break;
            if (src[i] == '\n') { line++; i++; lineStartByte = i; continue; }
            if (width > indents.back()) { indents.push_back(width); emit(Tk::INDENT, "", i); }
            else if (width < indents.back()) { while (width < indents.back()) { indents.pop_back(); emit(Tk::DEDENT, "", i); } if (width != indents.back()) throw LexError("inconsistent indentation at line " + std::to_string(line)); }
            lineStart = false; continue;
        }
        size_t sB = i; char c = src[i];
        if (c == '\n') { if (paren == 0) { if (!toks.empty() && toks.back().t != Tk::NEWLINE && toks.back().t != Tk::INDENT && toks.back().t != Tk::DEDENT) emit(Tk::NEWLINE, "", sB); line++; i++; lineStartByte = i; lineStart = true; } else { line++; i++; lineStartByte = i; } continue; }
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
        if (c == '"') { i++; std::string buf; while (i < n && src[i] != '"') { if (src[i] == '\\' && i + 1 < n) { char e = src[i + 1]; if (e == 'n') buf += '\n'; else if (e == 't') buf += '\t'; else if (e == '"') buf += '"'; else if (e == '\\') buf += '\\'; else { buf += '\\'; buf += e; } i += 2; } else buf += src[i++]; } if (i >= n) throw LexError("unterminated string at line " + std::to_string(line)); i++; emit(Tk::STR, buf, sB); continue; }
        if (std::isdigit((unsigned char)c)) { size_t st = i; while (i < n && std::isdigit((unsigned char)src[i])) i++; bool flt = false; if (i + 1 < n && src[i] == '.' && std::isdigit((unsigned char)src[i + 1])) { flt = true; i++; while (i < n && std::isdigit((unsigned char)src[i])) i++; } emit(flt ? Tk::FLT : Tk::INT, src.substr(st, i - st), sB); continue; }
        if (std::isalpha((unsigned char)c) || c == '_') { size_t st = i; while (i < n && (std::isalnum((unsigned char)src[i]) || src[i] == '_')) i++; std::string w = src.substr(st, i - st); emit(isKw(w) ? Tk::KW : Tk::IDENT, w, sB); continue; }
        std::string two = (i + 1 < n) ? src.substr(i, 2) : "";
        static const std::vector<std::string> TWO = {"->", "=>", "==", "!=", "<=", ">=", "&&", "||"};
        bool m = false; for (auto& t : TWO) if (two == t) { emit(Tk::OP, two, sB); i += 2; m = true; break; } if (m) continue;
        if (std::string("()[]<>=!+-*/%,:.").find(c) != std::string::npos) { emit(Tk::OP, std::string(1, c), sB); if (c == '(' || c == '[') paren++; else if (c == ')' || c == ']') { if (paren > 0) paren--; } i++; continue; }
        throw LexError(std::string("unexpected character '") + c + "' at line " + std::to_string(line));
    }
    if (!toks.empty() && toks.back().t != Tk::NEWLINE && toks.back().t != Tk::INDENT && toks.back().t != Tk::DEDENT) emit(Tk::NEWLINE, "", i);
    while ((int)indents.size() > 1) { indents.pop_back(); emit(Tk::DEDENT, "", i); }
    emit(Tk::END, "", i); return toks;
}

