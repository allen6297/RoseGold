module demo

func fib(n: Int) -> Int:
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

func main():
    var total = 0
    var i = 0
    while i < 10:
        total = total + i
        i = i + 1
    print("sum 0..9 =", total)
    print("fib(10)  =", fib(10))
    print("fib(20)  =", fib(20))
    print("greeting =", "hello " + "RoseGold")
