-- Flat color for editor line boxes (selection bounds and actor markers).
local S = shader("Selection Box")

local position  = S:attribute("position", "vec3")
local model     = S:builtin("model")
local view_proj = S:builtin("view_proj")
local color     = S:param("color", "color", { 1.0, 0.6, 0.1 })

S:vertex(function(a)
    return view_proj * (model * vec4(a.position, 1.0)), {}
end)

S:fragment(function(v)
    return vec4(color, 1.0)
end)

return S
