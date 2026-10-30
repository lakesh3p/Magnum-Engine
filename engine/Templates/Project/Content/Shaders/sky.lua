-- Procedural sky: a horizon-to-zenith gradient, a sun along the light direction and a dark ground.
-- A fullscreen shader: drawn by a "sky" pass in Content/Pipelines/default.lua.
local S = shader("Sky")

local light_dir = S:builtin("light_dir")

local zenith    = S:param("zenith", "color", { 0.12, 0.30, 0.65 })
local horizon   = S:param("horizon", "color", { 0.62, 0.78, 0.95 })
local ground    = S:param("ground", "color", { 0.16, 0.17, 0.19 })
local sun_size  = S:param("sun_size", "float", 0.9996, { min = 0.99, max = 1 })
local sun_glow  = S:param("sun_glow", "float", 0.35, { min = 0, max = 2 })

S:fullscreen(function(v)
    local up = v.ray.y
    -- Sky above the horizon, ground below, blended over a thin band.
    local sky = mix(horizon, zenith, pow(clamp(up, 0.0, 1.0), 0.55))
    local color = mix(ground, sky, smoothstep(-0.02, 0.02, up))

    local sun_dot = max(dot(v.ray, light_dir), 0.0)
    local disk = smoothstep(sun_size - 0.0002, sun_size, sun_dot)
    local glow = pow(sun_dot, 24.0) * sun_glow
    color = color + vec3(1.0, 0.85, 0.6) * (glow + disk * 4.0) * smoothstep(-0.05, 0.05, up)

    -- The viewport framebuffer is not sRGB, so encode to gamma here.
    return vec4(pow(color, vec3(1.0 / 2.2)), 1.0)
end)

return S
