// ---- UI from Lua ------------------------------------------------------------------------------------

namespace {

// A preview with a temporary project folder: Lua snippets run in it, documents live in Content/UI.
struct LuaUi {
    ScriptProject project;
    magnum::UiPreview preview;
    magnum::Scene scene;
    LuaUi() {
        magnum::SetEngineRoot(MAGNUM_ENGINE_DIR);
        magnum::SetProjectRoot(MAGNUM_PROJECT_DIR);
        std::filesystem::create_directories(project.dir / "Content" / "UI");
        preview.Open(project.dir);
    }
    bool Run(const std::string& source) {
        std::string error;
        const bool ok = preview.Run(source, error);
        if (!ok) std::printf("  Lua error: %s\n", error.c_str());
        return ok;
    }
    std::string Eval(const std::string& expression) { return preview.Eval(expression); }
    float Num(const std::string& expression) { return std::stof(Eval(expression)); }
    void Frames(int n, const magnum::UiPointer& pointer = magnum::UiPointer{}) {
        for (int i = 0; i < n; ++i) preview.Update(scene, 1.0f / 60.0f, 1280, 720, pointer);
    }
    magnum::UiSystem& ui() { return preview.System(); }
    // The error message of a snippet that is expected to fail.
    std::string ErrorOf(const std::string& body) {
        Run("failure = select(2, pcall(function() " + body + " end))");
        return Eval("failure");
    }
    void WriteDocument(const std::string& name, const std::string& text) { WriteText(project.dir / "Content" / "UI" / name, text); }
};

magnum::UiPointer Click(magnum::UiRect rect, bool down) {
    return At(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f, down);
}

} // namespace

void UiLuaBuildsTreesAndRunsCallbacks() {
    LuaUi f;
    CHECK(f.Run(R"LUA(
        clicks, sound, vol = 0, nil, nil
        menu = ui.column{ name = "Menu", anchor = "center", gap = 8, padding = 10, items = "stretch",
            ui.text{ "Title", name = "Title", font_size = 28 },
            ui.button{ "Play", name = "Play", size = { 200, 40 }, on_click = function(b) clicks = clicks + 1 end },
            ui.row{ name = "Options", gap = 12,
                ui.toggle{ "Sound", name = "Sound", value = true, on_change = function(t, v) sound = v end },
                ui.slider{ name = "Vol", min = 0, max = 10, step = 1, value = 3, size = { 200, 20 },
                    on_change = function(s, v) vol = v end } } }
        ui.show(menu)
    )LUA"));
    f.Frames(2);
    CHECK(f.Eval("menu.kind") == "panel" && f.Eval("#menu.children") == "3" && f.Eval("ui.find('Play').text") == "Play");
    CHECK(f.Eval("ui.find('Nope')") == "nil" && f.Eval("ui.find('Sound').value") == "true" && f.Num("ui.find('Vol').value") == 3.0f);
    const magnum::UiNode* menu = f.ui().Get(f.ui().Find("Menu"));
    CHECK(Near(menu->rect.Center().x, 640.0f, 0.6f) && Near(menu->rect.Center().y, 360.0f, 0.6f)); // anchor = "center"
    const magnum::UiNode* play = f.ui().Get(f.ui().Find("Play"));
    CHECK(Near(play->rect.w, 200.0f) && Near(play->rect.h, 40.0f));
    CHECK(Near(f.ui().Get(f.ui().Find("Title"))->rect.w, f.ui().Get(f.ui().Find("Options"))->rect.w)); // items = "stretch"

    // Pointer: a click on the button runs its callback once, on release.
    const magnum::UiRect playRect = play->rect;
    f.Frames(1, Click(playRect, false));
    f.Frames(1, Click(playRect, true));
    CHECK(f.Eval("clicks") == "0");
    f.Frames(1, Click(playRect, false));
    CHECK(f.Eval("clicks") == "1");
    // The toggle reports a boolean, the slider a number.
    const magnum::UiRect toggleRect = f.ui().Get(f.ui().Find("Sound"))->rect;
    const float tx = toggleRect.x + 9, ty = toggleRect.y + toggleRect.h * 0.5f;
    f.Frames(1, At(tx, ty));
    f.Frames(1, At(tx, ty, true));
    f.Frames(1, At(tx, ty, false));
    CHECK(f.Eval("sound") == "false" && f.Eval("ui.find('Sound').value") == "false");
    const magnum::UiRect sliderRect = f.ui().Get(f.ui().Find("Vol"))->rect;
    f.Frames(1, Click(sliderRect, false));
    f.Frames(1, Click(sliderRect, true));
    f.Frames(1, Click(sliderRect, false));
    CHECK(f.Eval("math.floor(vol)") == "5" && f.Eval("math.floor(ui.find('Vol').value)") == "5");
    // Programmatic changes report too; node:click() acts like a click.
    CHECK(f.Run("ui.find('Vol').value = 9; ui.find('Play'):click()"));
    f.Frames(1);
    CHECK(f.Eval("math.floor(vol)") == "9" && f.Eval("clicks") == "2");
    // A callback that fails is reported once and switched off.
    CHECK(f.Run("ui.find('Play').on_click = function() error('boom') end; ui.find('Play'):click(); ui.find('Play'):click()"));
    f.Frames(2);
    CHECK(f.Eval("ui.find('Play').on_click") == "nil");
}

void UiLuaPropertiesAndErrors() {
    LuaUi f;
    CHECK(f.Run(R"LUA(
        t = ui.text{ "x", color = "#ff8000", opacity = 0.5, pos = { 4, 5 }, size = { 100, "auto" } }
        t.text = "hello"
        t.font_size = 30
        b = ui.button{ "B", hover = { color = "#ffffff" }, radius = 9, border = { color = "#00ff00", width = 3 } }
        p = ui.panel{ bg = { 1, 0, 0, 0.5 }, padding = { h = 4, v = 2 }, margin = 6, shadow = { y = 6, blur = 12 } }
    )LUA"));
    CHECK(f.Eval("t.text") == "hello" && f.Num("t.font_size") == 30.0f && f.Num("t.opacity") == 0.5f);
    CHECK(f.Eval("math.floor(t.color.r * 255 + 0.5)") == "255" && f.Eval("math.floor(t.color.g * 255 + 0.5)") == "128" && f.Eval("math.floor(t.color.b * 255)") == "0");
    CHECK(f.Num("t.pos.x") == 4.0f && f.Num("t.pos.y") == 5.0f && f.Num("t.size.w") == 100.0f);
    magnum::UiNode* b = f.ui().Get(static_cast<magnum::UiId>(f.Num("b.id")));
    CHECK(b && Near(b->style.base.radius.value_or(0), 9.0f) && Near(b->style.base.borderWidth.value_or(0), 3.0f) && Near(b->style.base.border->g, 1.0f));
    CHECK(b->style.hover.bg && Near(b->style.hover.bg->r, 1.0f));
    magnum::UiNode* p = f.ui().Get(static_cast<magnum::UiId>(f.Num("p.id")));
    CHECK(Near(p->style.base.bg->a, 0.5f) && Near(p->padding.x, 4.0f) && Near(p->padding.y, 2.0f) && Near(p->padding.z, 4.0f) && Near(p->marginMin.x, 6.0f) &&
          p->style.shadow.enabled && Near(p->style.shadow.offset.y, 6.0f));

    // Mistakes say what is wrong.
    CHECK(f.ErrorOf("local n = ui.panel{}; n.nonsense = 1").find("unknown property 'nonsense'") != std::string::npos);
    CHECK(f.ErrorOf("ui.panel{ anchor = 'nowhere' }").find("unknown anchor 'nowhere'") != std::string::npos);
    CHECK(f.ErrorOf("ui.panel{ color = 'red' }").find("is not a color") != std::string::npos);
    CHECK(f.ErrorOf("ui.panel{ opacity = 'half' }").find("opacity needs a number") != std::string::npos);
    CHECK(f.ErrorOf("ui.panel{ layout = 'diagonal' }").find("layout is") != std::string::npos);
    CHECK(f.ErrorOf("ui.panel{ on_clik = function() end }").find("unknown event 'on_clik'") != std::string::npos);
    CHECK(f.ErrorOf("ui.panel{ style = 'ghost' }").find("no style named 'ghost'") != std::string::npos);
    CHECK(f.ErrorOf("local n = ui.panel{}; n:animate{}").find("needs at least one property") != std::string::npos);
    CHECK(f.ErrorOf("local n = ui.panel{}; n:animate{ opacity = 0, ease = 'wobbly' }").find("unknown ease") != std::string::npos);
    CHECK(f.ErrorOf("ui.panel{ 42 }").find("child node or a text") != std::string::npos);
    CHECK(f.ErrorOf("local a = ui.panel{}; local b = ui.panel{ a }; a:add(b)").find("cannot be added to itself") != std::string::npos);

    // A destroyed node reads as invalid, writes to it do nothing.
    CHECK(f.Run("d = ui.panel{ name = 'D' }; ui.show(d); d:destroy()"));
    CHECK(f.Eval("d.valid") == "false" && f.Eval("d.text") == "nil" && f.Run("d.text = 'still fine'") && f.ui().Find("D") == 0);
    // Equality compares nodes, not wrappers.
    CHECK(f.Run("e = ui.panel{ name = 'E' }; ui.show(e)"));
    CHECK(f.Eval("ui.find('E') == e") == "true" && f.Eval("ui.find('E') == t") == "false");
}

void UiLuaAnimatesNodes() {
    LuaUi f;
    CHECK(f.Run(R"LUA(
        n = ui.panel{ name = "N", size = { 10, 10 } }
        ui.show(n)
        done = false
        n:animate{ opacity = 0, from = { opacity = 1 }, duration = 0.5, ease = "linear", on_done = function(node) done = (node == n) end }
    )LUA"));
    f.Frames(15);
    CHECK(Near(f.Num("n.opacity"), 0.5f, 0.05f) && f.Eval("done") == "false");
    f.Frames(25);
    CHECK(Near(f.Num("n.opacity"), 0.0f) && f.Eval("done") == "true");

    // Several properties at once; a looping tween runs until stopped; remove deletes the node.
    CHECK(f.Run("n:animate{ pos = { 100, 50 }, scale = 2, rotation = 90, bg = '#ff0000', duration = 0.1, ease = 'out_back' }"));
    f.Frames(10);
    CHECK(Near(f.Num("n.pos.x"), 100.0f) && Near(f.Num("n.scale"), 2.0f) && Near(f.Num("n.rotation"), 90.0f) && f.Eval("math.floor(n.bg.r)") == "1");
    CHECK(f.Run("n:animate{ opacity = 0.2, duration = 0.2, loop = true, yoyo = true }"));
    f.Frames(60);
    CHECK(f.ui().HasTweens(static_cast<magnum::UiId>(f.Num("n.id"))));
    CHECK(f.Run("n:stop()"));
    CHECK(!f.ui().HasTweens(static_cast<magnum::UiId>(f.Num("n.id"))));
    CHECK(f.Run("m = ui.text{ 'bye' }; ui.show(m); m:animate{ duration = 0.1, remove = true }"));
    f.Frames(10);
    CHECK(f.Eval("m.valid") == "false");
    CHECK(f.Run("h = ui.panel{}; ui.show(h); h:animate{ duration = 0.1, hide = true }"));
    f.Frames(10);
    CHECK(f.Eval("h.valid") == "true" && f.Eval("h.visible") == "false");

    // The conveniences: a toast appears, fades and removes itself.
    CHECK(f.Run("toast = ui.toast('Saved', { seconds = 0.2 })"));
    CHECK(f.Eval("toast.valid") == "true");
    f.Frames(90);
    CHECK(f.Eval("toast.valid") == "false");
}

void UiLuaStylesAndStates() {
    LuaUi f;
    CHECK(f.Run(R"LUA(
        ui.style("primary", { color = "#2a6fdb", radius = 8, transition = 0, hover = { color = "#f0f0f0" }, pressed = { color = "#102040" } })
        b = ui.button{ "Go", name = "B", style = "primary", size = { 100, 40 } }
        ui.show(b)
        local override = ui.button{ "Go", name = "O", style = "primary", radius = 2, pos = { 0, 100 } }
        ui.show(override)
    )LUA"));
    f.Frames(2);
    const magnum::UiNode* b = f.ui().Get(f.ui().Find("B"));
    CHECK(Near(b->visual.bg.b, 0xdb / 255.0f, 0.01f) && Near(b->visual.radius, 8.0f));
    CHECK(Near(f.ui().Get(f.ui().Find("O"))->visual.radius, 2.0f)); // inline properties win over the named style
    f.Frames(1, Click(b->rect, false));
    CHECK(Near(b->visual.bg.r, 0xf0 / 255.0f, 0.01f)); // hover
    f.Frames(1, Click(b->rect, true));
    CHECK(Near(b->visual.bg.r, 0x10 / 255.0f, 0.01f)); // pressed
    f.Frames(1, At(900, 600));
    CHECK(Near(b->visual.bg.b, 0xdb / 255.0f, 0.01f));
    CHECK(f.Run("b.enabled = false"));
    f.Frames(2);
    CHECK(Near(b->visual.bg.a, 0.5f) && f.Eval("b.enabled") == "false");
}

void UiLuaDocumentsLoadAndReload() {
    LuaUi f;
    f.WriteDocument("menu.lua",
                    "return ui.panel{ name = 'Doc', anchor = 'fill',\n"
                    "  ui.text{ 'v1', name = 'Label' },\n"
                    "  ui.button{ 'Quit', name = 'Quit', pos = { 0, 40 }, on_click = function() game.stop(); log('bye') end } }\n");
    CHECK(f.Run("root = ui.load('Content/UI/menu.lua')"));
    f.Frames(2);
    magnum::UiCanvas* canvas = f.ui().FindCanvas("menu");
    CHECK(canvas && canvas->document == "Content/UI/menu.lua" && f.Eval("root.kind") == "panel");
    CHECK(f.Eval("ui.find('Label').text") == "v1" && f.Eval("root:find('Quit').text") == "Quit");
    // A callback that uses game functions is harmless in the preview (they are stubs).
    const magnum::UiRect quit = f.ui().Get(f.ui().Find("Quit"))->rect;
    f.Frames(1, Click(quit, false));
    f.Frames(1, Click(quit, true));
    f.Frames(1, Click(quit, false));

    // Editing the file loads it again.
    const std::filesystem::path file = f.project.dir / "Content" / "UI" / "menu.lua";
    auto edit = [&](const std::string& text, int seconds) {
        f.WriteDocument("menu.lua", text);
        std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(seconds));
        f.preview.Update(f.scene, 1.0f, 1280, 720); // a second passes: the file check runs
    };
    edit("return ui.panel{ name = 'Doc', ui.text{ 'v2', name = 'Label' } }\n", 2);
    CHECK(f.Eval("ui.find('Label').text") == "v2" && f.ui().Find("Quit") == 0);
    // A broken file keeps the last good UI.
    edit("return ui.panel{ ui.text{ 'v3'\n", 4);
    CHECK(f.Eval("ui.find('Label').text") == "v2");
    edit("return ui.panel{ name = 'Doc', ui.text{ nonsense_value = 1 } }\n", 6); // a runtime error
    CHECK(f.Eval("ui.find('Label').text") == "v2");
    edit("return ui.panel{ name = 'Doc', ui.text{ 'v4', name = 'Label' } }\n", 8);
    CHECK(f.Eval("ui.find('Label').text") == "v4");
    // A document that never returns is stopped, a missing one is reported.
    f.WriteDocument("loop.lua", "while true do end\n");
    CHECK(f.Eval("ui.load('Content/UI/loop.lua')") == "nil");
    CHECK(f.Eval("ui.load('Content/UI/missing.lua')") == "nil");
    // And so is a runaway callback.
    CHECK(f.Run("ui.show(ui.panel{ name = 'Spin', on_update = function() while true do end end })"));
    f.Frames(3);
    CHECK(f.Eval("1 + 1") == "2");
}

void UiLuaCanvasComponentsFollowTheScene() {
    LuaUi f;
    f.WriteDocument("hud.lua", "return ui.text{ 'HUD', name = 'HudText' }\n");
    const uint64_t actor = f.scene.CreateActor("Canvas");
    magnum::CanvasComponent component;
    component.document = "Content/UI/hud.lua";
    component.order = 3;
    component.scaleMode = magnum::CanvasScaleMode::Fit;
    component.referenceWidth = 640.0f;
    component.referenceHeight = 360.0f;
    f.scene.Registry().emplace<magnum::CanvasComponent>(f.scene.Find(actor), component);
    f.Frames(2);
    const std::string name = "scene:" + std::to_string(actor);
    magnum::UiCanvas* canvas = f.ui().FindCanvas(name);
    CHECK(canvas && canvas->order == 3 && canvas->scaleMode == magnum::UiScaleMode::Fit && Near(canvas->scale, 2.0f));
    CHECK(f.Eval("ui.find('HudText').text") == "HUD");
    f.scene.TryGet<magnum::CanvasComponent>(actor)->visible = false;
    f.scene.TryGet<magnum::CanvasComponent>(actor)->order = 8;
    f.Frames(1);
    CHECK(!f.ui().FindCanvas(name)->visible && f.ui().FindCanvas(name)->order == 8);
    f.scene.TryGet<magnum::CanvasComponent>(actor)->visible = true;
    f.scene.TryGet<magnum::CanvasComponent>(actor)->document.clear(); // clearing the document empties the canvas
    f.Frames(1);
    CHECK(f.ui().Find("HudText") == 0);
    f.scene.DestroyActor(actor);
    f.Frames(1);
    CHECK(f.ui().FindCanvas(name) == nullptr);
}

void UiGameplayRoutesThePointerAndRunsDocuments() {
    ScriptProject project;
    std::filesystem::create_directories(project.dir / "Content" / "UI");
    WriteText(project.dir / "Content" / "UI" / "hud.lua",
              "return ui.panel{ anchor = 'fill',\n"
              "  ui.text{ '0', name = 'Score', pos = { 10, 10 } },\n"
              "  ui.button{ 'Quit', name = 'Quit', anchor = 'center', size = { 200, 60 }, on_click = function() game.stop() end } }\n");
    project.Write("watcher.lua",
                  "function on_start(self) self.state.label = ui.find('Score') end\n"
                  "function on_update(self, dt)\n"
                  "  self.state.frames = (self.state.frames or 0) + 1\n"
                  "  self.state.label.text = tostring(self.state.frames)\n"
                  "  if input.mouse(0) then self.state.free = (self.state.free or 0) + 1 end\n"
                  "  self.position = vec3(ui.pointer_over() and 1 or 0, input.ui_blocked() and 1 or 0, input.mouse(0) and 1 or 0)\n"
                  "end\n");
    magnum::Scene scene;
    const uint64_t canvas = scene.CreateActor("Canvas");
    magnum::CanvasComponent component;
    component.document = "Content/UI/hud.lua";
    scene.Registry().emplace<magnum::CanvasComponent>(scene.Find(canvas), component);
    const uint64_t watcher = AddScripted(scene, "Watcher", "Content/Scripts/watcher.lua");

    bool down = false;
    magnum::GameInput input;
    input.mouseDown = [&](int b) { return b == 0 && down; };
    input.viewSize = {1280, 720};
    input.mouseInView = true;
    magnum::Gameplay gameplay;
    gameplay.Begin(scene, project.dir);
    CHECK(gameplay.Ui().FindCanvas("scene:" + std::to_string(canvas)) != nullptr); // loaded before the scripts started
    input.mousePosition = {0.5f, 0.5f};                                             // the middle of the view: the Quit button
    gameplay.Update(scene, 1.0f / 60.0f, input);
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(watcher)->position.x, 1.0f)); // ui.pointer_over()
    CHECK(gameplay.Ui().Get(gameplay.Ui().Find("Score"))->text == "1");              // scripts and documents share nodes
    down = true;
    gameplay.Update(scene, 1.0f / 60.0f, input);
    // The press belongs to the UI: the script's input.mouse(0) stays false although the button is down.
    CHECK(!gameplay.StopRequested() && Near(scene.TryGet<magnum::TransformComponent>(watcher)->position.y, 1.0f) &&
          Near(scene.TryGet<magnum::TransformComponent>(watcher)->position.z, 0.0f));
    down = false;
    gameplay.Update(scene, 1.0f / 60.0f, input);
    CHECK(gameplay.StopRequested()); // on_click ran game.stop()

    // Away from the button the game gets the mouse and the UI does not.
    magnum::Gameplay second;
    second.Begin(scene, project.dir);
    input.mousePosition = {0.9f, 0.9f};
    down = true;
    second.Update(scene, 1.0f / 60.0f, input);
    second.Update(scene, 1.0f / 60.0f, input);
    CHECK(!second.StopRequested() && Near(scene.TryGet<magnum::TransformComponent>(watcher)->position.x, 0.0f) &&
          Near(scene.TryGet<magnum::TransformComponent>(watcher)->position.z, 1.0f));
    second.End();
    CHECK(second.Ui().Canvases().empty()); // the UI is gone with the session
}
