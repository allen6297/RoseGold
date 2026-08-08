#pragma once
#include "compiler.hpp"

// ---------------------------------------------------------------------
// Bytecode dump  (rosegoldc --bytecode)
// ---------------------------------------------------------------------
// Per-function disassembly of the compiled program — the ground truth the
// self-hosted compiler (examples/rgcompiler.rg) is diffed against, mirroring
// --tokens/--ast/--check for the earlier stages. Each line is "index: OP a b"
// (operands always shown); a CONST's `a` is its (global, monotonic) const-pool
// index, LOAD/STORE's is a local slot, CALL's is a function index + argc,
// BUILTIN's a builtin id + argc, and JUMP/JFALSE's is an absolute target.
// ---------------------------------------------------------------------
static std::string dumpBytecode(Program& prog) {
    static const char* N[] = {
        "CONST","PUSHNIL","LOAD","STORE","LOADG","STOREG","POP",
        "ADD","SUB","MUL","DIV","MOD","NEG","NOT","LT","LE","GT","GE","EQ","NE",
        "JUMP","JFALSE","JTRUE","MAKELIST","IGET","ISET",
        "NEWOBJ","GETPROP","SETPROP","INVOKE","MKVARIANT","ISVARIANT","VGET",
        "MKCLOSURE","CALLV","SETUP_TRY","POP_TRY","RAISE","YIELD","CALL","BUILTIN","NATIVE","RET"};
    std::string o;
    for (auto& f : prog.funcs) {
        std::string nm = f.name; auto p = nm.find("::"); if (p != std::string::npos) nm = nm.substr(p + 2);
        o += "func " + nm + " nlocals=" + std::to_string(f.nlocals) + "\n";
        for (size_t i = 0; i < f.code.size(); i++) {
            const Instr& in = f.code[i];
            o += "  " + std::to_string(i) + ": " + N[(int)in.op] + " " + std::to_string(in.a) + " " + std::to_string(in.b) + "\n";
        }
    }
    return o;
}
