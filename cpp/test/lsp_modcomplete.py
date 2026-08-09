#!/usr/bin/env python3
# Completion of imported module members: `util.` offers only the module's pub
# symbols (funcs/classes/enums), with correct icons, and hides internal ones.
import json, os, subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN  = os.path.join(ROOT, "cpp", "rosegoldc")
WS   = "/tmp/rgmod_test"
os.makedirs(WS, exist_ok=True)
open(WS + "/util.rg", "w").write(
    "module util\n\n"
    "pub fn twice(n: Int) -> Int:\n    return n + n\n\n"
    "pub fn shout(s: String) -> String:\n    return s\n\n"
    "internal fn secret() -> Int:\n    return 0\n\n"
    "pub enum Color:\n    Red\n    Green\n\n"
    "pub class Box:\n    var v: Int\n    init(v: Int):\n        self.v = v\n"
)
open(WS + "/app.rg", "w").write("module app\n\nimport util\n\nfn main():\n    print(util.twice(21))\n")
APP = WS + "/app.rg"; URI = "file://" + APP

KIND = {2:"Method",3:"Function",5:"Field",7:"Class",8:"Interface",13:"Enum",20:"EnumMember",6:"Var"}
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

s({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file://"+WS,"capabilities":{}}}); w(1)
s({"jsonrpc":"2.0","method":"initialized","params":{}})
s({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":URI,"languageId":"rosegold","version":1,"text":open(APP).read()}}})
# cursor right after `util.` on line 5 (`    print(util.twice(21))`), char 15
s({"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":URI},"position":{"line":5,"character":15}}})
items = w(2)["result"]["items"]
got = sorted((i["label"], KIND.get(i["kind"], i["kind"])) for i in items)
print("[completion util.]", got)
labels = [l for l, _ in got]
print("  secret hidden:", "secret" not in labels)
s({"jsonrpc":"2.0","method":"exit","params":{}}); p.wait(timeout=5)
