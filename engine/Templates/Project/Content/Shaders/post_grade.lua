-- Final image: exposure, saturation and a vignette. A "post" pass reads the frame so far as scene_color.
local S = shader("Post Grade")

local scene = S:builtin("scene_color")

local exposure   = S:param("exposure", "float", 1.0, { min = 0.2, max = 3 })
local saturation = S:param("saturation", "float", 1.05, { min = 0, max = 2 })
local vignette   = S:param("vignette", "float", 0.35, { min = 0, max = 1 })

S:fullscreen(function(v)
    local c = texture(scene, v.uv).rgb * exposure
    local grey = dot(c, vec3(0.299, 0.587, 0.114))
    c = mix(vec3(grey), c, saturation)

    local d = length(v.uv - vec2(0.5)) * 1.4142
    c = c * (1.0 - vignette * smoothstep(0.35, 1.0, d))
    return vec4(c, 1.0)
end)

return S
