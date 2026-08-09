module lsp_demo

class Counter:
    var value: Int
    init(start: Int):
        self.value = start
    fn bump(self):
        self.value = self.value + 1
    fn get(self) -> Int:
        return self.value

fn main():
    var counts: Map<String, Int> = map()
    set(counts, "a", 1)
    var c = Counter(10)
    c.bump()
    print(get(counts, "a"))
    print(c.get())
