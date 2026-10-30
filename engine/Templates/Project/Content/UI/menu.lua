-- A main menu: a dimmed backdrop with a card in the middle. Put it on a Canvas actor with a higher Order than the HUD.
-- game.pause() holds the game still until Play is pressed (it does nothing in the editor preview).
--
-- Named styles (ui.style) give buttons a shared look: `color` is the background, hover / pressed / disabled change it per state.

ui.style("primary", {
    color = "#2f6fe4", radius = 8, border = { color = "#ffffff30", width = 1 },
    hover = { color = "#4784f5" }, pressed = { color = "#2358bd" },
})
ui.style("secondary", {
    color = "#232838", radius = 8, border = { color = "#ffffff1a", width = 1 },
    hover = { color = "#2f3650" }, pressed = { color = "#1a1e2c" },
})

local function start()
    ui.find("MainMenu"):animate{ opacity = 0, duration = 0.3, hide = true } -- fade out, then hide
    game.resume()
end

local menu = ui.panel{ name = "MainMenu", anchor = "fill", bg = "#070911ee", block = true, -- block: clicks do not reach the game

    ui.column{ anchor = "center", size = { 360, -1 }, gap = 12, padding = { 32, 28 }, items = "stretch",
        image = "Content/Textures/panel.png", slice = 20, -- a 9-slice picture: the corners keep their size
        ui.text{ "My Game", font = "SansBold", font_size = 44, text_align = "center", color = "#f4d35e" },
        ui.text{ "made with Magnum", font_size = 16, text_align = "center", color = "#9aa3bd" },
        ui.spacer{ size = { 1, 10 } },
        ui.button{ "Play", style = "primary", size = { -1, 48 }, font_size = 20, on_click = start },
        ui.toggle{ "Fullscreen hints", value = true, look = "switch",
            on_change = function(_, on) print("hints:", on) end },
        ui.button{ "Quit", style = "secondary", size = { -1, 44 }, font_size = 18, on_click = function() game.stop() end } },
}

game.pause()
return menu
