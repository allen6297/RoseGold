module errors

# try / catch / raise
func risky(n: Int) -> Int:
    if n < 0:
        raise "negative input!"
    return n * 2

func main():
    try:
        print("ok   =", risky(5))
        print("boom =", risky(-1))    # raises
        print("unreached")
    catch e:
        print("caught:", e)
