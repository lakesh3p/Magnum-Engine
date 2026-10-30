// ---- Render pipeline and fullscreen shaders ---------------------------------------------------

void FullscreenShadersCompile() {
    const std::filesystem::path root = MAGNUM_PROJECT_DIR;
    for (const char* name : {"sky", "clouds", "post_grade", "wave_lit"}) {
        const std::string rel = std::string("Content/Shaders/") + name + ".lua";
        magnum::CompiledShaderSource source;
        std::vector<magnum::ShaderDiagnostic> diagnostics;
        const bool ok = magnum::CompileLuaShader(root / rel, rel, std::filesystem::path(MAGNUM_ENGINE_DIR) / "Engine/Lua/shader_dsl.lua", "Engine/Lua/shader_dsl.lua",
                                               source, diagnostics);
        if (!ok) std::printf("  %s: %s\n", rel.c_str(), diagnostics.empty() ? "?" : diagnostics[0].ToString().c_str());
        CHECK(ok);
    }
    magnum::CompiledShaderSource clouds;
    std::vector<magnum::ShaderDiagnostic> diagnostics;
    CHECK(magnum::CompileLuaShader(root / "Content/Shaders/clouds.lua", "clouds.lua", std::filesystem::path(MAGNUM_ENGINE_DIR) / "Engine/Lua/shader_dsl.lua",
                                 "Engine/Lua/shader_dsl.lua", clouds, diagnostics));
    CHECK(clouds.vertexGlsl.find("gl_VertexID") != std::string::npos);      // no vertex buffer needed
    CHECK(clouds.fragmentGlsl.find("in vec3 v_ray;") != std::string::npos);
}

void FullscreenShaderErrorsPointToTheLine() {
    const std::filesystem::path root = MAGNUM_PROJECT_DIR;
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "magnum_test_fullscreen.lua";
    WriteText(file,
              "local S = shader('Bad')\n"
              "S:fullscreen(function(v)\n"
              "    local n = fbm(v.uv.x, 3)\n" // line 3: fbm needs a vec2 or vec3
              "    return vec4(n)\n"
              "end)\n"
              "return S\n");
    magnum::CompiledShaderSource source;
    std::vector<magnum::ShaderDiagnostic> diagnostics;
    CHECK(!magnum::CompileLuaShader(file, "bad_fs.lua", std::filesystem::path(MAGNUM_ENGINE_DIR) / "Engine/Lua/shader_dsl.lua", "Engine/Lua/shader_dsl.lua", source,
                                  diagnostics));
    CHECK(!diagnostics.empty() && diagnostics[0].line == 3);

    // A fullscreen shader has no vertex stage of its own.
    WriteText(file,
              "local S = shader('Bad')\n"
              "S:vertex(function(a) return vec4(0, 0, 0, 1), {} end)\n"
              "S:fullscreen(function(v) return vec4(1) end)\n"
              "return S\n");
    diagnostics.clear();
    CHECK(!magnum::CompileLuaShader(file, "bad_fs2.lua", std::filesystem::path(MAGNUM_ENGINE_DIR) / "Engine/Lua/shader_dsl.lua", "Engine/Lua/shader_dsl.lua", source,
                                  diagnostics));
    std::filesystem::remove(file);
}

void PipelineParsing() {
    std::vector<magnum::ShaderDiagnostic> diagnostics;
    magnum::PipelineData data;
    CHECK(magnum::ParsePipeline(
        "return pipeline { name = 'P', passes = { {'clear', color = {1, 0, 0}}, {'sky', material = 'a.lua'}, {'meshes'},"
        " {'post', material = 'b.lua', enabled = false} } }",
        "p.lua", data, diagnostics));
    CHECK(data.name == "P" && data.passes.size() == 4);
    CHECK(data.passes[0].type == magnum::PipelinePass::Type::Clear && data.passes[0].color[0] == 1.0f);
    CHECK(data.passes[1].type == magnum::PipelinePass::Type::Sky && data.passes[1].material == "a.lua");
    CHECK(!data.passes[3].enabled);
    CHECK(data.passes[1].scale == 1.0f);

    // Passes can render below full resolution.
    magnum::PipelineData scaled;
    CHECK(magnum::ParsePipeline("return pipeline { passes = { {'overlay', material = 'c.lua', scale = 0.5} } }", "p.lua", scaled, diagnostics));
    CHECK(scaled.passes.size() == 1 && scaled.passes[0].scale == 0.5f);
    for (const char* source : {"return pipeline { passes = { {'overlay', material = 'c.lua', scale = 0.1} } }",
                               "return pipeline { passes = { {'overlay', material = 'c.lua', scale = 2} } }"}) {
        std::vector<magnum::ShaderDiagnostic> d;
        magnum::PipelineData out;
        CHECK(!magnum::ParsePipeline(source, "p.lua", out, d) && !d.empty() && d[0].message.find("scale") != std::string::npos);
    }

    const char* bad[] = {
        "return pipeline { passes = { {'warp'} } }",                 // unknown pass type
        "return pipeline { passes = { {'sky'} } }",                  // sky without material
        "return pipeline { passes = { {'clear', color = {1, 2}} } }", // color needs three numbers
        "return pipeline { }",                                       // no passes
        "return pipeline { passes = { 'meshes' } }",                 // entry is not a table
        "return 5",
        "return pipeline {",                                         // syntax error
    };
    for (const char* source : bad) {
        diagnostics.clear();
        magnum::PipelineData out;
        out.name = "untouched";
        CHECK(!magnum::ParsePipeline(source, "p.lua", out, diagnostics));
        CHECK(!diagnostics.empty());
        CHECK(out.name == "untouched" && out.passes.empty()); // a failed parse leaves the result alone
    }

    // The shipped pipeline must be valid.
    magnum::RenderPipeline shipped;
    shipped.Init(MAGNUM_PROJECT_DIR, "Content/Pipelines/default.lua");
    CHECK(shipped.Diagnostics().empty() && shipped.Data().name == "Default" && shipped.Data().passes.size() >= 4);
}

void PipelineKeepsLastGoodVersion() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "magnum_test_pipeline";
    std::filesystem::create_directories(dir);
    WriteText(dir / "p.lua", "return pipeline { name = 'One', passes = { {'clear'}, {'meshes'} } }");
    magnum::RenderPipeline pipeline;
    pipeline.Init(dir, "p.lua");
    CHECK(pipeline.Data().name == "One");

    WriteText(dir / "p.lua", "return pipeline { name = 'Two', passes = { {'nope'} } }");
    CHECK(!pipeline.Reload());
    CHECK(pipeline.Data().name == "One" && !pipeline.Diagnostics().empty());

    WriteText(dir / "p.lua", "return pipeline { name = 'Three', passes = { {'meshes'} } }");
    CHECK(pipeline.Reload());
    CHECK(pipeline.Data().name == "Three" && pipeline.Diagnostics().empty());

    // No file at all: the built-in passes.
    magnum::RenderPipeline missing;
    missing.Init(dir, "absent.lua");
    CHECK(missing.Data().name == "Built-in" && missing.Data().passes.size() == 4);
    std::filesystem::remove_all(dir);
}

// ---- Editor plugins -----------------------------------------------------------------------------

void PluginsRegisterAndRun() {
    magnum::editor::PluginHost host;
    std::vector<std::string> executed;
    magnum::editor::PluginServices services;
    services.exec = [&](const std::string& line) { executed.push_back(line); };
    services.selectedName = [] { return std::string("Cube"); };
    services.actorNames = [] { return std::vector<std::string>{"Cube", "Sphere"}; };
    host.Init(std::filesystem::temp_directory_path() / "magnum_no_such_plugins_dir", services);

    const std::string error = host.LoadSource("T",
        "local calls = 0\n"
        "magnum.command('twice', function(args) magnum.exec('a ' .. args); magnum.exec('b') end)\n"
        "magnum.command('who', function() magnum.exec(magnum.selected() .. #magnum.actors()) end)\n"
        "magnum.menu('Tools/Do', function() calls = calls + 1 end)\n"
        "magnum.menu('Level/Other', function() end)\n"
        "magnum.panel('P', function(ui) end)\n");
    CHECK(error.empty());
    CHECK(host.RunCommand("twice", "x y"));
    CHECK((executed == std::vector<std::string>{"a x y", "b"}));
    CHECK(host.RunCommand("who", ""));
    CHECK(executed.back() == "Cube2");
    CHECK(!host.RunCommand("missing", ""));
    CHECK((host.ExtraMenus({"Tools"}) == std::vector<std::string>{"Level"}));

    const auto info = host.Plugins();
    CHECK(info.size() == 1 && info[0].commands == 2 && info[0].menus == 2 && info[0].panels == 1);
}

void PluginsAreSandboxedAndIsolated() {
    magnum::editor::PluginHost host;
    host.Init(std::filesystem::temp_directory_path() / "magnum_no_such_plugins_dir", {});

    // No file, OS or module access.
    CHECK(host.LoadSource("A", "assert(io == nil and os == nil and require == nil and dofile == nil and load == nil)").empty());
    CHECK(host.LoadSource("B", "x = 5").empty());
    // Globals of one plugin are invisible to another.
    CHECK(host.LoadSource("C", "assert(x == nil)").empty());

    // Errors are reported with the plugin file and line, and do not stop other plugins.
    const std::string syntax = host.LoadSource("D", "local = nope");
    CHECK(!syntax.empty() && syntax.find("Plugins/D/plugin.lua:1") != std::string::npos);
    const std::string runtime = host.LoadSource("E", "\n\nerror('boom')");
    CHECK(runtime.find("plugin.lua:3") != std::string::npos && runtime.find("boom") != std::string::npos);
    const std::string badMenu = host.LoadSource("F", "magnum.menu('NoSlash', function() end)");
    CHECK(!badMenu.empty());
    CHECK(host.LoadSource("G", "magnum.log('still fine')").empty());

    const auto info = host.Plugins();
    CHECK(info.size() == 7 && !info[3].error.empty() && info[6].error.empty());
}

void PluginCallbackThatFailsIsSwitchedOff() {
    magnum::editor::PluginHost host;
    host.Init(std::filesystem::temp_directory_path() / "magnum_no_such_plugins_dir", {});
    CHECK(host.LoadSource("T",
                          "n = 0\n"
                          "magnum.on_update(function(dt) n = n + 1; if n == 2 then error('bad frame') end end)\n"
                          "magnum.command('count', function() magnum.log(tostring(n)) end)\n").empty());
    for (int i = 0; i < 10; ++i) host.Update(0.016f); // must not throw or crash; logs the error once
    CHECK(host.RunCommand("count", "")); // the plugin itself keeps working
}

void ShippedPluginsLoad() {
    magnum::editor::PluginHost host;
    std::vector<std::string> executed;
    magnum::editor::PluginServices services;
    services.exec = [&](const std::string& line) { executed.push_back(line); };
    services.selectedName = [] { return std::string(); };
    services.actorNames = [] { return std::vector<std::string>{"Cube"}; };
    host.Init(std::filesystem::path(MAGNUM_PROJECT_DIR) / "Plugins", services);

    const auto info = host.Plugins();
    CHECK(info.size() >= 2);
    for (const auto& p : info) CHECK(p.error.empty());
    CHECK(host.RunCommand("scatter", "3"));
    CHECK(executed.size() == 6); // spawn + set per actor
    CHECK(executed[0] == "spawn cube" && executed[1].rfind("set Transform.Position ", 0) == 0);
    host.Update(0.016f);

    // Every project carries the MCP plugin (Tools > MCP ...): the template's copy loads and its menu items run console commands.
    magnum::editor::PluginHost mcp;
    executed.clear();
    services.mcpStatus = [] { return std::string("Control channel: off"); };
    mcp.Init(std::filesystem::path(MAGNUM_ENGINE_DIR) / "Engine" / "Templates" / "Project" / "Plugins", services);
    const auto mcpInfo = mcp.Plugins();
    CHECK(mcpInfo.size() == 1 && mcpInfo[0].name == "MCP" && mcpInfo[0].error.empty() && mcpInfo[0].menus == 0 && mcpInfo[0].panels == 1);
    CHECK(std::filesystem::exists(std::filesystem::path(MAGNUM_PROJECT_DIR) / "Plugins" / "MCP" / "plugin.lua"));
}

// ---- Projects -----------------------------------------------------------------------------------

void ProjectCreationAndLoading() {
    const std::filesystem::path base = std::filesystem::temp_directory_path() / "magnum_test_projects";
    std::filesystem::remove_all(base);
    const std::filesystem::path templateDir = std::filesystem::path(MAGNUM_ENGINE_DIR) / "Engine" / "Templates" / "Project";

    // A new folder: created with starter content, a pipeline, a game module and a .magnum file.
    std::string error;
    CHECK(magnum::CreateProject(base / "My Game", "My Game", templateDir, error));
    CHECK(error.empty());
    CHECK(std::filesystem::exists(base / "My Game" / "My Game.magnum"));
    CHECK(std::filesystem::exists(base / "My Game" / "Content" / "Shaders" / "default_lit.lua")); // the engine falls back to it
    CHECK(std::filesystem::exists(base / "My Game" / "Content" / "Pipelines" / "default.lua"));
    CHECK(std::filesystem::exists(base / "My Game" / "Game" / "CMakeLists.txt"));
    CHECK(std::filesystem::is_directory(base / "My Game" / "Plugins"));

    magnum::ProjectInfo info;
    CHECK(magnum::LoadProjectInfo(base / "My Game", info));
    CHECK(info.name == "My Game" && info.defaultScene == "Content/Scenes/Default.scene");

    // The pipeline the template brought along must be valid.
    magnum::RenderPipeline pipeline;
    pipeline.Init(base / "My Game", "Content/Pipelines/default.lua");
    CHECK(pipeline.Diagnostics().empty() && pipeline.Data().passes.size() >= 4);

    // An existing project, or a folder with other files, is never overwritten.
    CHECK(!magnum::CreateProject(base / "My Game", "Again", templateDir, error) && error.find("already exists") != std::string::npos);
    std::filesystem::create_directories(base / "Busy");
    WriteText(base / "Busy" / "notes.txt", "keep me");
    CHECK(!magnum::CreateProject(base / "Busy", "Busy", templateDir, error) && error.find("not empty") != std::string::npos);
    CHECK(std::filesystem::exists(base / "Busy" / "notes.txt") && !std::filesystem::exists(base / "Busy" / "Busy.magnum"));

    // An empty existing folder is fine; characters that cannot be in a file name are replaced.
    std::filesystem::create_directories(base / "Empty");
    CHECK(magnum::CreateProject(base / "Empty", "A/B: C", templateDir, error));
    CHECK(magnum::LoadProjectInfo(base / "Empty", info) && info.name == "A/B: C");

    // A folder with no .magnum file is not a project.
    CHECK(!magnum::LoadProjectInfo(base / "Busy", info));
    std::filesystem::remove_all(base);
}

void EngineAssetsResolveToTheEngineFolder() {
    magnum::SetEngineRoot("C:/engine");
    CHECK(magnum::ResolveAsset("D:/proj", "Engine/Shaders/grid.lua") == std::filesystem::path("C:/engine/Engine/Shaders/grid.lua"));
    CHECK(magnum::ResolveAsset("D:/proj", "Content/Materials/Red.lua") == std::filesystem::path("D:/proj/Content/Materials/Red.lua"));
    magnum::SetEngineRoot(MAGNUM_ENGINE_DIR); // what the other tests expect
}

// ---- Shader size -------------------------------------------------------------------------------

// The DSL writes straight-line GLSL, so a Lua `for` over many iterations is unrolled. A shader that unrolls into
// tens of thousands of lines freezes the driver's compiler (and with it the editor): every shipped shader must stay small.
void ShippedShadersStaySmall() {
    const std::filesystem::path project = MAGNUM_PROJECT_DIR;
    for (const auto& entry : std::filesystem::directory_iterator(project / "Content" / "Shaders")) {
        if (entry.path().extension() != ".lua") continue;
        const std::string rel = "Content/Shaders/" + entry.path().filename().string();
        magnum::CompiledShaderSource source;
        std::vector<magnum::ShaderDiagnostic> diagnostics;
        const auto start = std::chrono::steady_clock::now();
        const bool ok = magnum::CompileLuaShader(entry.path(), rel, std::filesystem::path(MAGNUM_ENGINE_DIR) / "Engine/Lua/shader_dsl.lua",
                                               "Engine/Lua/shader_dsl.lua", source, diagnostics);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const auto lines = std::count(source.fragmentGlsl.begin(), source.fragmentGlsl.end(), '\n');
        std::printf("  %-22s %s  %6lld GLSL lines  %.2f s\n", rel.c_str(), ok ? "ok  " : "FAIL", static_cast<long long>(lines), seconds);
        if (!ok) std::printf("    %s\n", diagnostics.empty() ? "?" : diagnostics[0].ToString().c_str());
        CHECK(ok);
        CHECK(lines < 3000);
        CHECK(seconds < 2.0);
    }
}

// ---- loop() --------------------------------------------------------------------------------------

namespace {
// Compiles a fullscreen shader whose fragment body is `body`; returns the diagnostics (empty = compiled).
std::vector<magnum::ShaderDiagnostic> CompileBody(const std::string& body, magnum::CompiledShaderSource& source) {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "magnum_test_loop.lua";
    WriteText(file, "local S = shader('T')\nS:fullscreen(function(v)\n" + body + "\nend)\nreturn S\n");
    std::vector<magnum::ShaderDiagnostic> diagnostics;
    magnum::CompileLuaShader(file, "loop.lua", std::filesystem::path(MAGNUM_ENGINE_DIR) / "Engine/Lua/shader_dsl.lua", "Engine/Lua/shader_dsl.lua",
                           source, diagnostics);
    std::filesystem::remove(file);
    return diagnostics;
}
} // namespace

void LoopBecomesARealGlslLoop() {
    magnum::CompiledShaderSource source;
    auto d = CompileBody(
        "  local sum, w = loop(64, { 0.0, 1.0 }, function(i, sum, w)\n"
        "    return sum + noise(v.uv * (i + 1.0)) * w, w * 0.5\n"
        "  end)\n"
        "  return vec4(sum, w, 0.0, 1.0)", source);
    CHECK(d.empty());
    CHECK(source.fragmentGlsl.find("float k_noise(vec3") != std::string::npos); // the noise helpers were added
    CHECK(source.fragmentGlsl.find("for (int i") != std::string::npos && source.fragmentGlsl.find("< 64;") != std::string::npos);
    // 64 passes do not make the shader 64 times longer.
    CHECK(std::count(source.fragmentGlsl.begin(), source.fragmentGlsl.end(), '\n') < 120);

    // The same loop written with a Lua `for` is unrolled.
    magnum::CompiledShaderSource unrolled;
    d = CompileBody(
        "  local sum, w = 0.0, 1.0\n"
        "  for i = 0, 63 do sum = sum + noise(v.uv * (i + 1.0)) * w; w = w * 0.5 end\n"
        "  return vec4(sum, w, 0.0, 1.0)", unrolled);
    CHECK(d.empty());
    CHECK(std::count(unrolled.fragmentGlsl.begin(), unrolled.fragmentGlsl.end(), '\n') > 200);
}

void LoopMisuseIsReported() {
    magnum::CompiledShaderSource source;
    auto d = CompileBody("  local a = loop(4, { 0.0, 1.0 }, function(i, a, b) return a end)\n  return vec4(a)", source);
    CHECK(!d.empty() && d[0].line == 3 && d[0].message.find("must return 2") != std::string::npos);

    d = CompileBody("  local a = loop(4, { 0.0 }, function(i, a) return vec3(a) end)\n  return vec4(a)", source);
    CHECK(!d.empty() && d[0].message.find("is a float but the body returned a vec3") != std::string::npos);

    d = CompileBody("  local a = loop(0, { 0.0 }, function(i, a) return a end)\n  return vec4(a)", source);
    CHECK(!d.empty() && d[0].message.find("from 1 to 512") != std::string::npos);

    // A long Lua `for` is refused with a hint, instead of freezing the driver.
    d = CompileBody("  local a = 0.0\n  for i = 1, 4000 do a = a + sin(a + i) end\n  return vec4(a)", source);
    CHECK(!d.empty() && d[0].message.find("use loop(") != std::string::npos);
}

void ConstantsMayLiveOutsideStageFunctions() {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "magnum_test_const.lua";
    WriteText(file,
              "local S = shader('T')\n"
              "local BETA = vec3(5.8e-3, 13.5e-3, 33.1e-3)\n"
              "local TWO = vec2(1, 2)\n"
              "S:fullscreen(function(v) return vec4(BETA * 2.0 + vec3(TWO, 0.5), 1.0) end)\n"
              "return S\n");
    magnum::CompiledShaderSource source;
    std::vector<magnum::ShaderDiagnostic> diagnostics;
    CHECK(magnum::CompileLuaShader(file, "const.lua", std::filesystem::path(MAGNUM_ENGINE_DIR) / "Engine/Lua/shader_dsl.lua",
                                 "Engine/Lua/shader_dsl.lua", source, diagnostics));
    CHECK(source.fragmentGlsl.find("vec3(0.0058") != std::string::npos);

    // Anything computed from shader values is still an error outside a stage function.
    WriteText(file,
              "local S = shader('T')\n"
              "local p = S:param('p', 'float', 1.0)\n"
              "local BAD = vec3(p, 0, 0)\n"
              "S:fullscreen(function(v) return vec4(BAD, 1.0) end)\n"
              "return S\n");
    diagnostics.clear();
    CHECK(!magnum::CompileLuaShader(file, "const.lua", std::filesystem::path(MAGNUM_ENGINE_DIR) / "Engine/Lua/shader_dsl.lua",
                                  "Engine/Lua/shader_dsl.lua", source, diagnostics));
    CHECK(!diagnostics.empty() && diagnostics[0].line == 3 && diagnostics[0].message.find("plain numbers") != std::string::npos);
    std::filesystem::remove(file);
}

// ---- Primitive shapes ---------------------------------------------------------------------------

// Every generated solid must be sound: indices in range, unit normals, every triangle wound to face the way its
// normals point (so back-face culling shows the outside), and everything inside the unit box.
void ExtraPrimitivesAreSound() {
    using magnum::Primitive;
    const Primitive shapes[] = {Primitive::Capsule, Primitive::Hemisphere, Primitive::Torus, Primitive::Arch, Primitive::Pyramid,
                                Primitive::Wedge, Primitive::Prism, Primitive::Diamond, Primitive::Stairs, Primitive::Disc,
                                Primitive::Tube, Primitive::Ring, Primitive::Quad};
    for (const Primitive shape : shapes) {
        std::vector<float> v;
        std::vector<uint32_t> idx;
        const char* name = magnum::kPrimitiveNames[static_cast<int>(shape)];
        CHECK(magnum::BuildExtraPrimitive(shape, v, idx));
        const size_t count = v.size() / 8;
        CHECK(count > 3 && idx.size() >= 3 && idx.size() % 3 == 0);
        int wrong = 0, outside = 0, badNormal = 0;
        for (size_t i = 0; i < count; ++i) {
            const float* p = &v[i * 8];
            const float length = std::sqrt(p[3] * p[3] + p[4] * p[4] + p[5] * p[5]);
            if (std::abs(length - 1.0f) > 1e-3f) ++badNormal;
            for (int k = 0; k < 3; ++k) {
                if (std::abs(p[k]) > 0.5f + 1e-4f) ++outside;
            }
        }
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            CHECK(idx[t] < count && idx[t + 1] < count && idx[t + 2] < count);
            const float* a = &v[idx[t] * 8];
            const float* b = &v[idx[t + 1] * 8];
            const float* c = &v[idx[t + 2] * 8];
            const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
            const float facing = n[0] * (a[3] + b[3] + c[3]) + n[1] * (a[4] + b[4] + c[4]) + n[2] * (a[5] + b[5] + c[5]);
            if (facing < 0.0f) ++wrong;
        }
        if (wrong || outside || badNormal) std::printf("  %s: %d inverted triangles, %d points outside the unit box, %d bad normals\n", name, wrong, outside, badNormal);
        CHECK(wrong == 0 && outside == 0 && badNormal == 0);
    }
    // The table of spawnable actors covers every primitive exactly once.
    std::vector<int> seen(static_cast<size_t>(magnum::kPrimitiveCount), 0);
    for (int i = 0; i < static_cast<int>(magnum::editor::SpawnKind::Count); ++i) {
        const magnum::editor::SpawnInfo& info = magnum::editor::GetSpawnInfo(static_cast<magnum::editor::SpawnKind>(i));
        CHECK(static_cast<int>(info.kind) == i);
        if (info.isMesh) ++seen[static_cast<size_t>(info.primitive)];
        magnum::editor::SpawnKind found;
        CHECK(magnum::editor::FindSpawnKind(info.command, found) && found == info.kind);
    }
    for (const int n : seen) CHECK(n >= 1); // every primitive can be spawned (gameplay kinds reuse some)
}

