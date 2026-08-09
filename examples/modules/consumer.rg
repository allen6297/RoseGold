module consumer

import facade

fn use():
    var ok  = facade.circle    # LEGAL   : 'circle' is re-exported by facade (§6)
    var bad = facade.helper     # ILLEGAL : 'helper' is internal to core.shapes, not re-exported
    return ok
