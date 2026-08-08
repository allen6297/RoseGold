module debug_me

class Point:
    var x: Int
    var y: Int
    init(a: Int, b: Int):
        self.x = a
        self.y = b

func add(a: Int, b: Int) -> Int:
    var sum = a + b
    return sum

func main():
    var x = 10
    var y = 32
    var z = add(x, y)
    var pt = Point(3, 4)
    var nums = [z, x, y]
    print(z)
