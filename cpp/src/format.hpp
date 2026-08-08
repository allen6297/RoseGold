#pragma once
#include "lexer.hpp"

// ---------------------------------------------------------------------
// Formatter
// ---------------------------------------------------------------------
// A structural formatter for the offside rule. It rewrites only *insignificant*
// whitespace — leading indentation (normalized to FMT_UNIT spaces per block
// level), trailing whitespace, and runs of blank lines — while leaving comments,
// strings, and all inline spacing byte-for-byte untouched. Because the lexer
// derives INDENT/DEDENT from *relative* indent widths, rewriting every level to
// a fixed unit leaves the token stream (hence the AST and the program's
// behavior) invariant. Each line's block depth comes straight from the real
// lexer, so the formatter can never disagree with the compiler about nesting.
// ---------------------------------------------------------------------
static const int FMT_UNIT = 4;

static std::string fmtRtrim(const std::string& s) {
    size_t e = s.find_last_not_of(" \t\r");
    return e == std::string::npos ? std::string() : s.substr(0, e + 1);
}
static size_t fmtLeadLen(const std::string& s) {
    size_t i = 0; while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++; return i;
}

static std::string formatSource(const std::string& raw) {
    // 1. Per-line block depth, straight from the offside lexer.
    std::map<int, int> lineDepth;                       // 1-based line -> block depth
    try {
        auto toks = lex(raw);
        int depth = 0;
        for (auto& t : toks) {
            if (t.t == Tk::INDENT) depth++;
            else if (t.t == Tk::DEDENT) depth--;
            else if (t.t != Tk::END && !lineDepth.count(t.line)) lineDepth[t.line] = depth;
        }
    } catch (const LexError&) {
        return raw;                                     // never touch code we can't lex
    }

    // 2. Split into physical lines and track string / block-comment / paren
    //    state across line boundaries (mirrors stripComments + paren nesting).
    std::vector<std::string> lines;
    { std::string cur; for (char c : raw) { if (c == '\n') { lines.push_back(cur); cur.clear(); } else cur += c; } lines.push_back(cur); }
    size_t L = lines.size();
    std::vector<char> startInBlock(L, 0), startInStr(L, 0), hasCode(L, 0);
    std::vector<int>  startParen(L, 0);
    {
        bool inStr = false, inBlock = false; int paren = 0;
        for (size_t li = 0; li < L; li++) {
            startInBlock[li] = inBlock; startInStr[li] = inStr; startParen[li] = paren;
            const std::string& s = lines[li]; size_t i = 0, n = s.size();
            while (i < n) {
                char c = s[i];
                if (inStr) { hasCode[li] = 1; if (c == '\\' && i + 1 < n) { i += 2; continue; } if (c == '"') inStr = false; i++; continue; }
                if (inBlock) { if (c == '/' && i + 1 < n && s[i + 1] == '#') { inBlock = false; i += 2; continue; } i++; continue; }
                if (c == '"') { inStr = true; hasCode[li] = 1; i++; continue; }
                if (c == '#' && i + 1 < n && s[i + 1] == '/') { inBlock = true; i += 2; continue; }
                if (c == '#') break;                    // line comment: rest of line is comment
                if (c != ' ' && c != '\t' && c != '\r') { hasCode[li] = 1; if (c == '(' || c == '[') paren++; else if (c == ')' || c == ']') { if (paren > 0) paren--; } }
                i++;
            }
        }
    }

    // 3. Emit each line with canonical indentation.
    std::vector<std::string> out; out.reserve(L);
    int curDepth = 0;
    for (size_t li = 0; li < L; li++) {
        int lineNo = (int)li + 1;
        const std::string& s = lines[li];
        if (startInStr[li] || startInBlock[li]) { out.push_back(fmtRtrim(s)); continue; }   // inside a string / block comment: verbatim
        if (!hasCode[li]) {
            std::string t = fmtRtrim(s);
            if (t.empty()) { out.push_back(""); continue; }                                 // blank line
            auto nx = lineDepth.upper_bound(lineNo);                                        // comment aligns with the next code line
            int d = (nx != lineDepth.end()) ? nx->second : curDepth;
            out.push_back(std::string(d * FMT_UNIT, ' ') + fmtRtrim(s.substr(fmtLeadLen(s))));
            continue;
        }
        int depth;
        if (startParen[li] > 0) depth = curDepth + 1;                                        // continuation of a multi-line ( … )
        else { auto it = lineDepth.find(lineNo); depth = (it != lineDepth.end()) ? it->second : curDepth; curDepth = depth; }
        out.push_back(std::string(depth * FMT_UNIT, ' ') + fmtRtrim(s.substr(fmtLeadLen(s))));
    }

    // 4. Collapse blank-line runs to one, strip leading/trailing blanks, end with a newline.
    std::string res; bool prevBlank = true; std::vector<std::string> body;
    for (auto& l : out) { bool b = l.empty(); if (b && prevBlank) continue; body.push_back(l); prevBlank = b; }
    while (!body.empty() && body.back().empty()) body.pop_back();
    for (auto& l : body) { res += l; res += '\n'; }
    return res;
}
