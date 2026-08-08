#!/usr/bin/env python3
# Exercises textDocument/documentSymbol and textDocument/signatureHelp on examples/features/lsp_demo.rg.
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
print("[caps] documentSymbol=%s signatureHelp=%s" % (caps.get("documentSymbolProvider"), bool(caps.get("signatureHelpProvider"))))
send({"jsonrpc":"2.0","method":"initialized","params":{}})
send({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":URI,"languageId":"rosegold","version":1,"text":open(DOC).read()}}})

send({"jsonrpc":"2.0","id":2,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":URI}}})
syms = wait(2)["result"]
KIND = {5:"Class",6:"Method",8:"Field",10:"Enum",11:"Interface",12:"Function",13:"Var"}
def show(s, ind=0):
    print("  "*ind + "- %s (%s)%s" % (s["name"], KIND.get(s["kind"], s["kind"]), s.get("detail","")))
    for c in s.get("children", []): show(c, ind+1)
print("[documentSymbol]")
for s in syms: show(s, 1)

def sighelp(line, ch, i):
    send({"jsonrpc":"2.0","id":i,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":URI},"position":{"line":line,"character":ch}}})
    r = wait(i)["result"]
    if not r: return "null"
    s = r["signatures"][0]
    return "%s  [active param %d]" % (s["label"], r["activeParameter"])

print("[sig set(     ]", sighelp(13, 8, 3))    # cursor just inside set(
print("[sig set(a, | ]", sighelp(13, 16, 4))   # after the first comma -> active param 1
print("[sig Counter( ]", sighelp(14, 20, 5))   # inside Counter(

send({"jsonrpc":"2.0","id":9,"method":"shutdown","params":{}}); wait(9)
send({"jsonrpc":"2.0","method":"exit","params":{}}); p.wait(timeout=5)
print("[exit]", p.returncode)
