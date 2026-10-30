-- A small heads-up display. Add a Canvas actor (Place Actors > UI) and point its Document at this file: the editor shows
-- it over the viewport right away and again whenever you save. Scripts reach the nodes by name, for example:
--     ui.find("Score").text = "Score: " .. score
--     ui.find("Health").value = 0.5
-- Everything about the language is in docs/UI.md; open the UI panel (next to Details) to inspect and tweak the nodes live.

return ui.panel{ name = "Hud", anchor = "fill",

    -- Top left: a card with the score and a health bar.
    ui.column{ anchor = "topleft", pos = { 20, 20 }, gap = 8, padding = { 14, 10 },
        bg = "#0b0d13c4", radius = 10, border = { color = "#ffffff18", width = 1 },
        ui.text{ name = "Score", "Score: 0", font = "SansBold", font_size = 22, color = "#f6d365" },
        ui.progress{ name = "Health", size = { 180, 10 }, value = 1, fill = "#e0434f", radius = 5 } },

    -- Bottom: a hint that fades in.
    ui.text{ name = "Hint", "Edit Content/UI/hud.lua and save: the viewport updates", anchor = "bottom", pos = { 0, -20 },
        font_size = 15, color = "#ffffffb0", text_shadow = { x = 1, y = 1, color = "#000000aa" } },
}
