#!/usr/bin/env python3
# Cross-file (workspace) references + rename: a `pub func` defined in one module,
# used in another. Creates a throwaway two-file workspace under /tmp.
import json, os, subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN  = os.path.join(ROOT, "cpp", "rosegoldc")
WS   = "/tmp/rgws_test"
os.makedirs(WS, exist_ok=True)
open(WS + "/lib.rg", "w").write("module lib\n\npub func greet(name: String) -> String:\n    return name\n")
open(WS + "/main.rg", "w").write('module main\n\nimport lib\n\nfunc main():\n    print(lib.greet("a"))\n    print(lib.greet("b"))\n')
MAIN = WS + "/main.rg"; MURI = "file://" + MAIN

def frame(m): b = json.dumps(m).encode(); return b"Content-Length: %d\r\n\r\n%s" % (len(b), b)
def read(f):
    n = 0
    while True:
        l = f.readline()
        if not l: return None
        l = l.rstrip(b"\r\n")
        if l == b"": break
        if l.lower().startswith(b"content-length:"): n = int(l.split(b":")[1])
    return json.loads(f.read(n))

p = subprocess.Popen([BIN, "--lsp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
def s(m): p.stdin.write(frame(m)); p.stdin.flush()
def w(i):
    while True:
        m = read(p.stdout)
        if m is None: raise SystemExit("closed")
        if m.get("method") == "textDocument/publishDiagnostics": continue
        if m.get("id") == i: return m
rel = lambda u: os.path.basename(u.replace("file://", ""))

s({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file://"+WS,"capabilities":{}}}); w(1)
s({"jsonrpc":"2.0","method":"initialized","params":{}})
s({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":MURI,"languageId":"rosegold","version":1,"text":open(MAIN).read()}}})

# references on `greet` in `lib.greet("a")` (main.rg line 5) -> decl in lib.rg + 2 uses in main.rg
s({"jsonrpc":"2.0","id":2,"method":"textDocument/references","params":{"textDocument":{"uri":MURI},"position":{"line":5,"character":15},"context":{"includeDeclaration":True}}})
refs = sorted((rel(r["uri"]), r["range"]["start"]["line"]) for r in w(2)["result"])
print("[refs greet]", refs)

# rename greet -> hello across the workspace
s({"jsonrpc":"2.0","id":3,"method":"textDocument/rename","params":{"textDocument":{"uri":MURI},"position":{"line":5,"character":15},"newName":"hello"}})
changes = w(3)["result"]["changes"]
print("[rename greet->hello]", {rel(u): len(e) for u, e in changes.items()})

s({"jsonrpc":"2.0","method":"exit","params":{}}); p.wait(timeout=5)

# apply and verify the renamed workspace still runs
for u, edits in changes.items():
    path = u.replace("file://", ""); lines = open(path).read().split("\n")
    from collections import defaultdict
    byl = defaultdict(list)
    for e in edits: byl[e["range"]["start"]["line"]].append(e)
    for ln, es in byl.items():
        for e in sorted(es, key=lambda z: -z["range"]["start"]["character"]):
            a = e["range"]["start"]["character"]; b = e["range"]["end"]["character"]
            lines[ln] = lines[ln][:a] + e["newText"] + lines[ln][b:]
    open(path, "w").write("\n".join(lines))
r = subprocess.run([BIN, MAIN], capture_output=True, text=True)
print("[after rename] run exit=%d output=%s" % (r.returncode, r.stdout.split()))
