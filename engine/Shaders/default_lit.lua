-- Built-in fallback surface, used when a project has no Content/Shaders/default_lit.lua of its own (or its material is
-- missing), so that meshes are still drawn. Kept in step with the project template's Content/Shaders/default_lit.lua.
-- One directional light, ambient term and a Blinn-Phong highlight.
local S = shader("Default Lit")

local position = S:attribute("position", "vec3")
local normal   = S:attribute("normal", "vec3")
local uv       = S:attribute("uv", "vec2")

local model         = S:builtin("model")
local view_proj     = S:builtin("view_proj")
local normal_matrix = S:builtin("normal_matrix")
local camera_pos    = S:builtin("camera_pos")
local light_dir     = S:builtin("light_dir")
local light_color   = S:builtin("light_color")

local base_color = S:param("base_color", "color", { 0.8, 0.8, 0.8 })
local ambient    = S:param("ambient", "float", 0.18, { min = 0, max = 1 })
local specular   = S:param("specular", "float", 0.4, { min = 0, max = 1 })
local shininess  = S:param("shininess", "float", 32.0, { min = 1, max = 256 })

S:vertex(function(a)
    local world = model * vec4(a.position, 1.0)
    return view_proj * world, {
        world_pos = world.xyz,
        normal = normalize(normal_matrix * a.normal),
    }
end)

S:fragment(function(v)
    local n = normalize(v.normal)
    local ndl = max(dot(n, light_dir), 0.0)
    local view_dir = normalize(camera_pos - v.world_pos)
    local half_vec = normalize(light_dir + view_dir)
    local spec = pow(max(dot(n, half_vec), 0.0), shininess) * specular * min(ndl * 8.0, 1.0)

    local lit = base_color * (ambient + light_color * ndl) + light_color * spec
    -- The viewport framebuffer is not sRGB, so encode to gamma here.
    return vec4(pow(lit, vec3(1.0 / 2.2)), 1.0)
end)

return S
