#!/usr/bin/env python3
# Verifies the LSP diagnostics path: type errors on open, live update on
# didChange, and a parse error surfaced as a diagnostic.
import json, os, subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN  = os.path.join(ROOT, "cpp", "rosegoldc")

def frame(msg):
    body = json.dumps(msg).encode()
    return b"Content-Length: %d\r\n\r\n%s" % (len(body), body)

def read_message(f):
    length = 0
    while True:
        line = f.readline()
        if not line: return None
        line = line.rstrip(b"\r\n")
        if line == b"": break
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":")[1].strip())
    return json.loads(f.read(length))

p = subprocess.Popen([BIN, "--lsp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
def send(m): p.stdin.write(frame(m)); p.stdin.flush()

def next_diag():
    while True:
        m = read_message(p.stdout)
        if m is None: raise SystemExit("closed")
        if m.get("method") == "textDocument/publishDiagnostics":
            return m["params"]["diagnostics"]

send({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}})
while read_message(p.stdout).get("id") != 1: pass
send({"jsonrpc":"2.0","method":"initialized","params":{}})

DOC = os.path.join(ROOT, "demo", "typeerrors.rg")
URI = "file://" + DOC
send({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":URI,"languageId":"rosegold","version":1,"text":open(DOC).read()}}})
d = next_diag()
print(f"[open typeerrors.rg] {len(d)} diagnostics:")
for x in sorted(d, key=lambda z: z["range"]["start"]["line"]):
    print(f"   line {x['range']['start']['line']+1}: {x['message']}")

# didChange -> fix the file to a clean program; expect 0 diagnostics
clean = "module typeerrors\n\nfunc main():\n    print(\"ok\")\n"
send({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":URI,"version":2},"contentChanges":[{"text":clean}]}})
d2 = next_diag()
print(f"[didChange -> clean] {len(d2)} diagnostics")

# didChange -> parse error; expect >=1 diagnostic from the lexer/parser
broken = "module typeerrors\n\nfunc main(:\n    print(\"x\")\n"
send({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":URI,"version":3},"contentChanges":[{"text":broken}]}})
d3 = next_diag()
print(f"[didChange -> parse error] {len(d3)} diagnostics:")
for x in d3: print(f"   line {x['range']['start']['line']+1}: {x['message']}")

send({"jsonrpc":"2.0","id":9,"method":"shutdown","params":{}})
while read_message(p.stdout).get("id") != 9: pass
send({"jsonrpc":"2.0","method":"exit","params":{}})
p.wait(timeout=5)
print("[exit]", p.returncode)
