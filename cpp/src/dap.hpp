#pragma once
#include <filesystem>
#include "runtime.hpp"
#include "json.hpp"

// =====================================================================
// RoseGold Debug Adapter  (rosegoldc --dap)
//
// A single-threaded Debug Adapter Protocol server (Content-Length-framed
// JSON over stdio) that runs a .rg program under the bytecode VM and
// drives it through g_dbgHook: line breakpoints, step in/over/out, a call
// stack, and local/global variable inspection. Because the VM and the
// adapter share one thread, a stop just enters a nested request loop that
// answers inspection requests from the live VM state, then returns to let
// the VM continue. The debuggee's stdout is captured and forwarded as DAP
// `output` events so it can't corrupt the protocol stream.
// =====================================================================
struct Dbg {
    // --- transport ---------------------------------------------------
    std::streambuf* realBuf = std::cout.rdbuf();   // the true stdout — DAP messages go here
    std::ostringstream cap;                        // the debuggee's stdout, captured while it runs
    size_t capSent = 0;
    int seq = 1;

    bool readMessage(std::string& body) {
        size_t len = 0; std::string line;
        while (true) {
            if (!std::getline(std::cin, line)) return false;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break;                          // end of headers
            const std::string h = "Content-Length:";
            if (line.rfind(h, 0) == 0) len = (size_t)atol(line.c_str() + h.size());
        }
        body.resize(len); std::cin.read(&body[0], (std::streamsize)len); return (bool)std::cin;
    }
    void raw(const std::string& msg) { std::ostream o(realBuf); o << "Content-Length: " << msg.size() << "\r\n\r\n" << msg; o.flush(); }
    void response(const Json& req, const std::string& body, bool ok = true) {
        int rs = req.get("seq") ? (int)req.get("seq")->num : 0;
        std::string cmd = req.get("command") ? req.get("command")->s : "";
        raw("{\"seq\":" + std::to_string(seq++) + ",\"type\":\"response\",\"request_seq\":" + std::to_string(rs)
            + ",\"success\":" + (ok ? "true" : "false") + ",\"command\":" + jstr(cmd)
            + (body.empty() ? "" : ",\"body\":" + body) + "}");
    }
    void event(const std::string& ev, const std::string& body) {
        raw("{\"seq\":" + std::to_string(seq++) + ",\"type\":\"event\",\"event\":" + jstr(ev)
            + (body.empty() ? "" : ",\"body\":" + body) + "}");
    }
    void flushOutput() {   // forward any newly-captured debuggee stdout as an output event
        std::string s = cap.str();
        if (s.size() > capSent) { event("output", "{\"category\":\"stdout\",\"output\":" + jstr(s.substr(capSent)) + "}"); capSent = s.size(); }
    }

    // --- program + config -------------------------------------------
    std::string entryPath, root, entryMod;
    Program prog;
    std::vector<std::string> order;
    std::map<std::string, int> globalsFunc, initFunc;
    std::map<std::string, std::string> modFile;    // module -> canonical file path
    std::map<int, std::string> globalNames;         // global slot -> name
    std::map<std::string, std::set<int>> bps;       // canonical file path -> breakpoint lines
    bool loaded = false, stopOnEntry = false;

    // --- run / step state -------------------------------------------
    enum Step { RUN, IN, OVER, OUT } step = RUN;
    size_t stepDepth = 0;
    // inspection context — valid only while stopped
    std::vector<Value>* G = nullptr; std::vector<Value>* S = nullptr; std::vector<Frame>* F = nullptr;
    // composite-value references, so the client can drill into objects/lists/maps.
    // Handed out lazily as composites are shown; reset at each stop (the values
    // they point at only stay valid while the VM is paused).
    std::map<int, Value> valueRefs; int nextValueRef = 1000;

    static std::string canon(const std::string& p) { std::error_code ec; auto c = std::filesystem::weakly_canonical(p, ec); return ec ? p : c.string(); }
    static std::string modOf(const std::string& fn) { auto s = fn.find("::"); return s == std::string::npos ? "" : fn.substr(0, s); }
    static std::string shortName(const std::string& fn) { auto s = fn.find("::"); return s == std::string::npos ? fn : fn.substr(s + 2); }
    std::string fileOf(CFunc* fn) { auto it = modFile.find(modOf(fn->name)); return it == modFile.end() ? "" : it->second; }
    int lineOfFrame(const Frame& fr, bool top) {
        int ip = top ? fr.ip : fr.ip - 1; if (ip < 0) ip = 0;
        if (ip >= (int)fr.fn->code.size()) ip = (int)fr.fn->code.size() - 1;
        return ip >= 0 ? fr.fn->code[ip].line : 0;
    }

    // --- request handlers -------------------------------------------
    bool doLaunch(const Json& req) {
        auto args = req.get("arguments");
        std::string program = (args && args->get("program")) ? args->get("program")->s : "";
        if (program.empty()) { event("output", "{\"category\":\"stderr\",\"output\":\"no program specified\\n\"}"); return false; }
        entryPath = canon(program); root = rgDirOf(entryPath);
        try {
            std::map<std::string, Parsed> mods;
            entryMod = loadModules(entryPath, mods, order, &modFile);
            for (auto& kv : modFile) kv.second = canon(kv.second);
            TypeChecker tc(mods, order); tc.build(); tc.check();
            if (!tc.errors.empty()) {
                std::string msg = "type errors:\n";
                for (auto& e : tc.errors) { int ln = std::get<1>(e); msg += "  " + std::get<0>(e) + (ln ? ":" + std::to_string(ln) : "") + ": " + std::get<2>(e) + "\n"; }
                event("output", "{\"category\":\"stderr\",\"output\":" + jstr(msg) + "}");
                return false;
            }
            buildProgram(mods, order, prog, globalsFunc, initFunc);
            for (auto& mp : prog.syms) for (auto& np : mp.second) if (np.second.kind == 3) globalNames[np.second.index] = np.first;
            loaded = true; return true;
        } catch (const std::exception& e) {
            event("output", "{\"category\":\"stderr\",\"output\":" + jstr(std::string("error: ") + e.what() + "\n") + "}");
            return false;
        }
    }
    std::string setBps(const Json& req) {
        auto args = req.get("arguments");
        std::string path = (args && args->get("source") && args->get("source")->get("path")) ? args->get("source")->get("path")->s : "";
        std::set<int> lines; std::string arr;
        if (args) if (auto b = args->get("breakpoints")) if (b->t == Json::ARR)
            for (auto& bp : b->a) { int ln = bp.get("line") ? (int)bp.get("line")->num : 0; lines.insert(ln);
                if (!arr.empty()) arr += ","; arr += "{\"verified\":true,\"line\":" + std::to_string(ln) + "}"; }
        bps[canon(path)] = lines;
        return "{\"breakpoints\":[" + arr + "]}";
    }
    std::string stackTrace() {
        if (!F) return "{\"stackFrames\":[],\"totalFrames\":0}";
        std::string frames; int n = (int)F->size();
        for (int k = 0; k < n; k++) {                        // 0 = innermost
            Frame& fr = (*F)[n - 1 - k];
            int line = lineOfFrame(fr, k == 0);
            std::string file = fileOf(fr.fn);
            if (!frames.empty()) frames += ",";
            frames += "{\"id\":" + std::to_string(k) + ",\"name\":" + jstr(shortName(fr.fn->name))
                + ",\"line\":" + std::to_string(line) + ",\"column\":1,\"source\":{\"name\":"
                + jstr(std::filesystem::path(file).filename().string()) + ",\"path\":" + jstr(file) + "}}";
        }
        return "{\"stackFrames\":[" + frames + "],\"totalFrames\":" + std::to_string(n) + "}";
    }
    std::string scopes(const Json& req) {
        int fid = 0; if (auto a = req.get("arguments")) if (auto f = a->get("frameId")) fid = (int)f->num;
        return "{\"scopes\":[{\"name\":\"Locals\",\"variablesReference\":" + std::to_string(2 + fid) + ",\"expensive\":false},"
               "{\"name\":\"Globals\",\"variablesReference\":1,\"expensive\":false}]}";
    }
    // A composite value (object / list / map / enum variant) gets a fresh
    // variablesReference so the client can drill into it; scalars stay leaves (0).
    static bool isComposite(const Value& v) {
        return std::holds_alternative<std::shared_ptr<Instance>>(v)
            || std::holds_alternative<std::shared_ptr<ListObj>>(v)
            || std::holds_alternative<std::shared_ptr<MapObj>>(v)
            || std::holds_alternative<std::shared_ptr<VariantVal>>(v);
    }
    int refFor(const Value& v) { if (!isComposite(v)) return 0; int r = nextValueRef++; valueRefs[r] = v; return r; }
    void childrenOf(const Value& v, std::vector<std::pair<std::string, Value>>& out) {
        if (auto p = std::get_if<std::shared_ptr<Instance>>(&v)) {
            Instance& I = **p;
            if (I.clsIndex >= 0 && I.clsIndex < (int)prog.classes.size())          // declaration order
                for (auto& fn : prog.classes[I.clsIndex].fieldNames) { auto f = I.fields.find(fn); if (f != I.fields.end()) out.push_back({fn, f->second}); }
            else for (auto& f : I.fields) out.push_back({f.first, f.second});
        } else if (auto p = std::get_if<std::shared_ptr<ListObj>>(&v)) {
            auto& L = **p; for (size_t i = 0; i < L.items.size(); i++) out.push_back({"[" + std::to_string(i) + "]", L.items[i]});
        } else if (auto p = std::get_if<std::shared_ptr<MapObj>>(&v)) {
            auto& M = **p; for (auto& kv : M.items) out.push_back({toStr(kv.first), kv.second});
        } else if (auto p = std::get_if<std::shared_ptr<VariantVal>>(&v)) {
            auto& V = **p; for (size_t i = 0; i < V.vals.size(); i++) out.push_back({"[" + std::to_string(i) + "]", V.vals[i]});
        }
    }
    std::string emitVars(const std::vector<std::pair<std::string, Value>>& items) {
        std::string vars;
        for (auto& it : items) { if (!vars.empty()) vars += ",";
            vars += "{\"name\":" + jstr(it.first) + ",\"value\":" + jstr(toStr(it.second)) + ",\"variablesReference\":" + std::to_string(refFor(it.second)) + "}"; }
        return "{\"variables\":[" + vars + "]}";
    }
    std::string variables(const Json& req) {
        int ref = 0; if (auto a = req.get("arguments")) if (auto r = a->get("variablesReference")) ref = (int)r->num;
        std::vector<std::pair<std::string, Value>> items;
        if (ref >= 1000) {                                   // drill into a stored composite
            auto it = valueRefs.find(ref); if (it != valueRefs.end()) childrenOf(it->second, items);
        } else if (ref == 1) {                                // globals
            if (G) for (auto& gp : globalNames) if (gp.first < (int)G->size()) items.push_back({gp.second, (*G)[gp.first]});
        } else if (ref >= 2 && F) {                           // locals of frame (ref - 2)
            int fid = ref - 2, n = (int)F->size();
            if (fid >= 0 && fid < n) {
                Frame& fr = (*F)[n - 1 - fid]; auto& names = fr.fn->localNames;
                for (int slot = 0; slot < (int)names.size(); slot++) {
                    const std::string& nm = names[slot]; if (nm.empty() || nm[0] == '$') continue;   // skip compiler temporaries
                    int idx = fr.base + slot; if (S && idx >= 0 && idx < (int)S->size()) items.push_back({nm, (*S)[idx]});
                }
            }
        }
        return emitVars(items);
    }
    bool evalValue(const Json& req, Value& out) {   // resolve a bare variable name in the selected frame / globals
        auto a = req.get("arguments"); std::string expr = a && a->get("expression") ? a->get("expression")->s : "";
        int fid = a && a->get("frameId") ? (int)a->get("frameId")->num : 0;
        size_t b = expr.find_first_not_of(" \t"), e = expr.find_last_not_of(" \t");
        if (b == std::string::npos) return false; expr = expr.substr(b, e - b + 1);
        if (F) { int n = (int)F->size();
            if (fid >= 0 && fid < n) { Frame& fr = (*F)[n - 1 - fid]; auto& names = fr.fn->localNames;
                for (int slot = 0; slot < (int)names.size(); slot++) if (names[slot] == expr) { int idx = fr.base + slot; if (S && idx < (int)S->size()) { out = (*S)[idx]; return true; } } }
        }
        if (G) for (auto& gp : globalNames) if (gp.second == expr && gp.first < (int)G->size()) { out = (*G)[gp.first]; return true; }
        return false;
    }
    void handleCommon(const Json& req) {
        std::string cmd = req.get("command") ? req.get("command")->s : "";
        if (cmd == "threads") response(req, "{\"threads\":[{\"id\":1,\"name\":\"main\"}]}");
        else if (cmd == "stackTrace") response(req, stackTrace());
        else if (cmd == "scopes") response(req, scopes(req));
        else if (cmd == "variables") response(req, variables(req));
        else if (cmd == "evaluate") { Value v; if (evalValue(req, v)) response(req, "{\"result\":" + jstr(toStr(v)) + ",\"variablesReference\":" + std::to_string(refFor(v)) + "}"); else response(req, "{}", false); }
        else if (cmd == "disconnect") { response(req, "{}"); std::exit(0); }
        else if (cmd == "source") response(req, "{\"content\":\"\"}");
        else response(req, "{}");
    }
    // request loop entered while stopped; returns true to resume the VM
    bool handleStopped(const Json& req) {
        std::string cmd = req.get("command") ? req.get("command")->s : "";
        if (cmd == "continue") { step = RUN; response(req, "{\"allThreadsContinued\":true}"); event("continued", "{\"threadId\":1,\"allThreadsContinued\":true}"); return true; }
        if (cmd == "next")   { step = OVER; stepDepth = F ? F->size() : 0; response(req, "{}"); return true; }
        if (cmd == "stepIn") { step = IN; response(req, "{}"); return true; }
        if (cmd == "stepOut"){ step = OUT; stepDepth = F ? F->size() : 0; response(req, "{}"); return true; }
        if (cmd == "pause")  { response(req, "{}"); return false; }
        handleCommon(req); return false;
    }

    // --- the debug hook: called before each instruction -------------
    void onInstr(Program&, std::vector<Value>& globals, std::vector<Value>& st, std::vector<Frame>& frames) {
        if (frames.empty()) return;
        Frame& fr = frames.back();
        int ip = fr.ip;
        if (ip >= (int)fr.fn->code.size()) return;
        int line = fr.fn->code[ip].line;
        if (line <= 0) return;                               // synthetic instruction, no source line
        // Only stop at a "line start" — the first instruction of a source line.
        // This makes each pass through a line a single stop-point, so a breakpoint
        // doesn't re-fire when control returns to the tail of the line (e.g. the
        // store in `var z = add(x, y)` after the call returns).
        if (ip > 0 && fr.fn->code[ip - 1].line == line) return;
        std::string file = fileOf(fr.fn);
        size_t depth = frames.size();
        std::string reason;
        auto bit = bps.find(file);
        if (bit != bps.end() && bit->second.count(line)) reason = "breakpoint";
        else if (step == IN) reason = "step";
        else if (step == OVER && depth <= stepDepth) reason = "step";
        else if (step == OUT && depth < stepDepth) reason = "step";
        if (reason.empty()) return;

        flushOutput();
        valueRefs.clear(); nextValueRef = 1000;              // previous stop's drill-in refs are now stale
        G = &globals; S = &st; F = &frames;
        event("stopped", "{\"reason\":\"" + reason + "\",\"threadId\":1,\"allThreadsStopped\":true}");
        std::string body;
        while (readMessage(body)) { Json req = JParse(body).parse(); if (handleStopped(req)) return; }
        std::exit(0);                                        // client closed the connection
    }

    void execute() {
        std::cout.rdbuf(cap.rdbuf());                        // capture the debuggee's stdout
        g_dbgHook = [this](Program& pr, std::vector<Value>& gl, std::vector<Value>& s, std::vector<Frame>& f) { onInstr(pr, gl, s, f); };
        step = stopOnEntry ? IN : RUN;
        try {
            std::vector<Value> globals(prog.nglobals);
            for (auto& m : order) if (globalsFunc.count(m)) execTop(prog, globals, globalsFunc[m]);
            for (auto& m : order) if (initFunc.count(m)) execTop(prog, globals, initFunc[m]);
            auto ms = prog.syms[entryMod].find("main");
            if (ms == prog.syms[entryMod].end()) throw VMError("entry module '" + entryMod + "' has no func main()");
            execTop(prog, globals, ms->second.index);
        } catch (const std::exception& e) {
            std::cout.rdbuf(realBuf); flushOutput();
            event("output", "{\"category\":\"stderr\",\"output\":" + jstr(std::string("error: ") + e.what() + "\n") + "}");
            std::cout.rdbuf(cap.rdbuf());
        }
        g_dbgHook = nullptr;
        std::cout.rdbuf(realBuf); flushOutput();
        F = nullptr; G = nullptr; S = nullptr;
        event("terminated", "{}"); event("exited", "{\"exitCode\":0}");
    }

    int run() {
        std::string body;
        while (readMessage(body)) {
            Json req = JParse(body).parse();
            std::string cmd = req.get("command") ? req.get("command")->s : "";
            if (cmd == "initialize") {
                response(req, "{\"supportsConfigurationDoneRequest\":true,\"supportsEvaluateForHovers\":true}");
                event("initialized", "{}");
            } else if (cmd == "launch") {
                if (auto a = req.get("arguments")) if (auto se = a->get("stopOnEntry")) stopOnEntry = (se->t == Json::BOOL && se->b);
                bool ok = doLaunch(req); response(req, "{}", ok);
                if (!ok) { event("terminated", "{}"); event("exited", "{\"exitCode\":1}"); }
            } else if (cmd == "setBreakpoints") {
                response(req, setBps(req));
            } else if (cmd == "setExceptionBreakpoints") {
                response(req, "{\"breakpoints\":[]}");
            } else if (cmd == "configurationDone") {
                response(req, "{}");
                if (loaded) execute();
            } else if (cmd == "disconnect") {
                response(req, "{}"); return 0;
            } else {
                handleCommon(req);
            }
        }
        return 0;
    }
};

static int runDap() { Dbg d; return d.run(); }
