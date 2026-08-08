#include "runtime.hpp"
#include "format.hpp"
#include "doc.hpp"
#include "lsp.hpp"
#include "dap.hpp"

// ---------------------------------------------------------------------
// CLI driver (module loading + program building live in runtime.hpp,
// shared with the embeddable Runtime).
// ---------------------------------------------------------------------
int main(int argc, char** argv) {
    // `rosegoldc <file.rg>`         type-check, compile, and run
    // `rosegoldc --check <file.rg>` type-check only (front-end gate; no execution)
    // `rosegoldc --lsp`             run the language server (JSON-RPC over stdio)
    if (argc >= 2 && std::string(argv[1]) == "--lsp") return runLsp();
    if (argc >= 2 && std::string(argv[1]) == "--dap") return runDap();
    if (argc >= 3 && std::string(argv[1]) == "--format") {
        std::ifstream f(argv[2]);
        if (!f) { std::cerr << "cannot open " << argv[2] << "\n"; return 2; }
        std::stringstream ss; ss << f.rdbuf();
        std::cout << formatSource(ss.str());
        return 0;
    }
    // Generate a Markdown documentation page from a file's declarations + their doc comments.
    if (argc >= 3 && std::string(argv[1]) == "--doc") {
        std::ifstream f(argv[2]);
        if (!f) { std::cerr << "cannot open " << argv[2] << "\n"; return 2; }
        std::stringstream ss; ss << f.rdbuf(); std::string src = ss.str();
        try {
            Parsed P = Parser{lex(src)}.program();
            std::string mod = P.module.empty() ? "$entry" : P.module;
            std::cout << generateDoc(mod, P, collectDocs(src));
        } catch (const std::exception& e) { std::cerr << "error: " << e.what() << "\n"; return 1; }
        return 0;
    }
    // Dump the token stream (ground truth for the self-hosted lexer in examples/rglexer.rg).
    // One token per line: "KIND value" for INT/FLT/IDENT/KW/OP; bare "KIND" for STR /
    // NEWLINE / INDENT / DEDENT / END (STR's decoded value can hold arbitrary bytes).
    if (argc >= 3 && std::string(argv[1]) == "--tokens") {
        std::ifstream f(argv[2]);
        if (!f) { std::cerr << "cannot open " << argv[2] << "\n"; return 2; }
        std::stringstream ss; ss << f.rdbuf();
        static const char* NM[] = {"INT","FLT","STR","IDENT","KW","OP","NEWLINE","INDENT","DEDENT","END"};
        try {
            for (auto& t : lex(ss.str())) {
                const char* nm = NM[(int)t.t];
                if (t.t == Tk::INT || t.t == Tk::FLT || t.t == Tk::IDENT || t.t == Tk::KW || t.t == Tk::OP)
                    std::cout << nm << " " << t.s << "\n";
                else std::cout << nm << "\n";
            }
        } catch (const std::exception& e) { std::cerr << "error: " << e.what() << "\n"; return 1; }
        return 0;
    }
    bool checkOnly = false; int argi = 1;
    if (argc >= 2 && std::string(argv[1]) == "--check") { checkOnly = true; argi = 2; }
    if (argc < argi + 1) { std::cerr << "usage: rosegoldc [--check] <file.rg>\n"; return 2; }
    try {
        std::string entryPath = argv[argi];
        std::map<std::string, Parsed> mods; std::vector<std::string> order;
        std::string entryName = loadModules(entryPath, mods, order);   // parse entry + transitive imports, dependency order

        // static type check (front-end gate)
        TypeChecker tc(mods, order); tc.build(); tc.check();
        if (!tc.errors.empty()) {
            std::cerr << "type errors:\n";
            for (auto& e : tc.errors) { const std::string& mm = std::get<0>(e); int ln = std::get<1>(e); const std::string& msg = std::get<2>(e); if (ln) std::cerr << "  " << mm << ":" << ln << ": " << msg << "\n"; else std::cerr << "  " << mm << ": " << msg << "\n"; }
            return 1;
        }
        if (checkOnly) return 0;   // front-end gate passed; skip execution

        Program prog; std::map<std::string, int> globalsFunc, initFunc;
        buildProgram(mods, order, prog, globalsFunc, initFunc);
        // run: each module's globals in dep order, then entry main
        std::vector<Value> globals(prog.nglobals);
        for (auto& m : order) if (globalsFunc.count(m)) execTop(prog, globals, globalsFunc[m]);
        for (auto& m : order) if (initFunc.count(m)) execTop(prog, globals, initFunc[m]);   // load-time init blocks, dependencies first
        auto mainSym = prog.syms[entryName].find("main");
        if (mainSym == prog.syms[entryName].end()) throw VMError("entry module '" + entryName + "' has no func main()");
        execTop(prog, globals, mainSym->second.index);
    } catch (const std::exception& e) { std::cerr << "error: " << e.what() << "\n"; return 1; }
    return 0;
}
