#pragma once

#include "shader/ShaderTypes.h"

#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace magnum {

// A shader as a node graph. The graph and its .lua file are two views of the same thing:
//
//   graph --GraphToLua--> canonical Lua text --ParseGraphLua--> the same graph
//
// The canonical text is ordinary shader DSL code (see Engine/Lua/shader_dsl.lua), one operation per line:
//
//     local n7 = mix(n3, n5, 0.5)
//
// so it compiles like any hand-written shader, and the editor can read an edited file back into a graph.
// Things the text format does not carry (an idle node that is connected to nothing, node positions) are kept
// in comment blocks at the end of the file.

enum class NodeType {
    // Declared at the top of the file.
    Attribute, // vertex input: position, normal, uv...
    Builtin,   // engine uniform: time, model, light_dir...
    Param,     // parameter shown in the material inspector
    // Values.
    Constant,  // a single number
    Add, Sub, Mul, Div,
    Sin, Cos, Tan, Abs, Floor, Ceil, Fract, Sqrt, Normalize, Sign, Exp, Log,
    Length,
    Min, Max, Pow, Mod, Step,
    Dot, Distance, Cross, Reflect,
    Mix, Clamp, Smoothstep,
    Vec,       // vec2/vec3/vec4 built from numbers or smaller vectors
    Swizzle,   // .xyz, .rg, .x ...
    Texture,   // texture(slot, uv)
    // The two results of the shader. Everything the fragment output needs is computed per pixel;
    // vertex attributes it uses are passed to the fragment stage automatically.
    VertexOutput,   // clip-space position (vec4)
    FragmentOutput, // final color (vec4, or vec3 with alpha 1)
};

struct GraphPin {
    int source = 0;        // id of the node feeding this pin, 0 = use `constant`
    double constant = 0.0; // value of an unconnected pin
    bool operator==(const GraphPin&) const = default;
};

struct GraphNode {
    int id = 0;
    NodeType type = NodeType::Constant;

    // Attribute / Builtin / Param: the declared name, which is also the Lua variable name.
    std::string name;
    std::string dataType; // Attribute / Builtin: "float", "vec2", "vec3", "vec4", "mat3", "mat4"

    // Param
    std::string paramKind;            // "float", "vec2", "vec3", "vec4", "color", "texture"
    int paramComponents = 1;          // 1-4 (color: 3 or 4); 0 for textures
    std::array<double, 4> value{};    // Param default; Constant: value[0]
    bool hasRange = false;
    double min = 0.0, max = 1.0;
    bool srgb = true;                 // texture params
    std::string texturePath;          // texture params: default image ("" = white)

    std::string swizzle = "xyz";      // Swizzle
    int vecComponents = 3;            // Vec: number of components of the result

    std::vector<GraphPin> inputs;     // pins, in the order of the node's spec
    float x = 0.0f, y = 0.0f;         // position in the editor canvas

    std::string LuaName() const;      // name for inputs, "n<id>" for everything else
    bool operator==(const GraphNode&) const = default;
};

struct MaterialGraph {
    std::string name = "Graph Shader";
    std::vector<GraphNode> nodes;
    int nextId = 1;

    GraphNode* Find(int id);
    const GraphNode* Find(int id) const;
    const GraphNode* FindOutput(NodeType type) const; // VertexOutput / FragmentOutput
    const GraphNode* FindInput(NodeType type, const std::string& inputName) const;

    // Editing. New nodes get the pins and defaults of their type.
    int AddNode(NodeType type, float x, float y);
    int AddAttribute(const std::string& inputName, const std::string& dataType, float x, float y);
    int AddBuiltin(const std::string& inputName, float x, float y);
    int AddParam(const std::string& inputName, const std::string& kind, float x, float y);
    int AddVec(int components, int arity, float x, float y);
    void RemoveNode(int id); // also unplugs the node from everything that used it; outputs cannot be removed
    // Fails (with `error`) if the link would create a loop or the pin does not exist.
    bool Connect(int sourceId, int destinationId, int pin, std::string& error);
    void Disconnect(int destinationId, int pin);

    bool operator==(const MaterialGraph&) const = default;
};

// ---- Node catalog --------------------------------------------------------------------------------

struct NodeSpec {
    NodeType type;
    const char* title;
    const char* category;   // menu group
    const char* lua;        // function name, or the operator for Add/Sub/Mul/Div
    int pins;               // fixed number of input pins (Vec: variable, see GraphNode::inputs)
    std::array<const char*, 4> pinNames;
    std::array<double, 4> pinDefaults;
};

const NodeSpec& SpecFor(NodeType type);
const std::vector<NodeSpec>& AllSpecs();
std::vector<std::string> BuiltinNames();
std::string BuiltinType(const std::string& name); // "" if unknown
// Names that would shadow DSL functions or Lua keywords inside the generated code cannot be used for inputs.
bool IsValidInputName(const std::string& name);
std::string PinLabel(const GraphNode& node, size_t pin);

// ---- Types ---------------------------------------------------------------------------------------

struct NodeTypeInfo {
    std::string type;  // "float", "vec3", "mat4", "sampler2D" ... ("" if it has an error)
    std::string error; // why the node cannot be typed
    bool inherited = false; // the problem is in an earlier node; this one is only broken because of it
};

// Mirrors the type rules of the shader DSL, so mistakes show up in the graph before the shader is compiled.
class GraphTypes {
public:
    explicit GraphTypes(const MaterialGraph& graph) : m_graph(graph) {}
    const NodeTypeInfo& Of(int id);
    std::string PinType(const GraphNode& node, size_t pin); // type arriving at a pin ("float" for constants)

private:
    const MaterialGraph& m_graph;
    std::map<int, NodeTypeInfo> m_cache;
    std::set<int> m_active; // cycle guard
};

// Ids of the nodes the vertex / fragment output depends on (including the output itself).
std::set<int> ReachableFrom(const MaterialGraph& graph, NodeType outputType);

// Errors in the part of the graph that actually becomes shader code. Empty = the graph can be applied.
// Maps node id -> message.
std::map<int, std::string> ValidateGraph(const MaterialGraph& graph);

// ---- Lua ---------------------------------------------------------------------------------------

struct GraphLuaInfo {
    std::map<int, int> lineToNode; // generated Lua line (1-based) -> node that produced it
};

// Canonical Lua for a valid graph (see ValidateGraph). Always succeeds on valid graphs.
std::string GraphToLua(const MaterialGraph& graph, GraphLuaInfo* info = nullptr);

constexpr const char* kGraphMarker = "-- magnum:graph 1";
bool IsGraphLua(const std::string& text); // starts with the marker

// Reads canonical Lua back into a graph. Diagnostics point at the offending lines of the file.
bool ParseGraphLua(const std::string& text, const std::string& displayName, MaterialGraph& out,
                   std::vector<ShaderDiagnostic>& diagnostics);

// A ready-made lit shader (the same look as Content/Shaders/default_lit.lua) to start a new graph from.
MaterialGraph MakeLitTemplate();

} // namespace magnum
