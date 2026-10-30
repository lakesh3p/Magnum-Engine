#include "shader/MaterialGraph.h"

#include <algorithm>
#include <cctype>
#include <functional>

namespace magnum {

namespace {

constexpr NodeSpec MakeSpec(NodeType type, const char* title, const char* category, const char* lua, int pins,
                            std::array<const char*, 4> names = {"", "", "", ""},
                            std::array<double, 4> defaults = {0, 0, 0, 0}) {
    return NodeSpec{type, title, category, lua, pins, names, defaults};
}

const std::vector<NodeSpec>& Specs() {
    static const std::vector<NodeSpec> specs = {
        MakeSpec(NodeType::Attribute, "Attribute", "Input", "", 0),
        MakeSpec(NodeType::Builtin, "Engine Value", "Input", "", 0),
        MakeSpec(NodeType::Param, "Parameter", "Input", "", 0),
        MakeSpec(NodeType::Constant, "Constant", "Input", "", 0),

        MakeSpec(NodeType::Add, "Add", "Math", "+", 2, {"a", "b", "", ""}, {0, 0, 0, 0}),
        MakeSpec(NodeType::Sub, "Subtract", "Math", "-", 2, {"a", "b", "", ""}, {0, 0, 0, 0}),
        MakeSpec(NodeType::Mul, "Multiply", "Math", "*", 2, {"a", "b", "", ""}, {1, 1, 0, 0}),
        MakeSpec(NodeType::Div, "Divide", "Math", "/", 2, {"a", "b", "", ""}, {1, 1, 0, 0}),
        MakeSpec(NodeType::Sin, "Sin", "Math", "sin", 1, {"x", "", "", ""}),
        MakeSpec(NodeType::Cos, "Cos", "Math", "cos", 1, {"x", "", "", ""}),
        MakeSpec(NodeType::Tan, "Tan", "Math", "tan", 1, {"x", "", "", ""}),
        MakeSpec(NodeType::Abs, "Abs", "Math", "abs", 1, {"x", "", "", ""}),
        MakeSpec(NodeType::Floor, "Floor", "Math", "floor", 1, {"x", "", "", ""}),
        MakeSpec(NodeType::Ceil, "Ceil", "Math", "ceil", 1, {"x", "", "", ""}),
        MakeSpec(NodeType::Fract, "Fract", "Math", "fract", 1, {"x", "", "", ""}),
        MakeSpec(NodeType::Sqrt, "Sqrt", "Math", "sqrt", 1, {"x", "", "", ""}),
        MakeSpec(NodeType::Sign, "Sign", "Math", "sign", 1, {"x", "", "", ""}),
        MakeSpec(NodeType::Exp, "Exp", "Math", "exp", 1, {"x", "", "", ""}),
        MakeSpec(NodeType::Log, "Log", "Math", "log", 1, {"x", "", "", ""}),
        MakeSpec(NodeType::Min, "Min", "Math", "min", 2, {"a", "b", "", ""}),
        MakeSpec(NodeType::Max, "Max", "Math", "max", 2, {"a", "b", "", ""}),
        MakeSpec(NodeType::Pow, "Power", "Math", "pow", 2, {"x", "exponent", "", ""}, {0, 2, 0, 0}),
        MakeSpec(NodeType::Mod, "Modulo", "Math", "mod", 2, {"x", "y", "", ""}, {0, 1, 0, 0}),
        MakeSpec(NodeType::Step, "Step", "Math", "step", 2, {"edge", "x", "", ""}, {0.5, 0, 0, 0}),
        MakeSpec(NodeType::Clamp, "Clamp", "Math", "clamp", 3, {"x", "min", "max", ""}, {0, 0, 1, 0}),
        MakeSpec(NodeType::Smoothstep, "Smoothstep", "Math", "smoothstep", 3, {"edge0", "edge1", "x", ""}, {0, 1, 0, 0}),
        MakeSpec(NodeType::Mix, "Mix", "Math", "mix", 3, {"a", "b", "t", ""}, {0, 1, 0.5, 0}),

        MakeSpec(NodeType::Length, "Length", "Vector", "length", 1, {"v", "", "", ""}),
        MakeSpec(NodeType::Normalize, "Normalize", "Vector", "normalize", 1, {"v", "", "", ""}),
        MakeSpec(NodeType::Dot, "Dot", "Vector", "dot", 2, {"a", "b", "", ""}),
        MakeSpec(NodeType::Distance, "Distance", "Vector", "distance", 2, {"a", "b", "", ""}),
        MakeSpec(NodeType::Cross, "Cross", "Vector", "cross", 2, {"a", "b", "", ""}),
        MakeSpec(NodeType::Reflect, "Reflect", "Vector", "reflect", 2, {"i", "n", "", ""}),
        MakeSpec(NodeType::Vec, "Make Vector", "Vector", "vec", 0),
        MakeSpec(NodeType::Swizzle, "Swizzle", "Vector", "", 1, {"value", "", "", ""}),

        MakeSpec(NodeType::Texture, "Sample Texture", "Texture", "texture", 2, {"texture", "uv", "", ""}),

        MakeSpec(NodeType::VertexOutput, "Vertex Position", "Output", "", 1, {"position", "", "", ""}),
        MakeSpec(NodeType::FragmentOutput, "Fragment Color", "Output", "", 1, {"color", "", "", ""}),
    };
    return specs;
}

int Components(const std::string& type) {
    if (type == "float") return 1;
    if (type == "vec2") return 2;
    if (type == "vec3") return 3;
    if (type == "vec4") return 4;
    return 0;
}

std::string VectorType(int components) {
    switch (components) {
    case 1: return "float";
    case 2: return "vec2";
    case 3: return "vec3";
    case 4: return "vec4";
    default: return {};
    }
}

// Same rules as binary_type() in shader_dsl.lua.
std::string BinaryType(const std::string& op, const std::string& a, const std::string& b, std::string& error) {
    auto vecOf = [](const std::string& t) { return t == "mat4" ? std::string("vec4") : (t == "mat3" ? std::string("vec3") : std::string()); };
    const std::string av = vecOf(a), bv = vecOf(b);
    if (!av.empty() || !bv.empty()) {
        if (op == "*" && !av.empty() && (b == av || b == a)) return b;
    } else if (a == "sampler2D" || b == "sampler2D") {
        // fall through to the error
    } else if (a == b) {
        return a;
    } else if (a == "float") {
        return b;
    } else if (b == "float") {
        return a;
    }
    error = "cannot apply '" + op + "' to " + a + " and " + b;
    return {};
}

bool IsSwizzleLetters(const std::string& s) {
    if (s.empty() || s.size() > 4) return false;
    const bool xyzw = s.find_first_not_of("xyzw") == std::string::npos;
    const bool rgba = s.find_first_not_of("rgba") == std::string::npos;
    return xyzw || rgba;
}

} // namespace

const NodeSpec& SpecFor(NodeType type) {
    for (const NodeSpec& s : Specs()) {
        if (s.type == type) return s;
    }
    return Specs().front();
}

const std::vector<NodeSpec>& AllSpecs() {
    return Specs();
}

std::vector<std::string> BuiltinNames() {
    return {"time", "aspect", "resolution", "model", "view_proj", "normal_matrix", "camera_pos", "light_dir", "light_color"};
}

std::string BuiltinType(const std::string& name) {
    static const std::map<std::string, std::string> types = {
        {"time", "float"}, {"aspect", "float"}, {"resolution", "vec2"}, {"model", "mat4"}, {"view_proj", "mat4"},
        {"normal_matrix", "mat3"}, {"camera_pos", "vec3"}, {"light_dir", "vec3"}, {"light_color", "vec3"},
    };
    const auto it = types.find(name);
    return it == types.end() ? std::string() : it->second;
}

bool IsValidInputName(const std::string& name) {
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
    for (const char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    }
    // n12 is a node's own name; x/y/z/w/r/g/b would be mistaken for swizzles; the rest would shadow code the generator writes.
    if (name[0] == 'n' && name.size() > 1 && name.find_first_not_of("0123456789", 1) == std::string::npos) return false;
    if (IsSwizzleLetters(name)) return false;
    static const std::set<std::string> reserved = {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local", "nil", "not",
        "or", "repeat", "return", "then", "true", "until", "while",
        "S", "a", "v", "shader", "vec2", "vec3", "vec4", "float", "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "abs",
        "sign", "floor", "ceil", "fract", "fwidth", "sqrt", "inversesqrt", "exp", "exp2", "log", "log2", "radians", "degrees",
        "normalize", "min", "max", "pow", "mod", "dot", "distance", "length", "cross", "reflect", "clamp", "mix", "step",
        "smoothstep", "texture", "math", "string", "table", "ipairs", "pairs", "select", "type", "tostring", "tonumber",
        "error", "assert", "next", "unpack", "pi", "print"};
    return !reserved.contains(name);
}

std::string PinLabel(const GraphNode& node, size_t pin) {
    if (node.type == NodeType::Vec) {
        static const char* names[] = {"x", "y", "z", "w"};
        return pin < 4 ? names[pin] : "?";
    }
    const NodeSpec& spec = SpecFor(node.type);
    return pin < spec.pinNames.size() ? spec.pinNames[pin] : "?";
}

// ---- GraphNode / MaterialGraph -----------------------------------------------------------------

std::string GraphNode::LuaName() const {
    switch (type) {
    case NodeType::Attribute:
    case NodeType::Builtin:
    case NodeType::Param: return name;
    case NodeType::VertexOutput: return "vertex_output";
    case NodeType::FragmentOutput: return "fragment_output";
    default: return "n" + std::to_string(id);
    }
}

GraphNode* MaterialGraph::Find(int id) {
    for (GraphNode& n : nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

const GraphNode* MaterialGraph::Find(int id) const {
    for (const GraphNode& n : nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

const GraphNode* MaterialGraph::FindOutput(NodeType type) const {
    for (const GraphNode& n : nodes) {
        if (n.type == type) return &n;
    }
    return nullptr;
}

const GraphNode* MaterialGraph::FindInput(NodeType type, const std::string& inputName) const {
    for (const GraphNode& n : nodes) {
        if (n.type == type && n.name == inputName) return &n;
    }
    return nullptr;
}

int MaterialGraph::AddNode(NodeType type, float x, float y) {
    GraphNode node;
    node.id = nextId++;
    node.type = type;
    node.x = x;
    node.y = y;
    const NodeSpec& spec = SpecFor(type);
    for (int i = 0; i < spec.pins; ++i) {
        GraphPin pin;
        pin.constant = spec.pinDefaults[static_cast<size_t>(i)];
        node.inputs.push_back(pin);
    }
    if (type == NodeType::Vec) {
        node.vecComponents = 3;
        node.inputs.assign(3, GraphPin{});
    }
    nodes.push_back(std::move(node));
    return nextId - 1;
}

int MaterialGraph::AddAttribute(const std::string& inputName, const std::string& dataType, float x, float y) {
    const int id = AddNode(NodeType::Attribute, x, y);
    GraphNode& n = *Find(id);
    n.name = inputName;
    n.dataType = dataType;
    int suffix = 2;
    while (std::count_if(nodes.begin(), nodes.end(), [&](const GraphNode& o) { return o.type == NodeType::Attribute && o.name == n.name; }) > 1) {
        n.name = inputName + "_" + std::to_string(suffix++);
    }
    return id;
}

int MaterialGraph::AddBuiltin(const std::string& inputName, float x, float y) {
    const int id = AddNode(NodeType::Builtin, x, y);
    GraphNode& n = *Find(id);
    n.name = inputName;
    n.dataType = BuiltinType(inputName);
    return id;
}

int MaterialGraph::AddParam(const std::string& inputName, const std::string& kind, float x, float y) {
    const int id = AddNode(NodeType::Param, x, y);
    GraphNode& n = *Find(id);
    n.name = inputName;
    n.paramKind = kind;
    n.paramComponents = kind == "float" ? 1 : kind == "vec2" ? 2 : kind == "vec3" ? 3 : kind == "vec4" ? 4 : kind == "color" ? 3 : 0;
    n.value = kind == "color" ? std::array<double, 4>{1, 1, 1, 1} : std::array<double, 4>{0, 0, 0, 0};
    int suffix = 2;
    while (std::count_if(nodes.begin(), nodes.end(), [&](const GraphNode& o) { return o.type == NodeType::Param && o.name == n.name; }) > 1) {
        n.name = inputName + "_" + std::to_string(suffix++);
    }
    return id;
}

int MaterialGraph::AddVec(int components, int arity, float x, float y) {
    const int id = AddNode(NodeType::Vec, x, y);
    GraphNode& n = *Find(id);
    n.vecComponents = std::clamp(components, 2, 4);
    n.inputs.assign(static_cast<size_t>(std::clamp(arity, 1, 4)), GraphPin{});
    return id;
}

void MaterialGraph::RemoveNode(int id) {
    const GraphNode* n = Find(id);
    if (!n || n->type == NodeType::VertexOutput || n->type == NodeType::FragmentOutput) return;
    for (GraphNode& other : nodes) {
        for (GraphPin& pin : other.inputs) {
            if (pin.source == id) pin.source = 0;
        }
    }
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const GraphNode& o) { return o.id == id; }), nodes.end());
}

bool MaterialGraph::Connect(int sourceId, int destinationId, int pin, std::string& error) {
    const GraphNode* source = Find(sourceId);
    GraphNode* destination = Find(destinationId);
    if (!source || !destination || pin < 0 || static_cast<size_t>(pin) >= destination->inputs.size()) {
        error = "no such pin";
        return false;
    }
    if (source->type == NodeType::VertexOutput || source->type == NodeType::FragmentOutput) {
        error = "an output node has nothing to connect from";
        return false;
    }
    // Would the source (transitively) depend on the destination? Then this link closes a loop.
    std::set<int> seen;
    std::function<bool(int)> dependsOnDestination = [&](int id) {
        if (id == destinationId) return true;
        if (!seen.insert(id).second) return false;
        const GraphNode* n = Find(id);
        if (!n) return false;
        for (const GraphPin& p : n->inputs) {
            if (p.source && dependsOnDestination(p.source)) return true;
        }
        return false;
    };
    if (dependsOnDestination(sourceId)) {
        error = "this link would create a loop";
        return false;
    }
    destination->inputs[static_cast<size_t>(pin)].source = sourceId;
    return true;
}

void MaterialGraph::Disconnect(int destinationId, int pin) {
    GraphNode* n = Find(destinationId);
    if (n && pin >= 0 && static_cast<size_t>(pin) < n->inputs.size()) n->inputs[static_cast<size_t>(pin)].source = 0;
}

// ---- Types -------------------------------------------------------------------------------------

std::string GraphTypes::PinType(const GraphNode& node, size_t pin) {
    if (pin >= node.inputs.size()) return {};
    const GraphPin& p = node.inputs[pin];
    if (p.source == 0) return "float";
    return Of(p.source).type;
}

const NodeTypeInfo& GraphTypes::Of(int id) {
    if (const auto it = m_cache.find(id); it != m_cache.end()) return it->second;
    NodeTypeInfo info;
    const GraphNode* node = m_graph.Find(id);
    if (!node) {
        info.error = "missing node";
        return m_cache[id] = info;
    }
    if (!m_active.insert(id).second) {
        info.error = "loop in the graph";
        return m_cache[id] = info;
    }

    // Types of all linked pins; a broken input makes this node broken too.
    std::vector<std::string> in;
    std::vector<bool> isNumber;
    std::string upstream;
    for (size_t i = 0; i < node->inputs.size(); ++i) {
        const GraphPin& p = node->inputs[i];
        if (p.source == 0) {
            in.push_back("float");
            isNumber.push_back(true);
        } else {
            const NodeTypeInfo& src = Of(p.source);
            if (!src.error.empty() && upstream.empty()) upstream = src.error;
            in.push_back(src.type);
            isNumber.push_back(false);
        }
    }

    auto fail = [&](const std::string& message) { info.type.clear(); info.error = message; };

    if (!upstream.empty()) {
        fail(upstream);
        info.inherited = true;
    } else {
        std::string err;
        switch (node->type) {
        case NodeType::Attribute:
        case NodeType::Builtin: info.type = node->dataType; break;
        case NodeType::Param:
            info.type = node->paramKind == "texture" ? "sampler2D"
                        : node->paramKind == "color" ? (node->paramComponents == 4 ? "vec4" : "vec3")
                                                     : VectorType(node->paramComponents);
            break;
        case NodeType::Constant: info.type = "float"; break;
        case NodeType::Add: case NodeType::Sub: case NodeType::Mul: case NodeType::Div: {
            info.type = BinaryType(SpecFor(node->type).lua, in[0], in[1], err);
            if (!err.empty()) fail(err);
            break;
        }
        case NodeType::Sin: case NodeType::Cos: case NodeType::Tan: case NodeType::Abs: case NodeType::Floor:
        case NodeType::Ceil: case NodeType::Fract: case NodeType::Sqrt: case NodeType::Sign: case NodeType::Exp:
        case NodeType::Log: case NodeType::Normalize:
            if (Components(in[0]) == 0) fail(std::string(SpecFor(node->type).title) + " needs a number or a vector, got " + in[0]);
            else info.type = in[0];
            break;
        case NodeType::Length:
            if (Components(in[0]) == 0) fail("Length needs a number or a vector, got " + in[0]);
            else info.type = "float";
            break;
        case NodeType::Min: case NodeType::Max: case NodeType::Pow: case NodeType::Mod: {
            if (Components(in[0]) == 0 || Components(in[1]) == 0) { fail("needs numbers or vectors"); break; }
            info.type = BinaryType(SpecFor(node->type).lua, in[0], in[1], err);
            if (!err.empty()) fail(err);
            break;
        }
        case NodeType::Step:
            if (Components(in[1]) == 0 || Components(in[0]) == 0) fail("needs numbers or vectors");
            else info.type = in[1];
            break;
        case NodeType::Dot: case NodeType::Distance:
            if (Components(in[0]) == 0 || in[0] != in[1]) fail("needs two values of the same type, got " + in[0] + " and " + in[1]);
            else info.type = "float";
            break;
        case NodeType::Cross:
            if (in[0] != "vec3" || in[1] != "vec3") fail("Cross needs two vec3, got " + in[0] + " and " + in[1]);
            else info.type = "vec3";
            break;
        case NodeType::Reflect:
            if (Components(in[0]) == 0 || Components(in[1]) == 0) fail("needs vectors");
            else info.type = in[0];
            break;
        case NodeType::Mix:
            if (Components(in[0]) == 0 || Components(in[1]) == 0 || Components(in[2]) == 0) { fail("needs numbers or vectors"); break; }
            info.type = BinaryType("mix", in[0], in[1], err);
            if (!err.empty()) fail(err);
            break;
        case NodeType::Clamp:
            if (Components(in[0]) == 0 || Components(in[1]) == 0 || Components(in[2]) == 0) fail("needs numbers or vectors");
            else info.type = in[0];
            break;
        case NodeType::Smoothstep:
            if (Components(in[0]) == 0 || Components(in[1]) == 0 || Components(in[2]) == 0) fail("needs numbers or vectors");
            else info.type = in[2];
            break;
        case NodeType::Vec: {
            int total = 0;
            bool ok = true;
            for (const std::string& t : in) {
                if (Components(t) == 0) { fail("cannot put a " + t + " into a vector"); ok = false; break; }
                total += Components(t);
            }
            if (!ok) break;
            const int k = node->vecComponents;
            if (in.size() == 1 && total == 1) info.type = VectorType(k);                  // vecN(x): same value in every component
            else if (total == k) info.type = VectorType(k);
            else fail("vec" + std::to_string(k) + " needs " + std::to_string(k) + " components, got " + std::to_string(total));
            break;
        }
        case NodeType::Swizzle: {
            const int have = Components(in[0]);
            int need = 0;
            bool valid = IsSwizzleLetters(node->swizzle);
            const std::string& letters = node->swizzle;
            const std::string set = letters.find_first_of("xyzw") != std::string::npos ? "xyzw" : "rgba";
            for (const char c : letters) {
                const size_t pos = set.find(c);
                if (pos == std::string::npos) valid = false;
                else need = std::max(need, static_cast<int>(pos) + 1);
            }
            if (!valid) fail("'" + letters + "' is not a valid swizzle");
            else if (have == 0) fail("Swizzle needs a number or a vector, got " + in[0]);
            else if (need > have) fail("'" + letters + "' does not fit in a " + in[0]);
            else info.type = VectorType(static_cast<int>(letters.size()));
            break;
        }
        case NodeType::Texture: {
            const GraphPin& slot = node->inputs[0];
            const GraphNode* slotNode = slot.source ? m_graph.Find(slot.source) : nullptr;
            if (!slotNode || slotNode->type != NodeType::Param || slotNode->paramKind != "texture") fail("connect a texture parameter to 'texture'");
            else if (in[1] != "vec2") fail("uv must be a vec2, got " + in[1]);
            else info.type = "vec4";
            break;
        }
        case NodeType::VertexOutput:
            if (node->inputs[0].source == 0) fail("Vertex Position is not connected");
            else if (in[0] != "vec4") fail("the position must be a vec4, got " + in[0]);
            else info.type = "vec4";
            break;
        case NodeType::FragmentOutput:
            if (node->inputs[0].source == 0) fail("Fragment Color is not connected");
            else if (in[0] != "vec4" && in[0] != "vec3") fail("the color must be a vec4 or vec3, got " + in[0]);
            else info.type = in[0];
            break;
        }
    }
    m_active.erase(id);
    return m_cache[id] = info;
}

std::set<int> ReachableFrom(const MaterialGraph& graph, NodeType outputType) {
    std::set<int> seen;
    std::function<void(int)> visit = [&](int id) {
        if (!seen.insert(id).second) return;
        if (const GraphNode* n = graph.Find(id)) {
            for (const GraphPin& p : n->inputs) {
                if (p.source) visit(p.source);
            }
        }
    };
    if (const GraphNode* out = graph.FindOutput(outputType)) visit(out->id);
    return seen;
}

std::map<int, std::string> ValidateGraph(const MaterialGraph& graph) {
    std::map<int, std::string> errors;
    GraphTypes types(graph);

    if (!graph.FindOutput(NodeType::VertexOutput) || !graph.FindOutput(NodeType::FragmentOutput)) {
        errors[0] = "the graph needs a Vertex Position and a Fragment Color node";
        return errors;
    }

    // Input names are Lua variable names: they must be valid and unique.
    std::map<std::string, int> seenNames;
    for (const GraphNode& n : graph.nodes) {
        if (n.type != NodeType::Attribute && n.type != NodeType::Builtin && n.type != NodeType::Param) continue;
        if (!IsValidInputName(n.name)) errors[n.id] = "'" + n.name + "' cannot be used as a name";
        else if (!seenNames.emplace(n.name, n.id).second) errors[n.id] = "the name '" + n.name + "' is used twice";
        else if (n.type == NodeType::Builtin && BuiltinType(n.name).empty()) errors[n.id] = "unknown engine value '" + n.name + "'";
    }

    std::set<int> used = ReachableFrom(graph, NodeType::VertexOutput);
    const std::set<int> fragment = ReachableFrom(graph, NodeType::FragmentOutput);
    used.insert(fragment.begin(), fragment.end());
    for (const int id : used) {
        const NodeTypeInfo& info = types.Of(id);
        // Only the node that actually has the problem; everything below it just inherits the message.
        if (!info.error.empty() && !info.inherited && !errors.contains(id)) errors[id] = info.error;
    }

    // Plain Lua numbers have no swizzles or methods. A node counts as a number when nothing but constants feeds it.
    std::function<bool(const GraphNode&)> isNumber = [&](const GraphNode& n) {
        if (n.type == NodeType::Constant) return true;
        if (n.type != NodeType::Add && n.type != NodeType::Sub && n.type != NodeType::Mul && n.type != NodeType::Div) return false;
        for (const GraphPin& p : n.inputs) {
            if (p.source) {
                const GraphNode* s = graph.Find(p.source);
                if (!s || !isNumber(*s)) return false;
            }
        }
        return true;
    };
    for (const int id : used) {
        const GraphNode* n = graph.Find(id);
        if (!n || errors.contains(id)) continue;
        if (n->type == NodeType::Swizzle && n->inputs[0].source) {
            const GraphNode* s = graph.Find(n->inputs[0].source);
            if (s && isNumber(*s)) errors[id] = "Swizzle needs a vector or a connected value, not a constant";
        }
        if (n->type == NodeType::Swizzle && !n->inputs[0].source) errors[id] = "Swizzle input is not connected";
    }
    return errors;
}

// ---- Template ----------------------------------------------------------------------------------

MaterialGraph MakeLitTemplate() {
    MaterialGraph g;
    g.name = "Graph Lit";

    // Inputs, left column.
    const int position = g.AddAttribute("position", "vec3", 0, 0);
    const int normal = g.AddAttribute("normal", "vec3", 0, 90);
    const int model = g.AddBuiltin("model", 0, 180);
    const int viewProj = g.AddBuiltin("view_proj", 0, 250);
    const int normalMatrix = g.AddBuiltin("normal_matrix", 0, 320);
    const int cameraPos = g.AddBuiltin("camera_pos", 0, 390);
    const int lightDir = g.AddBuiltin("light_dir", 0, 460);
    const int lightColor = g.AddBuiltin("light_color", 0, 530);
    const int baseColor = g.AddParam("base_color", "color", 0, 620);
    g.Find(baseColor)->value = {0.8, 0.8, 0.8, 1};
    const int ambient = g.AddParam("ambient", "float", 0, 720);
    g.Find(ambient)->value = {0.18, 0, 0, 0};
    g.Find(ambient)->hasRange = true;
    g.Find(ambient)->min = 0;
    g.Find(ambient)->max = 1;
    const int specular = g.AddParam("specular", "float", 0, 790);
    g.Find(specular)->value = {0.4, 0, 0, 0};
    g.Find(specular)->hasRange = true;
    const int shininess = g.AddParam("shininess", "float", 0, 860);
    g.Find(shininess)->value = {32, 0, 0, 0};
    g.Find(shininess)->hasRange = true;
    g.Find(shininess)->min = 1;
    g.Find(shininess)->max = 256;

    std::string error;
    auto node = [&](NodeType type, float x, float y, std::initializer_list<int> sources) {
        const int id = g.AddNode(type, x, y);
        int pin = 0;
        for (const int s : sources) {
            if (s) g.Connect(s, id, pin, error);
            ++pin;
        }
        return id;
    };
    auto constant = [&](int id, int pin, double v) { g.Find(id)->inputs[static_cast<size_t>(pin)].constant = v; };

    // Vertex: clip = view_proj * (model * vec4(position, 1))
    const int homogeneous = g.AddVec(4, 2, 250, 0);
    g.Connect(position, homogeneous, 0, error);
    constant(homogeneous, 1, 1.0);
    const int world = node(NodeType::Mul, 450, 0, {model, homogeneous});
    const int clip = node(NodeType::Mul, 650, 0, {viewProj, world});
    const int worldPos = g.AddNode(NodeType::Swizzle, 650, 90);
    g.Connect(world, worldPos, 0, error);
    g.Find(worldPos)->swizzle = "xyz";

    // Lighting inputs.
    const int n0 = node(NodeType::Mul, 250, 90, {normalMatrix, normal});
    const int n = node(NodeType::Normalize, 450, 90, {n0});
    const int ndl0 = node(NodeType::Dot, 650, 180, {n, lightDir});
    const int ndl = node(NodeType::Max, 850, 180, {ndl0, 0});
    constant(ndl, 1, 0.0);
    const int toEye0 = node(NodeType::Sub, 850, 260, {cameraPos, worldPos});
    const int toEye = node(NodeType::Normalize, 1050, 260, {toEye0});
    const int halfSum = node(NodeType::Add, 1050, 340, {lightDir, toEye});
    const int half = node(NodeType::Normalize, 1250, 340, {halfSum});
    const int ndh0 = node(NodeType::Dot, 1250, 420, {n, half});
    const int ndh = node(NodeType::Max, 1450, 420, {ndh0, 0});
    constant(ndh, 1, 0.0);
    const int highlight = node(NodeType::Pow, 1450, 500, {ndh, shininess});
    const int highlightScaled = node(NodeType::Mul, 1650, 500, {highlight, specular});
    const int facing0 = node(NodeType::Mul, 1050, 180, {ndl, 0});
    constant(facing0, 1, 8.0);
    const int facing = node(NodeType::Min, 1250, 180, {facing0, 0});
    constant(facing, 1, 1.0);
    const int spec = node(NodeType::Mul, 1850, 500, {highlightScaled, facing});

    // Shading.
    const int diffuse = node(NodeType::Mul, 1050, 600, {lightColor, ndl});
    const int lit = node(NodeType::Add, 1250, 600, {ambient, diffuse});
    const int albedo = node(NodeType::Mul, 1450, 620, {baseColor, lit});
    const int specColor = node(NodeType::Mul, 2050, 560, {lightColor, spec});
    const int sum = node(NodeType::Add, 2250, 600, {albedo, specColor});

    // Gamma, because the viewport framebuffer is not sRGB.
    const int gamma = g.AddVec(3, 1, 2250, 700);
    constant(gamma, 0, 1.0 / 2.2);
    const int encoded = node(NodeType::Pow, 2450, 640, {sum, gamma});
    const int color = g.AddVec(4, 2, 2650, 640);
    g.Connect(encoded, color, 0, error);
    constant(color, 1, 1.0);

    const int vertexOut = g.AddNode(NodeType::VertexOutput, 900, 0);
    const int fragmentOut = g.AddNode(NodeType::FragmentOutput, 2850, 640);
    g.Connect(clip, vertexOut, 0, error);
    g.Connect(color, fragmentOut, 0, error);
    return g;
}

} // namespace magnum
