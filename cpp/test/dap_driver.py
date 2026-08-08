#!/usr/bin/env python3
# Exercises the Debug Adapter (rosegoldc --dap): breakpoints, call stack,
# locals/globals, stepIn, next (step over), evaluate, continue, output, and
# drilling into a composite object and a list via variablesReference.
import json, os, subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN  = os.path.join(ROOT, "cpp", "rosegoldc")
PROG = os.path.join(ROOT, "cpp", "test", "fixtures", "debug_me.rg")

p = subprocess.Popen([BIN, "--dap"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
_seq = [0]; EVENTS = []
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
    for i, e in enumerate(EVENTS):
        if e.get("event") == name: return EVENTS.pop(i)
    while True:
        m = recv()
        if m is None: raise SystemExit("closed")
        if m.get("type") == "event":
            if m.get("event") == name: return m
            EVENTS.append(m)

def variables(ref): send("variables", variablesReference=ref); return wait_response("variables")["body"]["variables"]
def locals_ref(fid):
    send("scopes", frameId=fid); sc = wait_response("scopes")["body"]["scopes"]
    return next(s["variablesReference"] for s in sc if s["name"] == "Locals")
def fmt(vs): return " ".join("%s=%s" % (v["name"], v["value"]) for v in vs)
def show_stop(label):
    ev = wait_event("stopped")
    send("stackTrace", threadId=1); fr = wait_response("stackTrace")["body"]["stackFrames"]
    print("[%s] reason=%s  stack: %s" % (label, ev["body"]["reason"], ", ".join("%s:%d" % (f["name"], f["line"]) for f in fr)))
    return fr

# ---- handshake ----
send("initialize", clientID="test", adapterID="rosegold")
caps = wait_response("initialize")["body"]
print("[caps] configurationDone=%s" % caps.get("supportsConfigurationDoneRequest"))
wait_event("initialized")
send("launch", program=PROG, stopOnEntry=False); wait_response("launch")
send("setBreakpoints", source={"path": PROG}, breakpoints=[{"line": 17}, {"line": 20}])
print("[breakpoints] %s" % [b["line"] for b in wait_response("setBreakpoints")["body"]["breakpoints"]])
send("configurationDone"); wait_response("configurationDone")

# ---- stop 1: line 17 in main -> step into add, step over ----
show_stop("stop")
send("stepIn", threadId=1); wait_response("stepIn")
fr = show_stop("stepIn"); print("  locals %s: %s" % (fr[0]["name"], fmt(variables(locals_ref(0)))))
send("next", threadId=1); wait_response("next")
fr = show_stop("next"); print("  locals %s: %s" % (fr[0]["name"], fmt(variables(locals_ref(0)))))
send("evaluate", expression="sum", frameId=0, context="watch")
print("  eval sum = %s" % wait_response("evaluate")["body"]["result"])
send("continue", threadId=1); wait_response("continue")

# ---- stop 2: line 20 in main -> drill into the object and the list ----
show_stop("stop")
lv = variables(locals_ref(0))
print("  locals main: %s" % fmt(lv))
pt = next(v for v in lv if v["name"] == "pt")
print("  drill pt (%s, ref!=0=%s): %s" % (pt["value"], pt["variablesReference"] != 0, fmt(variables(pt["variablesReference"]))))
nums = next(v for v in lv if v["name"] == "nums")
print("  drill nums (%s, ref!=0=%s): %s" % (nums["value"], nums["variablesReference"] != 0, fmt(variables(nums["variablesReference"]))))
send("continue", threadId=1); wait_response("continue")
print("[output] %s" % wait_event("output")["body"]["output"].strip())
wait_event("terminated"); print("[terminated]")
send("disconnect"); wait_response("disconnect")
p.wait(timeout=5)
print("[exit]", p.returncode)
