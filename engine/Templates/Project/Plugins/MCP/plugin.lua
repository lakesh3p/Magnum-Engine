-- MCP: lets an AI client (Claude Code, Claude Desktop, any MCP client) build and test your game through this editor.
-- Open it from Tools > Plugins > MCP. Docs: docs/MCP.md.

local notice = nil      -- what the last button did, shown under the buttons
local notice_age = 0

magnum.on_update(function(dt)
    if notice then
        notice_age = notice_age + dt
        if notice_age > 4 then notice = nil end
    end
end)

local function say(text)
    notice, notice_age = text, 0
end

magnum.panel("MCP - AI control", function(ui)
    local mcp = magnum.mcp()

    ui.heading("AI control (MCP)")
    ui.wrapped("Let an AI client such as Claude build your game: it can add objects, write scripts and UI, play the game and look at the result.")
    ui.separator()

    -- 1. the channel
    if mcp.running then
        ui.dot(0.35, 0.8, 0.45)
        if mcp.client then
            ui.text("An AI client is connected")
        else
            ui.text("Open for AI clients (waiting)")
        end
        ui.dim("port " .. mcp.port .. ", this computer only")
        if ui.button("Close the channel", -1) then
            magnum.exec("mcp stop")
            say("The channel is closed.")
        end
    else
        ui.dot(0.5, 0.5, 0.5)
        ui.text("Closed")
        ui.wrapped("Open it so an AI client can work in this very window. (A client can also start its own editor.)")
        if ui.button("Open the channel for AI clients", -1) then
            magnum.exec("mcp start")
            say("The channel is open.")
        end
    end
    ui.separator()

    -- 2. connecting a client
    ui.text("Add the server to your AI client")
    if ui.button("Copy command for Claude Code", -1) then
        magnum.exec("mcp copy claude")
        say("Copied. Paste it into a terminal.")
    end
    if ui.button("Copy JSON config (Claude Desktop, others)", -1) then
        magnum.exec("mcp copy json")
        say("Copied. Paste it into the client's MCP settings.")
    end
    if notice then ui.colored(0.45, 0.8, 0.55, notice) end

    if not mcp.server_built then
        ui.separator()
        ui.colored(0.95, 0.5, 0.35, "MagnumMcp.exe is not built yet.")
        ui.dim("Run build.bat Release in the engine folder.")
    end
    ui.separator()
    ui.dim("Then ask the AI: \"make a small platformer\".")
end, { window = true, width = 360, open = true })
