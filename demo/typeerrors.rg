module typeerrors

class Box:
    private var secret: Int
    init(s: Int):
        self.secret = s

func need(n: Int) -> Int:
    return n

func main():
    var a: Int = "hello"        # (1) String not assignable to Int
    var b = need(true)          # (2) Bool arg where Int expected
    if 42:                      # (3) condition must be Bool
        pass
    var box = Box(5)
    var c = box.secret          # (4) 'secret' is private to Box
    var d = box.nope            # (5) no member 'nope'
    var e = 1 + "x"             # (6) cannot apply + to Int and String
