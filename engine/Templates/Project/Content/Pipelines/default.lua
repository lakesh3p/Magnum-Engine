-- Magnum render pipeline: the passes of one frame, in order. Edit and save, the viewport updates at once.
--
--   clear    { color = {r, g, b} }
--   sky      { material = ... }   fullscreen shader behind everything
--   meshes                        every mesh actor, each with its own material
--   decals                        every Decal, projected onto the solid surfaces drawn so far
--   sprites                       every Sprite, sorted back to front
--   markers                       editor: boxes for lights/empties and the selection
--   grid                          editor: ground grid
--   overlay  { material = ... }   fullscreen shader blended over the image (clouds, fog, rain)
--   (sky and overlay take scale = 0.5: drawn at half the resolution and stretched, a quarter of the GPU cost)
--   ui                            the UI canvases (Canvas actors), after post so effects do not touch them
--   post     { material = ... }   fullscreen shader that reads the image so far (grading, blur, outline)
--
-- Any pass takes enabled = false to switch it off. Fullscreen materials point at shaders
-- written with S:fullscreen(function(v) ... end), see Content/Shaders/sky.lua.
return pipeline {
    name = "Default",
    passes = {
        { "clear", color = { 0.07, 0.07, 0.08 } },
        { "sky", material = "Content/Materials/Sky.lua" },
        { "overlay", material = "Content/Materials/Clouds.lua", scale = 0.5 },
        { "meshes" },
        { "decals" },
        { "sprites" },
        { "markers" },
        { "grid" },
        { "post", material = "Content/Materials/PostGrade.lua" },
        { "ui" },
    },
}
