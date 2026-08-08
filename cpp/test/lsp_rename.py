#!/usr/bin/env python3
# Exercises textDocument/references, prepareRename, and rename on examples/features/lsp_demo.rg.
import json, os, subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN  = os.path.join(ROOT, "cpp", "rosegoldc")
DOC  = os.path.join(ROOT, "examples", "features", "lsp_demo.rg")
URI  = "file://" + DOC

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
def send(m): p.stdin.write(frame(m)); p.stdin.flush()
def wait(i):
    while True:
        m = read(p.stdout)
        if m is None: raise SystemExit("closed")
        if m.get("method") == "textDocument/publishDiagnostics": continue
        if m.get("id") == i: return m

send({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}})
caps = wait(1)["result"]["capabilities"]
print("[caps] references=%s rename=%s" % (caps.get("referencesProvider"), caps.get("renameProvider")))
send({"jsonrpc":"2.0","method":"initialized","params":{}})
send({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":URI,"languageId":"rosegold","version":1,"text":open(DOC).read()}}})

def references(line, ch, i, incl=True):
    send({"jsonrpc":"2.0","id":i,"method":"textDocument/references","params":{"textDocument":{"uri":URI},"position":{"line":line,"character":ch},"context":{"includeDeclaration":incl}}})
    res = wait(i)["result"] or []
    return sorted((r["range"]["start"]["line"], r["range"]["start"]["character"]) for r in res)

# references on the field `value` (decl line 3; used on 5, 7, 7, 9)
print("[refs value ]", references(3, 9, 2))
# references on the local `counts` (decl line 12; used on 13, 16)
print("[refs counts]", references(12, 9, 3))
# references on the class `Counter` (decl line 2; used on 14)
print("[refs Counter]", references(2, 8, 4))

# prepareRename on a renamable local vs a builtin
send({"jsonrpc":"2.0","id":5,"method":"textDocument/prepareRename","params":{"textDocument":{"uri":URI},"position":{"line":12,"character":9}}})
print("[prepare counts]", json.dumps(wait(5)["result"]))
send({"jsonrpc":"2.0","id":6,"method":"textDocument/prepareRename","params":{"textDocument":{"uri":URI},"position":{"line":16,"character":5}}})  # `print` builtin
print("[prepare print ]", json.dumps(wait(6)["result"]), "(expect null)")

# rename local `counts` -> `totals`
send({"jsonrpc":"2.0","id":7,"method":"textDocument/rename","params":{"textDocument":{"uri":URI},"position":{"line":12,"character":9},"newName":"totals"}})
edits = wait(7)["result"]["changes"][URI]
locs = sorted((e["range"]["start"]["line"], e["range"]["start"]["character"]) for e in edits)
print("[rename counts->totals] %d edits at %s -> all newText='%s'" % (len(edits), locs, edits[0]["newText"]))

# rename the param `start` -> `begin` (decl line 4, use line 5)
send({"jsonrpc":"2.0","id":8,"method":"textDocument/rename","params":{"textDocument":{"uri":URI},"position":{"line":4,"character":10},"newName":"begin"}})
edits2 = wait(8)["result"]["changes"][URI]
print("[rename start->begin ] %d edits at %s" % (len(edits2), sorted((e["range"]["start"]["line"], e["range"]["start"]["character"]) for e in edits2)))

send({"jsonrpc":"2.0","id":9,"method":"shutdown","params":{}}); wait(9)
send({"jsonrpc":"2.0","method":"exit","params":{}}); p.wait(timeout=5)
print("[exit]", p.returncode)
