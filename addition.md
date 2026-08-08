# Additions
- [x] signals as keywords  — done: `signal name(params)` on a class, `.emit`/`.connect`, type-checked, handler overflow allowed
- [x] documentation, similar to java's  — done: `##` line / `#/ ... /#` block doc comments, `rosegoldc --doc` (Markdown, @param/@return), LSP hover shows docs
- [x] extern c/c++ like rust  — done: `extern func name(params) -> Ret`, type-checked standalone, bound by name to the host NativeRegistry at runtime (`cpp/embed/externdemo.*`)
