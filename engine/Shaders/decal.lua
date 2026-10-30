-- Decals: draws the box of a Decal actor and, for every pixel of it, looks up the surface behind that pixel in
-- scene_depth. The surface point is brought into the box's own space, so the image is projected along the box's local
-- Z axis and fills its local X / Y; pixels whose surface is outside the box stay clear.
-- Write your own decal shader the same way (material = ... on the Decal) to animate or procedurally draw the decal:
-- `p` below is the point in box space, -0.5 .. 0.5 on every axis.
local S = shader("Decal")

local position = S:attribute("position", "vec3")

local model         = S:builtin("model")
local inv_model     = S:builtin("inv_model")
local view_proj     = S:builtin("view_proj")
local inv_view_proj = S:builtin("inv_view_proj")
local scene_depth   = S:builtin("scene_depth")
local resolution    = S:builtin("resolution")
local frag_coord    = S:builtin("frag_coord")

local tex        = S:param("tex", "texture", nil, { srgb = false })
local tint       = S:param("tint", "color", { 1.0, 1.0, 1.0, 1.0 })
local angle_fade = S:param("angle_fade", "float", 0.5, { min = 0, max = 1 })

S:vertex(function(a)
    return view_proj * (model * vec4(a.position, 1.0)), {}
end)

S:fragment(function(v)
    -- The surface under this pixel, in the world and then in the box.
    local uv    = frag_coord.xy / resolution
    local depth = texture(scene_depth, uv).r
    local clip  = inv_view_proj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0)
    local world = clip.xyz / clip.w
    local p     = (inv_model * vec4(world, 1.0)).xyz

    -- Only the part of the surface inside the box gets the image.
    local furthest = max(max(abs(p.x), abs(p.y)), abs(p.z))
    local inside   = 1.0 - step(0.5, furthest)

    -- Surfaces seen edge-on (almost parallel to the projection) would smear the image: fade them out.
    local c       = cross(dFdx(world), dFdy(world))
    local normal  = c / max(length(c), 0.000001)
    local axis    = normalize((model * vec4(0.0, 0.0, 1.0, 0.0)).xyz)
    local facing  = abs(dot(normal, axis))
    local visible = mix(1.0, smoothstep(0.1, 0.45, facing), angle_fade)

    local color = texture(tex, p.xy + vec2(0.5, 0.5)) * tint
    return vec4(color.rgb, color.a * inside * visible)
end)

return S
