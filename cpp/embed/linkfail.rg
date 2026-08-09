module linkfail

# Declares two natives; the host (linkcheck.cpp) registers only host_add, so
# loading this must fail at link time with "undefined native 'host_missing'".
extern:
    fn host_add(a: Int, b: Int) -> Int
    fn host_missing(x: Int) -> Int

fn main():
    print(host_add(1, 2))
    print(host_missing(3))
