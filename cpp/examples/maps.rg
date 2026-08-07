module maps

# The Map<K, V> type + its builtins: map / set / get / has / keys / remove.
func main():
    var m: Map<String, Int> = map()
    set(m, "one", 1)
    set(m, "two", 2)
    set(m, "three", 3)
    print("size    =", len(m))
    print("get two =", get(m, "two"))
    print("has x   =", has(m, "x"))
    print("has one =", has(m, "one"))
    set(m, "two", 22)                 # update in place
    print("upd two =", get(m, "two"))
    remove(m, "one")
    print("keys    =", keys(m))
    print("size'   =", len(m))
    print("map     =", m)
