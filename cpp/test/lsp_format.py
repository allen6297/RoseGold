#!/usr/bin/env python3
# Exercises textDocument/formatting: opens a deliberately-messy document and
# prints the single full-document edit the server returns.
import json, os, subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN  = os.path.join(ROOT, "cpp", "rosegoldc")
URI  = "file://" + os.path.join(ROOT, "examples", "scratch.rg")

# valid but messy: 2-space + 6-space indents, trailing whitespace, blank runs
MESSY = 'module scratch\n\n\n\nfn main():\n  var x = 1   \n  if x < 2:\n      print("small")   \n\n\n  # note\n  print(x)\n'

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
print("[caps] documentFormatting=%s" % caps.get("documentFormattingProvider"))
send({"jsonrpc":"2.0","method":"initialized","params":{}})
send({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":URI,"languageId":"rosegold","version":1,"text":MESSY}}})

send({"jsonrpc":"2.0","id":2,"method":"textDocument/formatting","params":{"textDocument":{"uri":URI},"options":{"tabSize":4,"insertSpaces":True}}})
edits = wait(2)["result"]
if not edits:
    print("[format] no edits")
else:
    e = edits[0]
    r = e["range"]
    print("[format] 1 edit, range %d:%d..%d:%d" % (r["start"]["line"], r["start"]["character"], r["end"]["line"], r["end"]["character"]))
    print("---- newText ----")
    print(e["newText"], end="")
    print("---- end ----")

send({"jsonrpc":"2.0","id":9,"method":"shutdown","params":{}}); wait(9)
send({"jsonrpc":"2.0","method":"exit","params":{}}); p.wait(timeout=5)
print("[exit]", p.returncode)
