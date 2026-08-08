module signals

# ---------------------------------------------------------------------
#  Signals are a LANGUAGE feature: `signal name(typed params)` declares an
#  event on a class. Fire it with `.emit(args)` (checked against the params);
#  subscribe with `.connect(handler)` (the handler's params are checked too).
#  A handler may take FEWER params than the signal emits — the extra args are
#  dropped (Godot-style), so a no-arg listener can watch a rich signal.
# ---------------------------------------------------------------------

class Player:
    var hp: Int
    signal onHit(dmg: Int)
    signal onDied()

    init(hp: Int):
        self.hp = hp

    func hurt(self, dmg: Int):
        self.hp = self.hp - dmg
        self.onHit.emit(dmg)               # checked: dmg must be Int
        if self.hp <= 0:
            self.onDied.emit()

func main():
    var p = Player(25)
    p.onHit.connect(func(d: Int) => print("  ui:  -", d, "hp"))
    p.onHit.connect(func(d: Int) => print("  sfx: play hurt sound"))
    p.onHit.connect(func() => print("  log: (took a hit)"))   # overflow: no-arg handler on a 1-arg signal
    p.onDied.connect(func() => print("  *** game over ***"))

    print("hurt 10:")
    p.hurt(10)
    print("hurt 20:")
    p.hurt(20)
