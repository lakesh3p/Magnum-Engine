-- Editor ground grid: 1 m lines, stronger 10 m lines, colored X/Z axes, fading with distance.
-- Drawn on a large plane at y = 0 with alpha blending.
local S = shader("Editor Grid")

local position   = S:attribute("position", "vec3")
local model      = S:builtin("model")
local view_proj  = S:builtin("view_proj")
local camera_pos = S:builtin("camera_pos")

S:vertex(function(a)
    local world = model * vec4(a.position, 1.0)
    return view_proj * world, { world_pos = world.xyz }
end)

S:fragment(function(v)
    local p = v.world_pos.xz

    -- 1.0 on a grid line of the given spacing, antialiased with screen-space derivatives.
    local function grid_line(spacing)
        local c = p / spacing
        local g = abs(fract(c - 0.5) - 0.5) / fwidth(c)
        return 1.0 - min(min(g.x, g.y), 1.0)
    end

    local minor = grid_line(1.0) * 0.22
    local major = grid_line(10.0) * 0.45
    local x_axis = 1.0 - min(abs(p.y) / fwidth(p.y), 1.0)
    local z_axis = 1.0 - min(abs(p.x) / fwidth(p.x), 1.0)

    local color = mix(vec3(0.62), vec3(0.90, 0.22, 0.22), x_axis)
    color = mix(color, vec3(0.25, 0.45, 0.95), z_axis)

    local dist = length(camera_pos - v.world_pos)
    local fade = 1.0 - smoothstep(40.0, 160.0, dist)
    local alpha = max(max(minor, major), max(x_axis, z_axis) * 0.85) * fade
    return vec4(color, alpha)
end)

return S
