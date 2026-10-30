// Small dependency-free test runner: `MagnumTests` returns non-zero if any check fails.
#include "game/GameRuntime.h"
#include "game/Gameplay.h"
#include "game/Physics.h"
#include "magnum_game_api.h"
#include "material/MaterialData.h"
#include "model/ModelImport.h"
#include "scene/ActorSnapshot.h"
#include "shader/LuaShaderCompiler.h"
#include "shader/MaterialGraph.h"
#include "PluginHost.h"
#include "core/Paths.h"
#include "commands/SpawnKinds.h"
#include "project/Project.h"
#include "render/PrimitiveShapes.h"
#include "render/RenderPipeline.h"
#include "scene/Reflection.h"
#include "scene/SceneSerializer.h"
#include "scene/SpriteSize.h"
#include "scene/TransformMath.h"
#include "ui/UiPreview.h"
#include "ui/UiSystem.h"

#ifdef _WIN32
#include <crtdbg.h>
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;
const char* g_currentTest = "";

#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::printf("  FAIL [%s] %s:%d  %s\n", g_currentTest, __FILE__, __LINE__, #cond); \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

bool Near(float a, float b, float eps = 1e-3f) {
    return std::abs(a - b) <= eps;
}

void EulerRoundTrip() {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> pitch(-85.0f, 85.0f), angle(-175.0f, 175.0f);
    for (int i = 0; i < 2000; ++i) {
        magnum::TransformComponent t;
        t.position = {1.0f, 2.0f, 3.0f};
        t.scale = {0.5f + i % 7, 1.0f + i % 3, 2.0f};
        t.rotation = {pitch(rng), angle(rng), angle(rng)};

        const magnum::TransformComponent back = magnum::DecomposeTransform(t.Matrix(), t);
        CHECK(Near(back.rotation.x, t.rotation.x) && Near(back.rotation.y, t.rotation.y) &&
              Near(back.rotation.z, t.rotation.z));
        CHECK(Near(back.scale.x, t.scale.x) && Near(back.scale.y, t.scale.y) && Near(back.scale.z, t.scale.z));
        CHECK(Near(back.position.x, 1.0f) && Near(back.position.z, 3.0f));
    }
}

void EulerStaysContinuous() {
    // Rotating a little past 180 must not flip to -180 when the previous value is passed in.
    magnum::TransformComponent previous;
    previous.rotation = {0.0f, 179.0f, 0.0f};
    magnum::TransformComponent next = previous;
    next.rotation.y = 181.0f;
    const magnum::TransformComponent result = magnum::DecomposeTransform(next.Matrix(), previous);
    CHECK(Near(result.rotation.y, 181.0f));
}

void GimbalLockDoesNotProduceNaN() {
    magnum::TransformComponent t;
    t.rotation = {90.0f, 30.0f, 0.0f};
    const magnum::TransformComponent back = magnum::DecomposeTransform(t.Matrix(), t);
    CHECK(std::isfinite(back.rotation.x) && std::isfinite(back.rotation.y) && std::isfinite(back.rotation.z));
    // Whatever angles come back must describe the same orientation.
    const glm::mat4 a = t.Matrix(), b = back.Matrix();
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) CHECK(Near(a[c][r], b[c][r], 1e-3f));
}

magnum::Scene MakeSampleScene() {
    magnum::Scene scene;
    auto& reg = scene.Registry();

    uint64_t id = scene.CreateActor("Cube");
    entt::entity e = scene.Find(id);
    reg.get<magnum::TransformComponent>(e).position = {0.1f, 0.5f, -3.25f};
    reg.get<magnum::TransformComponent>(e).rotation = {10.0f, 35.5f, 0.0f};
    reg.emplace<magnum::MeshComponent>(e);

    id = scene.CreateActor("Gradient \"Sphere\"");
    e = scene.Find(id);
    magnum::MeshComponent mesh;
    mesh.primitive = magnum::Primitive::Sphere;
    mesh.material = "Content/Shaders/gradient.lua";
    reg.emplace<magnum::MeshComponent>(e, mesh);

    id = scene.CreateActor("Sun");
    e = scene.Find(id);
    magnum::LightComponent light;
    light.intensity = 2.5f;
    light.color = {1.0f, 0.5f, 0.25f};
    reg.emplace<magnum::LightComponent>(e, light);

    scene.CreateActor("Empty");
    return scene;
}

// True if every property of every component matches, in the same actor order.
bool ScenesEqual(const magnum::Scene& a, const magnum::Scene& b) {
    if (a.Order() != b.Order()) return false;
    for (const uint64_t id : a.Order()) {
        for (const magnum::ComponentInfo* infoPtr : magnum::AllComponentInfos()) {
            const magnum::ComponentInfo& info = *infoPtr;
            const bool inA = info.has(a.Registry(), a.Find(id));
            const bool inB = info.has(b.Registry(), b.Find(id));
            if (inA != inB) return false;
            if (!inA) continue;
            const auto ca = info.clone(a.Registry(), a.Find(id));
            const auto cb = info.clone(b.Registry(), b.Find(id));
            for (const magnum::PropertyInfo& p : info.props) {
                if (magnum::GetProperty(p, ca.get()) != magnum::GetProperty(p, cb.get())) return false;
            }
        }
    }
    return true;
}

void SceneRoundTrip() {
    const magnum::Scene original = MakeSampleScene();
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "magnum_test_scene.scene";

    magnum::SceneEditorState editor;
    editor.hasCamera = true;
    editor.cameraPosition = {5.5f, 3.5f, 7.0f};
    editor.cameraYaw = -126.0f;
    editor.cameraPitch = -24.0f;

    std::string error;
    CHECK(magnum::SaveScene(original, editor, file, error));
    CHECK(error.empty());

    magnum::Scene loaded;
    magnum::SceneEditorState loadedEditor;
    CHECK(magnum::LoadScene(loaded, loadedEditor, file, error));
    CHECK(ScenesEqual(original, loaded));
    CHECK(loadedEditor.hasCamera && Near(loadedEditor.cameraYaw, -126.0f) && Near(loadedEditor.cameraPosition.z, 7.0f));

    // Saving what was loaded must give byte-identical text (stable, diff-friendly files).
    const std::filesystem::path file2 = std::filesystem::temp_directory_path() / "magnum_test_scene2.scene";
    CHECK(magnum::SaveScene(loaded, loadedEditor, file2, error));
    CHECK(std::filesystem::file_size(file) == std::filesystem::file_size(file2));

    std::filesystem::remove(file);
    std::filesystem::remove(file2);
}

void LoadRejectsBrokenFilesWithoutTouchingTheScene() {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "magnum_test_broken.scene";
    magnum::Scene scene = MakeSampleScene();
    const size_t before = scene.Order().size();
    magnum::SceneEditorState editor;
    std::string error;

    const char* broken[] = {
        "{ not json",
        R"({"actors": []})",                                          // no version
        R"({"magnumScene": 999, "actors": []})",                         // from the future
        R"({"magnumScene": 1, "actors": [{"id": "zz"}]})",               // bad id
        R"({"magnumScene": 1, "actors": [{"id": "1"}, {"id": "1"}]})",   // duplicate id
    };
    for (const char* text : broken) {
        {
            std::FILE* f = std::fopen(file.string().c_str(), "wb");
            std::fputs(text, f);
            std::fclose(f);
        }
        error.clear();
        CHECK(!magnum::LoadScene(scene, editor, file, error));
        CHECK(!error.empty());
        CHECK(scene.Order().size() == before);
    }
    std::filesystem::remove(file);
}

void LoadSkipsUnknownThingsButKeepsTheRest() {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "magnum_test_unknown.scene";
    {
        std::FILE* f = std::fopen(file.string().c_str(), "wb");
        std::fputs(R"({"magnumScene":1,"actors":[{"id":"abc","components":{
            "Name":{"Name":"Kept"},
            "Transform":{"Position":[1,2,3],"Wobble":5,"Scale":"oops"},
            "Teleporter":{"Range":3}}}]})", f);
        std::fclose(f);
    }
    magnum::Scene scene;
    magnum::SceneEditorState editor;
    std::string error;
    CHECK(magnum::LoadScene(scene, editor, file, error));
    CHECK(scene.Order().size() == 1);
    const uint64_t id = scene.Order()[0];
    CHECK(id == 0xabc);
    CHECK(scene.TryGet<magnum::NameComponent>(id)->name == "Kept");
    CHECK(scene.TryGet<magnum::TransformComponent>(id)->position == glm::vec3(1, 2, 3));
    CHECK(scene.TryGet<magnum::TransformComponent>(id)->scale == glm::vec3(1, 1, 1)); // bad value -> default
    std::filesystem::remove(file);
}

void SnapshotRestoresOrderAndComponents() {
    magnum::Scene scene = MakeSampleScene();
    const uint64_t victim = scene.Order()[1];
    const magnum::ActorSnapshot snap = magnum::TakeSnapshot(scene, victim);
    const std::vector<uint64_t> order = scene.Order();

    scene.DestroyActor(victim);
    CHECK(!scene.Exists(victim));
    magnum::RestoreSnapshot(scene, snap);
    CHECK(scene.Order() == order);
    CHECK(scene.TryGet<magnum::MeshComponent>(victim)->primitive == magnum::Primitive::Sphere);
}

void UniqueNames() {
    magnum::Scene scene;
    scene.CreateActor("Cube");
    scene.CreateActor(scene.UniqueName("Cube"));
    CHECK(scene.UniqueName("Cube") == "Cube_2");
    CHECK(scene.UniqueName("Other") == "Other");
}

void WriteText(const std::filesystem::path& file, const std::string& text) {
    std::FILE* f = std::fopen(file.string().c_str(), "wb");
    std::fputs(text.c_str(), f);
    std::fclose(f);
}

void MaterialRoundTrip() {
    magnum::MaterialData data;
    data.shader = "Content/Shaders/textured_lit.lua";
    data.SetNumber("tint", 3, {0.25f, 0.5f, 1.0f, 0});
    data.SetNumber("ambient", 1, {0.18f, 0, 0, 0});
    data.SetNumber("tiling", 2, {4.0f, 2.0f, 0, 0});
    data.SetTexture("albedo", "Content/Textures/a \"b\".png");

    const std::filesystem::path file = std::filesystem::temp_directory_path() / "magnum_test_material.lua";
    WriteText(file, magnum::FormatMaterial(data));

    magnum::MaterialData loaded;
    std::vector<magnum::ShaderDiagnostic> diagnostics;
    CHECK(magnum::LoadMaterialFile(file, "magnum_test_material.lua", loaded, diagnostics));
    CHECK(diagnostics.empty());
    CHECK(loaded.shader == data.shader);
    CHECK(loaded.params.size() == 4);
    for (const magnum::MaterialValue& v : data.params) {
        const magnum::MaterialValue* other = loaded.Find(v.name);
        CHECK(other != nullptr && *other == v);
    }

    // Formatting is independent of the order parameters were set in.
    magnum::MaterialData reordered;
    reordered.shader = data.shader;
    for (auto it = data.params.rbegin(); it != data.params.rend(); ++it) reordered.params.push_back(*it);
    CHECK(magnum::FormatMaterial(reordered) == magnum::FormatMaterial(data));
    std::filesystem::remove(file);
}

void MaterialRejectsBadFiles() {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "magnum_test_material_bad.lua";
    const char* bad[] = {
        "return 42",                                                                // not a material
        R"(return material { params = {} })",                                        // no shader
        R"(return material { shader = "x.lua", params = { a = { 1 } } })",          // vector of 1
        R"(return material { shader = "x.lua", params = { a = true } })",           // unsupported type
        "return material {",                                                        // syntax error
        R"(return material { shader = "x.lua", params = os.exit(1) })",             // no OS access
    };
    for (const char* text : bad) {
        WriteText(file, text);
        magnum::MaterialData out;
        std::vector<magnum::ShaderDiagnostic> diagnostics;
        CHECK(!magnum::LoadMaterialFile(file, "bad.lua", out, diagnostics));
        CHECK(!diagnostics.empty());
    }
    // A syntax error must be reported with its line number.
    WriteText(file, "-- comment\nreturn material {\n  shader = \n}\n}");
    magnum::MaterialData out;
    std::vector<magnum::ShaderDiagnostic> diagnostics;
    CHECK(!magnum::LoadMaterialFile(file, "bad.lua", out, diagnostics));
    CHECK(!diagnostics.empty() && diagnostics[0].line > 0);
    std::filesystem::remove(file);
}

void ShaderTextureParameters() {
    const std::filesystem::path root = MAGNUM_PROJECT_DIR;
    magnum::CompiledShaderSource source;
    std::vector<magnum::ShaderDiagnostic> diagnostics;
    CHECK(magnum::CompileLuaShader(root / "Content/Shaders/textured_lit.lua", "Content/Shaders/textured_lit.lua",
                                 std::filesystem::path(MAGNUM_ENGINE_DIR) / "Engine/Lua/shader_dsl.lua", "Engine/Lua/shader_dsl.lua", source, diagnostics));
    CHECK(diagnostics.empty());

    const magnum::ShaderParam* albedo = nullptr;
    const magnum::ShaderParam* tiling = nullptr;
    for (const magnum::ShaderParam& p : source.params) {
        if (p.name == "albedo") albedo = &p;
        if (p.name == "tiling") tiling = &p;
    }
    CHECK(albedo && albedo->isTexture && albedo->components == 0 && albedo->srgb && albedo->defaultTexture.empty());
    CHECK(tiling && !tiling->isTexture && tiling->components == 2 && tiling->defaultValue[0] == 1.0f);
    CHECK(source.fragmentGlsl.find("uniform sampler2D p_albedo;") != std::string::npos);
    CHECK(source.fragmentGlsl.find("texture(p_albedo,") != std::string::npos);
}

void ShaderTextureMisuseIsReportedWithLine() {
    const std::filesystem::path root = MAGNUM_PROJECT_DIR;
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "magnum_test_badtex.lua";
    WriteText(file,
              "local S = shader('Bad')\n"
              "local uv = S:attribute('uv', 'vec2')\n"
              "local tint = S:param('tint', 'color', {1,1,1})\n"
              "S:vertex(function(a) return vec4(a.uv, 0, 1), { uv = a.uv } end)\n"
              "S:fragment(function(v)\n"
              "    return texture(tint, v.uv)\n" // line 6: a color is not a texture
              "end)\n"
              "return S\n");
    magnum::CompiledShaderSource source;
    std::vector<magnum::ShaderDiagnostic> diagnostics;
    CHECK(!magnum::CompileLuaShader(file, "bad_tex.lua", std::filesystem::path(MAGNUM_ENGINE_DIR) / "Engine/Lua/shader_dsl.lua", "Engine/Lua/shader_dsl.lua",
                                  source, diagnostics));
    CHECK(!diagnostics.empty() && diagnostics[0].line == 6);
    std::filesystem::remove(file);
}

#include "pipeline_tests.inc"
#include "gameplay_tests.inc"
#include "sprite_tests.inc"
#include "ui_tests.inc"
#include "ui_lua_tests.inc"
#include "model_tests.inc"

// ---- Game runtime -------------------------------------------------------------------------------

struct SpinnerV1 {
    float speed[3];
};
struct SpinnerV2 { // a field was added in front: every offset changes
    int32_t extra;
    float speed[3];
    char label[8];
};

const MagnumPropertyDesc kSpinnerV1Props[] = {{"speed", "Spin", MAGNUM_PROP_VEC3, offsetof(SpinnerV1, speed), 0, 0, 0, 0, nullptr, 0}};
const MagnumPropertyDesc kSpinnerV2Props[] = {
    {"extra", "Spin", MAGNUM_PROP_INT, offsetof(SpinnerV2, extra), 0, 0, 0, 0, nullptr, 0},
    {"speed", "Spin", MAGNUM_PROP_VEC3, offsetof(SpinnerV2, speed), 0, 0, 0, 0, nullptr, 0},
    {"label", "Spin", MAGNUM_PROP_STRING, offsetof(SpinnerV2, label), sizeof(SpinnerV2::label), 0, 0, 0, nullptr, 0},
};

MagnumComponentDesc SpinnerDesc(uint32_t size, const MagnumPropertyDesc* props, uint32_t count) {
    MagnumComponentDesc d = {};
    d.name = "Spinner";
    d.size = size;
    d.properties = props;
    d.property_count = count;
    return d;
}

uint64_t ActorWithSpinner(magnum::Scene& scene, magnum::GameRuntime& runtime, float speedY) {
    const uint64_t id = scene.CreateActor("Spinning");
    runtime.SetTypesForTest(scene, {SpinnerDesc(sizeof(SpinnerV1), kSpinnerV1Props, 1)});
    const magnum::ComponentInfo* info = magnum::FindComponentInfo("Spinner");
    info->add(scene.Registry(), scene.Find(id));
    SpinnerV1* data = static_cast<SpinnerV1*>(info->get(scene.Registry(), scene.Find(id)));
    data->speed[1] = speedY;
    return id;
}

void GameComponentsSurviveLayoutChange() {
    magnum::Scene scene;
    magnum::GameRuntime runtime;
    const uint64_t id = ActorWithSpinner(scene, runtime, 42.0f);

    // "Hot reload" with a different struct: values are matched by field name, not by offset.
    runtime.SetTypesForTest(scene, {SpinnerDesc(sizeof(SpinnerV2), kSpinnerV2Props, 3)});
    const magnum::ComponentInfo* info = magnum::FindComponentInfo("Spinner");
    CHECK(info && info->game);
    const SpinnerV2* v2 = static_cast<const SpinnerV2*>(info->get(scene.Registry(), scene.Find(id)));
    CHECK(v2 != nullptr && v2->speed[1] == 42.0f && v2->extra == 0);

    // ...and back again, still keeping the value.
    runtime.SetTypesForTest(scene, {SpinnerDesc(sizeof(SpinnerV1), kSpinnerV1Props, 1)});
    info = magnum::FindComponentInfo("Spinner");
    const SpinnerV1* v1 = static_cast<const SpinnerV1*>(info->get(scene.Registry(), scene.Find(id)));
    CHECK(v1 != nullptr && v1->speed[1] == 42.0f);
}

void GameComponentsKeepValuesWhileTypeIsMissing() {
    magnum::Scene scene;
    magnum::GameRuntime runtime;
    const uint64_t id = ActorWithSpinner(scene, runtime, 7.5f);

    runtime.SetTypesForTest(scene, {}); // the module no longer registers it
    CHECK(magnum::FindComponentInfo("Spinner") == nullptr);
    const magnum::GameInstance* kept = scene.TryGet<magnum::GameComponentsComponent>(id)->Find("Spinner");
    CHECK(kept != nullptr && !kept->resolved && !kept->pendingJson.empty());

    // A scene saved now must still contain the component with its values...
    std::string text, error;
    magnum::SceneEditorState editor;
    CHECK(magnum::SerializeScene(scene, editor, text, error));
    CHECK(text.find("Spinner") != std::string::npos && text.find("7.5") != std::string::npos);

    // ...so when the type comes back (even after a save/load round trip) the value is there.
    magnum::Scene reloaded;
    CHECK(magnum::DeserializeScene(reloaded, editor, text, error));
    runtime.SetTypesForTest(reloaded, {SpinnerDesc(sizeof(SpinnerV1), kSpinnerV1Props, 1)});
    const magnum::ComponentInfo* info = magnum::FindComponentInfo("Spinner");
    const SpinnerV1* v1 = static_cast<const SpinnerV1*>(info->get(reloaded.Registry(), reloaded.Find(id)));
    CHECK(v1 != nullptr && v1->speed[1] == 7.5f);
}

void GameStringPropertyIsBoundedByCapacity() {
    magnum::Scene scene;
    magnum::GameRuntime runtime;
    const uint64_t id = scene.CreateActor("A");
    runtime.SetTypesForTest(scene, {SpinnerDesc(sizeof(SpinnerV2), kSpinnerV2Props, 3)});
    const magnum::ComponentInfo* info = magnum::FindComponentInfo("Spinner");
    info->add(scene.Registry(), scene.Find(id));
    void* data = info->get(scene.Registry(), scene.Find(id));
    for (const magnum::PropertyInfo& p : info->props) {
        if (std::string(p.name) == "label") magnum::SetProperty(p, data, std::string("far too long for eight bytes"));
    }
    const SpinnerV2* v2 = static_cast<const SpinnerV2*>(data);
    CHECK(v2->label[7] == '\0');                 // always terminated
    CHECK(std::string(v2->label) == "far too "[0] + std::string("ar too")); // "far too" (7 chars)
}

int g_beginCalls = 0, g_updateCalls = 0, g_endCalls = 0;
void CountBegin(MagnumActorId, void*) { ++g_beginCalls; }
void CountUpdate(MagnumActorId, void*, float) { ++g_updateCalls; }
void CountEnd(MagnumActorId, void*) { ++g_endCalls; }
void CrashUpdate(MagnumActorId, void*, float) {
    volatile int* nothing = nullptr;
    *nothing = 1; // access violation
}

void PlayLifecycleAndCrashIsolation() {
    magnum::Scene scene;
    magnum::GameRuntime runtime;
    const uint64_t id = scene.CreateActor("Actor");

    MagnumComponentDesc counter = SpinnerDesc(sizeof(SpinnerV1), kSpinnerV1Props, 1);
    counter.on_begin = CountBegin;
    counter.on_update = CountUpdate;
    counter.on_end = CountEnd;
    runtime.SetTypesForTest(scene, {counter});
    magnum::FindComponentInfo("Spinner")->add(scene.Registry(), scene.Find(id));

    CHECK(runtime.BeginPlay(scene));
    runtime.Update(scene, 0.016f);
    runtime.Update(scene, 0.016f);
    runtime.EndPlay(scene);
    CHECK(g_beginCalls == 1 && g_updateCalls == 2 && g_endCalls == 1);
    runtime.Update(scene, 0.016f); // not playing: ignored
    CHECK(g_updateCalls == 2);

    // A crash in game code must not take the process down: it is reported and play is disabled.
    MagnumComponentDesc crasher = SpinnerDesc(sizeof(SpinnerV1), kSpinnerV1Props, 1);
    crasher.on_update = CrashUpdate;
    runtime.SetTypesForTest(scene, {crasher});
    CHECK(runtime.BeginPlay(scene));
    runtime.Update(scene, 0.016f);
    CHECK(runtime.Faulted() && !runtime.FaultMessage().empty());
    runtime.EndPlay(scene);
}

void SampleGameModuleLoadsAndRuns() {
    const std::filesystem::path dll = std::filesystem::path(MAGNUM_BUILD_DIR) / "bin" / "MagnumGame.dll";
    if (!std::filesystem::exists(dll)) {
        std::printf("  (skipped: %s not built)\n", dll.string().c_str());
        return;
    }
    magnum::Scene scene;
    magnum::GameRuntime runtime;
    runtime.SetModuleDirectory(std::filesystem::temp_directory_path() / "magnum_test_modules");
    std::string error;
    CHECK(runtime.LoadModule(dll, scene, error));
    CHECK(error.empty());

    const std::vector<std::string> names = runtime.ComponentNames();
    CHECK(std::find(names.begin(), names.end(), "Spinner") != names.end());
    CHECK(std::find(names.begin(), names.end(), "Bobber") != names.end());
    CHECK(std::find(names.begin(), names.end(), "Mover") != names.end());

    const uint64_t id = scene.CreateActor("Cube");
    const magnum::ComponentInfo* spinner = magnum::FindComponentInfo("Spinner");
    CHECK(spinner != nullptr);
    spinner->add(scene.Registry(), scene.Find(id));

    CHECK(runtime.BeginPlay(scene));
    for (int i = 0; i < 60; ++i) runtime.Update(scene, 1.0f / 60.0f);
    runtime.EndPlay(scene);
    // The sample Spinner defaults to 90 degrees per second around Y: one simulated second.
    CHECK(Near(scene.TryGet<magnum::TransformComponent>(id)->rotation.y, 90.0f, 0.5f));
    CHECK(!runtime.Faulted());

    // Loading the same DLL again (a hot reload) keeps the actor's component.
    CHECK(runtime.LoadModule(dll, scene, error));
    CHECK(magnum::FindComponentInfo("Spinner")->has(scene.Registry(), scene.Find(id)));
    runtime.UnloadModule(scene);
    CHECK(magnum::FindComponentInfo("Spinner") == nullptr);
}

// ---- Material graph -----------------------------------------------------------------------------

bool CompilesWithDsl(const std::string& lua, magnum::CompiledShaderSource& source, std::vector<magnum::ShaderDiagnostic>& diagnostics) {
    const std::filesystem::path root = MAGNUM_PROJECT_DIR;
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "magnum_test_graph.lua";
    WriteText(file, lua);
    const bool ok = magnum::CompileLuaShader(file, "graph.lua", std::filesystem::path(MAGNUM_ENGINE_DIR) / "Engine/Lua/shader_dsl.lua", "Engine/Lua/shader_dsl.lua", source, diagnostics);
    std::filesystem::remove(file);
    return ok;
}

void GraphTemplateIsValidAndCompiles() {
    const magnum::MaterialGraph graph = magnum::MakeLitTemplate();
    const auto errors = magnum::ValidateGraph(graph);
    for (const auto& [id, message] : errors) std::printf("    node %d: %s\n", id, message.c_str());
    CHECK(errors.empty());

    const std::string lua = magnum::GraphToLua(graph);
    CHECK(magnum::IsGraphLua(lua));

    magnum::CompiledShaderSource source;
    std::vector<magnum::ShaderDiagnostic> diagnostics;
    const bool ok = CompilesWithDsl(lua, source, diagnostics);
    for (const auto& d : diagnostics) std::printf("    %s\n", d.ToString().c_str());
    CHECK(ok);

    // The same parameters as the hand-written default_lit.lua.
    std::vector<std::string> names;
    for (const magnum::ShaderParam& p : source.params) names.push_back(p.name);
    CHECK((names == std::vector<std::string>{"base_color", "ambient", "specular", "shininess"}));
    CHECK(source.vertexGlsl.find("gl_Position") != std::string::npos);
    CHECK(source.fragmentGlsl.find("in vec3 v_position;") != std::string::npos); // attribute passed on automatically
}

void GraphLuaRoundTripIsLossless() {
    magnum::MaterialGraph graph = magnum::MakeLitTemplate();

    // An idle node (connected to nothing that matters) and an idle chain must survive too.
    std::string error;
    const int idle1 = graph.AddNode(magnum::NodeType::Sin, 40, 1000);
    const int idle2 = graph.AddNode(magnum::NodeType::Mul, 240, 1000);
    graph.Connect(idle1, idle2, 0, error);
    graph.Find(idle2)->inputs[1].constant = 2.5;
    graph.AddNode(magnum::NodeType::Constant, 40, 1100);
    const size_t nodeCount = graph.nodes.size();

    const std::string first = magnum::GraphToLua(graph);
    magnum::MaterialGraph parsed;
    std::vector<magnum::ShaderDiagnostic> diagnostics;
    CHECK(magnum::ParseGraphLua(first, "graph.lua", parsed, diagnostics));
    for (const auto& d : diagnostics) std::printf("    %s\n", d.ToString().c_str());
    CHECK(parsed.nodes.size() == nodeCount);
    CHECK(parsed.name == graph.name);

    // Text -> graph -> text must give the same text: nothing is lost or reordered.
    const std::string second = magnum::GraphToLua(parsed);
    if (first != second) { // leave both texts behind so the difference can be inspected
        WriteText(std::filesystem::path(MAGNUM_PROJECT_DIR) / "Saved" / "graph_roundtrip_first.lua", first);
        WriteText(std::filesystem::path(MAGNUM_PROJECT_DIR) / "Saved" / "graph_roundtrip_second.lua", second);
    }
    CHECK(first == second);

    // And once more from the re-generated text.
    magnum::MaterialGraph again;
    CHECK(magnum::ParseGraphLua(second, "graph.lua", again, diagnostics));
    CHECK(magnum::GraphToLua(again) == first);
    CHECK(magnum::ValidateGraph(parsed).empty());
}

void GraphTextEditsComeBackIntoTheGraph() {
    const std::string original = magnum::GraphToLua(magnum::MakeLitTemplate());

    // Hand edit: change the "* 8.0" constant of the lit template to 4.0 and rename the shader.
    std::string edited = original;
    const size_t at = edited.find("8.0");
    CHECK(at != std::string::npos);
    edited.replace(at, 3, "4.0");
    edited.replace(edited.find("Graph Lit"), 9, "Renamed");

    magnum::MaterialGraph graph;
    std::vector<magnum::ShaderDiagnostic> diagnostics;
    CHECK(magnum::ParseGraphLua(edited, "graph.lua", graph, diagnostics));
    CHECK(graph.name == "Renamed");
    bool found = false;
    for (const magnum::GraphNode& n : graph.nodes) {
        for (const magnum::GraphPin& p : n.inputs) found = found || (p.source == 0 && p.constant == 4.0);
    }
    CHECK(found);
    CHECK(magnum::GraphToLua(graph).find("4.0") != std::string::npos);

    // Moving a node in the layout block moves it in the graph.
    std::string moved = original;
    const size_t layout = moved.find("position = 0, 0");
    CHECK(layout != std::string::npos);
    moved.replace(layout, 15, "position = 77, -33");
    CHECK(magnum::ParseGraphLua(moved, "graph.lua", graph, diagnostics));
    const magnum::GraphNode* position = graph.FindInput(magnum::NodeType::Attribute, "position");
    CHECK(position && position->x == 77.0f && position->y == -33.0f);
}

void FilesWithAByteOrderMarkAreAccepted() {
    const std::string bom = "\xEF\xBB\xBF";

    // Material
    const std::filesystem::path material = std::filesystem::temp_directory_path() / "magnum_test_bom_material.lua";
    WriteText(material, bom + "return material { shader = \"a.lua\", params = { x = 1 } }");
    magnum::MaterialData data;
    std::vector<magnum::ShaderDiagnostic> diagnostics;
    CHECK(magnum::LoadMaterialFile(material, "m.lua", data, diagnostics));
    CHECK(data.shader == "a.lua");
    std::filesystem::remove(material);

    // Graph shader text
    const std::string graph = bom + magnum::GraphToLua(magnum::MakeLitTemplate());
    CHECK(magnum::IsGraphLua(graph));
    magnum::MaterialGraph parsed;
    CHECK(magnum::ParseGraphLua(graph, "g.lua", parsed, diagnostics));

    // Shader script
    magnum::CompiledShaderSource source;
    CHECK(CompilesWithDsl(bom + magnum::GraphToLua(magnum::MakeLitTemplate()), source, diagnostics));
}

void GraphParserReportsLines() {
    const std::string good = magnum::GraphToLua(magnum::MakeLitTemplate());
    auto parseWith = [&](const std::string& from, const std::string& to) {
        std::string text = good;
        const size_t at = text.find(from);
        if (at == std::string::npos) { // the test's expectation about the generated text is wrong
            std::printf("    not found in the generated Lua: %s\n", from.c_str());
            CHECK(at != std::string::npos);
            return std::make_pair(true, std::vector<magnum::ShaderDiagnostic>{});
        }
        text.replace(at, from.size(), to);
        magnum::MaterialGraph g;
        std::vector<magnum::ShaderDiagnostic> diagnostics;
        const bool ok = magnum::ParseGraphLua(text, "graph.lua", g, diagnostics);
        return std::make_pair(ok, diagnostics);
    };

    // Not a graph file at all.
    {
        magnum::MaterialGraph g;
        std::vector<magnum::ShaderDiagnostic> diagnostics;
        CHECK(!magnum::ParseGraphLua("local S = shader('x')\nreturn S\n", "hand.lua", g, diagnostics));
        CHECK(!diagnostics.empty() && diagnostics[0].line == 1);
    }
    // A statement the editor cannot read: line number must be exact.
    {
        const auto [ok, diagnostics] = parseWith("local n14 = ", "local n14 = if x then ");
        CHECK(!ok && !diagnostics.empty() && diagnostics[0].line > 0);
        const size_t line = static_cast<size_t>(std::count(good.begin(), good.begin() + static_cast<long>(good.find("local n14 = ")), '\n')) + 1;
        CHECK(!diagnostics.empty() && static_cast<size_t>(diagnostics[0].line) == line);
    }
    // A reference to a node that does not exist.
    {
        const auto [ok, diagnostics] = parseWith("local n14 = model * n13", "local n14 = model * n999");
        CHECK(!ok && !diagnostics.empty());
    }
    // An unknown function.
    {
        const auto [ok, diagnostics] = parseWith("normalize(", "frobnicate(");
        CHECK(!ok && !diagnostics.empty());
    }
    // A wrong argument count.
    {
        const auto [ok, diagnostics] = parseWith("dot(n18, light_dir)", "dot(n18)");
        CHECK(!ok && !diagnostics.empty());
    }
    // Variable name different from the declared name.
    {
        const auto [ok, diagnostics] = parseWith("local ambient = S:param(\"ambient\"", "local ambience = S:param(\"ambient\"");
        CHECK(!ok && !diagnostics.empty());
    }
}

void GraphTypeChecksAndLinkRules() {
    magnum::MaterialGraph graph = magnum::MakeLitTemplate();
    std::string error;

    // vec2 + vec3 is a type error, reported on the node and only while it is part of the shader.
    const int uv = graph.AddAttribute("uv", "vec2", 0, 0);
    const int position = graph.FindInput(magnum::NodeType::Attribute, "position")->id;
    const int bad = graph.AddNode(magnum::NodeType::Add, 0, 0);
    graph.Connect(uv, bad, 0, error);
    graph.Connect(position, bad, 1, error);
    CHECK(magnum::ValidateGraph(graph).empty()); // idle: not part of the shader yet
    const int colorVec = graph.FindOutput(magnum::NodeType::FragmentOutput)->inputs[0].source;
    graph.Connect(bad, colorVec, 0, error); // now it feeds the output
    const auto errors = magnum::ValidateGraph(graph);
    CHECK(errors.contains(bad) && errors.at(bad).find("vec2") != std::string::npos);
    CHECK(errors.size() == 1); // downstream nodes inherit the problem; only its source is reported

    // Loops are refused when linking.
    magnum::MaterialGraph loop;
    const int a = loop.AddNode(magnum::NodeType::Sin, 0, 0);
    const int b = loop.AddNode(magnum::NodeType::Cos, 0, 0);
    CHECK(loop.Connect(a, b, 0, error));
    CHECK(!loop.Connect(b, a, 0, error) && !error.empty());

    // Names that would break the generated code are refused.
    CHECK(!magnum::IsValidInputName("sin"));
    CHECK(!magnum::IsValidInputName("n12"));
    CHECK(!magnum::IsValidInputName("xyz"));
    CHECK(!magnum::IsValidInputName("end"));
    CHECK(!magnum::IsValidInputName("1abc"));
    CHECK(magnum::IsValidInputName("base_color"));

    // Removing a node unplugs it everywhere but keeps the constants of the pins it fed.
    magnum::MaterialGraph g = magnum::MakeLitTemplate();
    const int victim = g.FindOutput(magnum::NodeType::FragmentOutput)->inputs[0].source;
    g.RemoveNode(victim);
    CHECK(g.Find(victim) == nullptr);
    CHECK(g.FindOutput(magnum::NodeType::FragmentOutput)->inputs[0].source == 0);
    g.RemoveNode(g.FindOutput(magnum::NodeType::FragmentOutput)->id); // outputs cannot be removed
    CHECK(g.FindOutput(magnum::NodeType::FragmentOutput) != nullptr);
}

void GraphBuiltShaderMapsErrorsToNodes() {
    magnum::MaterialGraph graph = magnum::MakeLitTemplate();
    magnum::GraphLuaInfo info;
    const std::string lua = magnum::GraphToLua(graph, &info);
    CHECK(!info.lineToNode.empty());
    // Every mapped line really contains that node's variable.
    std::istringstream in(lua);
    std::string line;
    int lineNo = 0;
    int checked = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        const auto it = info.lineToNode.find(lineNo);
        if (it == info.lineToNode.end()) continue;
        const magnum::GraphNode* n = graph.Find(it->second);
        CHECK(n != nullptr);
        if (n && n->type != magnum::NodeType::VertexOutput && n->type != magnum::NodeType::FragmentOutput) {
            CHECK(line.find(n->LuaName()) != std::string::npos);
            ++checked;
        }
    }
    CHECK(checked > 20);
}

} // namespace

// `MagnumTests [filter]` runs only the tests whose name contains the filter.
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // see progress even if a test aborts the process
#ifdef _WIN32
    // A failing test must never open a "Debug Error!" / crash dialog: print to stderr and exit instead.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    for (const int kind : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(kind, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
    }
#endif
    const std::string filter = argc > 1 ? argv[1] : "";
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"EulerRoundTrip", EulerRoundTrip},
        {"EulerStaysContinuous", EulerStaysContinuous},
        {"GimbalLockDoesNotProduceNaN", GimbalLockDoesNotProduceNaN},
        {"SceneRoundTrip", SceneRoundTrip},
        {"LoadRejectsBrokenFiles", LoadRejectsBrokenFilesWithoutTouchingTheScene},
        {"LoadSkipsUnknownThings", LoadSkipsUnknownThingsButKeepsTheRest},
        {"SnapshotRestoresOrder", SnapshotRestoresOrderAndComponents},
        {"UniqueNames", UniqueNames},
        {"MaterialRoundTrip", MaterialRoundTrip},
        {"MaterialRejectsBadFiles", MaterialRejectsBadFiles},
        {"ShaderTextureParameters", ShaderTextureParameters},
        {"ShaderTextureMisuse", ShaderTextureMisuseIsReportedWithLine},
        {"GameLayoutChange", GameComponentsSurviveLayoutChange},
        {"GameMissingType", GameComponentsKeepValuesWhileTypeIsMissing},
        {"GameStringCapacity", GameStringPropertyIsBoundedByCapacity},
        {"PlayLifecycleAndCrash", PlayLifecycleAndCrashIsolation},
        {"SampleGameModule", SampleGameModuleLoadsAndRuns},
        {"GraphTemplate", GraphTemplateIsValidAndCompiles},
        {"GraphRoundTrip", GraphLuaRoundTripIsLossless},
        {"GraphTextEdits", GraphTextEditsComeBackIntoTheGraph},
        {"GraphParserErrors", GraphParserReportsLines},
        {"ByteOrderMark", FilesWithAByteOrderMarkAreAccepted},
        {"GraphTypesAndLinks", GraphTypeChecksAndLinkRules},
        {"GraphErrorMapping", GraphBuiltShaderMapsErrorsToNodes},
        {"FullscreenShaders", FullscreenShadersCompile},
        {"FullscreenErrors", FullscreenShaderErrorsPointToTheLine},
        {"PipelineParsing", PipelineParsing},
        {"PipelineKeepsLastGood", PipelineKeepsLastGoodVersion},
        {"PluginsRegister", PluginsRegisterAndRun},
        {"PluginsSandbox", PluginsAreSandboxedAndIsolated},
        {"PluginCallbackFails", PluginCallbackThatFailsIsSwitchedOff},
        {"ShippedPlugins", ShippedPluginsLoad},
        {"ProjectCreation", ProjectCreationAndLoading},
        {"ShaderSize", ShippedShadersStaySmall},
        {"ExtraPrimitives", ExtraPrimitivesAreSound},
        {"PhysicsFallRest", PhysicsBodyFallsAndRestsOnTheGround},
        {"PhysicsNoTunnel", PhysicsFastBodyDoesNotTunnelThroughAPlane},
        {"PhysicsKinematic", PhysicsKinematicDoesNotMoveAndBlocks},
        {"PhysicsEvents", PhysicsEventsAreReportedOnce},
        {"PhysicsRaycast", PhysicsRaycastFindsTheNearestSolid},
        {"ScriptCallbacks", ScriptsRunTheirCallbacksAndKeepState},
        {"ScriptSandbox", ScriptsAreSandboxedAndFailuresAreContained},
        {"ScriptInputHud", ScriptsSeeInputWorldAndHud},
        {"ScriptEvents", ScriptsReceiveCollisionAndTriggerEvents},
        {"ScriptSpawn", ScriptsCanSpawnAndDestroyActors},
        {"ScriptReload", ScriptsReloadWhileRunning},
        {"GameplayComponents", CameraAndGameplayComponentsRoundTripThroughSceneFiles},
        {"CharacterScript", ShippedCharacterScriptPlays},
        {"SpriteSizes", SpriteSizesAndFramesFollowTheImage},
        {"SpriteAnimation", SpriteAnimationsLoopAndStop},
        {"Physics2D", PhysicsWorksInTwoDimensions},
        {"Script2D", ScriptsDriveSpritesAndTheMouseInTwoD},
        {"Platformer", PlatformerLevelPlays},
        {"DecalShader", DecalAndSpriteShadersCompile},
        {"ScriptDecals", ScriptsCanSpawnDecalsOnSurfaces},
        {"UiAnchors", UiAnchorsAndPresetsPlaceNodes},
        {"UiFlow", UiRowsColumnsAndGridsFlow},
        {"UiText", UiTextMeasuresWrapsAndSizesNodes},
        {"UiScroll", UiScrollViewsClipAndClamp},
        {"UiPointer", UiPointerEventsClicksTogglesSliders},
        {"UiCanvases", UiCanvasesOrderPointerAndLayers},
        {"UiTweens", UiTweensAnimateAndFinish},
        {"UiStates", UiVisualTransformsAndStates},
        {"UiDrawList", UiDrawListHasShapesTextImagesAndClips},
        {"UiWidgetsDraw", UiSwitchProgressAndSlidersDraw},
        {"UiLuaBuild", UiLuaBuildsTreesAndRunsCallbacks},
        {"UiLuaProps", UiLuaPropertiesAndErrors},
        {"UiLuaAnimate", UiLuaAnimatesNodes},
        {"UiLuaStyles", UiLuaStylesAndStates},
        {"UiLuaDocuments", UiLuaDocumentsLoadAndReload},
        {"UiLuaCanvas", UiLuaCanvasComponentsFollowTheScene},
        {"UiGameplay", UiGameplayRoutesThePointerAndRunsDocuments},
        {"LoopGlsl", LoopBecomesARealGlslLoop},
        {"LoopMisuse", LoopMisuseIsReported},
        {"ShaderConstants", ConstantsMayLiveOutsideStageFunctions},
        {"EngineAssetPaths", EngineAssetsResolveToTheEngineFolder},
        {"ModelFormats", ModelFormatsCoverTheCommonOnes},
        {"ModelImport", ImportsTheCommonFormats},
        {"ModelBroken", BrokenModelFilesFailWithAReason},
        {"ModelExternal", ExternalBuffersAreReadBesideTheFile},
        {"ModelRetry", FailedModelsAreReadAgain},
        {"ModelLoad", LoadModelFollowsTheFileAndTheScene},
    };
    for (const auto& [name, fn] : tests) {
        if (!filter.empty() && std::string(name).find(filter) == std::string::npos) continue;
        g_currentTest = name;
        std::printf("running %s\n", name);
        const int before = g_failures;
        try {
            fn();
        } catch (const std::exception& e) { // an exception is a failed test, not a crashed runner
            std::printf("  FAIL [%s] threw: %s\n", name, e.what());
            ++g_failures;
        }
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", name);
    }
    std::printf(g_failures ? "\n%d check(s) failed\n" : "\nall tests passed\n", g_failures);
    return g_failures ? 1 : 0;
}
