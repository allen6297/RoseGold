module tools.features            # dotted module path

import math                       # qualified whole-module
import physics as phys           # aliased
import math.(sin, cos)           # selective (parens, not braces)
pub import shapes                # re-exported through this module

# generic class declaration with a trait bound
pub class Box<T: Comparable>:
    var value: T

    # constructor
    init(value: T):
        self.value = value

pub enum Option<T>:
    Some(value: T)
    None

# generic function + function-typed parameter + while loop + indexing
pub func each<T>(items: List<T>, fn: func(T) -> Void):
    var i = 0
    while i < len(items):
        fn(items[i])
        i = i + 1

func demo():
    # single-expression closure (arrow form) composes inside parens
    var inc: func(Int) -> Int = func(x: Int) -> Int => x + 1
    var b = Box(inc(41))          # construction via call syntax
    each([1, 2, 3], func(x: Int) => x)
