#include "panels/MaterialGraphPanel.h"

#include "commands/GraphCommands.h"
#include "Icons.h"
#include "Theme.h"
#include "core/Log.h"
#include "panels/PanelUtil.h"
#include "platform/FileOps.h"

#include <imgui.h>
#include <imnodes.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace magnum::editor {

namespace fs = std::filesystem;

namespace {

// ImNodes wants one integer id per pin. Inputs are node*16 + pin (pins 0-7); the output is node*16 + 15.
int InPin(int node, int pin) { return node * 16 + pin; }
int OutPin(int node) { return node * 16 + 15; }
int NodeOfPin(int attr) { return attr / 16; }
int SlotOfPin(int attr) { return attr % 16; }
bool IsOutPin(int attr) { return SlotOfPin(attr) == 15; }

std::string ReadText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    StripUtf8Bom(text);
    return text;
}

// Pin and wire colors by type. Unconnected inputs are grey.
ImU32 TypeColor(const std::string& type) {
    if (type == "float") return IM_COL32(0x9e, 0xc1, 0xf0, 255);
    if (type == "vec2") return IM_COL32(0x7b, 0xc4, 0x7f, 255);
    if (type == "vec3") return IM_COL32(0xf0, 0x8a, 0x3c, 255);
    if (type == "vec4") return IM_COL32(0xe9, 0xb4, 0x4c, 255);
    if (type == "mat3" || type == "mat4") return IM_COL32(0xb7, 0x8f, 0xdc, 255);
    if (type == "sampler2D") return IM_COL32(0xe0, 0x7a, 0xb0, 255);
    return IM_COL32(0xef, 0x6b, 0x6b, 255); // no type: an error upstream
}
constexpr ImU32 kPinOff = IM_COL32(0x6a, 0x6a, 0x6a, 255);

// Header tints: inputs blue-grey, math green-grey, the shader outputs brown.
ImU32 HeaderColor(NodeType type) {
    switch (type) {
    case NodeType::Attribute:
    case NodeType::Builtin:
    case NodeType::Param:
    case NodeType::Constant:
    case NodeType::Texture: return IM_COL32(0x2c, 0x3a, 0x4d, 255);
    case NodeType::VertexOutput:
    case NodeType::FragmentOutput: return IM_COL32(0x5a, 0x2f, 0x12, 255);
    default: return IM_COL32(0x2f, 0x3b, 0x33, 255);
    }
}
constexpr ImU32 kHeaderError = IM_COL32(0x8a, 0x28, 0x28, 255);

bool HasOutput(NodeType t) {
    return t != NodeType::VertexOutput && t != NodeType::FragmentOutput;
}

std::string NodeTitle(const GraphNode& n) {
    switch (n.type) {
    case NodeType::Attribute:
    case NodeType::Builtin:
    case NodeType::Param: return n.name;
    case NodeType::Vec: return "vec" + std::to_string(n.vecComponents);
    case NodeType::Swizzle: return "." + n.swizzle;
    default: return SpecFor(n.type).title;
    }
}

} // namespace

void MaterialGraphPanel::Init() {
    ImNodes::CreateContext();
    ImNodes::StyleColorsDark();
    ImNodesStyle& style = ImNodes::GetStyle();
    style.NodeCornerRounding = 8.0f;
    style.NodePadding = ImVec2(10.0f, 4.0f);
    style.NodeBorderThickness = 1.0f;
    style.LinkThickness = 2.5f;
    style.PinCircleRadius = 5.0f;
    style.GridSpacing = 22.0f;
    style.Flags |= ImNodesStyleFlags_GridLines;
    style.Colors[ImNodesCol_GridBackground] = IM_COL32(0x17, 0x17, 0x17, 255);
    style.Colors[ImNodesCol_GridLine] = IM_COL32(255, 255, 255, 10);
    style.Colors[ImNodesCol_GridLinePrimary] = IM_COL32(255, 255, 255, 16);
    style.Colors[ImNodesCol_NodeBackground] = IM_COL32(0x22, 0x21, 0x20, 255);
    style.Colors[ImNodesCol_NodeBackgroundHovered] = IM_COL32(0x27, 0x26, 0x25, 255);
    style.Colors[ImNodesCol_NodeBackgroundSelected] = IM_COL32(0x27, 0x26, 0x25, 255);
    style.Colors[ImNodesCol_NodeOutline] = IM_COL32(0x40, 0x40, 0x40, 255);
    style.Colors[ImNodesCol_BoxSelector] = IM_COL32(0x2b, 0x9d, 0xf7, 30);
    style.Colors[ImNodesCol_BoxSelectorOutline] = IM_COL32(0x2b, 0x9d, 0xf7, 150);
}

void MaterialGraphPanel::Shutdown() {
    ImNodes::DestroyContext();
}

// ---- Files ---------------------------------------------------------------------------------------

bool MaterialGraphPanel::Open(EditorContext& ctx, const std::string& shaderPath) {
    const fs::path file = ctx.projectRoot / shaderPath;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        Log::Error("Cannot open graph: {} does not exist", shaderPath);
        return false;
    }
    m_path = shaderPath;
    m_graph = MaterialGraph{};
    m_text.clear();
    m_parseErrors.clear();
    m_nodeErrors.clear();
    m_shaderErrors.clear();
    m_positionsApplied.clear();
    m_changed = false;
    Reload(ctx, true);
    if (!m_isGraph) Log::Warn("{} is not a graph shader (it does not start with '{}'); it is shown as code only", shaderPath, kGraphMarker);
    return true;
}

bool MaterialGraphPanel::CreateNew(EditorContext& ctx, const std::string& shaderPath) {
    const fs::path file = ctx.projectRoot / shaderPath;
    MaterialGraph graph = MakeLitTemplate();
    graph.name = fs::path(shaderPath).stem().string();
    const std::string text = GraphToLua(graph);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (!WriteTextFile(file, text)) {
        Log::Error("Cannot write {}", shaderPath);
        return false;
    }
    Log::Info("Created graph shader {}", shaderPath);
    return Open(ctx, shaderPath);
}

void MaterialGraphPanel::Reload(EditorContext& ctx, bool force) {
    if (m_path.empty()) return;
    const fs::path file = ctx.projectRoot / m_path;
    std::error_code ec;
    const auto stamp = fs::last_write_time(file, ec);
    if (ec) return;
    if (!force && stamp == m_stamp) return;
    m_stamp = stamp;

    const std::string text = ReadText(file);
    if (!force && text == m_text) return; // our own write
    m_text = text;

    m_isGraph = IsGraphLua(text);
    if (!m_isGraph) return;
    MaterialGraph parsed;
    std::vector<ShaderDiagnostic> diagnostics;
    if (ParseGraphLua(text, m_path, parsed, diagnostics)) {
        m_graph = std::move(parsed);
        m_parseErrors.clear();
        m_positionsApplied.clear();
        m_nodeErrors = ValidateGraph(m_graph);
        m_lineMap = GraphLuaInfo{};
        if (m_nodeErrors.empty()) GraphToLua(m_graph, &m_lineMap);
        m_changed = false;
        m_applied = true;
        Log::Info("Graph {} loaded ({} node(s))", m_path, m_graph.nodes.size());
    } else {
        // Hand edits the graph cannot read: keep showing the last good graph and say what is wrong.
        m_parseErrors = diagnostics;
        for (const auto& d : diagnostics) Log::Error("{}", d.ToString());
    }
}

void MaterialGraphPanel::Commit(EditorContext& ctx) {
    if (!m_changed || m_path.empty() || !m_isGraph) return;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsAnyItemActive()) return; // wait until the edit is finished
    m_changed = false;

    m_nodeErrors = ValidateGraph(m_graph);
    if (!m_nodeErrors.empty()) {
        m_applied = false; // keep the file, and with it the last working shader
        return;
    }
    GraphLuaInfo info;
    const std::string text = GraphToLua(m_graph, &info);
    m_lineMap = info;
    m_applied = true;
    if (text == m_text) return;

    if (!WriteTextFile(ctx.projectRoot / m_path, text)) {
        Log::Error("Cannot write {}", m_path);
        return;
    }
    RecordGraphEdit(ctx, m_path, m_text, text);
    m_text = text;
    std::error_code ec;
    m_stamp = fs::last_write_time(ctx.projectRoot / m_path, ec);
}

// ---- Scripted edits ----------------------------------------------------------------------------

int MaterialGraphPanel::AddNodeByName(const std::string& type, float x, float y) {
    if (!m_isGraph) return 0;
    std::string t = type;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    int id = 0;
    const size_t colon = t.find(':');
    if (colon != std::string::npos) { // attr:uv  builtin:time  param:float
        const std::string kind = t.substr(0, colon), what = t.substr(colon + 1);
        if (kind == "attr") id = m_graph.AddAttribute(what, what == "uv" ? "vec2" : "vec3", x, y);
        else if (kind == "builtin") id = m_graph.AddBuiltin(what, x, y);
        else if (kind == "param") id = m_graph.AddParam(what == "color" ? "color" : "param", what, x, y);
    } else if (t == "vec2" || t == "vec3" || t == "vec4") {
        id = m_graph.AddVec(t.back() - '0', t.back() - '0', x, y);
    } else {
        for (const NodeSpec& s : AllSpecs()) {
            std::string lua = s.lua;
            std::string title = s.title;
            std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const bool special = s.type == NodeType::Attribute || s.type == NodeType::Builtin || s.type == NodeType::Param ||
                                 s.type == NodeType::Vec || s.type == NodeType::VertexOutput || s.type == NodeType::FragmentOutput;
            if (!special && (t == lua || t == title || (s.type == NodeType::Constant && t == "constant") ||
                             (s.type == NodeType::Swizzle && t == "swizzle"))) {
                id = m_graph.AddNode(s.type, x, y);
                break;
            }
        }
    }
    if (id) m_changed = true;
    return id;
}

bool MaterialGraphPanel::Link(int source, int destination, int pin) {
    std::string error;
    const bool ok = m_isGraph && m_graph.Connect(source, destination, pin, error);
    if (ok) m_changed = true;
    else Log::Warn("Cannot link: {}", error.empty() ? "no graph is open" : error);
    return ok;
}

bool MaterialGraphPanel::Unlink(int destination, int pin) {
    if (!m_isGraph) return false;
    m_graph.Disconnect(destination, pin);
    m_changed = true;
    return true;
}

bool MaterialGraphPanel::SetPinConstant(int node, int pin, double value) {
    GraphNode* n = m_isGraph ? m_graph.Find(node) : nullptr;
    if (!n || pin < 0 || static_cast<size_t>(pin) >= n->inputs.size()) return false;
    n->inputs[static_cast<size_t>(pin)].constant = value;
    n->inputs[static_cast<size_t>(pin)].source = 0;
    m_changed = true;
    return true;
}

bool MaterialGraphPanel::DeleteNode(int id) {
    if (!m_isGraph || !m_graph.Find(id)) return false;
    m_graph.RemoveNode(id);
    m_changed = true;
    return true;
}

void MaterialGraphPanel::LogGraph() const {
    Log::Info("Graph '{}' ({}): {} node(s){}", m_graph.name, m_path, m_graph.nodes.size(), m_applied ? "" : " - NOT APPLIED (errors)");
    GraphTypes types(m_graph);
    for (const GraphNode& n : m_graph.nodes) {
        std::string links;
        for (size_t i = 0; i < n.inputs.size(); ++i) {
            links += (i ? ", " : "") + PinLabel(n, i) + "=" + (n.inputs[i].source ? "n" + std::to_string(n.inputs[i].source) : std::to_string(n.inputs[i].constant));
        }
        Log::Info("  id {:3}  {:<14} {:<16} {}  -> {}", n.id, SpecFor(n.type).title, n.LuaName(), links, types.Of(n.id).error.empty() ? types.Of(n.id).type : "ERROR: " + types.Of(n.id).error);
    }
}

// ---- Drawing -------------------------------------------------------------------------------------

void MaterialGraphPanel::DrawNode(GraphNode& n, GraphTypes& types) {
    // Red title: this node is the problem. Nodes that merely inherit it show a red output pin ("error") instead.
    const bool hasError = m_nodeErrors.contains(n.id) || m_shaderErrors.contains(n.id);
    const ImU32 title = hasError ? kHeaderError : HeaderColor(n.type);
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, title);
    ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, title + 0x00101010);
    ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, title + 0x00181818);

    ImNodes::BeginNode(n.id);
    if (!m_positionsApplied.contains(n.id)) {
        ImNodes::SetNodeGridSpacePos(n.id, ImVec2(n.x * m_zoom, n.y * m_zoom));
        m_positionsApplied.insert(n.id);
    }

    ImNodes::BeginNodeTitleBar();
    ImGui::PushFont(GetFont(UiFont::Semibold));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xf4, 0xf0, 0xe8, 255));
    ImGui::TextUnformatted(NodeTitle(n).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImNodes::EndNodeTitleBar();

    ImGui::PushID(n.id);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * m_zoom, 3.0f * m_zoom));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * m_zoom, 5.0f * m_zoom));
    if (n.type == NodeType::Constant) {
        ImGui::SetNextItemWidth(70.0f * m_zoom);
        double v = n.value[0];
        float f = static_cast<float>(v);
        if (ImGui::DragFloat("##value", &f, 0.01f)) {
            n.value[0] = f;
            m_changed = true;
        }
    }
    if (n.type == NodeType::Param || n.type == NodeType::Attribute || n.type == NodeType::Builtin) {
        ImGui::TextDisabled("%s", n.type == NodeType::Param ? n.paramKind.c_str() : n.dataType.c_str());
    }

    for (size_t i = 0; i < n.inputs.size(); ++i) {
        GraphPin& pin = n.inputs[i];
        const std::string type = types.PinType(n, i);
        ImNodes::PushColorStyle(ImNodesCol_Pin, pin.source ? TypeColor(type) : kPinOff);
        ImNodes::BeginInputAttribute(InPin(n.id, static_cast<int>(i)));
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextUnformatted(PinLabel(n, i).c_str());
        if (pin.source == 0) { // unconnected: a number typed right on the node
            ImGui::SameLine();
            ImGui::SetNextItemWidth(52.0f * m_zoom);
            float f = static_cast<float>(pin.constant);
            if (ImGui::DragFloat("##c", &f, 0.01f)) {
                pin.constant = f;
                m_changed = true;
            }
        }
        ImGui::PopID();
        ImNodes::EndInputAttribute();
        ImNodes::PopColorStyle();
    }
    ImGui::PopID();

    if (HasOutput(n.type)) {
        const NodeTypeInfo& info = types.Of(n.id);
        ImNodes::PushColorStyle(ImNodesCol_Pin, TypeColor(info.type));
        ImNodes::BeginOutputAttribute(OutPin(n.id));
        ImGui::Indent(40.0f * m_zoom);
        ImGui::TextUnformatted(info.type.empty() ? "error" : info.type.c_str());
        ImGui::Unindent(40.0f * m_zoom);
        ImNodes::EndOutputAttribute();
        ImNodes::PopColorStyle();
    }
    ImGui::PopStyleVar(2);
    ImNodes::EndNode();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
}

void MaterialGraphPanel::DrawInspector(GraphNode& n) {
    ImGui::PushID(n.id);
    ImGui::TextUnformatted(NodeTitle(n).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", n.LuaName().c_str());
    ImGui::Separator();

    auto text = [&](const char* label, std::string& value) {
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
        if (ImGui::InputText(label, buffer, sizeof(buffer))) {
            value = buffer;
            m_changed = true;
        }
    };
    auto combo = [&](const char* label, std::string& value, std::initializer_list<const char*> options) {
        if (ImGui::BeginCombo(label, value.c_str())) {
            for (const char* o : options) {
                if (ImGui::Selectable(o, value == o)) {
                    value = o;
                    m_changed = true;
                }
            }
            ImGui::EndCombo();
        }
    };

    switch (n.type) {
    case NodeType::Attribute:
        text("Name", n.name);
        combo("Type", n.dataType, {"float", "vec2", "vec3", "vec4"});
        ImGui::TextDisabled("position, normal, uv and color use the mesh's standard slots.");
        break;
    case NodeType::Builtin:
        if (ImGui::BeginCombo("Value", n.name.c_str())) {
            for (const std::string& name : BuiltinNames()) {
                if (ImGui::Selectable(name.c_str(), n.name == name)) {
                    n.name = name;
                    n.dataType = BuiltinType(name);
                    m_changed = true;
                }
            }
            ImGui::EndCombo();
        }
        break;
    case NodeType::Param: {
        text("Name", n.name);
        std::string kind = n.paramKind;
        if (ImGui::BeginCombo("Kind", kind.c_str())) {
            for (const char* k : {"float", "vec2", "vec3", "vec4", "color", "texture"}) {
                if (ImGui::Selectable(k, kind == k)) {
                    n.paramKind = k;
                    n.paramComponents = kind == "float" ? 1 : 0;
                    n.paramComponents = std::string(k) == "float" ? 1 : std::string(k) == "vec2" ? 2 : std::string(k) == "vec3" ? 3
                                        : std::string(k) == "vec4" ? 4 : std::string(k) == "color" ? 3 : 0;
                    if (std::string(k) == "color") n.value = {1, 1, 1, 1};
                    m_changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (n.paramKind == "texture") {
            text("Default image", n.texturePath);
            if (ImGui::Checkbox("sRGB (color data)", &n.srgb)) m_changed = true;
        } else {
            float v[4] = {static_cast<float>(n.value[0]), static_cast<float>(n.value[1]), static_cast<float>(n.value[2]), static_cast<float>(n.value[3])};
            bool edited = false;
            if (n.paramKind == "color") {
                if (n.paramComponents == 4) edited = ImGui::ColorEdit4("Default", v);
                else edited = ImGui::ColorEdit3("Default", v);
                bool alpha = n.paramComponents == 4;
                if (ImGui::Checkbox("Alpha", &alpha)) {
                    n.paramComponents = alpha ? 4 : 3;
                    m_changed = true;
                }
            } else if (n.paramComponents == 1) {
                edited = ImGui::DragFloat("Default", v, 0.01f);
            } else {
                edited = ImGui::DragScalarN("Default", ImGuiDataType_Float, v, n.paramComponents, 0.01f);
            }
            if (edited) {
                for (int i = 0; i < 4; ++i) n.value[static_cast<size_t>(i)] = v[i];
                m_changed = true;
            }
            if (n.paramKind == "float") {
                if (ImGui::Checkbox("Slider range", &n.hasRange)) m_changed = true;
                if (n.hasRange) {
                    float lo = static_cast<float>(n.min), hi = static_cast<float>(n.max);
                    if (ImGui::DragFloat("Min", &lo, 0.01f)) { n.min = lo; m_changed = true; }
                    if (ImGui::DragFloat("Max", &hi, 0.01f)) { n.max = hi; m_changed = true; }
                }
            }
        }
        break;
    }
    case NodeType::Swizzle: text("Components", n.swizzle); break;
    case NodeType::Vec: {
        int k = n.vecComponents, arity = static_cast<int>(n.inputs.size());
        if (ImGui::SliderInt("Result components", &k, 2, 4)) {
            n.vecComponents = k;
            m_changed = true;
        }
        if (ImGui::SliderInt("Inputs", &arity, 1, 4)) {
            n.inputs.resize(static_cast<size_t>(arity));
            m_changed = true;
        }
        ImGui::TextDisabled("Inputs may be numbers or smaller vectors; their components must add up to the result.");
        break;
    }
    case NodeType::Constant: {
        float f = static_cast<float>(n.value[0]);
        if (ImGui::DragFloat("Value", &f, 0.01f)) {
            n.value[0] = f;
            m_changed = true;
        }
        break;
    }
    default: ImGui::TextDisabled("Nothing to edit; connect the pins or type numbers on the node."); break;
    }

    if (const auto it = m_nodeErrors.find(n.id); it != m_nodeErrors.end()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s", it->second.c_str());
    }
    if (const auto it = m_shaderErrors.find(n.id); it != m_shaderErrors.end()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "Shader: %s", it->second.c_str());
    }
    ImGui::PopID();
}

std::vector<MaterialGraphPanel::MenuEntry> MaterialGraphPanel::BuildMenu() const {
    std::vector<MenuEntry> items;
    auto add = [&](const char* category, const std::string& label, std::function<int(MaterialGraph&, float, float)> create) {
        items.push_back({category, label, std::move(create)});
    };

    for (const auto& [name, type] : std::initializer_list<std::pair<const char*, const char*>>{
             {"position", "vec3"}, {"normal", "vec3"}, {"uv", "vec2"}, {"color", "vec3"}}) {
        if (!m_graph.FindInput(NodeType::Attribute, name)) {
            const std::string n = name, t = type;
            add("Input", "Attribute: " + n, [n, t](MaterialGraph& g, float x, float y) { return g.AddAttribute(n, t, x, y); });
        }
    }
    for (const std::string& name : BuiltinNames()) {
        if (!m_graph.FindInput(NodeType::Builtin, name)) {
            add("Input", "Engine value: " + name, [name](MaterialGraph& g, float x, float y) { return g.AddBuiltin(name, x, y); });
        }
    }
    for (const char* kind : {"float", "vec2", "vec3", "vec4", "color", "texture"}) {
        const std::string k = kind;
        add("Input", "Parameter: " + k, [k](MaterialGraph& g, float x, float y) { return g.AddParam(k == "texture" ? "texture" : (k == "color" ? "color" : "value"), k, x, y); });
    }
    add("Input", "Parameter: color (with alpha)", [](MaterialGraph& g, float x, float y) {
        const int id = g.AddParam("color", "color", x, y);
        g.Find(id)->paramComponents = 4;
        return id;
    });

    for (const NodeSpec& s : AllSpecs()) {
        if (s.type == NodeType::Attribute || s.type == NodeType::Builtin || s.type == NodeType::Param || s.type == NodeType::Vec ||
            s.type == NodeType::VertexOutput || s.type == NodeType::FragmentOutput) {
            continue;
        }
        const NodeType type = s.type;
        add(s.category, s.title, [type](MaterialGraph& g, float x, float y) { return g.AddNode(type, x, y); });
    }
    add("Vector", "Vec2 (x, y)", [](MaterialGraph& g, float x, float y) { return g.AddVec(2, 2, x, y); });
    add("Vector", "Vec3 (x, y, z)", [](MaterialGraph& g, float x, float y) { return g.AddVec(3, 3, x, y); });
    add("Vector", "Vec4 (x, y, z, w)", [](MaterialGraph& g, float x, float y) { return g.AddVec(4, 4, x, y); });
    add("Vector", "Vec3 from Vec2 + number", [](MaterialGraph& g, float x, float y) { return g.AddVec(3, 2, x, y); });
    add("Vector", "Vec4 from Vec3 + number", [](MaterialGraph& g, float x, float y) { return g.AddVec(4, 2, x, y); });
    add("Vector", "Vec3 from one number", [](MaterialGraph& g, float x, float y) { return g.AddVec(3, 1, x, y); });
    return items;
}

void MaterialGraphPanel::DrawAddMenu(float gridX, float gridY) {
    if (m_openMenu) {
        ImGui::OpenPopup("##addnode");
        m_menuX = gridX;
        m_menuY = gridY;
        m_menuFilter[0] = '\0';
        m_openMenu = false;
    }
    if (!ImGui::BeginPopup("##addnode")) return;

    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##filter", "Search nodes", m_menuFilter, sizeof(m_menuFilter));
    ImGui::Separator();

    const std::vector<MenuEntry> items = BuildMenu();
    auto create = [&](const MenuEntry& e) {
        if (e.create(m_graph, m_menuX, m_menuY)) m_changed = true;
        ImGui::CloseCurrentPopup();
    };
    if (m_menuFilter[0]) {
        for (const MenuEntry& e : items) {
            if (ContainsNoCase(e.label, m_menuFilter) || ContainsNoCase(e.category, m_menuFilter)) {
                if (ImGui::Selectable((e.label + "  [" + e.category + "]").c_str())) create(e);
            }
        }
    } else {
        std::vector<std::string> categories;
        for (const MenuEntry& e : items) {
            if (std::find(categories.begin(), categories.end(), e.category) == categories.end()) categories.push_back(e.category);
        }
        for (const std::string& category : categories) {
            if (!ImGui::BeginMenu(category.c_str())) continue;
            for (const MenuEntry& e : items) {
                if (e.category == category && ImGui::MenuItem(e.label.c_str())) create(e);
            }
            ImGui::EndMenu();
        }
    }
    ImGui::EndPopup();
}

void MaterialGraphPanel::HandleCanvasEdits() {
    // New link (dragged from either end).
    int a = 0, b = 0;
    if (ImNodes::IsLinkCreated(&a, &b)) {
        if (IsOutPin(a) != IsOutPin(b)) {
            const int out = IsOutPin(a) ? a : b;
            const int in = IsOutPin(a) ? b : a;
            std::string error;
            if (m_graph.Connect(NodeOfPin(out), NodeOfPin(in), SlotOfPin(in), error)) m_changed = true;
            else Log::Warn("Cannot link: {}", error);
        }
    }
    // A link picked up and dropped on empty space.
    int linkId = 0;
    if (ImNodes::IsLinkDestroyed(&linkId)) {
        m_graph.Disconnect(NodeOfPin(linkId), SlotOfPin(linkId));
        m_changed = true;
    }

    // Delete the selection.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !ImGui::GetIO().WantTextInput) {
        const int links = ImNodes::NumSelectedLinks();
        if (links > 0) {
            std::vector<int> selected(static_cast<size_t>(links));
            ImNodes::GetSelectedLinks(selected.data());
            for (const int l : selected) m_graph.Disconnect(NodeOfPin(l), SlotOfPin(l));
            ImNodes::ClearLinkSelection();
            m_changed = true;
        }
        const int nodes = ImNodes::NumSelectedNodes();
        if (nodes > 0) {
            std::vector<int> selected(static_cast<size_t>(nodes));
            ImNodes::GetSelectedNodes(selected.data());
            for (const int id : selected) m_graph.RemoveNode(id);
            ImNodes::ClearNodeSelection();
            m_changed = true;
        }
    }

    // Positions follow the nodes while they are dragged; the file is rewritten when the mouse is released.
    for (GraphNode& n : m_graph.nodes) {
        if (!m_positionsApplied.contains(n.id)) continue;
        const ImVec2 shown = ImNodes::GetNodeGridSpacePos(n.id);
        const ImVec2 p(shown.x / m_zoom, shown.y / m_zoom);
        if (std::abs(p.x - n.x) > 0.5f || std::abs(p.y - n.y) > 0.5f) {
            n.x = p.x;
            n.y = p.y;
            m_changed = true;
        }
    }
}

void MaterialGraphPanel::DrawCanvas(EditorContext&) {
    GraphTypes types(m_graph);
    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    m_canvasOrigin = canvasOrigin;
    m_canvasSize = ImGui::GetContentRegionAvail();

    // Wheel zoom around the mouse: the point under the cursor stays where it is. The hovered window is ImNodes's own
    // child of this one, so children count as hovered; a scripted wheel (console) acts at the center of the canvas.
    const ImGuiIO& io = ImGui::GetIO();
    float wheel = m_pendingWheel;
    ImVec2 zoomAt(canvasOrigin.x + m_canvasSize.x * 0.5f, canvasOrigin.y + m_canvasSize.y * 0.5f);
    m_pendingWheel = 0.0f;
    if (wheel == 0.0f && io.MouseWheel != 0.0f && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !io.WantTextInput &&
        !ImGui::IsPopupOpen(static_cast<const char*>(nullptr), ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        wheel = io.MouseWheel;
        zoomAt = io.MousePos;
    }
    if (wheel != 0.0f) {
        const float newZoom = std::clamp(m_zoom * std::pow(1.1f, wheel), 0.25f, 1.5f);
        if (newZoom != m_zoom) {
            const ImVec2 pan = ImNodes::EditorContextGetPanning();
            const ImVec2 mouse(zoomAt.x - canvasOrigin.x, zoomAt.y - canvasOrigin.y);
            const ImVec2 logical((mouse.x - pan.x) / m_zoom, (mouse.y - pan.y) / m_zoom);
            ImNodes::EditorContextResetPanning(ImVec2(mouse.x - logical.x * newZoom, mouse.y - logical.y * newZoom));
            m_zoom = newZoom;
            m_positionsApplied.clear(); // every node is placed again at the new scale
        }
    }
    {
        ImNodesStyle& style = ImNodes::GetStyle();
        style.NodeCornerRounding = 8.0f * m_zoom;
        style.NodePadding = ImVec2(10.0f * m_zoom, 4.0f * m_zoom);
        style.NodeBorderThickness = std::max(1.0f, m_zoom);
        style.LinkThickness = std::max(1.2f, 2.5f * m_zoom);
        style.PinCircleRadius = std::max(2.5f, 5.0f * m_zoom);
        style.PinQuadSideLength = 7.0f * m_zoom;
        style.PinTriangleSideLength = 9.0f * m_zoom;
        style.PinLineThickness = std::max(1.0f, m_zoom);
        style.PinHoverRadius = std::max(6.0f, 10.0f * m_zoom);
        style.GridSpacing = 22.0f * m_zoom;
    }
    ImNodes::BeginNodeEditor();
    ImGui::SetWindowFontScale(m_zoom);
    for (GraphNode& n : m_graph.nodes) DrawNode(n, types);
    for (const GraphNode& n : m_graph.nodes) {
        for (size_t i = 0; i < n.inputs.size(); ++i) {
            if (n.inputs[i].source) {
                ImNodes::PushColorStyle(ImNodesCol_Link, TypeColor(types.Of(n.inputs[i].source).type));
                ImNodes::Link(InPin(n.id, static_cast<int>(i)), OutPin(n.inputs[i].source), InPin(n.id, static_cast<int>(i)));
                ImNodes::PopColorStyle();
            }
        }
    }

    const bool hovered = ImNodes::IsEditorHovered();
    ImGui::SetWindowFontScale(1.0f);
    ImNodes::MiniMap(0.14f, ImNodesMiniMapLocation_BottomRight);
    ImNodes::EndNodeEditor();

    // Error text on hover.
    int hoveredNode = 0;
    if (ImNodes::IsNodeHovered(&hoveredNode)) {
        if (const auto it = m_nodeErrors.find(hoveredNode); it != m_nodeErrors.end()) ImGui::SetTooltip("%s", it->second.c_str());
        else if (const auto s = m_shaderErrors.find(hoveredNode); s != m_shaderErrors.end()) ImGui::SetTooltip("%s", s->second.c_str());
    }

    HandleCanvasEdits();

    {   // zoom readout in the corner
        char zoom[16];
        std::snprintf(zoom, sizeof(zoom), "%.0f%%", m_zoom * 100.0f);
        ImGui::PushFont(GetFont(UiFont::Mono));
        ImGui::GetWindowDrawList()->AddText(ImVec2(canvasOrigin.x + 10.0f, canvasOrigin.y + m_canvasSize.y - 24.0f), ui::U32(ui::kTextFaint), zoom);
        ImGui::PopFont();
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hoveredNode == 0) {
        // Grid space = screen position - top-left of the canvas - how far the view is panned.
        const ImVec2 mouse = ImGui::GetMousePos();
        const ImVec2 pan = ImNodes::EditorContextGetPanning();
        const ImVec2 grid((mouse.x - canvasOrigin.x - pan.x) / m_zoom, (mouse.y - canvasOrigin.y - pan.y) / m_zoom);
        m_openMenu = true;
        DrawAddMenu(grid.x, grid.y);
    } else {
        DrawAddMenu(m_menuX, m_menuY);
    }
}

namespace {

ImU32 CodeColor(const char* kind) {
    if (!std::strcmp(kind, "kw")) return IM_COL32(0xe0, 0x8a, 0x5a, 255);
    if (!std::strcmp(kind, "fn")) return IM_COL32(0x8f, 0xb4, 0xe8, 255);
    if (!std::strcmp(kind, "nm")) return IM_COL32(0xe9, 0xb4, 0x4c, 255);
    if (!std::strcmp(kind, "cm")) return IM_COL32(0x8a, 0x8a, 0x8a, 255);
    return IM_COL32(0xd0, 0xd0, 0xd0, 255);
}

bool IsKeyword(const std::string& w) {
    static const char* words[] = {"function", "local", "return", "end", "if", "then", "else", "elseif", "for", "while",
                                  "do", "in", "and", "or", "not", "nil", "true", "false", "repeat", "until", "break"};
    for (const char* k : words) {
        if (w == k) return true;
    }
    return false;
}

// Draws one line of Lua in colors: keywords, calls, numbers, strings and comments. `inBlock` carries a --[[ ]] comment over lines.
void DrawLuaLine(const std::string& line, bool& inBlock) {
    size_t i = 0;
    bool first = true;
    auto emit = [&](const std::string& text, const char* kind) {
        if (text.empty()) return;
        if (!first) ImGui::SameLine(0, 0);
        first = false;
        ImGui::PushStyleColor(ImGuiCol_Text, CodeColor(kind));
        ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
        ImGui::PopStyleColor();
    };
    if (line.empty()) {
        ImGui::TextUnformatted(" ");
        return;
    }
    while (i < line.size()) {
        if (inBlock) {
            const size_t end = line.find("]]", i);
            if (end == std::string::npos) {
                emit(line.substr(i), "cm");
                return;
            }
            emit(line.substr(i, end + 2 - i), "cm");
            inBlock = false;
            i = end + 2;
            continue;
        }
        const char c = line[i];
        if (c == '-' && line.compare(i, 2, "--") == 0) {
            if (line.compare(i, 4, "--[[") == 0) {
                const size_t end = line.find("]]", i + 4);
                if (end == std::string::npos) {
                    emit(line.substr(i), "cm");
                    inBlock = true;
                    return;
                }
                emit(line.substr(i, end + 2 - i), "cm");
                i = end + 2;
                continue;
            }
            emit(line.substr(i), "cm");
            return;
        }
        if (c == '"' || c == '\'') {
            size_t j = i + 1;
            while (j < line.size() && line[j] != c) j += line[j] == '\\' ? 2 : 1;
            j = std::min(j + 1, line.size());
            emit(line.substr(i, j - i), "nm");
            i = j;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t j = i;
            while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '.')) ++j;
            emit(line.substr(i, j - i), "nm");
            i = j;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t j = i;
            while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_')) ++j;
            const std::string word = line.substr(i, j - i);
            emit(word, IsKeyword(word) ? "kw" : (j < line.size() && line[j] == '(' ? "fn" : "txt"));
            i = j;
            continue;
        }
        size_t j = i + 1;
        while (j < line.size() && !std::isalnum(static_cast<unsigned char>(line[j])) && line[j] != '_' && line[j] != '"' &&
               line[j] != '\'' && line[j] != '-') {
            ++j;
        }
        emit(line.substr(i, j - i), "txt");
        i = j;
    }
}

// A section header strip: UPPERCASE title on the left, small mono note on the right.
void PanelHeader(const char* title, const char* note = nullptr) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(pos.x, pos.y + 33.5f), ImVec2(pos.x + width, pos.y + 33.5f), ui::U32(ui::kBorder));
    ImGui::PushFont(GetFont(UiFont::Semibold));
    const ImVec2 ts = ImGui::CalcTextSize(title);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.86f, ImVec2(pos.x + 12.0f, pos.y + (34.0f - ts.y * 0.86f) * 0.5f), ui::U32(ui::kTextDim), title);
    ImGui::PopFont();
    if (note) {
        ImGui::PushFont(GetFont(UiFont::Mono));
        const ImVec2 ns = ImGui::CalcTextSize(note);
        dl->AddText(ImVec2(pos.x + width - ns.x - 12.0f, pos.y + (34.0f - ns.y) * 0.5f), ui::U32(ui::kTextDim), note);
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(width, 34.0f));
}

} // namespace

// Left column: the library of nodes. A click adds the node at the middle of the canvas.
void MaterialGraphPanel::DrawLibrary() {
    PanelHeader("NODES");
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 10.0f);
    ImGui::InputTextWithHint("##nodefilter", "Search nodes", m_libraryFilter, sizeof(m_libraryFilter));

    std::string lastCategory;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 0.0f)); // rows touch, like a list
    for (const MenuEntry& entry : BuildMenu()) {
        if (m_libraryFilter[0] && !ContainsNoCase(entry.label, m_libraryFilter)) continue;
        if (entry.category != lastCategory) {
            lastCategory = entry.category;
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
            ImGui::PushFont(GetFont(UiFont::Semibold));
            ImGui::PushStyleColor(ImGuiCol_Text, ui::kTextDim);
            std::string upper = entry.category;
            for (char& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            ImGui::TextUnformatted(upper.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::PushID(entry.label.c_str());
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const bool clicked = ImGui::InvisibleButton("##row", ImVec2(width, 26.0f));
        if (ImGui::IsItemHovered()) ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + 26.0f), ui::U32(ui::kHover));
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + 12.0f, pos.y + (26.0f - ImGui::GetTextLineHeight()) * 0.5f), ui::U32(ui::kTextSoft),
                                            entry.label.c_str());
        ImGui::PopID();
        if (clicked) {
            const ImVec2 pan = ImNodes::EditorContextGetPanning();
            entry.create(m_graph, (m_canvasSize.x * 0.5f - pan.x) / m_zoom - 90.0f, (m_canvasSize.y * 0.5f - pan.y) / m_zoom - 40.0f);
            m_changed = true;
        }
    }
    ImGui::PopStyleVar();
}

// The graph/code switch, the file name and the compile state.
void MaterialGraphPanel::DrawToolbar() {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + 40.0f), ui::U32(ui::kPanel));
    dl->AddLine(ImVec2(pos.x, pos.y + 39.5f), ImVec2(pos.x + width, pos.y + 39.5f), ui::U32(ui::kBorder));
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 12.0f, pos.y + 6.0f));

    struct Mode { const char* label; ViewMode mode; };
    const Mode modes[] = {{"Graph", ViewMode::Graph}, {"Graph + Lua", ViewMode::GraphAndLua}, {"Lua only", ViewMode::LuaOnly}};
    for (size_t i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine(0, 6);
        const bool active = m_viewMode == modes[i].mode;
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 text = ImGui::CalcTextSize(modes[i].label);
        const ImVec2 size(text.x + 22.0f, 28.0f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##mode", size)) m_viewMode = modes[i].mode;
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ui::U32(active ? ui::kHover : (hovered ? ui::kHover : ui::kPanel), active ? 1.0f : 0.6f), 5.0f);
        dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), ui::U32(ui::kBorder), 5.0f);
        dl->AddText(ImVec2(p.x + 11.0f, p.y + (size.y - text.y) * 0.5f), ui::U32(active ? ui::kText : ui::kTextSoft), modes[i].label);
        ImGui::PopID();
    }

    // Right: file name and state.
    const bool ok = m_applied && m_parseErrors.empty() && m_shaderErrors.empty();
    const char* state = ok ? "Compiled" : "Errors";
    const float stateWidth = ImGui::CalcTextSize(state).x + 34.0f;
    float x = pos.x + width - 12.0f - stateWidth;
    const float y = pos.y + 8.0f;
    dl->AddRect(ImVec2(x, y), ImVec2(x + stateWidth, y + 24.0f), ui::U32(ui::kBorder), 12.0f);
    dl->AddCircleFilled(ImVec2(x + 14.0f, y + 12.0f), 3.5f, ui::U32(ok ? ui::kGreen : ui::kRed));
    dl->AddText(ImVec2(x + 24.0f, y + (24.0f - ImGui::GetTextLineHeight()) * 0.5f), ui::U32(ui::kTextSoft), state);
    ImGui::PushFont(GetFont(UiFont::Mono));
    const std::string file = fs::path(m_path).filename().string();
    const float fw = ImGui::CalcTextSize(file.c_str()).x;
    dl->AddText(ImVec2(x - 14.0f - fw, y + (24.0f - ImGui::GetTextLineHeight()) * 0.5f), ui::U32(ui::kTextDim), file.c_str());
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + 40.0f));
}

// Right column: the preview sphere, problems, the selected node, and (optionally) the generated Lua.
void MaterialGraphPanel::DrawSidePanel(EditorContext& ctx) {
    PanelHeader("PREVIEW", "Sphere");
    RenderResources resources = ctx.Resources();
    const uint32_t preview = ctx.thumbnails.Shader(ctx.renderer, resources, m_path, 256);
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = 200.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ui::U32(ui::kBase));
        if (preview) {
            const float side = height - 10.0f;
            const ImVec2 p(pos.x + (width - side) * 0.5f, pos.y + 5.0f);
            dl->AddImage(static_cast<ImTextureID>(preview), p, ImVec2(p.x + side, p.y + side), ImVec2(0, 1), ImVec2(1, 0));
        }
        ImGui::Dummy(ImVec2(width, height));
    }

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
    if (ImGui::SmallButton("Open file")) OpenWithDefaultApp(ctx.projectRoot / m_path);

    const ImVec4 red = ui::kRed;
    for (const ShaderDiagnostic& d : m_parseErrors) ImGui::TextColored(red, "%s", d.ToString().c_str());
    if (!m_parseErrors.empty()) ImGui::TextDisabled("The file has statements the graph cannot read; showing the last good graph.");
    if (!m_applied) {
        ImGui::TextColored(red, "The graph has errors and was not applied:");
        for (const auto& [id, message] : m_nodeErrors) {
            const GraphNode* n = m_graph.Find(id);
            ImGui::TextColored(red, "  %s: %s", n ? n->LuaName().c_str() : "graph", message.c_str());
        }
        ImGui::TextDisabled("The shader keeps its last working version.");
    }
    for (const auto& [id, message] : m_shaderErrors) ImGui::TextColored(red, "shader: %s", message.c_str());

    // Selected node.
    if (ImNodes::NumSelectedNodes() == 1) {
        ImGui::Separator();
        int selected = 0;
        ImNodes::GetSelectedNodes(&selected);
        if (GraphNode* n = m_graph.Find(selected)) DrawInspector(*n);
    }

    if (m_viewMode == ViewMode::Graph) return;

    // The generated Lua, read-only (edit the file in any editor; the graph follows).
    ImGui::Dummy(ImVec2(0, 4));
    PanelHeader(fs::path(m_path).filename().string().c_str(), "Lua 5.4");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::kInput);
    if (ImGui::BeginChild("##luacode", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::PushFont(GetFont(UiFont::Mono));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
        std::istringstream in(m_text);
        std::string line;
        bool inBlock = false;
        int number = 1;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            char num[16];
            std::snprintf(num, sizeof(num), "%3d  ", number++);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::kTextFaint);
            ImGui::TextUnformatted(num);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 0);
            DrawLuaLine(line, inBlock);
        }
        ImGui::PopStyleVar();
        ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void MaterialGraphPanel::Draw(EditorContext& ctx) {
    m_ctx = &ctx;
    if (!ctx.graphRequest.empty()) { // another panel asked for a shader to be opened
        const std::string request = ctx.graphRequest;
        ctx.graphRequest.clear();
        Open(ctx, request);
        ImGui::SetWindowFocus("Material Graph");
    }

    if (!ImGui::Begin("Material Graph")) {
        ImGui::End();
        return;
    }
    if (m_path.empty()) {
        ImGui::TextDisabled("Open a graph shader from the Content Browser (double-click one), or create one with\nright-click > New Graph Shader.");
        ImGui::End();
        return;
    }

    Reload(ctx, false);

    // Compile errors of the generated shader, pinned to the node whose line failed.
    m_shaderErrors.clear();
    if (ShaderAsset* shader = ctx.shaders.Get(m_path)) {
        for (const ShaderDiagnostic& d : shader->Diagnostics()) {
            const auto it = m_lineMap.lineToNode.find(d.line);
            m_shaderErrors[it != m_lineMap.lineToNode.end() ? it->second : 0] = d.message;
        }
    }

    if (!m_isGraph) {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
        ImGui::TextColored(ui::kYellow, "%s is hand-written code, not a graph shader, so it has no nodes.", m_path.c_str());
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
        ImGui::TextDisabled("Graph shaders start with the line:  %s. Shaders with loops, ray marching or custom functions cannot be drawn as nodes.", kGraphMarker);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
        if (ImGui::Button("Open in text editor")) OpenWithDefaultApp(ctx.projectRoot / m_path);
        ImGui::SameLine();
        if (ImGui::Button("Create a graph shader next to it")) {
            std::string target = m_path.substr(0, m_path.rfind('.')) + "_graph.lua";
            if (CreateNew(ctx, target)) ctx.graphRequest.clear();
        }
        ImGui::Dummy(ImVec2(0, 4));
        // The code, read-only.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::kInput);
        if (ImGui::BeginChild("##plaincode", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
            ImGui::PushFont(GetFont(UiFont::Mono));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
            std::istringstream in(ReadText(ctx.projectRoot / m_path));
            std::string line;
            bool inBlock = false;
            int number = 1;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '') line.pop_back();
                char num[16];
                std::snprintf(num, sizeof(num), "%3d  ", number++);
                ImGui::PushStyleColor(ImGuiCol_Text, ui::kTextFaint);
                ImGui::TextUnformatted(num);
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 0);
                DrawLuaLine(line, inBlock);
            }
            ImGui::PopStyleVar();
            ImGui::PopFont();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::End();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    DrawToolbar();
    const bool showGraph = m_viewMode != ViewMode::LuaOnly;
    const float rightWidth = m_viewMode == ViewMode::Graph ? 300.0f : 420.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::kPanel);
    if (showGraph) {
        if (ImGui::BeginChild("##graphlib", ImVec2(220.0f, 0.0f), ImGuiChildFlags_None)) DrawLibrary();
        ImGui::EndChild();
        ImGui::SameLine(0, 0);
        if (ImGui::BeginChild("##graphcanvas", ImVec2(-rightWidth, 0.0f), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            DrawCanvas(ctx);
        }
        ImGui::EndChild();
        ImGui::SameLine(0, 0);
    }
    if (ImGui::BeginChild("##graphside", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None)) DrawSidePanel(ctx);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    Commit(ctx);
    ImGui::End();
}

} // namespace magnum::editor
