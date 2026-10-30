-- Built-in material: drawn when a project's material file is missing (see SceneRenderer). The same as the project
-- template's Content/Materials/Default.lua, but the shader comes from the engine folder.
return material {
    shader = "Engine/Shaders/default_lit.lua",
    params = {},
}
