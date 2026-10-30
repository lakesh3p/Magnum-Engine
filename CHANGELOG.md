# Magnum Engine changelog

Releases are named after magnum cartridges: the **.22 Series** is every 0.x release, the **.357 Series** is 1.x, and the **.44 Series** is 2.x.

## Beginning

- **2026-10-25** - the project folder `kiln-engine` was created: the engine started under the name Kiln Engine. It was renamed to Magnum Engine on 2026-10-26 (see below); the folder keeps its first name until it is renamed by hand.

## Magnum Engine 0.2 (.22 Series)

### 2026-10-27 - Model import

- **3D model files** of about forty formats (OBJ, FBX, glTF and GLB, COLLADA, STL, PLY, 3DS, X, OFF and more) are read through **Assimp** (BSD-3-Clause, built from source): the one library that covers that many formats, FBX included.
- A **Mesh** component has a **Model** property: a model file drawn in place of the primitive, with the actor's material. The Content Browser shows model files; double-click or **Place in Scene** adds an actor for one, and a model can be dropped on the Model field.
- Colliders and selection boxes follow the model's bounding box. Changed files reload on the next frame; a file that cannot be read falls back to the primitive and logs the reason.
- Materials, textures, animations and lights of a model file are not imported (the actor's material is used).
- Units: the file's unit is converted to metres (`aiProcess_GlobalScale`), so FBX files made in centimetres are not huge.
- glTF files with external `.bin` buffers load; a file that failed is tried again about once a second.
- A missing material no longer hides meshes: the engine's built-in material (`Engine/Materials/Default.lua`) is drawn instead. `Engine/` paths now work for materials too, as they already did for shaders.
- Docs: `docs/Models.md`. Tests: `tests/model_tests.inc`.

### 2026-10-26 - Rename to Magnum Engine

- The engine is renamed from Kiln Engine to **Magnum Engine**. Everything that carried the old name is renamed: C++ types and macros (`Kiln*` → `Magnum*`, `KILN_*` → `MAGNUM_*`), the Lua API (`kiln.*` → `magnum.*`), the game module (`KilnGame` → `MagnumGame`), the API header (`kiln_game_api.h` → `magnum_game_api.h`), the launcher (`Kiln.bat` → `Magnum.bat`), the executables (`KilnEditor.exe` → `MagnumEditor.exe`, `KilnMcp.exe` → `MagnumMcp.exe`), the sample project (`KilnDemo` → `MagnumDemo`), logs and the `CMake` project name.
- **Project files use the `.magnum` extension** instead of `.kiln`. Existing `.kiln` projects must be renamed to `.magnum`, and scripts that call `kiln.*` must be updated to `magnum.*`.
- Docs, comments and the changelog use the new name, including the entries below that describe earlier work.

### 2026-10-26 - MCP server for AI clients

- **MagnumMcp.exe**: an MCP (Model Context Protocol) server over stdio. An AI client can start or attach to the editor and make a game: 50 tools for the scene (spawn, set properties, components, undo, save/open), files (read, write, edit with exact match, move, delete, templates), shaders and materials, the UI, playing (keys, mouse, Lua, pause, waiting), screenshots returned as images, the log, game build and the engine's tests. Writes of Lua files come back with the errors the editor logged.
- Resources (guide, component reference, console reference, all `docs/*.md`) and prompts (`make_a_game`, `make_a_ui`, `debug_the_game`).
- **Control channel** in the editor (`--control-port`): loopback only, token in `Saved/control.json`, structured ops for status, scene, actors, properties, screenshots, Lua and waiting.
- New console commands: `camera`, `lookat`, `view 2d x y half`, `key <name> down|up|tap`, `pause`, `resume`; `mouse` now also drives a playing game.
- `Log::Total()` / `Log::Since()`; `Gameplay::KeyCodeFromName`.
- **MCP plugin** in every project (`Plugins/MCP`, also in the project template): Tools > Plugins > MCP opens a small window at the mouse with the channel state, one button to open/close the channel for AI clients and buttons that copy the client setup; console `mcp start|stop|status|config|copy`, `plugins open <name>`.
- Plugins: `magnum.panel(title, fn, {window = true, width = 360})` makes a small floating window that opens from Tools > Plugins (the menu now has one entry per plugin); new `ui.heading`, `ui.wrapped`, `ui.colored`, `ui.dot`, `ui.space`, `ui.button(label, width)`; `magnum.mcp()`, `magnum.mcp_status()`.
- Docs: `docs/MCP.md`. Check: `python tests/mcp_smoke.py`.

## Magnum Engine 0.1 (.22 Series)

Dates are the days the work was done (taken from the project's file history). Within a day, changes are listed roughly in the order they landed.

### 2026-10-26 - UI, sprites, 2D and decals

**UI system**
- Retained UI: a tree of nodes per canvas (panel, row/column/grid, scroll, text, image, button, toggle, slider, progress, spacer).
- Anchors, pivots and margins; row/column/grid layouts with gap, padding, justify, items, grow; auto sizes; scale modes (Height, Width, Fit, Fill, Constant) so a UI looks the same at every resolution.
- Looks: rounded rectangles, borders, soft shadows, images, 9-slice, atlas `uv`, opacity, scale and rotation (hit testing follows), clipping, per-state styles (hover, pressed, disabled, active) with eased transitions, named styles (`ui.style`).
- Text through stb_truetype (no new dependency): Latin, Cyrillic and common symbols, wrapping, alignment, shadow; fonts Sans, SansBold, Mono or a project `.ttf`.
- Input: hover, press, click, drag (sliders, scroll bar), mouse wheel, pointer blocking so clicks on the UI do not reach the game.
- Tweens with 20 easings, loop, yoyo, delay, `remove`/`hide` at the end, `on_done`.
- Lua API (`ui.panel{...}` and friends, node properties, events, `node:animate`), declarative documents in `Content/UI/*.lua` with hot reload (a broken file keeps the last good UI), `ui.toast`, `ui.load`.
- **Canvas** component: a document drawn over the game view and, live, over the editor viewport while nothing plays.
- `game.pause()` / `resume()` / `toggle_pause()` / `paused()` and `game.vars` shared by all scripts and documents.
- Editor: **UI** panel (canvas/node tree, live property editing, selection outlined in the viewport), "UI" switch in the viewport's Show menu, Place Actors group "UI", Content Browser knows UI documents (`New UI Document`), console commands `ui tree`, `ui <lua>`, `lua <code>`, `ui select <name>`, `mouse <x> <y> [down|up]` for scripted runs.
- New render pass `ui` (drawn last when a pipeline does not list it); one batched draw with scissor clipping.
- Docs: `docs/UI.md`. Examples: `Content/UI/hud.lua` and `menu.lua` in the project template, HUD in MagnumDemo, HUD/menu/pause menu in Platformer2D.

**Sprites and 2D**
- Sprite component: atlas cells, frame animation, flip, billboard, pixel-art sampling, draw order; scripts: `sprite:play`, `world.spawn_sprite`.
- Orthographic cameras, a flat 2D editor view (pan, zoom, picking, grid), 2D physics (`Lock Z`), `input.mouse_world`.
- Place Actors group "2D" (Sprite, Physics Sprite, Tile, Player 2D, Camera 2D, Quad).
- New demo project **Platformer2D**: generated pixel art, level, coins, patrolling slimes, goal, camera follow, menus.
- Scripts in the template: `platformer.lua`, `slime.lua`, `cloud.lua`.
- Docs: `docs/2D.md`.

**Decals**
- Decal component projected onto solid surfaces using the scene depth; angle fade, custom decal materials, `decals` pipeline pass.
- Shader DSL: `scene_depth`, `inv_model`, `frag_coord`; `Engine/Shaders/decal.lua`; `world.spawn_decal` for bullet holes and similar.

**Fixes and changes**
- Outliner shows Sprite, Decal and Canvas actors with their own icons and labels; sprites and decals no longer get marker boxes.
- Debug builds compile (`/bigobj`).
- Scripts: character and platformer controllers use the UI when the scene has one and fall back to `hud.text` otherwise.
- 76 automated tests (Release and Debug).

### 2026-10-26 (early) - editor polish, performance, projects

- Editor redesigned after the two HTML mockups: dark muted palette, IBM Plex Sans and JetBrains Mono fonts, vector icons, compact Place Actors and Outliner, many more actor kinds and primitives (cylinder, cone, capsule, torus, arch, pyramid, wedge, prism, diamond, stairs, disc, tube, ring).
- Content Browser: folders without backgrounds, larger icons, new icons for shaders, graph shaders and scripts; double click opens shaders in the code editor, "Open in node editor" in the context menu.
- Material preview: transparent background, centered sphere that can be rotated.
- Performance: GPU timer queries per pass (Show > Statistics), half-resolution sky and overlay passes, vsync off, Release build.
- `Magnum.bat` launcher (opens the last project, builds Release when nothing is built).
- Projects live in their own folders (`<Name>.magnum`); New/Open project dialogs; `scripts/build_game.bat` builds a project's game module.
- Shaders on everything: Lua shader DSL for surfaces, vertex animation, sky, clouds and post effects (`S:fullscreen`), `loop()` as a real GLSL loop, noise helpers, constants outside stage functions; hot reload of shaders, materials and pipelines (fixed: edits not showing up, a freeze from unrolled loops).
- Render pipeline in Lua (`Content/Pipelines/default.lua`): clear, sky, meshes, markers, grid, overlay, post.
- Node editor: zoom out, thin white links, darker colors.
- Gameplay: Lua scripts per actor (`on_start`, `on_update`, `on_hit`, `on_trigger`), physics (boxes, spheres, triggers, raycasts), cameras, play-in-editor with a restored scene afterwards, HUD text and rectangles; shipped scripts `character.lua`, `collectible.lua`, `mover.lua`.

### 2026-10-25 - foundation

- C++20 engine and a Dear ImGui docking editor for Windows (MSVC, Ninja, OpenGL 4.5, GLFW).
- Scene with entities, reflection-driven components (Details panel, serialization and undo/redo come for free), `.scene` files, Outliner, Place Actors, viewport with fly camera, picking and ImGuizmo gizmos.
- Primitive meshes, material assets (`Content/Materials/*.lua`) and Lua surface shaders, textures with hot reload, Content Browser.
- C++ game module (DLL) with hot reload and a sample game; plugins in Lua (`Plugins/<name>/plugin.lua`).
- Material graph editor (ImNodes) that round-trips to readable Lua shader code.
- Console with commands and scripted runs (`--exec`, screenshots), automated tests.
