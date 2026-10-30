# Magnum MCP server

`MagnumMcp.exe` lets an AI client (Claude Code, Claude Desktop, any client that speaks the [Model Context Protocol](https://modelcontextprotocol.io))
make a game in the engine: it starts or attaches to the Magnum editor, creates and changes actors, writes scripts, shaders, materials and UI
documents, plays the game, presses keys and the mouse, takes screenshots and reads the log - and gets the editor's errors back.

It is built with the engine (`build.bat Release` makes `build-Release\bin\MagnumMcp.exe` next to `MagnumEditor.exe`). There is nothing else to install.

## Connect a client

Claude Code:

```
claude mcp add magnum -- C:\path\to\magnum-engine\build-Release\bin\MagnumMcp.exe --project C:\path\to\MyGame
```

Claude Desktop (`claude_desktop_config.json`) or any client with a JSON config:

```json
{
  "mcpServers": {
    "magnum": {
      "command": "C:\\path\\to\\magnum-engine\\build-Release\\bin\\MagnumMcp.exe",
      "args": ["--project", "C:\\path\\to\\MyGame"]
    }
  }
}
```

`--project` is the project folder (the one with the `.magnum` file); a missing or empty folder becomes a new project made from the template.
Without it the server uses `MAGNUM_PROJECT` from the environment, else the `Projects\MagnumDemo` sample. The tool `editor_launch` switches to another project at any time.

The server starts the editor by itself on the first tool that needs it (a few seconds), or attaches to an editor that is already open on the project
and was started with the control channel (`MagnumEditor.exe --project <folder> --control-port 0`). When the client disconnects the editor keeps running,
so nothing is lost; the next session attaches to it again.

## The MCP plugin (in every project)

Every project has a plugin `Plugins/MCP` (new projects get it from the template). Open it from **Tools > Plugins > MCP**: a small window opens at the mouse with

* the state of the control channel (closed / open and waiting / an AI client is connected) and one button to open or close it - open it so an AI client can attach to *this* editor window instead of starting its own;
* **Copy command for Claude Code** and **Copy JSON config** - a ready `claude mcp add ...` command or a JSON config with the right paths goes to the clipboard;
* a warning when `MagnumMcp.exe` is not built yet.

The same things as console commands: `mcp start`, `mcp stop`, `mcp status`, `mcp config`, `mcp copy claude|json`, and `plugins open MCP` for the window. The channel is off until you open it (or until an AI client opens the editor itself).
A project made before this plugin existed gets it by copying the folder `Engine/Templates/Project/Plugins/MCP` into its `Plugins/` folder, then Tools > Plugins > Reload Plugins.

## How it works

```
AI client  --stdio JSON-RPC-->  MagnumMcp.exe  --TCP 127.0.0.1 + token-->  MagnumEditor.exe (control channel)
                                    |
                                    +-- project files (read/write/edit/move/delete, inside the project only)
```

* The control channel listens on the loopback interface only. Its port and a random token are written to `<project>\Saved\control.json`; every request must carry the token.
* Actions that need the editor (scene, play, UI, screenshots) go through the channel and run on the editor's main thread, the same way console commands do, so undo/redo works and what the AI does is visible in the editor window.
* File tools work on the disk directly. Paths are project-relative; `..`, absolute paths, `Saved/`, build folders and the `.magnum` file are refused for writing, and `Engine/...` (the engine's shaders, Lua DSL, fonts) is readable but never writable.
* After a write the server waits for the editor to reload the file and returns the errors it logged (Lua error with its line, shader compile errors).

## Tools

Tools that change something report editor warnings and errors in the answer and set `isError`.

| Group | Tools |
|---|---|
| Editor | `editor_status`, `editor_launch`, `editor_quit`, `console_run`, `console_help`, `log_read` |
| Scene | `scene_list`, `scene_get_actor`, `scene_dump`, `scene_spawn`, `scene_set`, `scene_add_component`, `scene_remove_component`, `scene_delete`, `scene_duplicate`, `scene_select`, `scene_focus`, `scene_new`, `scene_open`, `scene_save`, `scene_undo`, `scene_redo`, `reference_components` |
| View | `viewport_screenshot` (returns the PNG as an image), `viewport_camera` (3D camera, 2D view) |
| Playing | `play_start`, `play_stop`, `play_pause`, `play_wait`, `play_keys`, `play_mouse`, `play_lua` |
| UI | `ui_tree`, `ui_lua`, `ui_select` |
| Files | `project_info`, `project_files`, `project_read`, `project_write`, `project_edit`, `project_move`, `project_delete`, `project_create_asset` |
| Assets | `shader_reload`, `material_set`, `graph_command` |
| Build and tests | `game_build`, `game_status`, `run_tests` |
| Docs | `docs_read` |

Properties are set as `{"Component": {"Property": value}}`; vectors and colors are `[x, y, z]` (colors also `"#rrggbb"`), enums are their names, missing components are added.
`reference_components` lists everything that can be set.

Resources: `magnum://guide` (how to work), `magnum://reference/components`, `magnum://reference/console`, and every file of `docs/` (`magnum://docs/UI.md` ...).
Prompts: `make_a_game`, `make_a_ui`, `debug_the_game`.

## Typical session

1. `editor_status`, `docs_read UI.md` / `2D.md`, `reference_components`
2. `scene_spawn` / `scene_set` for actors, `project_write` for scripts and UI documents (Lua hot-reloads, the answer lists errors), `viewport_screenshot`
3. `play_start`, `play_keys`, `play_mouse`, `play_wait`, `play_lua`, `viewport_screenshot`, `log_read`, `play_stop`
4. `scene_save`

## The control channel (for other tools)

One JSON object per line over TCP, `{"id": 1, "token": "...", "op": "exec", "line": "spawn cube"}` and the answer `{"id": 1, "ok": true, ...}`.
Ops: `status`, `exec`, `lua`, `wait`, `log`, `reflect`, `scene`, `scene_file`, `actor`, `spawn`, `set`, `add_component`, `remove_component`, `delete`, `duplicate`,
`select`, `screenshot`, `quit`. The editor console commands `camera`, `lookat`, `view 2d`, `key`, `mouse`, `pause`, `resume`, `ui`, `lua` exist for this.

## Checking it

`python tests\mcp_smoke.py` starts the server on a throw-away project and exercises all groups against the real editor (needs a desktop session and a GPU).
