module counter

# Module-global state that survives a hot reload.
var count = 0

func tick():
    count = count + 1

func show():
    print("counter =", count)
