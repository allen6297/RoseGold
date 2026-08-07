#pragma once
#include "common.hpp"

// ---------------------------------------------------------------------
// AST
// ---------------------------------------------------------------------
struct Expr;
struct TyNode { std::string name; std::vector<std::shared_ptr<TyNode>> args; bool isFunc = false; std::vector<std::shared_ptr<TyNode>> fparams; std::shared_ptr<TyNode> fret; };
using TyNodeP = std::shared_ptr<TyNode>;
struct Pattern { int k = 0; std::unique_ptr<Expr> lit; std::string name; std::vector<std::string> binds; };
struct Arm { std::vector<Pattern> pats; std::unique_ptr<Expr> body; };
struct Expr {
    enum K { INT, FLT, STR, BOOL, NAME, UNARY, BINARY, CALL, LIST, INDEX, MEMBER, MATCH, CLOSURE } k;
    int64_t ival = 0; double dval = 0; bool bval = false;
    std::string sval, op;
    std::unique_ptr<Expr> lhs, rhs;
    std::vector<std::unique_ptr<Expr>> args;
    std::vector<Arm> arms;
    std::vector<std::string> params;                 // CLOSURE param names
    std::vector<TyNodeP> ptypes; TyNodeP retType;    // CLOSURE param/return types
    int line = 0; int col = 1;                        // source position (of NAME / MEMBER field)
};
using ExprP = std::unique_ptr<Expr>;
struct Stmt {
    enum K { VAR, ASSIGN, EXPR, RET, IF, WHILE, FOR, BREAK, CONTINUE, TRY, RAISE, PASS } k;
    std::string name; int nameLine = 0, nameCol = 1; ExprP target; ExprP expr; bool hasExpr = false; TyNodeP vtype; int vis = 0;
    std::vector<Stmt> body; std::vector<std::pair<ExprP, std::vector<Stmt>>> elifs; std::vector<Stmt> elseBody; bool hasElse = false;
};
// Generic bounds: parameter name -> the trait names bounding it (`<T: A + B>`). Empty when unbounded.
using Bounds = std::map<std::string, std::vector<std::string>>;
struct Func { std::string name; int nameLine = 0, nameCol = 1; std::vector<std::string> params; std::vector<std::pair<int, int>> paramPos; std::vector<TyNodeP> ptypes; TyNodeP retType; std::vector<std::string> generics; Bounds bounds; bool isSig = false; int vis = 0; std::vector<Stmt> body; };
struct Field { std::string name; int nameLine = 0, nameCol = 1; TyNodeP type; int vis = 0; ExprP init; bool hasInit = false; };
struct ClassAst { std::string name; int nameLine = 0, nameCol = 1; std::vector<std::string> generics; Bounds bounds; std::string extends; std::vector<std::string> uses; int vis = 0; std::vector<Field> fields; bool hasCtor = false; std::vector<std::string> ctorParams; std::vector<std::pair<int, int>> ctorParamPos; std::vector<TyNodeP> ctorPtypes; std::vector<Stmt> ctorBody; std::vector<Func> methods; };
struct TraitAst { std::string name; int nameLine = 0, nameCol = 1; std::vector<std::string> generics; Bounds bounds; std::vector<std::string> uses; int vis = 0; std::vector<Func> methods; };  // methods are abstract signatures (isSig)
struct ExtendAst { std::string typeName; int nameLine = 0, nameCol = 1; std::vector<std::string> uses; std::vector<Func> methods; };  // retroactive conformance: `extend Type uses Trait`
struct EnumAst { std::string name; int nameLine = 0, nameCol = 1; std::vector<std::string> generics; int vis = 0; std::vector<std::pair<std::string, std::vector<TyNodeP>>> variants; };
struct Import { std::string path, alias; std::vector<std::string> names; bool pub = false; };
struct Parsed { std::string module; std::vector<Import> imports; std::vector<Func> funcs; std::vector<Stmt> globals; std::vector<ClassAst> classes; std::vector<TraitAst> traits; std::vector<ExtendAst> extensions; std::vector<EnumAst> enums; };
struct ParseError : std::runtime_error { using std::runtime_error::runtime_error; };

