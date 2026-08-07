module generic

# generic class: T is inferred at construction, flows through methods
class Box<T>:
    var value: T
    init(v: T):
        self.value = v
    func get(self) -> T:
        return self.value

# generic enum
enum Option<T>:
    Some(value: T)
    None

func first(xs: List<Int>) -> Option<Int>:
    for x in xs:
        return Some(x)
    return None

func main():
    var bi = Box(41)                 # Box<Int> inferred
    print("box int    =", bi.get())  # get() : Int
    var bs = Box("hello")            # Box<String> inferred
    print("box string =", bs.get())  # get() : String

    var o = first([10, 20, 30])
    var v = match o:
        Some(n): n
        None: 0
    print("first      =", v)
