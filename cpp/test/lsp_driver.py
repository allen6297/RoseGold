#!/usr/bin/env python3
# Minimal LSP client that drives `rosegoldc --lsp` over stdio and checks
# hover / go-to-definition / completion on examples/lsp_demo.rg.
import json, os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN  = os.path.join(ROOT, "cpp", "rosegoldc")
DOC  = os.path.join(ROOT, "examples", "lsp_demo.rg")
URI  = "file://" + DOC

def frame(msg):
    body = json.dumps(msg).encode()
    return b"Content-Length: %d\r\n\r\n%s" % (len(body), body)

def read_message(f):
    length = 0
    while True:
        line = f.readline()
        if not line:
            return None
        line = line.rstrip(b"\r\n")
        if line == b"":
            break
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":")[1].strip())
    return json.loads(f.read(length))

p = subprocess.Popen([BIN, "--lsp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
text = open(DOC).read()

def send(msg): p.stdin.write(frame(msg)); p.stdin.flush()

# id -> response collector
def wait_for(want_id):
    while True:
        m = read_message(p.stdout)
        if m is None:
            raise SystemExit("server closed unexpectedly")
        if m.get("method") == "textDocument/publishDiagnostics":
            diags = m["params"]["diagnostics"]
            print(f"[diagnostics] {len(diags)} " + json.dumps([d['message'] for d in diags]))
            continue
        if m.get("id") == want_id:
            return m

send({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file://"+ROOT,"capabilities":{}}})
init = wait_for(1)
print("[initialize] capabilities:", json.dumps(init["result"]["capabilities"]))
send({"jsonrpc":"2.0","method":"initialized","params":{}})
send({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":URI,"languageId":"rosegold","version":1,"text":text}}})

# hover on `counts` in `    set(counts, "a", 1)` (line 13, char 9)
send({"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":URI},"position":{"line":13,"character":9}}})
h = wait_for(2)
print("[hover counts]", json.dumps(h["result"]))

# hover on `c` in `    print(c.get())`? instead hover the map var name at decl use — also hover a method result
send({"jsonrpc":"2.0","id":6,"method":"textDocument/hover","params":{"textDocument":{"uri":URI},"position":{"line":15,"character":7}}})
h2 = wait_for(6)
print("[hover c.bump]", json.dumps(h2["result"]))

# definition on `Counter` in `    var c = Counter(10)` (line 14, char 13)
send({"jsonrpc":"2.0","id":3,"method":"textDocument/definition","params":{"textDocument":{"uri":URI},"position":{"line":14,"character":13}}})
d = wait_for(3)
print("[def Counter]", json.dumps(d["result"]))

# definition on `bump` in `    c.bump()` (line 15, char 7)
send({"jsonrpc":"2.0","id":4,"method":"textDocument/definition","params":{"textDocument":{"uri":URI},"position":{"line":15,"character":7}}})
d2 = wait_for(4)
print("[def bump]", json.dumps(d2["result"]))

# definition on the LOCAL `counts` in `    set(counts, "a", 1)` (line 13) -> its `var counts` decl (line 12)
send({"jsonrpc":"2.0","id":10,"method":"textDocument/definition","params":{"textDocument":{"uri":URI},"position":{"line":13,"character":9}}})
print("[def local counts]", json.dumps(wait_for(10)["result"]))

# definition on the LOCAL `c` in `    c.bump()` (line 15) -> its `var c` decl (line 14)
send({"jsonrpc":"2.0","id":11,"method":"textDocument/definition","params":{"textDocument":{"uri":URI},"position":{"line":15,"character":4}}})
print("[def local c]", json.dumps(wait_for(11)["result"]))

# definition on the PARAM `start` in `        self.value = start` (line 5) -> its declaration in init(start: Int) (line 4)
send({"jsonrpc":"2.0","id":12,"method":"textDocument/definition","params":{"textDocument":{"uri":URI},"position":{"line":5,"character":22}}})
print("[def param start]", json.dumps(wait_for(12)["result"]))

# completion after `c.` in `    c.bump()` (line 15, char 6)
send({"jsonrpc":"2.0","id":5,"method":"textDocument/completion","params":{"textDocument":{"uri":URI},"position":{"line":15,"character":6}}})
c = wait_for(5)
labels = [(i["label"], i.get("detail")) for i in c["result"]["items"]]
print("[completion c.]", json.dumps(labels))

send({"jsonrpc":"2.0","id":9,"method":"shutdown","params":{}})
wait_for(9)
send({"jsonrpc":"2.0","method":"exit","params":{}})
p.wait(timeout=5)
print("[exit] server returned", p.returncode)
