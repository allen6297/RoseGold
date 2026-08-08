#!/usr/bin/env python3
# Exercises the Debug Adapter (rosegoldc --dap): breakpoint, call stack,
# locals/globals, stepIn, next (step over), evaluate, continue, output.
import json, os, subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN  = os.path.join(ROOT, "cpp", "rosegoldc")
PROG = os.path.join(ROOT, "cpp", "test", "fixtures", "debug_me.rg")

p = subprocess.Popen([BIN, "--dap"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
_seq = [0]
def frame(m): b = json.dumps(m).encode(); return b"Content-Length: %d\r\n\r\n%s" % (len(b), b)
def send(command, **args):
    _seq[0] += 1
    p.stdin.write(frame({"seq": _seq[0], "type": "request", "command": command, "arguments": args})); p.stdin.flush()
def recv():
    n = 0
    while True:
        l = p.stdout.readline()
        if not l: return None
        l = l.rstrip(b"\r\n")
        if l == b"": break
        if l.lower().startswith(b"content-length:"): n = int(l.split(b":")[1])
    return json.loads(p.stdout.read(n))

def wait_response(cmd):
    while True:
        m = recv()
        if m is None: raise SystemExit("closed")
        if m.get("type") == "response" and m.get("command") == cmd: return m
        if m.get("type") == "event": EVENTS.append(m)
def wait_event(name):
    # look in the buffer first, then read
    for i, e in enumerate(EVENTS):
        if e.get("event") == name: return EVENTS.pop(i)
    while True:
        m = recv()
        if m is None: raise SystemExit("closed")
        if m.get("type") == "event":
            if m.get("event") == name: return m
            EVENTS.append(m)
EVENTS = []

# ---- handshake ----
send("initialize", clientID="test", adapterID="rosegold")
caps = wait_response("initialize")["body"]
print("[caps] configurationDone=%s evaluateForHovers=%s" % (caps.get("supportsConfigurationDoneRequest"), caps.get("supportsEvaluateForHovers")))
wait_event("initialized")
send("launch", program=PROG, stopOnEntry=False); wait_response("launch")
send("setBreakpoints", source={"path": PROG}, breakpoints=[{"line": 10}])
bps = wait_response("setBreakpoints")["body"]["breakpoints"]
print("[breakpoints] %s" % [(b["line"], b["verified"]) for b in bps])
send("configurationDone"); wait_response("configurationDone")

def show_stop(reason_label):
    ev = wait_event("stopped")
    print("[%s] reason=%s" % (reason_label, ev["body"]["reason"]))
    send("stackTrace", threadId=1); frames = wait_response("stackTrace")["body"]["stackFrames"]
    print("  stack: " + ", ".join("%s:%d" % (f["name"], f["line"]) for f in frames))
    return frames

def locals_of(frame_id, label):
    send("scopes", frameId=frame_id); scopes = wait_response("scopes")["body"]["scopes"]
    ref = next(s["variablesReference"] for s in scopes if s["name"] == "Locals")
    send("variables", variablesReference=ref); vs = wait_response("variables")["body"]["variables"]
    print("  locals %s: %s" % (label, " ".join("%s=%s" % (v["name"], v["value"]) for v in vs)))

# ---- stop 1: breakpoint at line 10 (in main) ----
frames = show_stop("stopped")
locals_of(0, frames[0]["name"])
send("scopes", frameId=0); scopes = wait_response("scopes")["body"]["scopes"]
gref = next(s["variablesReference"] for s in scopes if s["name"] == "Globals")
send("variables", variablesReference=gref); gv = wait_response("variables")["body"]["variables"]
print("  globals: %s" % (" ".join("%s=%s" % (v["name"], v["value"]) for v in gv) or "(none)"))

# ---- stepIn -> line 4 (into add) ----
send("stepIn", threadId=1); wait_response("stepIn")
frames = show_stop("stepIn")
locals_of(0, frames[0]["name"])

# ---- next -> line 5 (sum now assigned) ----
send("next", threadId=1); wait_response("next")
frames = show_stop("next")
locals_of(0, frames[0]["name"])
send("evaluate", expression="sum", frameId=0, context="watch")
ev = wait_response("evaluate")
print("  eval sum = %s (success=%s)" % (ev["body"].get("result", ""), ev["success"]))

# ---- continue -> program finishes ----
send("continue", threadId=1); wait_response("continue")
out = wait_event("output"); print("[output] %s" % out["body"]["output"].strip())
wait_event("terminated"); print("[terminated]")
send("disconnect"); wait_response("disconnect")
p.wait(timeout=5)
print("[exit]", p.returncode)
