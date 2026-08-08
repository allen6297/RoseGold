#!/usr/bin/env python3
# Exercises textDocument/codeAction: did-you-mean quick fixes off an
# "undefined name" diagnostic, and the "add type annotation" refactor.
import json, os, subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN  = os.path.join(ROOT, "cpp", "rosegoldc")
URI  = "file://" + os.path.join(ROOT, "examples", "scratch.rg")

DOC = ("module scratch\n"
       "class Box:\n"
       "    var v: Int\n"
       "    init(x: Int):\n"
       "        self.v = x\n"
       "func helper(n: Int) -> Int:\n"
       "    return n\n"
       "func main():\n"
       "    var r = helpr(3)\n"          # line 8: typo helpr -> helper
       "    var b = Box(5)\n"            # line 9: un-annotated var -> add ': Box'
       "    print(r)\n"
       "    print(b.v)\n")

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
diags = []
def send(m): p.stdin.write(frame(m)); p.stdin.flush()
def wait(i):
    while True:
        m = read(p.stdout)
        if m is None: raise SystemExit("closed")
        if m.get("method") == "textDocument/publishDiagnostics":
            diags[:] = m["params"]["diagnostics"]; continue
        if m.get("id") == i: return m
def drain_diags():
    # process any pending publishDiagnostics after didOpen
    import time
    for _ in range(3):
        m = read(p.stdout)
        if m and m.get("method") == "textDocument/publishDiagnostics":
            diags[:] = m["params"]["diagnostics"]; return

send({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}})
caps = wait(1)["result"]["capabilities"]
print("[caps] codeAction=%s" % bool(caps.get("codeActionProvider")))
send({"jsonrpc":"2.0","method":"initialized","params":{}})
send({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":URI,"languageId":"rosegold","version":1,"text":DOC}}})
drain_diags()
print("[diagnostics] %s" % sorted(d["message"] for d in diags))

def code_action(line, extra_diags):
    rng = {"start":{"line":line,"character":0},"end":{"line":line,"character":80}}
    send({"jsonrpc":"2.0","id":100+line,"method":"textDocument/codeAction","params":{
        "textDocument":{"uri":URI},"range":rng,"context":{"diagnostics":extra_diags}}})
    return wait(100+line)["result"]

# 1. quick fix on the undefined-name line (pass its diagnostic in context)
und = [d for d in diags if "undefined name" in d["message"]]
acts = code_action(8, und)
print("[quickfix line 8]")
for a in acts:
    nt = a["edit"]["changes"][URI][0]["newText"]
    print("  - %-28s newText=%r kind=%s pref=%s" % (a["title"], nt, a["kind"], a.get("isPreferred", False)))

# 2. refactor on the un-annotated var line (no diagnostics needed)
acts2 = code_action(9, [])
print("[refactor line 9]")
for a in acts2:
    nt = a["edit"]["changes"][URI][0]["newText"]
    print("  - %-28s newText=%r kind=%s" % (a["title"], nt, a["kind"]))

send({"jsonrpc":"2.0","id":9,"method":"shutdown","params":{}}); wait(9)
send({"jsonrpc":"2.0","method":"exit","params":{}}); p.wait(timeout=5)
print("[exit]", p.returncode)
