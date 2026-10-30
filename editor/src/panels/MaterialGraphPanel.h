#pragma once

#include "EditorContext.h"
#include "shader/MaterialGraph.h"

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace magnum::editor {

// Node editor for a graph shader (a .lua file starting with `-- magnum:graph 1`).
//
// The file is the source of truth. Editing the graph rewrites it (the shader hot-reloads, the preview updates),
// and editing the file by hand is read back into the graph. Every change is an undo step.
// A graph with errors is shown with the problem on the node and is NOT written, so the last working shader stays.
class MaterialGraphPanel {
public:
    void Init();
    void Shutdown();
    void Draw(EditorContext& ctx);

    bool Open(EditorContext& ctx, const std::string& shaderPath);
    // Writes a new graph shader (the lit template) and opens it.
    bool CreateNew(EditorContext& ctx, const std::string& shaderPath);
    const std::string& Path() const { return m_path; }

    // The same edits the mouse makes, for scripts and tests. They are applied on the next Draw.
    int AddNodeByName(const std::string& type, float x, float y);
    bool Link(int source, int destination, int pin);
    bool Unlink(int destination, int pin);
    bool SetPinConstant(int node, int pin, double value);
    bool DeleteNode(int id);
    void LogGraph() const;
    // Sets the canvas zoom (0.25 - 1.5), as the mouse wheel does.
    void Wheel(float notches) { m_pendingWheel += notches; }
    void SetZoom(float zoom) { m_zoom = zoom < 0.25f ? 0.25f : (zoom > 1.5f ? 1.5f : zoom); m_positionsApplied.clear(); }

private:
    struct MenuEntry {
        std::string category;
        std::string label;
        std::function<int(MaterialGraph&, float, float)> create;
    };

    void Reload(EditorContext& ctx, bool force);
    void Commit(EditorContext& ctx);
    void DrawLibrary();
    void DrawSidePanel(EditorContext& ctx);
    void DrawToolbar();
    void DrawCanvas(EditorContext& ctx);
    void DrawNode(GraphNode& node, GraphTypes& types);
    void DrawInspector(GraphNode& node);
    void DrawAddMenu(float gridX, float gridY);
    std::vector<MenuEntry> BuildMenu() const;
    void HandleCanvasEdits();

    std::string m_path;
    std::filesystem::file_time_type m_stamp{};
    std::string m_text;   // the text the file holds (what the graph was last generated from or read from)
    bool m_isGraph = false;
    MaterialGraph m_graph;
    GraphLuaInfo m_lineMap;
    std::vector<ShaderDiagnostic> m_parseErrors;
    std::map<int, std::string> m_nodeErrors;   // type problems in the graph
    std::map<int, std::string> m_shaderErrors; // GLSL/Lua problems of the compiled shader, by node
    bool m_changed = false;
    bool m_applied = true;
    std::set<int> m_positionsApplied;          // nodes whose position was handed to ImNodes
    float m_menuX = 0.0f, m_menuY = 0.0f;
    char m_menuFilter[64] = {};
    bool m_openMenu = false;
    EditorContext* m_ctx = nullptr;

    // Layout: which of the graph and the generated Lua are shown (Graph, Graph + Lua, Lua only).
    enum class ViewMode { Graph, GraphAndLua, LuaOnly };
    ViewMode m_viewMode = ViewMode::GraphAndLua;
    float m_pendingWheel = 0.0f; // a wheel turn injected by the console (graph wheel N)
    float m_zoom = 1.0f; // canvas zoom (mouse wheel): node positions, sizes and text are all scaled by it
    char m_libraryFilter[64] = {};
    ImVec2 m_canvasOrigin{0, 0}; // where the canvas starts on screen, and its size: new nodes appear at its center
    ImVec2 m_canvasSize{0, 0};
};

} // namespace magnum::editor
