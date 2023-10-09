"""
    CrystFEL

Julia bindings for CrystFEL data structures and routines

## Quick start
```julia
  using CrystFEL
  ...
```
"""
module CrystFEL

include("detgeom.jl")
using .DetGeoms
export Panel, DetGeom

include("symmetry.jl")
using .Symmetry
export SymOpList

include("datatemplates.jl")
using .DataTemplates
export DataTemplate, loaddatatemplate

include("reflists.jl")
using .RefLists
export RefList, Reflection, loadreflist, savereflections

end  # of module
