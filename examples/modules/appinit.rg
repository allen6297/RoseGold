module appinit

import loader

init:
    print("appinit: init ran")

func main():
    print("main: loader ready =", loader.status())
