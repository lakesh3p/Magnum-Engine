# Models

The engine reads 3D model files in every format Assimp can read: OBJ, FBX, glTF and GLB, COLLADA (DAE), STL, PLY,
3DS, X, OFF and about thirty more. The Content Browser shows these files as **Model**. The list of extensions is the
one the importer reports at run time (`ModelFormats()` in `engine/src/model/ModelImport.h`).

## Using a model

- **Place it:** double-click the file in the Content Browser, or right-click it and choose **Place in Scene**. The new
  actor is named after the file and appears in front of the camera, like a spawned primitive.
- **Put it on an actor:** drag the file from the Content Browser onto the **Model** field of a Mesh component in the
  Details panel. Clear the field to go back to the primitive.
- **Through the AI channel:** `scene_spawn` with kind `cube` and `"properties": {"Mesh": {"Model": "Content/Models/tower.glb"}}`,
  or `scene_set` with component `Mesh` and property `Model`.

The model path is saved with the scene file, like every other property.

## What is imported

- The triangles of every part of the file, with the nodes' transforms applied. Polygons are triangulated.
- Normals from the file; smooth normals are generated where the file has none. The first UV set is used.
- Units: the file's unit is converted to metres, one unit in the scene (FBX files made in centimetres come in at
  their real size). The origin is the file's own; the model is not recentred.
- Materials, textures, animations, cameras and lights of the file are ignored. Every part is drawn with the material
  of the actor that shows it (Mesh > Material).
- A glTF keeps its data in external `.bin` files next to it: they are read from the same folder.

## Materials

If the actor's material file is missing, or the material is broken, the engine draws the model with its built-in
material (`engine/Materials/Default.lua`, shader `engine/Shaders/default_lit.lua`), so a model is never invisible
just because a project lacks `Content/Materials/Default.lua`. The Details panel still reports the missing file.

## Collision and selection

The collider and the selection box use the model's bounding box. They are centred on the actor's position, so a model
whose origin is not at its centre has its collider offset from the visible shape.

## When a file cannot be read

The actor is drawn as its primitive and the editor logs `Cannot read model <path>: <reason>` once for each reason. A
failed file is tried again about once a second, so a glTF whose `.bin` arrives after it starts working by itself.
Changed files are read again on the next frame.

## Code

- `engine/src/model/ModelImport.{h,cpp}`: `ImportModel` (one file), `LoadModel` (project path, cached and reloaded when
  the file changes), `ModelFormats`, `IsModelFile`.
- `engine/src/render/SceneRenderer.cpp`: `ModelFor` uploads each part once as a `Mesh`; `DrawMeshes` draws them.
- `engine/src/scene/Scene.cpp`: `LocalHalfExtents` uses the model's size for colliders and selection.
- Tests: `tests/model_tests.inc` (formats, import of OBJ, STL, PLY, OFF, glTF and GLB, broken files, reload, save and load).

## Limits

- FBX is read by the importer, but the repository has no FBX sample, so it has not been tested with a real FBX file.
- No sample models ship with the demo project.
