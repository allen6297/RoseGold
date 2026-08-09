#pragma once
#include "parser.hpp"

// ---------------------------------------------------------------------
// Documentation comments  (Javadoc-style)
// ---------------------------------------------------------------------
//   ## one-line doc         (line doc; consecutive lines accumulate)
//   #/ ... /#               (block doc)
// A doc comment attaches to the declaration on the next code line. Because
// both markers start with `#`, the lexer already strips them as ordinary
// comments — this pass just *captures* them. `collectDocs` maps a
// declaration's source line to its doc text; `generateDoc` renders a Markdown
// page (used by `rosegoldc --doc`); the LSP appends the doc to hovers.
// ---------------------------------------------------------------------
static std::string docTrim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n"); if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n"); return s.substr(b, e - b + 1);
}

// line-of-declaration -> doc text
static std::map<int, std::string> collectDocs(const std::string& raw) {
    std::vector<std::string> lines; { std::string cur; for (char c : raw) { if (c == '\n') { lines.push_back(cur); cur.clear(); } else cur += c; } lines.push_back(cur); }
    std::map<int, std::string> docs;
    std::string pending; int state = 0;   // 0 none, 1 inside a block doc
    for (size_t li = 0; li < lines.size(); li++) {
        int lineNo = (int)li + 1;
        const std::string& raw_line = lines[li];
        std::string s = raw_line; { size_t i = 0; while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) i++; s = s.substr(i); }
        if (state == 1) {                              // inside a block doc
            size_t c = raw_line.find("/#");
            pending += docTrim(c == std::string::npos ? raw_line : raw_line.substr(0, c)) + "\n";
            if (c != std::string::npos) state = 0;
            continue;
        }
        if (s.rfind("#/", 0) == 0) {                   // block doc `#/ ... /#` (every #/ block is a doc)
            std::string rest = s.substr(2); size_t c = rest.find("/#");
            pending += docTrim(c == std::string::npos ? rest : rest.substr(0, c)) + "\n";
            if (c == std::string::npos) state = 1;
            continue;
        }
        if (s.rfind("##", 0) == 0) { pending += docTrim(s.substr(2)) + "\n"; continue; }   // line doc `## ...`
        if (!s.empty() && s[0] == '#') continue;       // normal line comment (single #, keeps pending)
        if (s.empty()) continue;                       // blank (keeps pending)
        if (!pending.empty()) { docs[lineNo] = docTrim(pending); pending.clear(); }   // code line: attach
    }
    return docs;
}

// Render a parsed type node for docs (e.g. Map<String, Int>, func(Int) -> Bool).
static std::string docTy(const TyNodeP& t) {
    if (!t) return "";
    if (t->isFunc) { std::string s = "fn("; for (size_t i = 0; i < t->fparams.size(); i++) { if (i) s += ", "; s += docTy(t->fparams[i]); } s += ") -> " + docTy(t->fret); return s; }
    std::string s = t->name;
    if (!t->args.empty()) { s += "<"; for (size_t i = 0; i < t->args.size(); i++) { if (i) s += ", "; s += docTy(t->args[i]); } s += ">"; }
    return s;
}
static std::string docFuncSig(const Func& f) {
    std::string s = f.name + "(";
    bool first = true;
    for (size_t k = 0; k < f.params.size(); k++) {
        if (f.params[k] == "self") continue;
        if (!first) s += ", "; first = false;
        s += f.params[k]; if (k < f.ptypes.size() && f.ptypes[k]) s += ": " + docTy(f.ptypes[k]);
    }
    s += ")"; if (f.retType) s += " -> " + docTy(f.retType);
    return s;
}
// Render doc body: prose lines as-is; @param/@return lines as a bullet list.
static std::string docBody(const std::string& doc) {
    if (doc.empty()) return "";
    std::vector<std::string> lines; { std::string cur; for (char c : doc) { if (c == '\n') { lines.push_back(cur); cur.clear(); } else cur += c; } lines.push_back(cur); }
    std::string prose, tags;
    for (auto& ln : lines) {
        std::string l = docTrim(ln); if (l.empty()) continue;
        if (l.rfind("@param", 0) == 0) { std::string r = docTrim(l.substr(6)); size_t sp = r.find(' '); std::string nm = sp == std::string::npos ? r : r.substr(0, sp); std::string desc = sp == std::string::npos ? "" : docTrim(r.substr(sp + 1)); tags += "- `" + nm + "` — " + desc + "\n"; }
        else if (l.rfind("@return", 0) == 0) { tags += "- **returns** — " + docTrim(l.substr(7)) + "\n"; }
        else prose += (prose.empty() ? "" : " ") + l;
    }
    std::string out; if (!prose.empty()) out += prose + "\n"; if (!tags.empty()) out += (prose.empty() ? "" : "\n") + tags; return docTrim(out);
}
// A one-line doc (prose only, no @tags) for compact member bullets.
static std::string docInline(const std::string& doc) {
    std::string b = docBody(doc); size_t nl = b.find('\n'); return nl == std::string::npos ? b : b.substr(0, nl);
}

static std::string generateDoc(const std::string& modName, Parsed& P, const std::map<int, std::string>& docs) {
    auto docOf = [&](int line) -> std::string { auto it = docs.find(line); return it == docs.end() ? "" : it->second; };
    std::string out = "# Module `" + modName + "`\n";
    if (!P.funcs.empty()) {
        out += "\n## Functions\n";
        for (auto& f : P.funcs) { out += "\n### `" + docFuncSig(f) + "`\n"; std::string d = docBody(docOf(f.nameLine)); if (!d.empty()) out += "\n" + d + "\n"; }
    }
    if (!P.classes.empty()) {
        out += "\n## Classes\n";
        for (auto& C : P.classes) {
            out += "\n### `" + C.name + "`\n"; std::string d = docBody(docOf(C.nameLine)); if (!d.empty()) out += "\n" + d + "\n";
            if (!C.fields.empty() || !C.signals.empty()) {
                out += "\n**Fields**\n";
                for (auto& fl : C.fields) { std::string fd = docInline(docOf(fl.nameLine)); out += "- `" + fl.name + (fl.type ? ": " + docTy(fl.type) : "") + "`" + (fd.empty() ? "" : " — " + fd) + "\n"; }
                for (auto& sg : C.signals) { std::string sd = docInline(docOf(sg.nameLine)); std::string sig = sg.name + "("; for (size_t k = 0; k < sg.params.size(); k++) { if (k) sig += ", "; sig += sg.params[k]; if (k < sg.ptypes.size() && sg.ptypes[k]) sig += ": " + docTy(sg.ptypes[k]); } sig += ")"; out += "- `signal " + sig + "`" + (sd.empty() ? "" : " — " + sd) + "\n"; }
            }
            if (!C.methods.empty()) {
                out += "\n**Methods**\n";
                for (auto& m : C.methods) { std::string md = docInline(docOf(m.nameLine)); out += "- `" + docFuncSig(m) + "`" + (md.empty() ? "" : " — " + md) + "\n"; }
            }
        }
    }
    if (!P.enums.empty()) {
        out += "\n## Enums\n";
        for (auto& E : P.enums) { out += "\n### `" + E.name + "`\n"; std::string d = docBody(docOf(E.nameLine)); if (!d.empty()) out += "\n" + d + "\n"; }
    }
    if (!P.traits.empty()) {
        out += "\n## Traits\n";
        for (auto& Tr : P.traits) { out += "\n### `" + Tr.name + "`\n"; std::string d = docBody(docOf(Tr.nameLine)); if (!d.empty()) out += "\n" + d + "\n"; }
    }
    return out;
}
