# RoseGold — build & test.  `make` builds the compiler; `make test` runs the suite.
# Default to clang++ (the reference compiler the goldens were generated with);
# override with `make CXX=g++ ...` or the CXX env var. Exported so the Python
# harness builds with the same toolchain.
CXX := $(if $(filter default,$(origin CXX)),clang++,$(CXX))
export CXX
CXXFLAGS ?= -std=c++17 -O2
BIN       = cpp/rosegoldc
SRC       = cpp/src/main.cpp
HEADERS   = $(wildcard cpp/src/*.hpp)
EMBED     = engine game hotreload externdemo

.PHONY: all build test ts-parity update-golden run fmt embed clean

all: build

build: $(BIN)
$(BIN): $(SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(SRC)

# Full suite: golden examples, error fixtures, C++/Python parity, LSP drivers,
# embedding demos, and the builtin-table consistency guard.
test: build
	python3 cpp/test/run_tests.py

# Verify the TypeScript front-end port (ts/) is byte-identical to the canonical
# dumps — lexer vs --tokens, parser vs --ast — on every example. Needs Node >=23.6
# (kept out of `make test` so the core suite stays node-free).
ts-parity: build
	node ts/test/parity.mjs

# Regenerate the committed golden snapshots (do this deliberately after an
# intended behavior change, then review the diff before committing).
update-golden: build
	python3 cpp/test/run_tests.py --update

# Run a single program:  make run FILE=examples/prog.rg
run: build
	./$(BIN) $(FILE)

# Print a file in canonical style:  make fmt FILE=examples/prog.rg
fmt: build
	./$(BIN) --format $(FILE)

# Build the C++ host-embedding demos.
embed: build
	$(foreach e,$(EMBED),$(CXX) $(CXXFLAGS) -o cpp/embed/$(e) cpp/embed/$(e).cpp &&) true

clean:
	rm -f $(BIN) $(addprefix cpp/embed/,$(EMBED))
