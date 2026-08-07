# =============================================================
#  NORMALIZED SPEC — INDENTATION STYLE
#  Blocks = ':' + significant indentation, everywhere.
# =============================================================
#
#  Resolved conventions (differ from the draft):
#    - Comments:   '#' line comment, '#/ ... /#' block comment
#    - Visibility: pub | internal | private   (default: internal)
#    - 'static' is an independent modifier, combinable with the above
#    - Functions always have a name
#    - Inheritance: 'extends Base' and/or 'uses TraitA, TraitB'
#    - const must be initialized
#    - match is an expression; one delimiter style only
#    - keywords are lowercase; Type names are Capitalized
# =============================================================

# module header (from moduleHsynta.txt)
module Geometry

import math

pub enum Shape:
    Circle(radius: Float)
    Rect(width: Float, height: Float)

pub trait Describable:
    func describe(self) -> String     # trait method, no body

# 'extends' takes one base; 'uses' takes a comma list of traits
pub class Canvas extends Object uses Describable:

    pub const name: String = "untitled"   # const: must be initialized
    var shapes: List<Shape> = []
    internal var scale = 1.0               # type inferred as Float

    #/
      A block comment can span
      multiple lines cleanly.
    /#

    # return type is optional; omitting '-> Type' means it returns void
    pub func area(self, shape: Shape) -> Float:
        # match used as an expression
        var a = match shape:
            Circle(r):    math.pi * r * r
            Rect(w, h):   w * h
            _:            0.0
        return a * self.scale

    pub func report(self):
        for shape in self.shapes:
            if self.area(shape) > 100.0:
                print("large")
            elif self.area(shape) > 0.0:
                print("small")
            else:
                pass

    # nested class
    private class Layer uses Describable:
        var z: Int = 0

        func describe(self) -> String:
            return "layer"
