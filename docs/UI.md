# UI

Magnum's UI is a tree of nodes that you describe in Lua. It is drawn over the game view, takes the mouse, animates, scales
with the window, and is shown live in the editor while you write it.

```lua
-- Content/UI/hud.lua
return ui.column{ anchor = "topleft", pos = { 20, 20 }, gap = 8, padding = 12, bg = "#0b0d13c4", radius = 8,
    ui.text{ name = "Score", "Score: 0", font_size = 24 },
    ui.progress{ name = "Health", size = { 180, 10 }, value = 1 },
    ui.button{ "Quit", on_click = function() game.stop() end },
}
```

Put the file on a **Canvas** actor (Place Actors → UI, then set *Document*): the editor draws it over the viewport at once
and again every time you save. While playing, scripts find the nodes by name:

```lua
ui.find("Score").text = "Score: " .. score
ui.find("Health").value = hp / 100
```

Open the **UI** panel (a tab next to Details) to see the tree of the running game or of the preview, to select a node
(it is outlined in the viewport) and to change its properties live. Those edits are for trying things; copy what you like
into the document.

Console: `ui tree` prints the canvases and nodes with their rectangles, `ui <lua>` runs Lua in the preview, `lua <code>`
runs it in the running game, `ui select <name>` picks a node in the UI panel.

---

## Canvases and documents

A **canvas** is one tree of nodes drawn over the view. Several can be on screen; a higher *Order* is drawn on top and gets
the mouse first. A *Canvas* component has:

| Field | Meaning |
|---|---|
| Document | project path of the Lua file that builds the UI (`Content/UI/hud.lua`) |
| Order | draw order, higher = on top |
| Visible | switch the whole canvas off |
| Scale mode | how the design size is fitted to the view (below) |
| Reference width / height | the design size, 1280 x 720 by default |

A document is a Lua file that returns its root node (or a list of nodes). It runs once when the canvas is created and again
whenever the file changes (in the editor preview and while playing). If the new version has an error, the last good UI stays
and the error is in the console. A document or callback that runs for too long without returning is stopped.

Scripts can also build UI without a document: nodes made in `on_start` are shown with `ui.show(node)`.

**Scale mode.** Everything is laid out in *UI units* in a design space, then scaled to the real view:

* `Height` (default): 720 units tall at any resolution, as wide as the window is; use anchors for the sides.
* `Width`: the same, fitted to the width.
* `Fit` / `Fill`: the design size fits inside / covers the view.
* `Constant`: one UI unit is one pixel.

Text is drawn at the real pixel size, so it stays sharp at every scale.

## Nodes

| Constructor | What it is |
|---|---|
| `ui.panel{}` | a box: background, border, image, a place for children |
| `ui.row{}` `ui.column{}` `ui.grid{columns = 3}` | panels that place their children in a line or a grid |
| `ui.scroll{}` | a column that scrolls (wheel, scroll bar) and clips |
| `ui.text{"hello"}` | text; wraps with `wrap = true` |
| `ui.image{image = "Content/Textures/x.png"}` | a picture; part of an atlas with `uv`; 9-slice with `slice` |
| `ui.button{"Play", on_click = f}` | a clickable box with a label |
| `ui.toggle{"Sound", value = true}` | a check box, or a switch with `look = "switch"` |
| `ui.slider{min = 0, max = 10, step = 1, value = 3}` | a draggable value (`vertical = true` for an upright one) |
| `ui.progress{value = 0.5}` | a bar |
| `ui.spacer{}` | an empty node that grows to push its neighbours apart |

Every constructor takes one table. Entries **without a key are children** (a string is the text of a text, button or toggle):

```lua
ui.column{ gap = 8,
    ui.text{ "Title", font_size = 28 },
    ui.button{ "Play", on_click = start },
}
```

A node made but never attached is not shown. Attach it with `parent:add(node)`, with a `children` entry, or with
`ui.show(node)` (puts it on the active canvas).

## Placement

Nodes are placed against their parent's rectangle with **anchors**, like in most game UIs.

* `anchor = "center"` and the other presets: `topleft` `top` `topright` `left` `right` `bottomleft` `bottom`
  `bottomright`, and the stretching ones `fill` `top_wide` `bottom_wide` `center_wide` `left_tall` `right_tall` `center_tall`.
  A preset sets the anchors and the pivot together. You can also give `anchor = {min_x, min_y, max_x, max_y}` (fractions 0..1).
* `pos = {x, y}` moves the node's pivot away from its anchor point. `anchor = "bottomright", pos = {-16, -16}` is a 16-unit margin.
* `size = {w, h}`. A negative number or `"auto"` sizes that side to the content (text, picture, children).
  `min_size`, `max_size` clamp.
* On a **stretched** axis (different anchors) `margin = {left, top, right, bottom}` (or one number) leaves a gap at the edges.
* `pivot = {x, y}` (or a preset name) is the point scaling and rotation turn around, and the point `pos` moves.
* `padding` insets the children (also those placed by anchors); `{h = 8, v = 4}`, `{l, t, r, b}` or one number.

## Layout

`layout = "row" | "column" | "grid"` (or use `ui.row` / `ui.column` / `ui.grid`) makes a container place its children in a line:

* `gap` / `spacing`: space between children (a number or `{x, y}`).
* `justify` along the line: `start` `center` `end` `between` `around`.
* `items` across the line: `start` `center` `end` `stretch` (`stretch` fills the width of a column to children with auto width).
  A child can differ with `align_self`.
* `grow = 1` on a child gives it a share of the free space (`ui.spacer{}` has it already).
* `columns = 3` (grid), `cell_size = {w, h}` for fixed cells; without it columns share the width.
* `ignore_layout = true` places a child by its anchors even inside a row.

A container with auto size is as big as its content plus padding, so `ui.row{}` inside a `ui.column{}` simply works.
Hidden children (`visible = false`) take no room.

## Looks

* `color`: the background of boxes and buttons, the text color of text, the tint of images. Colors are `"#rrggbb"`,
  `"#rrggbbaa"`, `"#rgb"` or `{r, g, b, a}` (0..1, or 0..255 when a value is above 1).
* `bg`, `border = {color = "#...", width = 1}` (or just a width / just a color), `border_width`, `radius`, `opacity`,
  `shadow = {x = 0, y = 4, blur = 12, spread = 0, color = "#00000066"}`.
* `image = "Content/Textures/x.png"` on any box; `uv = {x, y, w, h}` (0..1 from the top left) picks a part, so one picture
  can hold many icons; `slice = {l, t, r, b}` (or one number) in picture pixels makes a 9-slice; `fit = "stretch" | "contain" | "cover"`;
  `pixelated = true` for crisp pixel art.
* Text: `text`, `font` (`"Sans"`, `"SansBold"`, `"Mono"` or a project path to a `.ttf`), `font_size`, `text_color`,
  `text_align`, `wrap`, `line_height`, `text_shadow = {x, y, color}`. Latin, Cyrillic and common symbols are included.
* `scale`, `rotation` (degrees) turn and scale the node and everything in it, hit testing follows.
* `clip = true` cuts children at the node's rectangle (scroll views always do).
* `block = true` makes a node stop the pointer even though it is not a button (a menu backdrop), `block = false` lets a
  button through.

**States.** `hover`, `pressed`, `disabled` and `active` (a toggle that is on) are tables of look fields that apply on top of
the normal look: `hover = {color = "#4784f5"}`. `transition = 0.1` is how long a change of state takes. A button without a
hover color gets a slightly lighter one by itself; a disabled node fades.

**Named styles** share a look:

```lua
ui.style("primary", { color = "#2f6fe4", radius = 8, hover = { color = "#4784f5" }, pressed = { color = "#2358bd" } })
ui.button{ "Play", style = "primary" }       -- other properties written in the table win over the style
```

## Events

Callbacks are plain properties: `on_click`, `on_press`, `on_release`, `on_enter`, `on_leave`, `on_change`, `on_update`.

```lua
ui.button{ "Play", on_click = function(button) ... end }
ui.slider{ on_change = function(slider, value) volume = value end }
ui.toggle{ "Sound", on_change = function(toggle, on) sound_on = on end }   -- on is a boolean
ui.panel{ on_update = function(panel, dt) ... end }   -- every frame while the node is visible
```

A click is a press and a release on the same node. A callback that raises an error is reported once and switched off
until you assign it again. `node:click()` does what a click does (shortcuts, tests).

**Who gets the mouse.** While the pointer is over a node that blocks it (buttons, toggles, sliders, scroll views, and panels
with `block = true`), `input.mouse(button)` is false for game scripts and the editor does not capture the mouse for
first-person play. `input.ui_blocked()` and `ui.pointer_over()` tell the same.

## Nodes from scripts

Reading and writing any property above works on the node:

```lua
local node = ui.find("Score")           -- by name, anywhere on screen
node.text = "Score: 10"
node.visible = false
print(node.rect.x, node.rect.w)         -- where it ended up, in UI units
node.parent, node.children, node.hovered, node.pressed, node.valid
```

`node:add(a, b, ...)`, `node:remove(child)`, `node:clear()`, `node:destroy()`, `node:find(name)`, `node:show()`, `node:hide()`,
`node:to_front()`, `node:to_back()`, `node:set_style(name)`, `node:on("click", f)`, `node:scroll_to(y)`.
A destroyed node reads as `valid == false`; writing to it does nothing.

Globals: `ui.show(node)`, `ui.hide(node)`, `ui.root()`, `ui.canvas(name, {order = 5, scale = "height", reference = {1280, 720}})`
(creates a canvas and makes it the one new nodes go to), `ui.load("Content/UI/menu.lua"[, canvas_name])`, `ui.find(name)`,
`ui.find_all(prefix)`, `ui.style(name, {...})`, `ui.screen()` (size in UI units), `ui.pointer_over()`, `ui.clear()`.

## Animation

```lua
node:animate{ opacity = 0, pos = {200, 40}, scale = 1.2, rotation = 15, size = {300, 80}, bg = "#ff0000",
              text_color = "#ffffff", value = 0.5, scroll = 120,
              duration = 0.3, delay = 0, ease = "out_cubic",
              loop = false, yoyo = false, remove = false, hide = false,
              from = { opacity = 1 }, on_done = function(node) end }
node:stop()
```

Each call moves the listed properties from where they are (or from `from`) to the given values. A new tween of the same
property replaces the running one. `remove = true` deletes the node at the end, `hide = true` hides it, `loop` + `yoyo` pulse
forever. Eases: `linear`, `in_quad`, `out_quad`, `in_out_quad`, the same for `cubic`, `quart`, `sine`, `expo`, `back`, and
`out_elastic`, `out_bounce` (`ui.ease_names()` lists them).

Ready-made: `ui.toast("Saved", {seconds = 2})`, `ui.fade_in(node)`, `ui.fade_out(node)`.

## Pausing and sharing state

`game.pause()` / `game.resume()` / `game.toggle_pause()` stop scripts, physics and sprite animation but not the UI, so a pause
menu is a UI document. `game.vars` is a table every script and document can read and write, for example a button sets
`game.vars.restart = true` and the player script reacts.

## Limits worth knowing

* There is no text input field and no keyboard focus yet: the UI is for the mouse. Keys can be read in `on_update` with `input.pressed(...)`.
* Text is laid out left to right with the glyphs the bundled fonts have; there is no shaping for complex scripts.
* Opacity multiplies down the tree per shape, so overlapping children of a half-transparent node show through each other.
* Clipping uses the bounding box of the clipping node, also when it is rotated.
