-- Sprites: a textured quad with a tint and a rectangle of the image (an atlas cell). Used by the "sprites" pass,
-- which sets the parameters from each Sprite component. Colors are shown as authored (no gamma curve).
local S = shader("Sprite")

local position = S:attribute("position", "vec3")
local uv       = S:attribute("uv", "vec2")

local model     = S:builtin("model")
local view_proj = S:builtin("view_proj")

local tex     = S:param("tex", "texture", nil, { srgb = false })
local tint    = S:param("tint", "color", { 1.0, 1.0, 1.0, 1.0 })
local uv_rect = S:param("uv_rect", "vec4", { 0.0, 0.0, 1.0, 1.0 })

S:vertex(function(a)
    return view_proj * (model * vec4(a.position, 1.0)), { uv = a.uv * uv_rect.zw + uv_rect.xy }
end)

S:fragment(function(v)
    return texture(tex, v.uv) * tint
end)

return S
