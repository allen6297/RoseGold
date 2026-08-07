#pragma once
#include <filesystem>
#include "parser.hpp"
#include "types.hpp"

// =====================================================================
// RoseGold Language Server  (rosegoldc --lsp)
//
// A single-threaded JSON-RPC-over-stdio server that reuses the real
// parser + type checker in-process. Provides:
//   • live diagnostics (parse / lex / type errors)
//   • hover            (type of the identifier under the cursor)
//   • go-to-definition (top-level symbols + class members)
//   • completion       (members after `.` on a class or module)
//
// LSP positions are 0-based (line, UTF-16 character); RoseGold tokens are
// 1-based (line, col). We treat characters as bytes (source is ASCII).
// =====================================================================

// ------------------------------- JSON --------------------------------
struct Json {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    bool b = false; double num = 0; std::string s;
    std::vector<Json> a; std::vector<std::pair<std::string, Json>> o;
    const Json* get(const std::string& k) const { if (t != OBJ) return nullptr; for (auto& p : o) if (p.first == k) return &p.second; return nullptr; }
    std::string str(const std::string& d = "") const { return t == STR ? s : d; }
    double n(double d = 0) const { return t == NUM ? num : d; }
    const Json* at(size_t i) const { return (t == ARR && i < a.size()) ? &a[i] : nullptr; }
};
struct JParse {
    const std::string& src; size_t i = 0;
    JParse(const std::string& s) : src(s) {}
    void ws() { while (i < src.size() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n' || src[i] == '\r')) i++; }
    Json parse() { return val(); }
    Json val() {
        ws(); if (i >= src.size()) return {};
        char c = src[i];
        if (c == '{') return objp(); if (c == '[') return arrp(); if (c == '"') return strp();
        if (c == 't' || c == 'f') return boolp(); if (c == 'n') { i += 4; return {}; }
        return nump();
    }
    Json strp() {
        Json j; j.t = Json::STR; i++;                     // opening quote
        while (i < src.size() && src[i] != '"') {
            char c = src[i++];
            if (c == '\\' && i < src.size()) {
                char e = src[i++];
                switch (e) { case 'n': j.s += '\n'; break; case 't': j.s += '\t'; break; case 'r': j.s += '\r'; break;
                    case 'b': j.s += '\b'; break; case 'f': j.s += '\f'; break; case '/': j.s += '/'; break;
                    case '"': j.s += '"'; break; case '\\': j.s += '\\'; break;
                    case 'u': { if (i + 4 <= src.size()) { int cp = (int)strtol(src.substr(i, 4).c_str(), nullptr, 16); i += 4;
                        if (cp < 0x80) j.s += (char)cp; else if (cp < 0x800) { j.s += (char)(0xC0 | (cp >> 6)); j.s += (char)(0x80 | (cp & 0x3F)); }
                        else { j.s += (char)(0xE0 | (cp >> 12)); j.s += (char)(0x80 | ((cp >> 6) & 0x3F)); j.s += (char)(0x80 | (cp & 0x3F)); } } break; }
                    default: j.s += e; }
            } else j.s += c;
        }
        if (i < src.size()) i++;                          // closing quote
        return j;
    }
    Json nump() { size_t st = i; while (i < src.size() && (isdigit((unsigned char)src[i]) || src[i] == '-' || src[i] == '+' || src[i] == '.' || src[i] == 'e' || src[i] == 'E')) i++; Json j; j.t = Json::NUM; j.num = strtod(src.substr(st, i - st).c_str(), nullptr); return j; }
    Json boolp() { Json j; j.t = Json::BOOL; if (src[i] == 't') { j.b = true; i += 4; } else { j.b = false; i += 5; } return j; }
    Json arrp() { Json j; j.t = Json::ARR; i++; ws(); if (i < src.size() && src[i] == ']') { i++; return j; } while (i < src.size()) { j.a.push_back(val()); ws(); if (i < src.size() && src[i] == ',') { i++; continue; } break; } ws(); if (i < src.size() && src[i] == ']') i++; return j; }
    Json objp() { Json j; j.t = Json::OBJ; i++; ws(); if (i < src.size() && src[i] == '}') { i++; return j; } while (i < src.size()) { ws(); Json k = strp(); ws(); if (i < src.size() && src[i] == ':') i++; Json v = val(); j.o.emplace_back(k.s, std::move(v)); ws(); if (i < src.size() && src[i] == ',') { i++; continue; } break; } ws(); if (i < src.size() && src[i] == '}') i++; return j; }
};
static std::string jesc(const std::string& s) {
    std::string r; for (char c : s) {
        switch (c) { case '"': r += "\\\""; break; case '\\': r += "\\\\"; break; case '\n': r += "\\n"; break;
            case '\t': r += "\\t"; break; case '\r': r += "\\r"; break;
            default: if ((unsigned char)c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); r += buf; } else r += c; }
    }
    return r;
}
static std::string jstr(const std::string& s) { return "\"" + jesc(s) + "\""; }

// --------------------------- file helpers ----------------------------
static std::string lspSlurp(const std::string& path) { std::ifstream f(path); if (!f) throw std::runtime_error("cannot open " + path); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static std::string lspDirOf(const std::string& p) { auto s = p.find_last_of('/'); return s == std::string::npos ? "." : p.substr(0, s); }
static std::string lspModToFile(const std::string& root, const std::string& mod) { std::string r = root + "/"; for (char c : mod) r += (c == '.') ? '/' : c; return r + ".rg"; }
static std::string uriToPath(const std::string& uri) {
    std::string p = uri; const std::string pre = "file://";
    if (p.rfind(pre, 0) == 0) p = p.substr(pre.size());
    std::string out; for (size_t i = 0; i < p.size(); i++) { if (p[i] == '%' && i + 2 < p.size()) { out += (char)strtol(p.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; } else out += p[i]; }
    return out;
}
static std::string pathToUri(const std::string& path) {
    std::string out = "file://"; for (char c : path) { if (c == ' ') out += "%20"; else out += c; } return out;
}
static int extractLine(const std::string& msg) {   // pull "line N" out of a parse/lex error
    auto p = msg.find("line "); if (p == std::string::npos) return 0; return atoi(msg.c_str() + p + 5);
}
static std::string stripLinePrefix(const std::string& msg) {   // drop a leading "line N: " (the range already carries the line)
    if (msg.rfind("line ", 0) != 0) return msg; auto c = msg.find(": "); size_t k = 5; while (k < msg.size() && isdigit((unsigned char)msg[k])) k++;
    return (c != std::string::npos && c == k) ? msg.substr(c + 2) : msg;
}

// ----------------------- analysis result cache -----------------------
struct DocState {
    std::string text, path, root, entryMod;
    std::vector<Occ> occs;                                   // identifier index for the open file (declarations + uses)
    std::vector<InlayH> inlays;                              // inferred-type hints for the open file
    std::map<std::string, std::string> modFile;             // module key -> source path
    std::set<std::pair<int, int>> enumDecls;                // enum-name positions (enum/variant symbols aren't renamable in v1)
};

struct Lsp {
    std::map<std::string, DocState> docs;                    // keyed by uri
    bool initialized = false;
    std::string wsRoot;                                      // workspace root path (for cross-file references/rename)

    // --- transport -------------------------------------------------------
    bool readMessage(std::string& body) {
        size_t len = 0; std::string line;
        while (true) {
            if (!std::getline(std::cin, line)) return false;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break;                          // end of headers
            const std::string h = "Content-Length:";
            if (line.rfind(h, 0) == 0) len = (size_t)atol(line.c_str() + h.size());
        }
        body.resize(len);
        std::cin.read(&body[0], (std::streamsize)len);
        return (bool)std::cin;
    }
    void send(const std::string& body) { std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body; std::cout.flush(); }
    void reply(const Json& id, const std::string& resultJson) {
        std::string ids = id.t == Json::STR ? jstr(id.s) : (id.t == Json::NUM ? std::to_string((long long)id.num) : "null");
        send("{\"jsonrpc\":\"2.0\",\"id\":" + ids + ",\"result\":" + resultJson + "}");
    }
    void notify(const std::string& method, const std::string& paramsJson) {
        send("{\"jsonrpc\":\"2.0\",\"method\":" + jstr(method) + ",\"params\":" + paramsJson + "}");
    }

    // --- module graph loading (entry text may be an in-memory override) ---
    // Returns false on a parse/lex failure, pushing a diagnostic into `diags`.
    bool loadGraph(const std::string& entryPath, const std::string* override_,
                   std::map<std::string, Parsed>& mods, std::vector<std::string>& order,
                   std::string& entryMod, std::string& root, std::map<std::string, std::string>& modFile,
                   std::vector<std::tuple<std::string, int, std::string>>& diags) {
        root = lspDirOf(entryPath);
        try {
            std::string txt = override_ ? *override_ : lspSlurp(entryPath);
            Parsed entry = Parser{lex(txt)}.program();
            entryMod = entry.module.empty() ? "$entry" : entry.module;
            mods[entryMod] = std::move(entry); modFile[entryMod] = entryPath;
        } catch (const std::exception& e) { diags.emplace_back(entryMod.empty() ? "$entry" : entryMod, extractLine(e.what()), stripLinePrefix(e.what())); return false; }
        std::vector<std::string> work = {entryMod};
        while (!work.empty()) {
            std::string m = work.back(); work.pop_back();
            for (auto& imp : mods[m].imports) {
                if (mods.count(imp.path)) continue;
                std::string f = lspModToFile(root, imp.path);
                try { mods[imp.path] = Parser{lex(lspSlurp(f))}.program(); modFile[imp.path] = f; work.push_back(imp.path); }
                catch (const std::exception&) { /* missing/broken import: leave unresolved, checker reports use sites */ }
            }
        }
        std::set<std::string> done, active;
        std::function<void(const std::string&)> dfs = [&](const std::string& m) {
            if (done.count(m) || active.count(m)) return; active.insert(m);
            for (auto& imp : mods[m].imports) if (mods.count(imp.path)) dfs(imp.path);
            active.erase(m); done.insert(m); order.push_back(m);
        };
        dfs(entryMod);
        return true;
    }

    // --- diagnostics + occurrence index ---------------------------------
    void analyze(const std::string& uri) {
        DocState& d = docs[uri];
        d.occs.clear(); d.inlays.clear(); d.modFile.clear(); d.enumDecls.clear();
        std::vector<std::tuple<std::string, int, std::string>> diags;
        std::map<std::string, Parsed> mods; std::vector<std::string> order;
        if (loadGraph(d.path, &d.text, mods, order, d.entryMod, d.root, d.modFile, diags)) {
            TypeChecker tc(mods, order); tc.recordOcc = true;
            try { tc.build(); tc.check(); } catch (const std::exception&) {}
            for (auto& e : tc.errors) diags.push_back(e);
            for (auto& oc : tc.occs) if (oc.occMod == d.entryMod) d.occs.push_back(oc);
            for (auto& h : tc.inlays) if (h.mod == d.entryMod) d.inlays.push_back(h);
            for (auto& E : mods[d.entryMod].enums) d.enumDecls.insert({E.nameLine, E.nameCol});
        }
        publishDiagnostics(uri, d.entryMod, diags);
    }
    void publishDiagnostics(const std::string& uri, const std::string& entryMod,
                            std::vector<std::tuple<std::string, int, std::string>>& diags) {
        std::string items;
        for (auto& e : diags) {
            const std::string& mm = std::get<0>(e); int ln = std::get<1>(e); const std::string& msg = std::get<2>(e);
            if (mm != entryMod && mm != "$entry") continue;                 // only mark the open file
            int l = ln > 0 ? ln - 1 : 0;
            if (!items.empty()) items += ",";
            items += "{\"range\":{\"start\":{\"line\":" + std::to_string(l) + ",\"character\":0},"
                     "\"end\":{\"line\":" + std::to_string(l) + ",\"character\":500}},"
                     "\"severity\":1,\"source\":\"rosegold\",\"message\":" + jstr(msg) + "}";
        }
        notify("textDocument/publishDiagnostics", "{\"uri\":" + jstr(uri) + ",\"diagnostics\":[" + items + "]}");
    }

    // find the identifier occurrence covering a 0-based (line,char)
    const Occ* occAt(const DocState& d, int line, int ch) const {
        for (auto& o : d.occs) { if (o.line - 1 != line) continue; int c0 = o.col - 1; if (ch >= c0 && ch < c0 + o.len) return &o; }
        return nullptr;
    }

    // --- request handlers ------------------------------------------------
    void onHover(const Json& id, const Json& params) {
        auto [uri, line, ch] = docPos(params);
        auto it = docs.find(uri); if (it == docs.end()) { reply(id, "null"); return; }
        const Occ* o = occAt(it->second, line, ch);
        if (!o) { reply(id, "null"); return; }
        std::string sig = o->name + ": " + tStr(o->ty);
        std::string md = "```rosegold\n" + sig + "\n```";
        reply(id, "{\"contents\":{\"kind\":\"markdown\",\"value\":" + jstr(md) + "}}");
    }
    void onDefinition(const Json& id, const Json& params) {
        auto [uri, line, ch] = docPos(params);
        auto it = docs.find(uri); if (it == docs.end()) { reply(id, "null"); return; }
        const DocState& d = it->second;
        const Occ* o = occAt(d, line, ch);
        if (!o || o->defLine <= 0) { reply(id, "null"); return; }
        auto mf = d.modFile.find(o->defMod);
        std::string defUri = (mf != d.modFile.end()) ? pathToUri(mf->second) : uri;
        int l = o->defLine - 1, c = o->defCol - 1;
        std::string range = "{\"start\":{\"line\":" + std::to_string(l) + ",\"character\":" + std::to_string(c) + "},"
                            "\"end\":{\"line\":" + std::to_string(l) + ",\"character\":" + std::to_string(c + (int)o->name.size()) + "}}";
        reply(id, "[{\"uri\":" + jstr(defUri) + ",\"range\":" + range + "}]");
    }
    void onCompletion(const Json& id, const Json& params) {
        auto [uri, line, ch] = docPos(params);
        auto it = docs.find(uri); if (it == docs.end()) { reply(id, "{\"isIncomplete\":false,\"items\":[]}"); return; }
        std::string items = completionItems(it->second, line, ch);
        reply(id, "{\"isIncomplete\":false,\"items\":[" + items + "]}");
    }

    // --- references / rename --------------------------------------------
    static std::string rng(int l0, int c0, int l1, int c1) {
        return "{\"start\":{\"line\":" + std::to_string(l0) + ",\"character\":" + std::to_string(c0) + "},"
               "\"end\":{\"line\":" + std::to_string(l1) + ",\"character\":" + std::to_string(c1) + "}}";
    }
    static std::string rangeJson(int line0, int c0, int c1) { return rng(line0, c0, line0, c1); }
    bool enumBacked(const DocState& d, const Occ* o) const { return o && o->defLine > 0 && d.enumDecls.count({o->defLine, o->defCol}); }
    // All occurrences (declaration + uses) that share the symbol's definition identity.
    std::vector<const Occ*> refsOf(const DocState& d, const Occ* sym, bool inclDecl) const {
        std::vector<const Occ*> out; if (!sym) return out;
        bool hasDef = sym->defLine > 0;
        for (auto& o : d.occs) {
            bool same = hasDef ? (o.defMod == sym->defMod && o.defLine == sym->defLine && o.defCol == sym->defCol) : (&o == sym);
            if (!same) continue;
            bool isDecl = hasDef && o.occMod == sym->defMod && o.line == sym->defLine && o.col == sym->defCol;
            if (isDecl && !inclDecl) continue;
            out.push_back(&o);
        }
        return out;
    }
    // --- workspace scan (cross-file references / rename) ----------------
    std::vector<std::string> workspaceFiles() const {
        std::vector<std::string> out; if (wsRoot.empty()) return out;
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(wsRoot, ec), e; !ec && it != e; it.increment(ec))
            if (it->is_regular_file(ec) && it->path().extension() == ".rg") out.push_back(it->path().string());
        return out;
    }
    static std::string slurpSafe(const std::string& p) { try { return lspSlurp(p); } catch (...) { return ""; } }
    // Analyze one file as an entry; append its OWN-module occurrences to `occs`. Returns the module name.
    bool analyzeFile(const std::string& path, const std::string* override_, std::vector<Occ>& occs, std::string& entryMod) {
        std::map<std::string, Parsed> mods; std::vector<std::string> order; std::string root; std::map<std::string, std::string> mf; std::vector<std::tuple<std::string, int, std::string>> diags;
        if (!loadGraph(path, override_, mods, order, entryMod, root, mf, diags)) return false;
        TypeChecker tc(mods, order); tc.recordOcc = true;
        try { tc.build(); tc.check(); } catch (const std::exception&) {}
        for (auto& o : tc.occs) if (o.occMod == entryMod) occs.push_back(o);
        return true;
    }
    // All references to a symbol (by definition identity) across every workspace .rg file; keyed by file path.
    std::map<std::string, std::vector<const Occ*>> workspaceRefs(const std::string& openUri, const std::string& openText,
                                                                 const Occ& sym, bool inclDecl, std::vector<std::vector<Occ>>& storage) {
        std::map<std::string, std::vector<const Occ*>> out;
        for (auto& path : workspaceFiles()) {
            std::string uri = pathToUri(path);
            bool isOpen = (uri == openUri);
            std::string text = isOpen ? openText : slurpSafe(path);
            if (text.find(sym.name) == std::string::npos) continue;          // quick prune
            storage.emplace_back(); std::vector<Occ>& occs = storage.back(); std::string em;
            const std::string* ov = isOpen ? &openText : nullptr;
            if (!analyzeFile(path, ov, occs, em)) { storage.pop_back(); continue; }
            std::vector<const Occ*> hits;
            for (auto& o : occs) {
                if (o.defMod != sym.defMod || o.defLine != sym.defLine || o.defCol != sym.defCol) continue;
                bool isDecl = o.occMod == sym.defMod && o.line == sym.defLine && o.col == sym.defCol;
                if (isDecl && !inclDecl) continue;
                hits.push_back(&o);
            }
            if (!hits.empty()) out[path] = std::move(hits);
        }
        return out;
    }
    void onReferences(const Json& id, const Json& params) {
        auto [uri, line, ch] = docPos(params);
        bool inclDecl = true; if (auto ctx = params.get("context")) if (auto v = ctx->get("includeDeclaration")) inclDecl = v->b;
        auto it = docs.find(uri); if (it == docs.end()) { reply(id, "null"); return; }
        const DocState& d = it->second; const Occ* sym = occAt(d, line, ch);
        if (!sym || enumBacked(d, sym)) { reply(id, "null"); return; }
        std::string arr;
        if (!wsRoot.empty() && sym->defLine > 0) {                            // workspace-wide
            std::vector<std::vector<Occ>> storage;
            for (auto& kv : workspaceRefs(uri, d.text, *sym, inclDecl, storage)) {
                std::string furi = pathToUri(kv.first);
                for (auto* o : kv.second) { if (!arr.empty()) arr += ","; arr += "{\"uri\":" + jstr(furi) + ",\"range\":" + rangeJson(o->line - 1, o->col - 1, o->col - 1 + o->len) + "}"; }
            }
        } else {                                                             // current file only
            for (auto* o : refsOf(d, sym, inclDecl)) { if (!arr.empty()) arr += ","; arr += "{\"uri\":" + jstr(uri) + ",\"range\":" + rangeJson(o->line - 1, o->col - 1, o->col - 1 + o->len) + "}"; }
        }
        reply(id, "[" + arr + "]");
    }
    // Renamable iff the symbol has a definition and isn't enum/variant-backed. With a workspace we edit every
    // file (safe cross-file rename); without one we restrict to the open file so we never leave a file stale.
    bool renamable(const DocState& d, const Occ* sym) const {
        if (!sym || sym->defLine <= 0 || enumBacked(d, sym)) return false;
        return !wsRoot.empty() || sym->defMod == d.entryMod;
    }
    void onPrepareRename(const Json& id, const Json& params) {
        auto [uri, line, ch] = docPos(params);
        auto it = docs.find(uri); if (it == docs.end()) { reply(id, "null"); return; }
        const DocState& d = it->second; const Occ* sym = occAt(d, line, ch);
        if (!renamable(d, sym)) { reply(id, "null"); return; }
        reply(id, rangeJson(sym->line - 1, sym->col - 1, sym->col - 1 + sym->len));
    }
    static std::string editJson(const Occ* o, const std::string& newName) {
        return "{\"range\":" + rangeJson(o->line - 1, o->col - 1, o->col - 1 + o->len) + ",\"newText\":" + jstr(newName) + "}";
    }
    void onRename(const Json& id, const Json& params) {
        auto [uri, line, ch] = docPos(params);
        std::string newName; if (auto n = params.get("newName")) newName = n->s;
        auto it = docs.find(uri); if (it == docs.end()) { reply(id, "null"); return; }
        const DocState& d = it->second; const Occ* sym = occAt(d, line, ch);
        if (!renamable(d, sym) || newName.empty()) { reply(id, "null"); return; }
        std::string changes;
        auto addFile = [&](const std::string& furi, const std::string& edits) { if (!changes.empty()) changes += ","; changes += jstr(furi) + ":[" + edits + "]"; };
        if (!wsRoot.empty()) {                                                // workspace-wide edit
            std::vector<std::vector<Occ>> storage;
            for (auto& kv : workspaceRefs(uri, d.text, *sym, true, storage)) {
                std::string edits; for (auto* o : kv.second) { if (!edits.empty()) edits += ","; edits += editJson(o, newName); }
                addFile(pathToUri(kv.first), edits);
            }
        } else {                                                             // current file only
            std::string edits; for (auto* o : refsOf(d, sym, true)) { if (!edits.empty()) edits += ","; edits += editJson(o, newName); }
            addFile(uri, edits);
        }
        reply(id, "{\"changes\":{" + changes + "}}");
    }

    // --- document symbols (outline) -------------------------------------
    static std::string joinNames(const std::vector<std::string>& v) { std::string s; for (size_t i = 0; i < v.size(); i++) { if (i) s += ", "; s += v[i]; } return s; }
    // One DocumentSymbol; range spans [nameLine..endLine], selectionRange is the name.
    std::string dsym(const std::string& name, int kind, int nl, int nc, const std::string& detail, int endL, int endC, const std::string& children) {
        if (nl <= 0) return "";
        int l = nl - 1, c = nc - 1;
        std::string s = "{\"name\":" + jstr(name) + ",\"kind\":" + std::to_string(kind);
        if (!detail.empty()) s += ",\"detail\":" + jstr(detail);
        s += ",\"range\":" + rng(l, 0, endL, endC);
        s += ",\"selectionRange\":" + rng(l, c, l, c + (int)name.size());
        s += ",\"children\":[" + children + "]}";
        return s;
    }
    void onDocumentSymbol(const Json& id, const Json& params) {
        std::string uri; if (auto td = params.get("textDocument")) if (auto u = td->get("uri")) uri = u->s;
        auto it = docs.find(uri); if (it == docs.end()) { reply(id, "[]"); return; }
        Parsed p;
        try { p = Parser{lex(it->second.text)}.program(); } catch (const std::exception&) { reply(id, "[]"); return; }
        std::string arr;
        auto push = [&](const std::string& s) { if (s.empty()) return; if (!arr.empty()) arr += ","; arr += s; };
        for (auto& e : p.enums) push(dsym(e.name, 10 /*Enum*/, e.nameLine, e.nameCol, "", e.nameLine - 1, e.nameCol - 1 + (int)e.name.size(), ""));
        for (auto& g : p.globals) if (g.k == Stmt::VAR) push(dsym(g.name, 13 /*Variable*/, g.nameLine, g.nameCol, "", g.nameLine - 1, g.nameCol - 1 + (int)g.name.size(), ""));
        for (auto& f : p.funcs) push(dsym(f.name, 12 /*Function*/, f.nameLine, f.nameCol, "(" + joinNames(f.params) + ")", f.nameLine - 1, f.nameCol - 1 + (int)f.name.size(), ""));
        for (auto& tr : p.traits) {
            std::string kids; int endL = tr.nameLine - 1;
            for (auto& m : tr.methods) { if (m.nameLine - 1 > endL) endL = m.nameLine - 1; std::string s = dsym(m.name, 6 /*Method*/, m.nameLine, m.nameCol, "(" + joinNames(m.params) + ")", m.nameLine - 1, m.nameCol - 1 + (int)m.name.size(), ""); if (!s.empty()) { if (!kids.empty()) kids += ","; kids += s; } }
            push(dsym(tr.name, 11 /*Interface*/, tr.nameLine, tr.nameCol, "", endL, 999, kids));
        }
        for (auto& c : p.classes) {
            std::string kids; int endL = c.nameLine - 1;
            auto add = [&](const std::string& s, int ln) { if (s.empty()) return; if (ln - 1 > endL) endL = ln - 1; if (!kids.empty()) kids += ","; kids += s; };
            for (auto& fld : c.fields) add(dsym(fld.name, 8 /*Field*/, fld.nameLine, fld.nameCol, "", fld.nameLine - 1, fld.nameCol - 1 + (int)fld.name.size(), ""), fld.nameLine);
            for (auto& m : c.methods) add(dsym(m.name, 6 /*Method*/, m.nameLine, m.nameCol, "(" + joinNames(m.params) + ")", m.nameLine - 1, m.nameCol - 1 + (int)m.name.size(), ""), m.nameLine);
            push(dsym(c.name, 5 /*Class*/, c.nameLine, c.nameCol, "", endL, 999, kids));
        }
        reply(id, "[" + arr + "]");
    }

    // --- semantic tokens (type-aware highlighting) ----------------------
    void onSemanticTokens(const Json& id, const Json& params) {
        std::string uri; if (auto td = params.get("textDocument")) if (auto u = td->get("uri")) uri = u->s;
        auto it = docs.find(uri); if (it == docs.end()) { reply(id, "{\"data\":[]}"); return; }
        const DocState& d = it->second;
        std::vector<const Occ*> toks;
        for (auto& o : d.occs) if (o.sem >= 0 && o.occMod == d.entryMod && o.line > 0) toks.push_back(&o);
        std::sort(toks.begin(), toks.end(), [](const Occ* a, const Occ* b) { return a->line != b->line ? a->line < b->line : a->col < b->col; });
        std::string data; int pl = 0, pc = 0, seenL = -1, seenC = -1;
        for (auto* o : toks) {
            int line = o->line - 1, col = o->col - 1;
            if (line == seenL && col == seenC) continue;                       // dedupe overlaps
            seenL = line; seenC = col;
            int dl = line - pl, dc = (dl == 0) ? col - pc : col;
            if (!data.empty()) data += ",";
            data += std::to_string(dl) + "," + std::to_string(dc) + "," + std::to_string(o->len) + "," + std::to_string(o->sem) + ",0";
            pl = line; pc = col;
        }
        reply(id, "{\"data\":[" + data + "]}");
    }

    // --- document highlight (all occurrences of the symbol under the cursor) ---
    void onDocumentHighlight(const Json& id, const Json& params) {
        auto [uri, line, ch] = docPos(params);
        auto it = docs.find(uri); if (it == docs.end()) { reply(id, "null"); return; }
        const DocState& d = it->second; const Occ* sym = occAt(d, line, ch);
        if (!sym) { reply(id, "null"); return; }
        std::string arr;
        for (auto* o : refsOf(d, sym, true)) { if (!arr.empty()) arr += ","; arr += "{\"range\":" + rangeJson(o->line - 1, o->col - 1, o->col - 1 + o->len) + ",\"kind\":1}"; }
        reply(id, "[" + arr + "]");
    }

    // --- folding ranges (indentation-based) -----------------------------
    void onFoldingRange(const Json& id, const Json& params) {
        std::string uri; if (auto td = params.get("textDocument")) if (auto u = td->get("uri")) uri = u->s;
        auto it = docs.find(uri); if (it == docs.end()) { reply(id, "[]"); return; }
        std::vector<std::string> lines; { std::string cur; for (char c : it->second.text) { if (c == '\n') { lines.push_back(cur); cur.clear(); } else cur += c; } lines.push_back(cur); }
        auto indent = [](const std::string& s) { int k = 0; while (k < (int)s.size() && s[k] == ' ') k++; return k == (int)s.size() ? -1 : k; };   // -1 = blank
        std::string arr; int n = (int)lines.size();
        for (int i = 0; i < n; i++) {
            int ii = indent(lines[i]); if (ii < 0) continue;
            int j = i + 1; while (j < n && indent(lines[j]) < 0) j++;
            if (j < n && indent(lines[j]) > ii) {
                int last = j, k = j;
                while (k < n) { int ik = indent(lines[k]); if (ik < 0) { k++; continue; } if (ik > ii) { last = k; k++; } else break; }
                if (!arr.empty()) arr += ",";
                arr += "{\"startLine\":" + std::to_string(i) + ",\"endLine\":" + std::to_string(last) + "}";
            }
        }
        reply(id, "[" + arr + "]");
    }

    // --- inlay hints (inferred types on un-annotated vars) --------------
    void onInlayHint(const Json& id, const Json& params) {
        std::string uri; if (auto td = params.get("textDocument")) if (auto u = td->get("uri")) uri = u->s;
        auto it = docs.find(uri); if (it == docs.end()) { reply(id, "[]"); return; }
        int lo = 0, hi = 1 << 30;
        if (auto r = params.get("range")) { if (auto s = r->get("start")) if (auto l = s->get("line")) lo = (int)l->num; if (auto e = r->get("end")) if (auto l = e->get("line")) hi = (int)l->num; }
        std::string arr;
        for (auto& h : it->second.inlays) { int ln = h.line - 1; if (ln < lo || ln > hi) continue; if (!arr.empty()) arr += ","; arr += "{\"position\":{\"line\":" + std::to_string(ln) + ",\"character\":" + std::to_string(h.col - 1) + "},\"label\":" + jstr(h.label) + ",\"kind\":1,\"paddingLeft\":false}"; }
        reply(id, "[" + arr + "]");
    }

    // --- signature help --------------------------------------------------
    void onSignatureHelp(const Json& id, const Json& params) {
        auto [uri, line, ch] = docPos(params);
        auto it = docs.find(uri); if (it == docs.end()) { reply(id, "null"); return; }
        const DocState& d = it->second;
        size_t cur = offsetOf(d.text, line, ch);
        // Scan back for the innermost unclosed '(' (the enclosing call) and count top-level commas.
        int depth = 0, activeParam = 0; size_t i = cur, openp = 0; bool found = false;
        while (i > 0) {
            char c = d.text[i - 1];
            if (c == ')' || c == ']') depth++;
            else if (c == '(' || c == '[') { if (depth == 0) { if (c == '(') { openp = i - 1; found = true; } break; } depth--; }
            else if (c == ',' && depth == 0) activeParam++;
            else if (c == '\n' && depth == 0) break;
            i--;
        }
        if (!found) { reply(id, "null"); return; }
        // The callee identifier is immediately before '('.
        size_t j = openp; while (j > 0 && (d.text[j - 1] == ' ' || d.text[j - 1] == '\t')) j--;
        size_t end = j; while (j > 0 && (isalnum((unsigned char)d.text[j - 1]) || d.text[j - 1] == '_')) j--;
        if (j == end) { reply(id, "null"); return; }
        size_t calleeOff = j;
        int cl = 0; size_t ls = 0; for (size_t k = 0; k < calleeOff; k++) if (d.text[k] == '\n') { cl++; ls = k + 1; }
        int cc = (int)(calleeOff - ls);
        auto funcOccAt = [&](const std::vector<Occ>& list, const std::string& mod) -> const Occ* {
            for (auto& o : list) if (o.occMod == mod && o.line - 1 == cl && cc >= o.col - 1 && cc < o.col - 1 + o.len && o.ty && o.ty->k == Ty::FUNC) return &o;
            return nullptr;
        };
        auto emit = [&](const Occ* fo) {
            TyP ft = fo->ty;
            std::string label = fo->name + "(", pjson;
            auto addParam = [&](const std::string& seg) { int a = (int)label.size(); label += seg; int b = (int)label.size(); if (!pjson.empty()) pjson += ","; pjson += "{\"label\":[" + std::to_string(a) + "," + std::to_string(b) + "]}"; };
            int nparams;
            if (ft->variadic) { addParam("..."); nparams = 1; }
            else { for (size_t k = 0; k < ft->args.size(); k++) { if (k) label += ", "; addParam(tStr(ft->args[k])); } nparams = (int)ft->args.size(); }
            label += ") -> " + tStr(ft->ret);
            int active = nparams > 0 ? std::min(activeParam, nparams - 1) : 0;
            reply(id, "{\"signatures\":[{\"label\":" + jstr(label) + ",\"parameters\":[" + pjson + "]}],\"activeSignature\":0,\"activeParameter\":" + std::to_string(active) + "}");
        };
        // If the call is already closed, the file parses -> use the current index; otherwise patch a ')' in and re-check.
        bool closed = false; { int dep = 0; for (size_t k = openp; k < d.text.size(); k++) { char c = d.text[k]; if (c == '(' || c == '[') dep++; else if (c == ')' || c == ']') { if (--dep == 0) { closed = true; break; } } } }
        if (closed) { if (auto* fo = funcOccAt(d.occs, d.entryMod)) { emit(fo); return; } }
        std::map<std::string, Parsed> mods; std::vector<std::string> order; std::string em, root; std::map<std::string, std::string> mf; std::vector<std::tuple<std::string, int, std::string>> diags;
        std::string patched = d.text.substr(0, cur) + "Xrgsig)" + d.text.substr(cur);
        if (loadGraph(d.path, &patched, mods, order, em, root, mf, diags)) {
            TypeChecker tc(mods, order); tc.recordOcc = true; try { tc.build(); tc.check(); } catch (const std::exception&) {}
            if (auto* fo = funcOccAt(tc.occs, em)) { emit(fo); return; }
        }
        reply(id, "null");
    }

    // Completion: reparse a marker-patched copy of the text so `foo.` parses,
    // then look up the receiver's class/module and enumerate its members.
    std::string completionItems(const DocState& d, int line, int ch) {
        const std::string MARK = "XRGCOMPLZ";
        size_t off = offsetOf(d.text, line, ch);
        std::string patched = d.text.substr(0, off) + MARK + d.text.substr(off);
        std::map<std::string, Parsed> mods; std::vector<std::string> order; std::string em, root; std::map<std::string, std::string> mf;
        std::vector<std::tuple<std::string, int, std::string>> diags;
        if (!loadGraph(d.path, &patched, mods, order, em, root, mf, diags)) return "";
        TypeChecker tc(mods, order); tc.recordOcc = true;
        try { tc.build(); tc.check(); } catch (const std::exception&) {}
        const Occ* recv = nullptr;
        for (auto& o : tc.occs) if (o.recv && o.name.find(MARK) != std::string::npos) { recv = &o; break; }
        if (!recv) return "";
        tc.curm = em;
        std::string out;
        auto add = [&](const std::string& label, int kind, const std::string& detail) {
            if (!out.empty()) out += ",";
            out += "{\"label\":" + jstr(label) + ",\"kind\":" + std::to_string(kind) + ",\"detail\":" + jstr(detail) + "}";
        };
        if (!recv->recvClass.empty()) {
            std::set<std::string> seen; std::string c = recv->recvClass;
            while (!c.empty()) { ClassAst* C = tc.classAst(em, c); if (!C) break;
                for (auto& f : C->fields) if (seen.insert(f.name).second) add(f.name, 5 /*Field*/, tStr(tc.resolveType(f.type, {}, em)));
                for (auto& m : C->methods) if (seen.insert(m.name).second) add(m.name, 2 /*Method*/, tStr(tc.funcType(m, em, C->generics)));
                c = C->extends; }
        } else if (!recv->recvMod.empty() && tc.T.count(recv->recvMod)) {
            auto& MT = tc.T[recv->recvMod];
            for (auto& kv : MT.pub) {
                const std::string& n = kv.first; TyP t = kv.second; int kind;
                if (MT.classes.count(n)) kind = 7;            // Class
                else if (MT.enums.count(n)) kind = 13;        // Enum
                else if (MT.traits.count(n)) kind = 8;        // Interface (trait)
                else if (t && t->k == Ty::FUNC) kind = (t->ret && t->ret->k == Ty::NAMED && t->ret->nkind == 1) ? 20 /*EnumMember ctor*/ : 3 /*Function*/;
                else if (t && t->k == Ty::NAMED && t->nkind == 1) kind = 20;  // nullary variant
                else kind = 6;                                // Variable
                add(n, kind, tStr(t));
            }
        }
        return out;
    }

    // --- position helpers ------------------------------------------------
    std::tuple<std::string, int, int> docPos(const Json& params) {
        std::string uri; int line = 0, ch = 0;
        if (auto td = params.get("textDocument")) if (auto u = td->get("uri")) uri = u->s;
        if (auto p = params.get("position")) { if (auto l = p->get("line")) line = (int)l->num; if (auto c = p->get("character")) ch = (int)c->num; }
        return {uri, line, ch};
    }
    size_t offsetOf(const std::string& text, int line, int ch) {
        size_t i = 0; int cur = 0;
        while (cur < line && i < text.size()) { if (text[i] == '\n') cur++; i++; }
        return std::min(i + (size_t)ch, text.size());
    }

    // --- main loop -------------------------------------------------------
    int run() {
        std::ios::sync_with_stdio(false);
        std::string body;
        while (readMessage(body)) {
            Json msg = JParse(body).parse();
            const Json* m = msg.get("method"); if (!m) continue;
            std::string method = m->s;
            const Json* idp = msg.get("id");
            const Json* params = msg.get("params");
            Json empty; if (!params) params = &empty;
            if (method == "initialize") {
                initialized = true;
                if (auto wf = params->get("workspaceFolders")) { if (wf->t == Json::ARR && !wf->a.empty()) if (auto u = wf->a[0].get("uri")) wsRoot = uriToPath(u->s); }
                if (wsRoot.empty()) if (auto ru = params->get("rootUri")) { if (ru->t == Json::STR) wsRoot = uriToPath(ru->s); }
                if (wsRoot.empty()) if (auto rp = params->get("rootPath")) { if (rp->t == Json::STR) wsRoot = rp->s; }
                reply(*idp, "{\"capabilities\":{\"textDocumentSync\":1,\"hoverProvider\":true,"
                            "\"definitionProvider\":true,\"referencesProvider\":true,"
                            "\"documentSymbolProvider\":true,\"documentHighlightProvider\":true,"
                            "\"foldingRangeProvider\":true,\"inlayHintProvider\":true,"
                            "\"semanticTokensProvider\":{\"legend\":{\"tokenTypes\":[\"type\",\"class\",\"enum\",\"interface\",\"function\",\"method\",\"property\",\"variable\",\"parameter\"],\"tokenModifiers\":[]},\"full\":true},"
                            "\"renameProvider\":{\"prepareProvider\":true},"
                            "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]},"
                            "\"completionProvider\":{\"triggerCharacters\":[\".\"]}},"
                            "\"serverInfo\":{\"name\":\"rosegold-lsp\",\"version\":\"0.1.0\"}}");
            } else if (method == "initialized") {
                // no-op
            } else if (method == "shutdown") {
                reply(*idp, "null");
            } else if (method == "exit") {
                return 0;
            } else if (method == "textDocument/didOpen") {
                if (auto td = params->get("textDocument")) { std::string uri = td->get("uri")->s; DocState d; d.text = td->get("text") ? td->get("text")->s : ""; d.path = uriToPath(uri); docs[uri] = std::move(d); analyze(uri); }
            } else if (method == "textDocument/didChange") {
                if (auto td = params->get("textDocument")) { std::string uri = td->get("uri")->s; auto cc = params->get("contentChanges"); if (cc && !cc->a.empty()) { auto last = cc->a.back(); if (auto txt = last.get("text")) docs[uri].text = txt->s; if (docs[uri].path.empty()) docs[uri].path = uriToPath(uri); } analyze(uri); }
            } else if (method == "textDocument/didClose") {
                if (auto td = params->get("textDocument")) docs.erase(td->get("uri")->s);
            } else if (method == "textDocument/hover") {
                onHover(*idp, *params);
            } else if (method == "textDocument/definition") {
                onDefinition(*idp, *params);
            } else if (method == "textDocument/completion") {
                onCompletion(*idp, *params);
            } else if (method == "textDocument/references") {
                onReferences(*idp, *params);
            } else if (method == "textDocument/prepareRename") {
                onPrepareRename(*idp, *params);
            } else if (method == "textDocument/rename") {
                onRename(*idp, *params);
            } else if (method == "textDocument/documentSymbol") {
                onDocumentSymbol(*idp, *params);
            } else if (method == "textDocument/signatureHelp") {
                onSignatureHelp(*idp, *params);
            } else if (method == "textDocument/semanticTokens/full") {
                onSemanticTokens(*idp, *params);
            } else if (method == "textDocument/documentHighlight") {
                onDocumentHighlight(*idp, *params);
            } else if (method == "textDocument/foldingRange") {
                onFoldingRange(*idp, *params);
            } else if (method == "textDocument/inlayHint") {
                onInlayHint(*idp, *params);
            } else if (idp) {
                reply(*idp, "null");                         // unknown request: null result
            }
        }
        return 0;
    }
};

static int runLsp() { Lsp s; return s.run(); }
