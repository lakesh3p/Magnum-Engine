// ---- Models -------------------------------------------------------------------------------------

namespace {

// A folder of its own for the files these tests write.
std::filesystem::path ModelTestFolder() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "magnum_model_tests";
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path WriteModelFile(const std::string& name, const std::string& bytes) {
    const std::filesystem::path file = ModelTestFolder() / name;
    std::ofstream(file, std::ios::binary) << bytes;
    return file;
}

// The same triangle, (0,0,0) (1,0,0) (0,1,0), in each text format.
const char* const kTriangleObj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1/1 2/2/1 3/3/1\n";
const char* const kTriangleStl =
    "solid t\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid t\n";
const char* const kTrianglePly =
    "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
    "element face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n";
const char* const kTriangleOff = "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n";
const char* const kBigTriangleObj = "v 0 0 0\nv 2 0 0\nv 0 2 0\nf 1 2 3\n";

// The triangle's binary data: three float positions, then three uint16 indices (42 bytes).
std::string TriangleBuffer() {
    const float positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const uint16_t indices[3] = {0, 1, 2};
    std::string bytes(sizeof(positions) + sizeof(indices), '\0');
    std::memcpy(bytes.data(), positions, sizeof(positions));
    std::memcpy(bytes.data() + sizeof(positions), indices, sizeof(indices));
    return bytes;
}

std::string Base64(const std::string& in) {
    static const char* const chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < in.size(); i += 3) {
        const size_t rest = in.size() - i;
        const uint32_t n = (static_cast<uint32_t>(static_cast<uint8_t>(in[i])) << 16) |
                           (rest > 1 ? static_cast<uint32_t>(static_cast<uint8_t>(in[i + 1])) << 8 : 0) |
                           (rest > 2 ? static_cast<uint32_t>(static_cast<uint8_t>(in[i + 2])) : 0);
        out += chars[(n >> 18) & 63];
        out += chars[(n >> 12) & 63];
        out += rest > 1 ? chars[(n >> 6) & 63] : '=';
        out += rest > 2 ? chars[n & 63] : '=';
    }
    return out;
}

// The triangle as a glTF document. An empty uri leaves the buffer's uri out (a GLB keeps its data in a chunk).
std::string TriangleGltf(const std::string& uri) {
    const std::string uriField = uri.empty() ? "" : ",\"uri\":\"" + uri + "\"";
    return "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
           "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
           "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
           "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
           "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
           "\"buffers\":[{\"byteLength\":42" + uriField + "}]}";
}

std::string Le32(uint32_t value) {
    std::string bytes(4, '\0');
    std::memcpy(bytes.data(), &value, 4);
    return bytes;
}

// The triangle as a binary glTF: a 12-byte header, then a JSON chunk and a BIN chunk, each padded to 4 bytes.
std::string TriangleGlb() {
    std::string json = TriangleGltf("");
    json += std::string((4 - json.size() % 4) % 4, ' ');
    std::string bin = TriangleBuffer();
    bin += std::string((4 - bin.size() % 4) % 4, '\0');
    const uint32_t total = static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin.size());
    return "glTF" + Le32(2) + Le32(total) + Le32(static_cast<uint32_t>(json.size())) + "JSON" + json +
           Le32(static_cast<uint32_t>(bin.size())) + std::string("BIN\0", 4) + bin;
}

} // namespace

void ModelFormatsCoverTheCommonOnes() {
    const auto& formats = magnum::ModelFormats();
    for (const char* ext : {".obj", ".fbx", ".glb", ".gltf", ".dae", ".stl", ".ply", ".3ds", ".off"}) {
        CHECK(std::find(formats.begin(), formats.end(), ext) != formats.end());
    }
    CHECK(formats.size() >= 20);
    CHECK(magnum::IsModelFile("Content/Models/Car.FBX")); // the extension is case-insensitive
    CHECK(!magnum::IsModelFile("Content/Textures/tiles.png"));
    CHECK(!magnum::IsModelFile("Content/Scripts/player.lua"));
}

void ImportsTheCommonFormats() {
    struct Case {
        const char* name;
        std::string bytes;
    };
    const Case cases[] = {
        {"triangle.obj", kTriangleObj},
        {"triangle.stl", kTriangleStl},
        {"triangle.ply", kTrianglePly},
        {"triangle.off", kTriangleOff},
        {"triangle.gltf", TriangleGltf("data:application/octet-stream;base64," + Base64(TriangleBuffer()))},
        {"triangle.glb", TriangleGlb()},
    };
    for (const Case& c : cases) {
        std::string error;
        std::shared_ptr<const magnum::ImportedModel> model = magnum::ImportModel(WriteModelFile(c.name, c.bytes), error);
        CHECK(model && error.empty());
        if (!model) continue;
        CHECK(model->meshes.size() == 1);
        CHECK(model->meshes[0].indices.size() == 3);
        CHECK(model->meshes[0].vertices.size() == 3 * 8); // position, normal, uv per vertex
        CHECK(Near(model->Size().x, 1.0f) && Near(model->Size().y, 1.0f));
    }
}

void BrokenModelFilesFailWithAReason() {
    std::string error;
    CHECK(!magnum::ImportModel(WriteModelFile("broken.fbx", "not a model at all"), error));
    CHECK(!error.empty());
    error.clear();
    CHECK(!magnum::ImportModel(ModelTestFolder() / "does_not_exist.glb", error));
    CHECK(!error.empty());
    error.clear();
    CHECK(!magnum::ImportModel(WriteModelFile("empty.obj", "# nothing in here\n"), error));
    CHECK(!error.empty());
}

void ExternalBuffersAreReadBesideTheFile() {
    // A .gltf that keeps its data in a separate .bin, as most exporters write it.
    WriteModelFile("external_buffer.bin", TriangleBuffer());
    std::string error;
    std::shared_ptr<const magnum::ImportedModel> model =
        magnum::ImportModel(WriteModelFile("external_buffer.gltf", TriangleGltf("external_buffer.bin")), error);
    CHECK(model && error.empty());
    if (model) CHECK(model->meshes.size() == 1 && model->meshes[0].indices.size() == 3);
}

void FailedModelsAreReadAgain() {
    const std::filesystem::path project = ModelTestFolder() / "retry_project";
    std::filesystem::remove_all(project); // a leftover .bin from an earlier run would make the first load succeed
    std::filesystem::create_directories(project / "Content" / "Models");
    magnum::SetProjectRoot(project);

    // The .gltf arrives before its .bin: the first load fails, and the next load after the retry time succeeds.
    std::ofstream(project / "Content" / "Models" / "late.gltf") << TriangleGltf("late.bin");
    CHECK(!magnum::LoadModel("Content/Models/late.gltf"));
    std::ofstream(project / "Content" / "Models" / "late.bin", std::ios::binary) << TriangleBuffer();
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    CHECK(magnum::LoadModel("Content/Models/late.gltf"));

    magnum::SetProjectRoot(MAGNUM_PROJECT_DIR);
}

void LoadModelFollowsTheFileAndTheScene() {
    const std::filesystem::path project = ModelTestFolder() / "project";
    std::filesystem::create_directories(project / "Content" / "Models");
    magnum::SetProjectRoot(project);
    const std::filesystem::path file = project / "Content" / "Models" / "tri.obj";
    std::ofstream(file) << kTriangleObj;

    std::shared_ptr<const magnum::ImportedModel> first = magnum::LoadModel("Content/Models/tri.obj");
    CHECK(first && first->meshes.size() == 1);
    CHECK(magnum::LoadModel("Content/Models/tri.obj") == first); // kept: the same object again
    CHECK(!magnum::LoadModel("Content/Models/missing.obj"));
    CHECK(!magnum::LoadModel(""));

    // An actor with a model is as big as the model, so its collider and selection box follow the file.
    magnum::Scene scene;
    const uint64_t id = scene.CreateActor("Crate");
    magnum::MeshComponent mesh;
    mesh.model = "Content/Models/tri.obj";
    scene.Registry().emplace<magnum::MeshComponent>(scene.Find(id), mesh);
    CHECK(Near(scene.LocalHalfExtents(id).x, 0.5f) && Near(scene.LocalHalfExtents(id).y, 0.5f));

    // Change the file: the next load reads it again, and the actor follows.
    std::ofstream(file) << kBigTriangleObj;
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(2));
    std::shared_ptr<const magnum::ImportedModel> second = magnum::LoadModel("Content/Models/tri.obj");
    CHECK(second && second != first);
    CHECK(Near(scene.LocalHalfExtents(id).x, 1.0f) && Near(scene.LocalHalfExtents(id).y, 1.0f));

    // The model path is saved with the scene like any other property, and comes back on load.
    std::string text, error;
    magnum::SceneEditorState editorState;
    CHECK(magnum::SerializeScene(scene, editorState, text, error));
    CHECK(text.find("\"Content/Models/tri.obj\"") != std::string::npos);
    magnum::Scene loaded;
    CHECK(magnum::DeserializeScene(loaded, editorState, text, error));
    int models = 0;
    for (auto [e, m] : loaded.Registry().view<magnum::MeshComponent>().each()) {
        CHECK(m.model == "Content/Models/tri.obj");
        ++models;
    }
    CHECK(models == 1);

    magnum::SetProjectRoot(MAGNUM_PROJECT_DIR);
}
