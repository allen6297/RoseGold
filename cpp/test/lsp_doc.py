#!/usr/bin/env python3
# Verifies the language server surfaces doc comments (##*** / #/*** */#) on hover.
import json, os, subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN  = os.path.join(ROOT, "cpp", "rosegoldc")
DOC  = os.path.join(ROOT, "examples", "documented.rg")
URI  = "file://" + DOC
TEXT = open(DOC).read()

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

def pos(substr, needle):
    for ln, line in enumerate(TEXT.splitlines()):
        if substr in line: return ln, line.index(needle)
    raise SystemExit("not found: " + substr)

send({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}); wait(1)
send({"jsonrpc":"2.0","method":"initialized","params":{}})
send({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":URI,"languageId":"rosegold","version":1,"text":TEXT}}})

def hover(substr, needle, label):
    ln, ch = pos(substr, needle)
    send({"jsonrpc":"2.0","id":50,"method":"textDocument/hover","params":{"textDocument":{"uri":URI},"position":{"line":ln,"character":ch}}})
    r = wait(50)["result"]
    print("[hover %s]" % label)
    print(r["contents"]["value"] if r else "null")
    print("----")

hover("func add", "add", "add (@param/@return doc)")   # a function with a line-doc block
hover("print(add(", "add", "add — from a call site")   # doc shows on a USE, too
hover("func bump", "bump", "method with a doc")        # a class method

send({"jsonrpc":"2.0","id":9,"method":"shutdown","params":{}}); wait(9)
send({"jsonrpc":"2.0","method":"exit","params":{}}); p.wait(timeout=5)
print("[exit]", p.returncode)
