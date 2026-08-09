module counter

# Module-global state that survives a hot reload.
var count = 0

fn tick():
    count = count + 1

fn show():
    print("counter =", count)
