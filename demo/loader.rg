module loader

var ready = false

# runs once when this module is loaded (before any importer's init or main)
init:
    ready = true
    print("loader: init ran")

pub func status() -> Bool:
    return ready
