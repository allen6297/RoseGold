module stdlib

# Exercises the new standard-library builtins in one program.
fn main():
    # growable lists
    var xs = [10]
    push(xs, 20)
    push(xs, 30)
    print("list  =", xs, " len =", len(xs))
    var last = pop(xs)
    print("pop   =", last, "->", xs)

    # strings
    print("ord   =", ord("A"))
    print("chr   =", chr(66))
    print("substr=", substr("RoseGold", 0, 4))
    print("str   =", str(42) + "!")
    print("int   =", int("123") + 1)
    var parts = split("a,b,c", ",")
    print("split =", parts, " len =", len(parts))

    # file I/O
    writeFile("/tmp/rg_stdlib.txt", "hello from RoseGold")
    print("file  =", readFile("/tmp/rg_stdlib.txt"))
