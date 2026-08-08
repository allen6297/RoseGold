module check_sample

# A within-subset program with deliberate TYPE errors that the self-hosted
# checker (rgchecker.rg) reports identically to `rosegoldc --check`: undefined
# names, call arity, and argument-type mismatches.

func add(a: Int, b: Int) -> Int:
    return a + b

func greet(name: String) -> String:
    return name

func main():
    print(add(1, 2))
    print(add(1))
    print(add(1, "two"))
    print(greet(42))
    print(missing)
    var x = add(unknownVar, 2)
    print(x)
