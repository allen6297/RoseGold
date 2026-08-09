module appinit

import loader

init:
    print("appinit: init ran")

fn main():
    print("main: loader ready =", loader.status())
