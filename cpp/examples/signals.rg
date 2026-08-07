module signals

# A signal is a list of listener functions -- connect() subscribes, emit() fires
# them. Built purely from first-class functions + lists; no engine support needed.
class Signal:
    var listeners: List
    init():
        self.listeners = []
    func connect(self, fn):
        push(self.listeners, fn)
    func emit(self, arg):
        for fn in self.listeners:
            fn(arg)

func main():
    var on_hit = Signal()
    on_hit.connect(func(dmg) => print("  ui:  -", dmg, "hp"))
    on_hit.connect(func(dmg) => print("  sfx: play hurt sound"))
    print("emit 10:")
    on_hit.emit(10)
    print("emit 5:")
    on_hit.emit(5)
