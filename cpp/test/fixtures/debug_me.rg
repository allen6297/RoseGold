module debug_me

func add(a: Int, b: Int) -> Int:
    var sum = a + b
    return sum

func main():
    var x = 10
    var y = 32
    var z = add(x, y)
    print(z)
