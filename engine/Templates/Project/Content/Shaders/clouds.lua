-- Drifting clouds on a flat layer high above the camera, blended over the sky ("overlay" pass).
local S = shader("Clouds")

local time      = S:builtin("time")
local light_dir = S:builtin("light_dir")

local coverage = S:param("coverage", "float", 0.40, { min = 0, max = 1 })
local softness = S:param("softness", "float", 0.32, { min = 0.02, max = 1 })
local scale    = S:param("scale", "float", 1.6, { min = 0.2, max = 8 })
local wind     = S:param("wind", "vec2", { 0.020, 0.008 })
local tint     = S:param("tint", "color", { 1.0, 1.0, 1.0 })

S:fullscreen(function(v)
    local up = v.ray.y
    -- Project the view ray onto a plane; distant (low) clouds get compressed towards the horizon.
    local p = v.ray.xz / max(up, 0.02) * scale + wind * time

    local density = fbm(p, 5)
    local cloud = smoothstep(coverage, coverage + softness, density)

    -- Brighter on the side facing the sun, darker where the cloud is thick.
    local shade = 1.0 - 0.45 * smoothstep(coverage, coverage + 0.5, density)
    local lit = mix(0.78, 1.0, clamp(light_dir.y * 0.5 + 0.5, 0.0, 1.0)) * shade

    -- Only above the horizon, fading out before it.
    local alpha = cloud * smoothstep(0.02, 0.22, up) * 0.95
    return vec4(pow(tint * lit, vec3(1.0 / 2.2)), alpha)
end)

return S
