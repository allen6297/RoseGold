#!/usr/bin/env python3
# Exercises the richer LSP capabilities: semantic tokens, document highlight,
# and folding ranges on examples/lsp_demo.rg.
import json, os, subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN  = os.path.join(ROOT, "cpp", "rosegoldc")
DOC  = os.path.join(ROOT, "examples", "lsp_demo.rg")
URI  = "file://" + DOC
LEGEND = ["type", "class", "enum", "interface", "function", "method", "property", "variable", "parameter"]
LINES = open(DOC).read().split("\n")

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
print("[caps] semanticTokens=%s documentHighlight=%s folding=%s" %
      (bool(caps.get("semanticTokensProvider")), caps.get("documentHighlightProvider"), caps.get("foldingRangeProvider")))
send({"jsonrpc":"2.0","method":"initialized","params":{}})
send({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":URI,"languageId":"rosegold","version":1,"text":open(DOC).read()}}})

# semantic tokens: decode the delta stream back to (line, char, type, text)
send({"jsonrpc":"2.0","id":2,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":URI}}})
data = wait(2)["result"]["data"]
print("[semantic tokens] %d tokens:" % (len(data) // 5))
line = 0; ch = 0
for i in range(0, len(data), 5):
    dl, dc, ln, ty, mod = data[i:i+5]
    line += dl
    ch = dc if dl else ch + dc
    text = LINES[line][ch:ch+ln]
    print("   %2d:%-2d %-9s %s" % (line, ch, LEGEND[ty], text))

# document highlight on the field `value` (decl line 3) -> decl + all member uses
send({"jsonrpc":"2.0","id":3,"method":"textDocument/documentHighlight","params":{"textDocument":{"uri":URI},"position":{"line":3,"character":9}}})
hi = wait(3)["result"]
print("[highlight value] %d occurrences at lines %s" % (len(hi), sorted(h["range"]["start"]["line"] for h in hi)))

# folding ranges
send({"jsonrpc":"2.0","id":4,"method":"textDocument/foldingRange","params":{"textDocument":{"uri":URI}}})
folds = wait(4)["result"]
print("[folding] %d ranges: %s" % (len(folds), [(f["startLine"], f["endLine"]) for f in folds]))

send({"jsonrpc":"2.0","method":"exit","params":{}}); p.wait(timeout=5)
