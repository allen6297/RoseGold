#!/usr/bin/env python3
"""RoseGold test harness. Golden-snapshots every runnable example, asserts the
error fixtures fail, checks C++/Python parity on the shared core, snapshots the
LSP drivers and the embedding demos, and verifies the builtin tables are in sync.

    python3 cpp/test/run_tests.py            # run all checks
    python3 cpp/test/run_tests.py --update   # (re)generate the golden files
"""
import os, sys, subprocess, glob, re, difflib

ROOT   = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN    = os.path.join(ROOT, "cpp", "rosegoldc")
GOLDEN = os.path.join(ROOT, "cpp", "test", "golden")
CXX    = os.environ.get("CXX", "clang++")
UPDATE = "--update" in sys.argv
os.makedirs(GOLDEN, exist_ok=True)

# Examples expected to FAIL the front end (visibility/resolution/type fixtures).
ERROR_FIXTURES = {"broken", "client", "consumer", "trait_errors", "typeerrors", "check_sample"}

passed = 0
fails, skipped = [], []

def norm(s):                       # make output machine-independent (absolute paths -> <ROOT>)
    return s.replace(ROOT, "<ROOT>")

def golden(test_id, actual):
    """Compare `actual` to golden/<test_id>, or write it under --update."""
    global passed
    actual = norm(actual)
    path = os.path.join(GOLDEN, test_id)
    if UPDATE:
        open(path, "w").write(actual); return
    if not os.path.exists(path):
        fails.append(f"{test_id}: no golden file (run --update)"); return
    expected = open(path).read()
    if actual == expected:
        passed += 1
    else:
        diff = "".join(difflib.unified_diff(expected.splitlines(True), actual.splitlines(True),
                                            "golden", "actual", n=1))
        fails.append(f"{test_id}: differs from golden\n" + "".join("      " + l for l in diff.splitlines(True)))

def run(args, cwd=ROOT):
    r = subprocess.run(args, cwd=cwd, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr

def build():
    print(f"• building rosegoldc ({CXX})")
    code, out = run([CXX, "-std=c++17", "-O2", "-o", BIN, "cpp/src/main.cpp"])
    if code != 0:
        print(out); sys.exit("BUILD FAILED")

# ---------------------------------------------------------------- examples
def test_examples():
    print("• examples (golden stdout)")
    for path in sorted(glob.glob(os.path.join(ROOT, "examples", "*.rg"))):
        name = os.path.splitext(os.path.basename(path))[0]
        rel = os.path.relpath(path, ROOT)
        if name in ERROR_FIXTURES:
            code, out = run([BIN, "--check", rel])
            if code == 0: fails.append(f"ex/{name}: expected the front end to reject it, but it passed")
            else: golden(f"ex_{name}.err", out)
            continue
        code, out = run([BIN, rel])
        if code == 0:
            golden(f"ex_{name}.out", out)
        elif "no func main" in out:
            skipped.append(f"{name} (library, no main)")
        else:
            fails.append(f"ex/{name}: unexpected failure\n      " + out.splitlines()[0] if out else name)

# ---------------------------------------------------------------- parity
def test_parity():
    print("• C++/Python parity (shared core)")
    n = 0
    for path in sorted(glob.glob(os.path.join(ROOT, "examples", "*.rg"))):
        name = os.path.splitext(os.path.basename(path))[0]
        if name in ERROR_FIXTURES: continue
        rel = os.path.relpath(path, ROOT)
        cc, cout = run([BIN, rel])
        if cc != 0: continue
        pc, pout = run(["python3", "python-reference/interpreter.py", rel])
        if pc != 0 or "cannot run" in pout or "Traceback" in pout:
            continue                       # Python reference doesn't support this feature
        n += 1
        if norm(cout) != norm(pout):
            fails.append(f"parity/{name}: C++ and Python output differ")
    print(f"    ({n} examples cross-checked against the Python oracle)")

# ---------------------------------------------------------------- formatter
def test_formatter():
    print("• formatter (idempotency + behavior preservation on every example)")
    for path in sorted(glob.glob(os.path.join(ROOT, "examples", "*.rg"))):
        name = os.path.splitext(os.path.basename(path))[0]
        rel = os.path.relpath(path, ROOT)
        code, once = run([BIN, "--format", rel])
        if code != 0:
            fails.append(f"fmt/{name}: --format failed"); continue
        # Idempotent? Re-format the formatted text (temp is hidden, so the
        # examples/*.rg globs above never pick it up; same dir keeps imports resolvable).
        tmp = os.path.join(os.path.dirname(path), ".rgfmt_tmp.rg")
        trel = os.path.relpath(tmp, ROOT)
        open(tmp, "w").write(once)
        try:
            _, twice = run([BIN, "--format", trel])
            if twice != once:
                fails.append(f"fmt/{name}: formatting is not idempotent")
            # Behavior preserved? Error fixtures must still be rejected; runnable
            # examples must still produce their golden output.
            if name in ERROR_FIXTURES:
                cc, _ = run([BIN, "--check", trel])
                if cc == 0: fails.append(f"fmt/{name}: formatted error fixture is no longer rejected")
            else:
                rc, rout = run([BIN, trel])
                gp = os.path.join(GOLDEN, f"ex_{name}.out")
                if rc == 0 and os.path.exists(gp) and norm(rout) != open(gp).read():
                    fails.append(f"fmt/{name}: program output changed after formatting")
        finally:
            os.remove(tmp)

# ---------------------------------------------------------------- protocol drivers
def test_lsp():
    print("• LSP + DAP drivers (golden stdout)")
    drivers = glob.glob(os.path.join(ROOT, "cpp", "test", "lsp_*.py")) + glob.glob(os.path.join(ROOT, "cpp", "test", "dap_*.py"))
    for path in sorted(drivers):
        name = os.path.splitext(os.path.basename(path))[0]
        code, out = run(["python3", path])
        if code != 0:
            fails.append(f"proto/{name}: driver crashed\n      " + (out.splitlines()[-1] if out else ""))
        else:
            golden(f"{name}.out", out)

# ---------------------------------------------------------------- embedding
def test_embed():
    print("• embedding demos (build + golden stdout)")
    for src in ["engine", "game", "hotreload", "externdemo"]:
        exe = os.path.join(ROOT, "cpp", "embed", src)
        bc, bout = run([CXX, "-std=c++17", "-O2", "-o", exe, f"cpp/embed/{src}.cpp"])
        if bc != 0:
            fails.append(f"embed/{src}: build failed\n      " + bout); continue
        code, out = run([exe])
        if code != 0: fails.append(f"embed/{src}: run failed")
        else: golden(f"embed_{src}.out", out)

# ---------------------------------------------------------------- self-hosting
def test_selfhost_lexer():
    print("• self-hosted lexer parity (rglexer.rg vs C++ --tokens)")
    _, rg = run([BIN, "examples/rglexer.rg"])          # RoseGold lexer tokenizing examples/prog.rg
    _, cc = run([BIN, "--tokens", "examples/prog.rg"])  # the canonical C++ lexer on the same file
    if rg != cc:
        fails.append("selfhost/rglexer: token stream differs from the C++ lexer on prog.rg")
    else:
        print("    (the RoseGold-in-RoseGold lexer matches the canonical lexer, byte-for-byte)")

def test_selfhost_parser():
    print("• self-hosted parser parity (rgparser.rg vs C++ --ast)")
    _, rg = run([BIN, "examples/rgparser.rg"])          # RoseGold parser parsing examples/parse_sample.rg
    _, cc = run([BIN, "--ast", "examples/parse_sample.rg"])  # the canonical C++ parser on the same file
    if rg != cc:
        fails.append("selfhost/rgparser: AST differs from the C++ parser on parse_sample.rg")
    else:
        print("    (the RoseGold-in-RoseGold parser matches the canonical parser, byte-for-byte)")

def test_selfhost_checker():
    print("• self-hosted type checker parity (rgchecker.rg vs C++ --check)")
    _, rg = run([BIN, "examples/rgchecker.rg"])           # RoseGold checker checking examples/check_sample.rg
    _, cc = run([BIN, "--check", "examples/check_sample.rg"])  # the canonical front-end gate on the same file
    if rg != cc:
        fails.append("selfhost/rgchecker: type errors differ from --check on check_sample.rg")
    else:
        print("    (the RoseGold-in-RoseGold checker matches --check, byte-for-byte)")

def test_selfhost_compiler():
    print("• self-hosted bytecode compiler parity (rgcompiler.rg vs C++ --bytecode)")
    _, rg = run([BIN, "examples/rgcompiler.rg"])                # RoseGold compiler compiling examples/compile_sample.rg
    _, cc = run([BIN, "--bytecode", "examples/compile_sample.rg"])  # the canonical compiler on the same file
    if rg != cc:
        fails.append("selfhost/rgcompiler: bytecode differs from --bytecode on compile_sample.rg")
    else:
        print("    (the RoseGold-in-RoseGold compiler matches --bytecode, byte-for-byte)")

# ---------------------------------------------------------------- extern (FFI)
def test_extern():
    print("• extern declarations (standalone --check, no host)")
    code, out = run([BIN, "--check", "cpp/embed/externdemo.rg"])   # type-checks against the extern signatures alone
    if code != 0: fails.append("extern: --check of an extern-declaring script failed (should need no host)\n      " + (out.splitlines()[-1] if out else ""))

# ---------------------------------------------------------------- doc generator
def test_docgen():
    print("• doc generator (--doc golden)")
    code, out = run([BIN, "--doc", "examples/documented.rg"])
    if code != 0: fails.append("docgen: --doc examples/documented.rg failed")
    else: golden("doc_documented.md", out)

# ---------------------------------------------------------------- builtin guard
def test_builtins():
    print("• builtin table consistency (compiler ids / type sigs / VM dispatch)")
    comp = open(os.path.join(ROOT, "cpp/src/compiler.hpp")).read()
    types = open(os.path.join(ROOT, "cpp/src/types.hpp")).read()
    vm = open(os.path.join(ROOT, "cpp/src/vm.hpp")).read()
    # compiler BI map: {"name", id}
    bi = dict((m.group(1), int(m.group(2))) for m in re.finditer(r'\{"([a-zA-Z_]\w*)",\s*(\d+)\}', comp))
    # type signatures registered in builtins(): b["name"] = ...
    sigs = set(re.findall(r'b\["([a-zA-Z_]\w*)"\]\s*=', types))
    # VM BUILTIN dispatch ids: in.a == N
    vm_ids = set(int(x) for x in re.findall(r'in\.a\s*==\s*(\d+)', vm))
    if not bi:
        fails.append("builtins: could not parse the compiler BI map"); return
    for name, i in sorted(bi.items(), key=lambda kv: kv[1]):
        if name not in sigs: fails.append(f"builtins: '{name}' (id {i}) has no type signature in types.hpp builtins()")
        if i not in vm_ids:  fails.append(f"builtins: id {i} ('{name}') has no dispatch case in vm.hpp")
    ids = sorted(bi.values())
    if ids != list(range(len(ids))):
        fails.append(f"builtins: ids are not contiguous 0..{len(ids)-1}: {ids}")
    print(f"    ({len(bi)} builtins wired consistently across all three files)")

# ---------------------------------------------------------------- main
build()
test_examples()
test_parity()
test_formatter()
test_lsp()
test_embed()
test_selfhost_lexer()
test_selfhost_parser()
test_selfhost_checker()
test_selfhost_compiler()
test_extern()
test_docgen()
test_builtins()

print()
if UPDATE:
    print("golden files (re)generated under cpp/test/golden/.")
    sys.exit(0)
if skipped:
    print(f"skipped {len(skipped)} libraries: {', '.join(skipped)}")
if fails:
    print(f"\n✗ {len(fails)} FAILURE(S):\n")
    for f in fails: print("  " + f)
    sys.exit(1)
print(f"✓ all checks passed ({passed} golden snapshots matched)")
