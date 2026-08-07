module client

import secret

# ---------------------------------------------------------------------
#  PROBE: does your VS Code extension understand §5 (name resolution),
#  or is it just indexing identifiers as text?
#
#  Open this file in the editor. On each 'secret.___' below, try
#  go-to-definition (Cmd/Ctrl-click) and hover, and watch autocomplete
#  after typing 'secret.'.
#
#    REAL resolution  -> flags (B) and (C) as errors, refuses to jump,
#                        and does NOT offer 'hidden' in autocomplete.
#    HEURISTIC index  -> happily jumps to every one and suggests
#                        'hidden' too. Looks smart; is NOT real §5/§6.
#
#  Our own toolchain (parser.py / resolver.py) also does NOT catch
#  (B) or (C) yet -- that is exactly what §5/§6 would add.
# ---------------------------------------------------------------------

func probe():
    var a = secret.shown     # (A) LEGAL    : 'shown' is pub
    var b = secret.hidden    # (B) ILLEGAL  : 'hidden' is internal to 'secret'
    var c = hidden           # (C) ILLEGAL  : bare name, not imported or declared here
    return a
