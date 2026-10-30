#include "scene/SceneSerializer.h"

#include "core/Log.h"
#include "scene/GameComponents.h"
#include "scene/Reflection.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace magnum {

namespace {

using json = nlohmann::ordered_json; // keeps keys in declaration order, so files read top-down
constexpr int kSceneVersion = 1;

// Floats are written with 7 significant digits so files show 0.3, not 0.30000001192092896.
double Clean(float f) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.7g", static_cast<double>(f));
    return std::strtod(buffer, nullptr);
}

json VecToJson(const glm::vec3& v) {
    return json::array({Clean(v.x), Clean(v.y), Clean(v.z)});
}

json ValueToJson(const PropertyInfo& p, const PropertyValue& v) {
    switch (p.kind) {
    case PropKind::Bool: return std::get<bool>(v);
    case PropKind::Int: return std::get<int>(v);
    case PropKind::Float: return Clean(std::get<float>(v));
    case PropKind::Vec3:
    case PropKind::Color3: return VecToJson(std::get<glm::vec3>(v));
    case PropKind::String:
    case PropKind::CString: return std::get<std::string>(v);
    case PropKind::Enum: {
        const int i = std::get<int>(v);
        return i >= 0 && static_cast<size_t>(i) < p.options.size() ? json(p.options[static_cast<size_t>(i)]) : json(i);
    }
    }
    return nullptr;
}

bool JsonToValue(const PropertyInfo& p, const json& j, PropertyValue& out) {
    switch (p.kind) {
    case PropKind::Bool:
        if (!j.is_boolean()) return false;
        out = j.get<bool>();
        return true;
    case PropKind::Int:
        if (!j.is_number_integer()) return false;
        out = j.get<int>();
        return true;
    case PropKind::Float:
        if (!j.is_number()) return false;
        out = j.get<float>();
        return true;
    case PropKind::Vec3:
    case PropKind::Color3:
        if (!j.is_array() || j.size() != 3 || !j[0].is_number() || !j[1].is_number() || !j[2].is_number()) return false;
        out = glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
        return true;
    case PropKind::String:
    case PropKind::CString:
        if (!j.is_string()) return false;
        out = j.get<std::string>();
        return true;
    case PropKind::Enum:
        if (j.is_string()) {
            const std::string name = j.get<std::string>();
            for (size_t i = 0; i < p.options.size(); ++i) {
                if (name == p.options[i]) {
                    out = static_cast<int>(i);
                    return true;
                }
            }
            return false;
        }
        if (j.is_number_integer()) {
            out = j.get<int>();
            return true;
        }
        return false;
    }
    return false;
}

// nlohmann puts every array element on its own line. Vectors read much better as [x, y, z],
// so collapse arrays that contain only numbers back onto one line.
std::string CollapseNumberArrays(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '[') {
            const size_t close = text.find(']', i);
            const std::string body = close == std::string::npos ? std::string() : text.substr(i + 1, close - i - 1);
            const bool numeric = !body.empty() && body.find_first_not_of("0123456789.eE+-, \n\r\t") == std::string::npos;
            if (numeric) {
                out += '[';
                bool pendingSpace = false;
                for (const char c : body) {
                    if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
                        pendingSpace = true;
                    } else {
                        if (pendingSpace && out.back() == ',') out += ' ';
                        pendingSpace = false;
                        out += c;
                    }
                }
                out += ']';
                i = close + 1;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

std::string IdToString(uint64_t id) {
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(id));
    return buffer;
}

bool StringToId(const std::string& s, uint64_t& id) {
    if (s.empty() || s.size() > 16) return false;
    char* end = nullptr;
    id = std::strtoull(s.c_str(), &end, 16);
    return end && *end == '\0' && id != 0;
}

json PropsToJson(const ComponentInfo& info, const void* data) {
    json props = json::object();
    for (const PropertyInfo& p : info.props) {
        props[p.name] = ValueToJson(p, GetProperty(p, data));
    }
    return props;
}

// Applies a JSON object of property values to a component. Unknown/bad entries are skipped with a warning.
void ApplyJsonProps(const ComponentInfo& info, void* data, const json& props, const std::string& where) {
    if (!props.is_object()) return;
    for (const auto& [propName, value] : props.items()) {
        const PropertyInfo* prop = nullptr;
        for (const PropertyInfo& p : info.props) {
            if (propName == p.name) prop = &p;
        }
        PropertyValue parsed;
        if (!prop) {
            Log::Warn("{}: {} has no property '{}', skipped", where, info.name, propName);
        } else if (!JsonToValue(*prop, value, parsed)) {
            Log::Warn("{}: bad value for {}.{}, keeping the default", where, info.name, propName);
        } else {
            SetProperty(*prop, data, parsed);
        }
    }
}

} // namespace

std::string SerializeProperties(const ComponentInfo& info, const void* component) {
    return PropsToJson(info, component).dump();
}

bool ApplyProperties(const ComponentInfo& info, void* component, const std::string& jsonText, const std::string& where) {
    const json props = json::parse(jsonText, nullptr, false);
    if (props.is_discarded() || !props.is_object()) return false;
    ApplyJsonProps(info, component, props, where);
    return true;
}

bool SerializeScene(const Scene& scene, const SceneEditorState& editor, std::string& out, std::string& error) {
    (void)error;
    json root;
    root["magnumScene"] = kSceneVersion;
    if (editor.hasCamera) {
        root["editor"]["camera"] = {{"position", VecToJson(editor.cameraPosition)},
                                    {"yaw", Clean(editor.cameraYaw)},
                                    {"pitch", Clean(editor.cameraPitch)}};
    }

    json actors = json::array();
    for (const uint64_t id : scene.Order()) {
        const entt::entity e = scene.Find(id);
        json actor;
        actor["id"] = IdToString(id);
        json components = json::object();
        for (const ComponentInfo* info : AllComponentInfos()) {
            if (!info->has(scene.Registry(), e)) continue;
            const std::shared_ptr<void> copy = info->clone(scene.Registry(), e);
            components[info->name] = PropsToJson(*info, copy.get());
        }
        // Game components whose module is not loaded keep their saved values untouched.
        if (const auto* game = scene.Registry().try_get<GameComponentsComponent>(e)) {
            for (const GameInstance& item : game->items) {
                if (item.resolved) continue;
                json props = json::parse(item.pendingJson, nullptr, false);
                components[item.type] = props.is_object() ? props : json::object();
            }
        }
        actor["components"] = std::move(components);
        actors.push_back(std::move(actor));
    }
    root["actors"] = std::move(actors);

    out = CollapseNumberArrays(root.dump(2)) + "\n";
    return true;
}

bool DeserializeScene(Scene& scene, SceneEditorState& editor, const std::string& text, std::string& error) {
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        error = "not valid JSON";
        return false;
    }
    if (!root.contains("magnumScene") || !root["magnumScene"].is_number_integer()) {
        error = "missing 'magnumScene' version (is this a Magnum scene?)";
        return false;
    }
    if (root["magnumScene"].get<int>() > kSceneVersion) {
        error = "scene was saved by a newer Magnum (version " + std::to_string(root["magnumScene"].get<int>()) + ")";
        return false;
    }
    if (!root.contains("actors") || !root["actors"].is_array()) {
        error = "missing 'actors' list";
        return false;
    }

    Scene loaded;
    int index = 0;
    for (const json& actor : root["actors"]) {
        const std::string where = "actor #" + std::to_string(index++);
        uint64_t id = 0;
        if (!actor.is_object() || !actor.contains("id") || !actor["id"].is_string() ||
            !StringToId(actor["id"].get<std::string>(), id)) {
            error = where + " has no valid 'id'";
            return false;
        }
        if (loaded.Exists(id)) {
            error = where + " repeats id " + IdToString(id);
            return false;
        }
        loaded.CreateActor("Actor", id);
        const entt::entity e = loaded.Find(id);

        if (!actor.contains("components") || !actor["components"].is_object()) continue;
        for (const auto& [compName, props] : actor["components"].items()) {
            const ComponentInfo* info = FindComponentInfo(compName);
            if (!info) {
                // Not a built-in and not registered by the loaded game module (yet): keep the values as they are
                // so saving the scene does not lose them. They are bound when the type becomes available.
                GameInstance instance;
                instance.type = compName;
                instance.pendingJson = props.dump();
                loaded.Registry().get_or_emplace<GameComponentsComponent>(e).items.push_back(std::move(instance));
                continue;
            }
            if (!info->has(loaded.Registry(), e)) info->add(loaded.Registry(), e);
            ApplyJsonProps(*info, info->get(loaded.Registry(), e), props, where);
        }
    }

    editor = SceneEditorState{};
    if (root.contains("editor") && root["editor"].contains("camera")) {
        const json& cam = root["editor"]["camera"];
        PropertyInfo vec3Prop;
        vec3Prop.kind = PropKind::Vec3;
        PropertyValue pos;
        if (cam.contains("position") && JsonToValue(vec3Prop, cam["position"], pos) && cam.contains("yaw") &&
            cam.contains("pitch") && cam["yaw"].is_number() && cam["pitch"].is_number()) {
            editor.cameraPosition = std::get<glm::vec3>(pos);
            editor.cameraYaw = cam["yaw"].get<float>();
            editor.cameraPitch = cam["pitch"].get<float>();
            editor.hasCamera = true;
        }
    }

    scene = std::move(loaded);
    return true;
}

bool SaveScene(const Scene& scene, const SceneEditorState& editor, const std::filesystem::path& file,
               std::string& error) {
    std::string text;
    if (!SerializeScene(scene, editor, text, error)) return false;

    // Write next to the target and rename, so a crash never leaves a truncated scene.
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    const std::filesystem::path temp = file.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary);
        if (!out) {
            error = "cannot write " + temp.string();
            return false;
        }
        out << text;
    }
    std::filesystem::rename(temp, file, ec);
    if (ec) {
        error = "cannot replace " + file.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool LoadScene(Scene& scene, SceneEditorState& editor, const std::filesystem::path& file, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "cannot open " + file.string();
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return DeserializeScene(scene, editor, text.str(), error);
}

} // namespace magnum
