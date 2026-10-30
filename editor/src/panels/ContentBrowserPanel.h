#pragma once

#include "EditorContext.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace magnum::editor {

// Browses <project>/Content. Tiles can be dragged onto a Shader field in Details (payload "MAGNUM_ASSET",
// a project-relative path). File operations act on disk directly and are not part of undo;
// deleting goes to the Recycle Bin.
class ContentBrowserPanel {
public:
    void Draw(EditorContext& ctx);
    void ShowFolder(const std::string& relPath) { m_current = relPath; m_lastRefresh = -10.0; }

private:
    enum class Kind { Folder, Shader, Material, Scene, Texture, Other, GraphShader, Script, Ui, Model };

    struct Entry {
        std::string name;     // file name with extension
        std::string relPath;  // project-relative, forward slashes
        bool isDirectory = false;
        Kind kind = Kind::Other;
    };

    // .lua files are materials if they contain `return material`, otherwise shader scripts.
    Kind Classify(const std::filesystem::path& file, const std::string& relPath, bool isDirectory);

    void Refresh(EditorContext& ctx);
    void DrawFolderTree(const std::string& folder, const std::string& label);
    void DrawBreadcrumbs();
    void DrawTile(EditorContext& ctx, const Entry& entry, float size);
    void DrawBackgroundMenu(EditorContext& ctx);
    void DrawPopups(EditorContext& ctx);
    void BeginRename(const Entry& entry);
    void Create(EditorContext& ctx, const char* kind);

    std::string m_current = "Content"; // folder being shown
    std::string m_selected;            // relPath of the selected tile
    std::vector<Entry> m_entries;
    std::map<std::string, std::vector<std::string>> m_subfolders; // folder relPath -> child folder relPaths
    double m_lastRefresh = -10.0;
    std::filesystem::path m_root;
    struct SniffResult {
        std::filesystem::file_time_type stamp{};
        bool isMaterial = false;
        bool isShader = false;
        bool isGraph = false;
    };
    std::map<std::string, SniffResult> m_sniffCache;

    char m_filter[128] = {};
    float m_tileSize = 104.0f;

    std::string m_renameTarget;
    char m_renameBuffer[128] = {};
    bool m_openRename = false;
    std::string m_deleteTarget;
    bool m_openDelete = false;
};

} // namespace magnum::editor
