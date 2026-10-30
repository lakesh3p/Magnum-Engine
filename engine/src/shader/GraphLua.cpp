#include "shader/MaterialGraph.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <regex>
#include <sstream>

namespace magnum {

namespace {

// ---- Writing -----------------------------------------------------------------------------------

std::string Num(double v) {
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.9g", v);
    std::string s = buffer;
    if (s == "-0") s = "0";
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0"; // "1" -> "1.0"
    return s;
}

std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string NumList(const std::array<double, 4>& v, int count) {
    std::string out = "{ ";
    for (int i = 0; i < count; ++i) out += (i ? ", " : "") + Num(v[static_cast<size_t>(i)]);
    return out + " }";
}

enum class Stage { Vertex, Fragment, Detached };

// Lua text for one node, given the text of its inputs.
std::string Rhs(const GraphNode& n, const std::vector<std::string>& atoms) {
    switch (n.type) {
    case NodeType::Constant: return Num(n.value[0]);
    case NodeType::Add: case NodeType::Sub: case NodeType::Mul: case NodeType::Div:
        return atoms[0] + " " + SpecFor(n.type).lua + " " + atoms[1];
    case NodeType::Swizzle: return atoms[0] + "." + n.swizzle;
    case NodeType::Vec:
    case NodeType::Texture:
    default: {
        std::string name = n.type == NodeType::Vec ? "vec" + std::to_string(n.vecComponents) : SpecFor(n.type).lua;
        std::string out = name + "(";
        for (size_t i = 0; i < atoms.size(); ++i) out += (i ? ", " : "") + atoms[i];
        return out + ")";
    }
    }
}

bool IsInput(NodeType t) {
    return t == NodeType::Attribute || t == NodeType::Builtin || t == NodeType::Param;
}

bool IsOutput(NodeType t) {
    return t == NodeType::VertexOutput || t == NodeType::FragmentOutput;
}

} // namespace

bool IsGraphLua(const std::string& text) {
    std::string head = text.substr(0, 32);
    StripUtf8Bom(head);
    return head.rfind("-- magnum:graph", 0) == 0;
}

std::string GraphToLua(const MaterialGraph& g, GraphLuaInfo* info) {
    std::vector<std::string> lines;
    auto emit = [&](const std::string& text, int nodeId = 0) {
        lines.push_back(text);
        if (info && nodeId) info->lineToNode[static_cast<int>(lines.size())] = nodeId;
    };

    const std::set<int> vertexSet = ReachableFrom(g, NodeType::VertexOutput);
    const std::set<int> fragmentSet = ReachableFrom(g, NodeType::FragmentOutput);

    // Inner (non-input, non-output) nodes of a set in a stable dependency order: smallest id first among those ready.
    auto ordered = [&](const std::set<int>& ids) {
        std::vector<int> result;
        std::map<int, int> waiting;
        std::set<int> ready;
        for (const int id : ids) {
            const GraphNode* n = g.Find(id);
            if (!n || IsInput(n->type) || IsOutput(n->type)) continue;
            int deps = 0;
            for (const GraphPin& p : n->inputs) {
                const GraphNode* s = p.source ? g.Find(p.source) : nullptr;
                if (s && ids.contains(s->id) && !IsInput(s->type)) ++deps;
            }
            waiting[id] = deps;
            if (deps == 0) ready.insert(id);
        }
        while (!ready.empty()) {
            const int id = *ready.begin();
            ready.erase(ready.begin());
            result.push_back(id);
            for (auto& [other, deps] : waiting) {
                if (other == id) continue;
                const GraphNode* o = g.Find(other);
                int uses = 0;
                for (const GraphPin& p : o->inputs) uses += p.source == id ? 1 : 0;
                if (uses > 0 && (deps -= uses) == 0) ready.insert(other);
            }
        }
        return result;
    };

    std::set<std::string> fragmentVaryings; // attributes the fragment stage reads
    auto atomFor = [&](const GraphPin& pin, Stage stage) -> std::string {
        if (pin.source == 0) return Num(pin.constant);
        const GraphNode* s = g.Find(pin.source);
        if (!s) return Num(pin.constant);
        if (s->type == NodeType::Attribute) {
            if (stage == Stage::Vertex) return "a." + s->name;
            if (stage == Stage::Fragment) {
                fragmentVaryings.insert(s->name);
                return "v." + s->name;
            }
            return s->name;
        }
        return s->LuaName();
    };
    auto statement = [&](const GraphNode& n, Stage stage) {
        std::vector<std::string> atoms;
        for (const GraphPin& p : n.inputs) atoms.push_back(atomFor(p, stage));
        return "local " + n.LuaName() + " = " + Rhs(n, atoms);
    };

    // The fragment body first: it decides which attributes become varyings.
    std::vector<std::string> fragmentBody;
    std::vector<int> fragmentIds;
    for (const int id : ordered(fragmentSet)) {
        fragmentBody.push_back("    " + statement(*g.Find(id), Stage::Fragment));
        fragmentIds.push_back(id);
    }
    std::string fragmentReturn = "    return 0.0";
    if (const GraphNode* out = g.FindOutput(NodeType::FragmentOutput)) fragmentReturn = "    return " + atomFor(out->inputs[0], Stage::Fragment);

    // ---- File ----
    emit(kGraphMarker);
    emit("-- Generated by the Magnum material graph editor. You can edit this file by hand: keep every statement in the");
    emit("-- form `local nID = ...` (one operation per line) and the editor reads it back into the graph.");
    emit("local S = shader(" + Quote(g.name) + ")");
    emit("");

    std::vector<const GraphNode*> inputs;
    for (const GraphNode& n : g.nodes) {
        if (IsInput(n.type)) inputs.push_back(&n);
    }
    std::sort(inputs.begin(), inputs.end(), [](const GraphNode* a, const GraphNode* b) { return a->id < b->id; });
    for (const GraphNode* n : inputs) {
        std::string line = "local " + n->name + " = S:";
        if (n->type == NodeType::Attribute) {
            line += "attribute(" + Quote(n->name) + ", " + Quote(n->dataType) + ")";
        } else if (n->type == NodeType::Builtin) {
            line += "builtin(" + Quote(n->name) + ")";
        } else if (n->paramKind == "texture") {
            line += "param(" + Quote(n->name) + ", \"texture\"";
            if (!n->texturePath.empty() || !n->srgb) line += ", " + (n->texturePath.empty() ? std::string("nil") : Quote(n->texturePath));
            if (!n->srgb) line += ", { srgb = false }";
            line += ")";
        } else {
            line += "param(" + Quote(n->name) + ", " + Quote(n->paramKind) + ", ";
            line += n->paramKind == "float" ? Num(n->value[0]) : NumList(n->value, n->paramComponents);
            if (n->hasRange) line += ", { min = " + Num(n->min) + ", max = " + Num(n->max) + " }";
            line += ")";
        }
        emit(line, n->id);
    }
    emit("");

    // Vertex function (its varyings list needs the fragment analysis above).
    emit("S:vertex(function(a)");
    for (const int id : ordered(vertexSet)) emit("    " + statement(*g.Find(id), Stage::Vertex), id);
    std::string vertexReturn = "    return 0.0, {}";
    if (const GraphNode* out = g.FindOutput(NodeType::VertexOutput)) {
        std::string varyings = "{";
        bool first = true;
        for (const std::string& name : fragmentVaryings) {
            varyings += (first ? " " : ", ") + name + " = a." + name;
            first = false;
        }
        varyings += first ? "}" : " }";
        vertexReturn = "    return " + atomFor(out->inputs[0], Stage::Vertex) + ", " + varyings;
    }
    emit(vertexReturn, g.FindOutput(NodeType::VertexOutput) ? g.FindOutput(NodeType::VertexOutput)->id : 0);
    emit("end)");
    emit("");

    emit("S:fragment(function(v)");
    for (size_t i = 0; i < fragmentBody.size(); ++i) emit(fragmentBody[i], fragmentIds[i]);
    emit(fragmentReturn, g.FindOutput(NodeType::FragmentOutput) ? g.FindOutput(NodeType::FragmentOutput)->id : 0);
    emit("end)");
    emit("");
    emit("return S");

    // Idle nodes: not part of the shader, kept in a comment so they are not lost.
    std::set<int> used = vertexSet;
    used.insert(fragmentSet.begin(), fragmentSet.end());
    std::set<int> idle;
    for (const GraphNode& n : g.nodes) {
        if (!IsInput(n.type) && !IsOutput(n.type) && !used.contains(n.id)) idle.insert(n.id);
    }
    if (!idle.empty()) {
        emit("");
        emit("--[[ magnum:detached");
        for (const int id : ordered(idle)) emit(statement(*g.Find(id), Stage::Detached), id);
        emit("]]");
    }

    // Positions.
    emit("");
    emit("--[[ magnum:layout");
    // Inputs (in declaration order), then inner nodes by id, then the outputs. Not plain id order: a graph read back
    // from text numbers its inputs after the inner nodes, and the text must come out the same either way.
    std::vector<const GraphNode*> all;
    for (const GraphNode& n : g.nodes) all.push_back(&n);
    auto rank = [](const GraphNode* n) { return IsInput(n->type) ? 0 : (IsOutput(n->type) ? 2 : 1); };
    std::sort(all.begin(), all.end(), [&](const GraphNode* a, const GraphNode* b) {
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        if (rank(a) == 2) return a->type < b->type; // vertex before fragment
        return a->id < b->id;
    });
    for (const GraphNode* n : all) emit(n->LuaName() + " = " + std::to_string(std::lround(n->x)) + ", " + std::to_string(std::lround(n->y)));
    emit("]]");

    std::string text;
    for (const std::string& l : lines) text += l + "\n";
    return text;
}

// ---- Reading -----------------------------------------------------------------------------------

namespace {

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

struct LuaArg {
    enum class Kind { Nil, Number, String, Bool, Table } kind = Kind::Nil;
    double number = 0.0;
    std::string text;
    std::vector<double> list;
    std::map<std::string, double> fields; // booleans are 0/1
};

// Splits `a, {b, c}, "d,e"` at top-level commas.
std::vector<std::string> SplitTopLevel(const std::string& s) {
    std::vector<std::string> parts;
    std::string current;
    int depth = 0;
    bool inString = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (inString) {
            current += c;
            if (c == '\\' && i + 1 < s.size()) current += s[++i];
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') inString = true;
        if (c == '{') ++depth;
        if (c == '}') --depth;
        if (c == ',' && depth == 0) {
            parts.push_back(Trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!Trim(current).empty() || !parts.empty()) parts.push_back(Trim(current));
    return parts;
}

bool ParseNumber(const std::string& s, double& out) {
    static const std::regex pattern(R"(^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$)");
    if (!std::regex_match(s, pattern)) return false;
    out = std::strtod(s.c_str(), nullptr);
    return true;
}

bool ParseArg(const std::string& raw, LuaArg& out) {
    const std::string s = Trim(raw);
    if (s == "nil") return true;
    if (s == "true" || s == "false") {
        out.kind = LuaArg::Kind::Bool;
        out.number = s == "true";
        return true;
    }
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        out.kind = LuaArg::Kind::String;
        for (size_t i = 1; i + 1 < s.size(); ++i) {
            if (s[i] == '\\' && i + 2 < s.size()) ++i;
            out.text += s[i];
        }
        return true;
    }
    if (s.size() >= 2 && s.front() == '{' && s.back() == '}') {
        out.kind = LuaArg::Kind::Table;
        for (const std::string& item : SplitTopLevel(s.substr(1, s.size() - 2))) {
            if (item.empty()) continue;
            const size_t eq = item.find('=');
            double v = 0.0;
            if (eq != std::string::npos) {
                const std::string key = Trim(item.substr(0, eq)), value = Trim(item.substr(eq + 1));
                if (value == "true" || value == "false") v = value == "true";
                else if (!ParseNumber(value, v)) return false;
                out.fields[key] = v;
            } else {
                if (!ParseNumber(item, v)) return false;
                out.list.push_back(v);
            }
        }
        return true;
    }
    if (ParseNumber(s, out.number)) {
        out.kind = LuaArg::Kind::Number;
        return true;
    }
    return false;
}

struct Pending {
    int id = 0;
    NodeType type = NodeType::Constant;
    std::string normalized; // right-hand side without a./v. prefixes, to compare definitions in both stages
    std::vector<std::string> atoms;
    std::string swizzle;
    int vecComponents = 3;
    double constant = 0.0;
    int line = 0;
};

const NodeSpec* SpecByLua(const std::string& name) {
    for (const NodeSpec& s : AllSpecs()) {
        if (s.lua && *s.lua && name == s.lua && s.type != NodeType::Vec && s.type != NodeType::Add && s.type != NodeType::Sub &&
            s.type != NodeType::Mul && s.type != NodeType::Div) {
            return &s;
        }
    }
    return nullptr;
}

NodeType InfixType(char op) {
    switch (op) {
    case '+': return NodeType::Add;
    case '-': return NodeType::Sub;
    case '*': return NodeType::Mul;
    default: return NodeType::Div;
    }
}

} // namespace

bool ParseGraphLua(const std::string& rawText, const std::string& file, MaterialGraph& out,
                   std::vector<ShaderDiagnostic>& diagnostics) {
    std::string text = rawText;
    StripUtf8Bom(text);
    const size_t errorsBefore = diagnostics.size();
    auto fail = [&](int line, const std::string& message) {
        diagnostics.push_back({file, line, message});
    };

    std::vector<std::string> lines;
    {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }
    if (lines.empty() || lines[0].rfind("-- magnum:graph", 0) != 0) {
        fail(1, "not a graph shader (the first line must be '" + std::string(kGraphMarker) + "')");
        return false;
    }
    if (Trim(lines[0]) != kGraphMarker) {
        fail(1, "unsupported graph version: '" + Trim(lines[0]) + "' (this editor reads '" + kGraphMarker + "')");
        return false;
    }

    MaterialGraph g;
    struct Declared { std::string name; NodeType type; std::string dataType; LuaArg a3, a4; std::string kind; int line; };
    std::vector<Declared> declared;
    std::map<int, Pending> defs;
    std::map<std::string, std::pair<int, int>> layout; // name -> x, y
    struct Return { int line = 0; std::string atom; bool present = false; };
    Return vertexReturn, fragmentReturn;
    std::vector<Pending> detachedDefs;

    enum class Mode { Top, Vertex, Fragment, Layout, Detached, Done } mode = Mode::Top;
    static const std::regex declRe(R"(^local (\w+) = S:(attribute|builtin|param)\((.*)\)$)");
    static const std::regex stmtRe(R"(^local n(\d+) = (.+)$)");
    static const std::regex infixRe(R"(^(\S+) ([-+*/]) (\S+)$)");
    static const std::regex callRe(R"(^(\w+)\((.*)\)$)");
    static const std::regex swizzleRe(R"(^(\S+)\.([xyzwrgba]{1,4})$)");
    static const std::regex attrAtomRe(R"(^(a|v)\.(\w+)$)");
    static const std::regex layoutRe(R"(^(\w+) = (-?\d+), (-?\d+)$)");
    static const std::regex stripRe(R"((^|[^\w.])(a|v)\.(\w+))");

    auto parseStatement = [&](int lineNo, const std::string& body, Pending& p) -> bool {
        std::smatch m;
        p.line = lineNo;
        p.normalized = std::regex_replace(body, stripRe, "$1$3");
        double number = 0.0;
        if (ParseNumber(body, number)) {
            p.type = NodeType::Constant;
            p.constant = number;
            return true;
        }
        if (std::regex_match(body, m, infixRe)) {
            p.type = InfixType(m[2].str()[0]);
            p.atoms = {m[1], m[3]};
            return true;
        }
        if (std::regex_match(body, m, callRe)) {
            const std::string fn = m[1];
            std::vector<std::string> args = SplitTopLevel(m[2]);
            if (fn == "vec2" || fn == "vec3" || fn == "vec4") {
                p.type = NodeType::Vec;
                p.vecComponents = fn.back() - '0';
                p.atoms = args;
                if (args.empty() || args.size() > 4) {
                    fail(lineNo, fn + " takes 1 to 4 arguments");
                    return false;
                }
                return true;
            }
            const NodeSpec* spec = SpecByLua(fn);
            if (!spec) {
                fail(lineNo, "unknown function '" + fn + "' (the graph editor reads only the functions of its node catalog)");
                return false;
            }
            if (static_cast<int>(args.size()) != spec->pins) {
                fail(lineNo, fn + " takes " + std::to_string(spec->pins) + " argument(s), got " + std::to_string(args.size()));
                return false;
            }
            p.type = spec->type;
            p.atoms = args;
            return true;
        }
        if (std::regex_match(body, m, swizzleRe) && !std::regex_match(body, attrAtomRe)) {
            p.type = NodeType::Swizzle;
            p.atoms = {m[1]};
            p.swizzle = m[2];
            return true;
        }
        fail(lineNo, "cannot read this statement; the graph editor expects one operation per line (`local nID = a + b`, `local nID = f(a, b)`, `local nID = a.xyz`)");
        return false;
    };

    auto addDefinition = [&](Pending p, bool detached) {
        if (detached) {
            detachedDefs.push_back(std::move(p));
            return;
        }
        const auto it = defs.find(p.id);
        if (it == defs.end()) {
            defs[p.id] = std::move(p);
        } else if (it->second.normalized != p.normalized) {
            fail(p.line, "n" + std::to_string(p.id) + " is defined differently in the vertex and the fragment function (line " + std::to_string(it->second.line) + ")");
        }
    };

    for (size_t i = 1; i < lines.size(); ++i) {
        const int lineNo = static_cast<int>(i) + 1;
        const std::string line = Trim(lines[i]);
        std::smatch m;

        if (mode == Mode::Layout || mode == Mode::Detached) {
            if (line == "]]") {
                mode = Mode::Done;
            } else if (mode == Mode::Layout) {
                if (line.empty()) continue;
                if (std::regex_match(line, m, layoutRe)) layout[m[1]] = {std::stoi(m[2]), std::stoi(m[3])};
                else fail(lineNo, "bad layout line (expected `name = x, y`)");
            } else if (!line.empty()) {
                if (std::regex_match(line, m, stmtRe)) {
                    Pending p;
                    p.id = std::stoi(m[1]);
                    if (parseStatement(lineNo, m[2], p)) addDefinition(std::move(p), true);
                } else {
                    fail(lineNo, "bad statement in the detached block");
                }
            }
            continue;
        }
        if (line.empty() || (line.rfind("--", 0) == 0 && line.rfind("--[[", 0) != 0)) continue;

        if (line == "--[[ magnum:layout") { mode = Mode::Layout; continue; }
        if (line == "--[[ magnum:detached") { mode = Mode::Detached; continue; }
        if (line.rfind("--[[", 0) == 0) {
            fail(lineNo, "unknown comment block (only magnum:layout and magnum:detached are read)");
            continue;
        }

        if (mode == Mode::Top || mode == Mode::Done) {
            if (std::regex_match(line, m, std::regex(R"(^local S = shader\((".*")\)$)"))) {
                LuaArg a;
                if (ParseArg(m[1], a)) g.name = a.text;
            } else if (std::regex_match(line, m, declRe)) {
                if (mode == Mode::Done) { fail(lineNo, "inputs must be declared before `return S`"); continue; }
                Declared d;
                d.line = lineNo;
                d.name = m[1];
                std::vector<std::string> rawArgs = SplitTopLevel(m[3]);
                std::vector<LuaArg> args(rawArgs.size());
                bool ok = true;
                for (size_t k = 0; k < rawArgs.size(); ++k) ok = ok && ParseArg(rawArgs[k], args[k]);
                if (!ok || args.empty() || args[0].kind != LuaArg::Kind::String) {
                    fail(lineNo, "cannot read the arguments of S:" + m[2].str());
                    continue;
                }
                if (args[0].text != d.name) {
                    fail(lineNo, "the variable name '" + d.name + "' must match the declared name '" + args[0].text + "'");
                    continue;
                }
                const std::string what = m[2];
                if (what == "attribute") {
                    if (args.size() != 2 || args[1].kind != LuaArg::Kind::String) { fail(lineNo, "S:attribute(name, type) expects two strings"); continue; }
                    d.type = NodeType::Attribute;
                    d.dataType = args[1].text;
                } else if (what == "builtin") {
                    if (args.size() != 1 || BuiltinType(d.name).empty()) { fail(lineNo, "unknown engine value '" + d.name + "'"); continue; }
                    d.type = NodeType::Builtin;
                    d.dataType = BuiltinType(d.name);
                } else {
                    if (args.size() < 2 || args[1].kind != LuaArg::Kind::String) { fail(lineNo, "S:param(name, kind, ...) needs a kind string"); continue; }
                    d.type = NodeType::Param;
                    d.kind = args[1].text;
                    if (args.size() > 2) d.a3 = args[2];
                    if (args.size() > 3) d.a4 = args[3];
                }
                declared.push_back(std::move(d));
            } else if (line == "S:vertex(function(a)") {
                mode = Mode::Vertex;
            } else if (line == "S:fragment(function(v)") {
                mode = Mode::Fragment;
            } else if (line == "return S") {
                mode = Mode::Done;
            } else {
                fail(lineNo, "unexpected line outside the vertex/fragment functions");
            }
            continue;
        }

        // Inside S:vertex / S:fragment.
        if (line == "end)") {
            mode = Mode::Top;
        } else if (std::regex_match(line, m, stmtRe)) {
            Pending p;
            p.id = std::stoi(m[1]);
            if (parseStatement(lineNo, m[2], p)) addDefinition(std::move(p), false);
        } else if (line.rfind("return ", 0) == 0) {
            std::string value = Trim(line.substr(7));
            if (mode == Mode::Vertex) {
                // `return <atom>, { varyings }`: the varyings are derived from what the fragment function reads.
                const std::vector<std::string> parts = SplitTopLevel(value);
                if (parts.size() != 2 || parts[1].front() != '{') {
                    fail(lineNo, "S:vertex must end with `return position, { ... }`");
                } else {
                    vertexReturn = {lineNo, parts[0], true};
                }
            } else {
                fragmentReturn = {lineNo, value, true};
            }
        } else {
            fail(lineNo, "cannot read this line; inside the stage functions use `local nID = ...` and one `return`");
        }
    }
    if (mode == Mode::Vertex || mode == Mode::Fragment) fail(static_cast<int>(lines.size()), "missing `end)`");
    if (!vertexReturn.present) fail(static_cast<int>(lines.size()), "S:vertex has no `return position, { ... }`");
    if (!fragmentReturn.present) fail(static_cast<int>(lines.size()), "S:fragment has no `return color`");
    if (diagnostics.size() > errorsBefore) return false;

    // ---- Build the graph ----
    int maxId = 0;
    for (const auto& [id, p] : defs) maxId = std::max(maxId, id);
    for (const Pending& p : detachedDefs) maxId = std::max(maxId, p.id);
    g.nextId = maxId + 1;

    std::map<std::string, int> inputByName;
    for (const Declared& d : declared) {
        if (inputByName.contains(d.name)) {
            fail(d.line, "'" + d.name + "' is declared twice");
            continue;
        }
        GraphNode n;
        n.id = g.nextId++;
        n.type = d.type;
        n.name = d.name;
        n.dataType = d.dataType;
        if (d.type == NodeType::Param) {
            n.paramKind = d.kind;
            if (d.kind == "texture") {
                n.paramComponents = 0;
                if (d.a3.kind == LuaArg::Kind::String) n.texturePath = d.a3.text;
                if (d.a4.kind == LuaArg::Kind::Table && d.a4.fields.contains("srgb")) n.srgb = d.a4.fields.at("srgb") != 0.0;
            } else if (d.kind == "float" || d.kind == "vec2" || d.kind == "vec3" || d.kind == "vec4" || d.kind == "color") {
                const int fixed = d.kind == "float" ? 1 : d.kind == "vec2" ? 2 : d.kind == "vec3" ? 3 : d.kind == "vec4" ? 4 : 0;
                if (d.kind == "float" && d.a3.kind == LuaArg::Kind::Number) {
                    n.value[0] = d.a3.number;
                } else if (d.kind != "float" && d.a3.kind == LuaArg::Kind::Table) {
                    n.paramComponents = fixed ? fixed : static_cast<int>(d.a3.list.size());
                    if (static_cast<int>(d.a3.list.size()) != n.paramComponents || n.paramComponents < 2 || n.paramComponents > 4 ||
                        (d.kind == "color" && n.paramComponents < 3)) {
                        fail(d.line, "the default of '" + d.name + "' has the wrong number of values");
                        continue;
                    }
                    for (size_t k = 0; k < d.a3.list.size(); ++k) n.value[k] = d.a3.list[k];
                } else if (d.a3.kind != LuaArg::Kind::Nil) {
                    fail(d.line, "cannot read the default of '" + d.name + "'");
                    continue;
                }
                if (d.kind == "float") n.paramComponents = 1;
                if (d.kind == "color" && d.a3.kind == LuaArg::Kind::Nil) { n.paramComponents = 3; n.value = {1, 1, 1, 1}; }
                if (d.a4.kind == LuaArg::Kind::Table && d.a4.fields.contains("min") && d.a4.fields.contains("max")) {
                    n.hasRange = true;
                    n.min = d.a4.fields.at("min");
                    n.max = d.a4.fields.at("max");
                }
            } else {
                fail(d.line, "unknown parameter kind '" + d.kind + "'");
                continue;
            }
        }
        inputByName[d.name] = n.id;
        g.nodes.push_back(std::move(n));
    }

    // Resolves one atom to a link or a constant. `stage` is 'a' (vertex), 'v' (fragment) or 0 (detached).
    auto resolve = [&](const std::string& atom, int lineNo, char stage, GraphPin& pin) -> bool {
        double number = 0.0;
        std::smatch m;
        if (ParseNumber(atom, number)) {
            pin.constant = number;
            return true;
        }
        if (std::regex_match(atom, m, std::regex(R"(^n(\d+)$)"))) {
            pin.source = std::stoi(m[1]);
            return true;
        }
        if (std::regex_match(atom, m, attrAtomRe)) {
            if (stage != m[1].str()[0]) {
                fail(lineNo, "'" + atom + "' cannot be used here (attributes are 'a.' in S:vertex and 'v.' in S:fragment)");
                return false;
            }
            const auto it = inputByName.find(m[2]);
            const GraphNode* in = it == inputByName.end() ? nullptr : g.Find(it->second);
            if (!in || in->type != NodeType::Attribute) {
                fail(lineNo, "'" + m[2].str() + "' is not a declared attribute");
                return false;
            }
            pin.source = in->id;
            return true;
        }
        if (const auto it = inputByName.find(atom); it != inputByName.end()) {
            pin.source = it->second;
            return true;
        }
        fail(lineNo, "unknown name '" + atom + "'");
        return false;
    };

    auto build = [&](const Pending& p, char stage) {
        GraphNode n;
        n.id = p.id;
        n.type = p.type;
        n.swizzle = p.swizzle.empty() ? "xyz" : p.swizzle;
        n.vecComponents = p.vecComponents;
        n.value[0] = p.constant;
        for (const std::string& atom : p.atoms) {
            GraphPin pin;
            resolve(atom, p.line, stage, pin);
            n.inputs.push_back(pin);
        }
        return n;
    };

    // Stage definitions can come from either function; the atoms' prefixes tell which. Use the prefix that appears.
    for (const auto& [id, p] : defs) {
        char stage = 'v';
        // Re-resolve with whichever prefix the atoms actually use; mixed use is reported by resolve().
        for (const std::string& atom : p.atoms) {
            if (atom.rfind("a.", 0) == 0) stage = 'a';
        }
        g.nodes.push_back(build(p, stage));
    }
    for (const Pending& p : detachedDefs) {
        if (g.Find(p.id)) {
            fail(p.line, "n" + std::to_string(p.id) + " is defined twice");
            continue;
        }
        g.nodes.push_back(build(p, 0));
    }

    // Outputs.
    {
        GraphNode v;
        v.id = g.nextId++;
        v.type = NodeType::VertexOutput;
        GraphPin pin;
        resolve(vertexReturn.atom, vertexReturn.line, 'a', pin);
        v.inputs.push_back(pin);
        g.nodes.push_back(std::move(v));
        GraphNode f;
        f.id = g.nextId++;
        f.type = NodeType::FragmentOutput;
        GraphPin fpin;
        resolve(fragmentReturn.atom, fragmentReturn.line, 'v', fpin);
        f.inputs.push_back(fpin);
        g.nodes.push_back(std::move(f));
    }
    if (diagnostics.size() > errorsBefore) return false;

    // Every link must point at a node that exists, and there must be no loops.
    for (const GraphNode& n : g.nodes) {
        for (const GraphPin& pin : n.inputs) {
            if (pin.source && !g.Find(pin.source)) {
                const int line = defs.contains(n.id) ? defs.at(n.id).line : 0;
                fail(line, "n" + std::to_string(pin.source) + " is not defined");
            }
        }
    }
    if (diagnostics.size() > errorsBefore) return false;
    {
        std::map<int, int> state; // 1 = visiting, 2 = done
        std::function<bool(int)> acyclic = [&](int id) {
            if (state[id] == 2) return true;
            if (state[id] == 1) return false;
            state[id] = 1;
            for (const GraphPin& p : g.Find(id)->inputs) {
                if (p.source && !acyclic(p.source)) return false;
            }
            state[id] = 2;
            return true;
        };
        for (const GraphNode& n : g.nodes) {
            if (!acyclic(n.id)) {
                fail(0, "the statements form a loop (n" + std::to_string(n.id) + " depends on itself)");
                return false;
            }
        }
    }

    // Positions: from the layout block, or a simple left-to-right arrangement by depth.
    std::map<int, int> depth;
    std::function<int(int)> depthOf = [&](int id) {
        if (const auto it = depth.find(id); it != depth.end()) return it->second;
        int d = 0;
        for (const GraphPin& p : g.Find(id)->inputs) {
            if (p.source) d = std::max(d, depthOf(p.source) + 1);
        }
        return depth[id] = d;
    };
    std::map<int, int> rowInColumn;
    for (GraphNode& n : g.nodes) {
        const auto it = layout.find(n.LuaName());
        if (it != layout.end()) {
            n.x = static_cast<float>(it->second.first);
            n.y = static_cast<float>(it->second.second);
        } else {
            const int d = depthOf(n.id);
            n.x = static_cast<float>(d * 220);
            n.y = static_cast<float>(rowInColumn[d]++ * 110);
        }
    }

    // Keep nodes ordered by id so equal graphs compare equal.
    std::sort(g.nodes.begin(), g.nodes.end(), [](const GraphNode& a, const GraphNode& b) { return a.id < b.id; });
    out = std::move(g);
    return true;
}

} // namespace magnum
