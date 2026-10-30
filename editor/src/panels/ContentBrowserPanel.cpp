#include "panels/ContentBrowserPanel.h"

#include "Icons.h"
#include "Theme.h"
#include "commands/EditorCommands.h"
#include "core/Log.h"
#include "material/MaterialData.h"
#include "model/ModelImport.h"
#include "panels/PanelUtil.h"
#include "platform/FileOps.h"
#include "shader/MaterialGraph.h"
#include "scene/SceneSerializer.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <regex>

namespace magnum::editor {

namespace fs = std::filesystem;

namespace {

constexpr const char* kScriptTemplate = R"(-- A gameplay script. Put it on an actor in Details > Add Component > Script (or drop the file on the Script field).
-- Everything is optional; `self` is the actor the script runs on, and self.state is a table that belongs to it.
--   self.position / .rotation / .scale / .velocity   vec3      self.grounded   standing on something
--   input.key("w")  input.pressed("space")  input.mouse_delta()      world.find("Name")  world.spawn("cube", x, y, z)
--   hud.text(0.02, 0.03, "hello")    game.stop()    log("anything")

function on_start(self)
    self.state.t = 0
end

function on_update(self, dt)
    self.state.t = self.state.t + dt
    self.rotation = vec3(0, self.state.t * 45, 0)
end

-- function on_hit(self, other) end          -- touched a solid collider
-- function on_trigger(self, other) end      -- entered a trigger (or something entered this trigger)
-- function on_trigger_exit(self, other) end
)";

constexpr const char* kUiTemplate = R"(-- A UI document: it returns the root node of a screen. Put it on a Canvas actor (Place Actors > UI) and save to see it live.
-- Anchors place a node against its parent ("center", "topleft", "fill" ...), row / column / grid lay children out,
-- on_click and friends are plain properties. docs/UI.md has the whole list.

return ui.panel{ name = "Screen", anchor = "fill",
    ui.column{ anchor = "center", gap = 12, padding = 24, items = "center",
        bg = "#10131dd0", radius = 10, border = { color = "#ffffff18", width = 1 },
        ui.text{ "Hello", font = "SansBold", font_size = 36 },
        ui.button{ "Click me", size = { 200, 44 },
            on_click = function(button) button.text = "Clicked!" end },
    },
}
)";

constexpr const char* kShaderTemplate = R"(-- New surface shader. Save the file and the viewport updates.
local S = shader("New Shader")

local position  = S:attribute("position", "vec3")
local normal    = S:attribute("normal", "vec3")

local model         = S:builtin("model")
local view_proj     = S:builtin("view_proj")
local normal_matrix = S:builtin("normal_matrix")
local light_dir     = S:builtin("light_dir")
local light_color   = S:builtin("light_color")

local base_color = S:param("base_color", "color", { 0.8, 0.3, 0.2 })

S:vertex(function(a)
    return view_proj * (model * vec4(a.position, 1.0)), {
        normal = normalize(normal_matrix * a.normal),
    }
end)

S:fragment(function(v)
    local ndl = max(dot(normalize(v.normal), light_dir), 0.0)
    local lit = base_color * (0.15 + light_color * ndl)
    return vec4(pow(lit, vec3(1.0 / 2.2)), 1.0)
end)

return S
)";

std::string Generic(const fs::path& p) {
    return p.generic_string();
}

std::string Extension(const std::string& name) {
    const size_t dot = name.rfind('.');
    std::string ext = dot == std::string::npos ? std::string() : name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

ImU32 TypeColor(int kind) {
    switch (kind) {
    case 0: return IM_COL32(217, 164, 65, 255);  // folder
    case 1: return IM_COL32(74, 155, 234, 255);  // shader
    case 2: return IM_COL32(88, 184, 104, 255); // material
    case 3: return IM_COL32(233, 180, 76, 255);  // scene
    case 4: return IM_COL32(183, 143, 220, 255); // texture
    case 6: return IM_COL32(77, 201, 190, 255);  // graph shader
    case 7: return IM_COL32(240, 138, 60, 255);  // script
    case 9: return IM_COL32(230, 120, 170, 255); // model
    default: return IM_COL32(100, 100, 108, 255);
    }
}

const char* TypeName(int kind) {
    switch (kind) {
    case 1: return "Shader";
    case 2: return "Material";
    case 3: return "Scene";
    case 4: return "Texture";
    case 6: return "Graph Shader";
    case 7: return "Script";
    case 8: return "UI Document";
    case 9: return "Model";
    default: return "File";
    }
}

const char* TypeLabel(int kind) {
    switch (kind) {
    case 1: return "SHADER";
    case 2: return "MATERIAL";
    case 3: return "SCENE";
    case 4: return "IMAGE";
    case 6: return "GRAPH";
    case 7: return "SCRIPT";
    case 8: return "UI";
    case 9: return "MODEL";
    default: return "FILE";
    }
}

// "NewShader", "NewShader_1", ... until the name is free in `folder`.
fs::path UniquePath(const fs::path& folder, const std::string& stem, const std::string& ext) {
    fs::path candidate = folder / (stem + ext);
    for (int i = 1; fs::exists(candidate); ++i) {
        candidate = folder / (stem + "_" + std::to_string(i) + ext);
    }
    return candidate;
}

} // namespace

ContentBrowserPanel::Kind ContentBrowserPanel::Classify(const fs::path& file, const std::string& relPath,
                                                         bool isDirectory) {
    if (isDirectory) return Kind::Folder;
    const std::string ext = Extension(relPath);
    if (ext == ".scene") return Kind::Scene;
    if (TextureLibrary::IsImageFile(relPath)) return Kind::Texture;
    if (IsModelFile(relPath)) return Kind::Model;
    if (ext != ".lua") return Kind::Other;
    if (relPath.rfind("Content/UI/", 0) == 0) return Kind::Ui; // documents of the UI system

    std::error_code ec;
    const auto stamp = fs::last_write_time(file, ec);
    SniffResult& cached = m_sniffCache[relPath];
    if (cached.stamp != stamp) {
        cached.stamp = stamp;
        std::ifstream in(file, std::ios::binary);
        std::string head(4096, '\0');
        in.read(head.data(), static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<size_t>(in.gcount()));
        StripUtf8Bom(head);
        static const std::regex material(R"(return\s+material\b)");
        cached.isMaterial = std::regex_search(head, material);
        cached.isGraph = IsGraphLua(head);
        static const std::regex shader(R"((^|[^A-Za-z0-9_])shader\s*\()");
        cached.isShader = std::regex_search(head, shader);
    }
    if (cached.isGraph) return Kind::GraphShader;
    if (cached.isMaterial) return Kind::Material;
    return cached.isShader ? Kind::Shader : Kind::Script; // any other .lua is a gameplay script
}

void ContentBrowserPanel::Refresh(EditorContext& ctx) {
    m_root = ctx.projectRoot;
    m_lastRefresh = ImGui::GetTime();

    std::error_code ec;
    fs::create_directories(m_root / "Content", ec);
    if (!fs::is_directory(m_root / m_current, ec)) m_current = "Content";

    m_entries.clear();
    for (const auto& item : fs::directory_iterator(m_root / m_current, ec)) {
        Entry e;
        e.name = item.path().filename().string();
        if (e.name.empty() || e.name[0] == '.' || Extension(e.name) == ".tmp") continue;
        e.relPath = Generic(fs::relative(item.path(), m_root, ec));
        e.isDirectory = item.is_directory(ec);
        e.kind = Classify(item.path(), e.relPath, e.isDirectory);
        m_entries.push_back(std::move(e));
    }
    // Folders first, then by name.
    std::sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        return Extension(a.name) == Extension(b.name) ? a.name < b.name : Extension(a.name) < Extension(b.name);
    });

    m_subfolders.clear();
    for (const auto& item : fs::recursive_directory_iterator(m_root / "Content", ec)) {
        if (!item.is_directory(ec)) continue;
        const fs::path rel = fs::relative(item.path(), m_root, ec);
        m_subfolders[Generic(rel.parent_path())].push_back(Generic(rel));
    }
    for (auto& [folder, children] : m_subfolders) std::sort(children.begin(), children.end());
}

void ContentBrowserPanel::DrawFolderTree(const std::string& folder, const std::string& label) {
    const auto it = m_subfolders.find(folder);
    const bool hasChildren = it != m_subfolders.end() && !it->second.empty();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
    if (folder == m_current) flags |= ImGuiTreeNodeFlags_Selected;
    if (folder == "Content") flags |= ImGuiTreeNodeFlags_DefaultOpen;

    const bool open = ImGui::TreeNodeEx(folder.c_str(), flags, "%s", label.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) m_current = folder;
    if (open) {
        if (hasChildren) {
            for (const std::string& child : it->second) {
                DrawFolderTree(child, child.substr(child.rfind('/') + 1));
            }
        }
        ImGui::TreePop();
    }
}

void ContentBrowserPanel::DrawBreadcrumbs() {
    if (ImGui::Button("^") && m_current != "Content") m_current = m_current.substr(0, m_current.rfind('/'));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Up one folder");

    std::string path;
    size_t start = 0;
    while (start <= m_current.size()) {
        size_t end = m_current.find('/', start);
        if (end == std::string::npos) end = m_current.size();
        const std::string part = m_current.substr(start, end - start);
        path += (path.empty() ? "" : "/") + part;
        ImGui::SameLine();
        if (start > 0) {
            ImGui::TextDisabled("/");
            ImGui::SameLine();
        }
        ImGui::PushID(path.c_str());
        if (ImGui::SmallButton(part.c_str())) m_current = path;
        ImGui::PopID();
        start = end + 1;
    }
}

void ContentBrowserPanel::BeginRename(const Entry& entry) {
    m_renameTarget = entry.relPath;
    std::strncpy(m_renameBuffer, entry.name.c_str(), sizeof(m_renameBuffer) - 1);
    m_renameBuffer[sizeof(m_renameBuffer) - 1] = '\0';
    m_openRename = true;
}

void ContentBrowserPanel::Create(EditorContext& ctx, const char* kind) {
    const fs::path folder = ctx.projectRoot / m_current;
    fs::path created;
    std::error_code ec;

    if (std::strcmp(kind, "folder") == 0) {
        created = UniquePath(folder, "NewFolder", "");
        fs::create_directory(created, ec);
    } else if (std::strcmp(kind, "shader") == 0) {
        created = UniquePath(folder, "NewShader", ".lua");
        std::ofstream(created, std::ios::binary) << kShaderTemplate;
    } else if (std::strcmp(kind, "script") == 0) {
        created = UniquePath(folder, "NewScript", ".lua");
        std::ofstream(created, std::ios::binary) << kScriptTemplate;
    } else if (std::strcmp(kind, "ui") == 0) {
        created = UniquePath(folder, "NewUi", ".lua");
        std::ofstream(created, std::ios::binary) << kUiTemplate;
    } else if (std::strcmp(kind, "graphshader") == 0) {
        created = UniquePath(folder, "NewGraphShader", ".lua");
        MaterialGraph graph = MakeLitTemplate();
        graph.name = created.stem().string();
        std::ofstream(created, std::ios::binary) << GraphToLua(graph);
    } else if (std::strcmp(kind, "material") == 0) {
        created = UniquePath(folder, "NewMaterial", ".lua");
        MaterialData data;
        data.shader = "Content/Shaders/default_lit.lua";
        std::ofstream(created, std::ios::binary) << FormatMaterial(data);
    } else {
        created = UniquePath(folder, "NewScene", ".scene");
        Scene empty;
        std::string error;
        if (!SaveScene(empty, SceneEditorState{}, created, error)) Log::Error("Cannot create scene: {}", error);
    }
    if (ec) {
        Log::Error("Cannot create {}: {}", created.string(), ec.message());
        return;
    }
    Log::Info("Created {}", Generic(fs::relative(created, ctx.projectRoot)));
    Refresh(ctx);
    for (const Entry& e : m_entries) {
        if (e.relPath == Generic(fs::relative(created, ctx.projectRoot))) {
            m_selected = e.relPath;
            BeginRename(e); // new assets start in rename mode, like the Unreal content browser
        }
    }
}

void ContentBrowserPanel::DrawTile(EditorContext& ctx, const Entry& entry, float size) {
    // A thumbnail well, the name, and the asset type underneath; a 4px strip in the type color closes the well.
    const float wellHeight = size * 0.88f;
    const float labelHeight = ImGui::GetTextLineHeight() * 2.0f + 8.0f;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 total(size, wellHeight + labelHeight + 8.0f);

    ImGui::InvisibleButton("##tile", total);
    const bool hovered = ImGui::IsItemHovered();
    const bool selected = entry.relPath == m_selected;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + total.x, min.y + total.y);

    // The card: transparent, grey on hover, blue when selected.
    if (selected) {
        draw->AddRectFilled(min, max, ui::U32(ui::kSelection), 6.0f);
        draw->AddRect(min, max, ui::U32(ui::kAccent), 6.0f);
    } else if (hovered) {
        draw->AddRectFilled(min, max, ui::U32(ui::kHover), 6.0f);
    }

    const float pad = 6.0f;
    const ImVec2 wmin(min.x + pad, min.y + pad), wmax(min.x + size - pad, min.y + pad + wellHeight);
    const int kindIndex = static_cast<int>(entry.kind);
    const ImU32 color = TypeColor(kindIndex);

    bool drawnImage = false;
    if (entry.kind == Kind::Material) {
        RenderResources resources = ctx.Resources();
        if (const uint32_t id = ctx.thumbnails.Material(ctx.renderer, resources, entry.relPath, 128)) {
            // Spheres stay round: the largest square that fits, centered.
            const float side = std::min(wmax.x - wmin.x, wmax.y - wmin.y - 4.0f);
            const ImVec2 smin((wmin.x + wmax.x - side) * 0.5f, (wmin.y + wmax.y - side) * 0.5f);
            draw->AddImageRounded(static_cast<ImTextureID>(id), smin, ImVec2(smin.x + side, smin.y + side), ImVec2(0, 1),
                                  ImVec2(1, 0), IM_COL32_WHITE, 6.0f);
            drawnImage = true;
        }
    } else if (entry.kind == Kind::Texture) {
        const Texture& t = ctx.textures.Get(entry.relPath, false);
        if (t.id) {
            // Fit inside the well, keeping the aspect ratio.
            const float w = wmax.x - wmin.x - 4.0f, h = wmax.y - wmin.y - 8.0f;
            const float scale = std::min(w / static_cast<float>(t.width), h / static_cast<float>(t.height));
            const float iw = static_cast<float>(t.width) * scale, ih = static_cast<float>(t.height) * scale;
            const ImVec2 imin(wmin.x + (wmax.x - wmin.x - iw) * 0.5f, wmin.y + (wmax.y - wmin.y - ih) * 0.5f - 2.0f);
            draw->AddImageRounded(static_cast<ImTextureID>(t.id), imin, ImVec2(imin.x + iw, imin.y + ih), ImVec2(0, 1), ImVec2(1, 0),
                                  IM_COL32_WHITE, 3.0f);
            drawnImage = true;
        }
    }
    if (!drawnImage) {
        Icon icon = Icon::Code;
        switch (entry.kind) {
        case Kind::Folder: icon = Icon::Folder; break;
        case Kind::GraphShader: icon = Icon::Graph; break;
        case Kind::Shader: icon = Icon::Shader; break;
        case Kind::Script: icon = Icon::Script; break;
        case Kind::Ui: icon = Icon::Maximize; break;
        case Kind::Material: icon = Icon::Material; break;
        case Kind::Scene: icon = Icon::Scene; break;
        case Kind::Texture: icon = Icon::Texture; break;
        case Kind::Model: icon = Icon::Cube; break;
        default: icon = Icon::Code; break;
        }
        const float iconSize = std::min(wmax.x - wmin.x, wmax.y - wmin.y) * (entry.isDirectory ? 0.92f : 0.80f);
        DrawIcon(draw, icon, ImVec2((wmin.x + wmax.x) * 0.5f, (wmin.y + wmax.y) * 0.5f - 2.0f), iconSize,
                 entry.isDirectory ? ui::U32(ui::kFolder) : color);
    }
    if (entry.relPath == ctx.scenePath) {
        draw->AddCircleFilled(ImVec2(wmax.x - 8, wmin.y + 8), 3.5f, ui::U32(ui::kYellow)); // the open scene
    }

    // Name (one line, clipped) and type (dim).
    const float labelY = wmax.y + 5.0f;
    // The name stays on one line: centered when it fits, otherwise from the left edge and cut off by the tile.
    const ImVec4 clip(min.x + 2, labelY, max.x - 2, labelY + ImGui::GetTextLineHeight() + 2.0f);
    const float nameWidth = ImGui::CalcTextSize(entry.name.c_str()).x;
    const float nameX = nameWidth <= size - 8.0f ? min.x + (size - nameWidth) * 0.5f : min.x + 4.0f;
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(nameX, labelY), selected ? IM_COL32_WHITE : ui::U32(ui::kTextSoft),
                  entry.name.c_str(), nullptr, 0.0f, &clip);
    if (!entry.isDirectory) {
        const char* kindName = TypeName(kindIndex);
        const float tw = ImGui::CalcTextSize(kindName).x;
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.88f, ImVec2(min.x + (size - tw * 0.88f) * 0.5f, labelY + ImGui::GetTextLineHeight() + 1.0f),
                      ui::U32(ui::kTextDim), kindName);
    }

    // Folders and scenes open, materials open in the Material panel, everything else in its default app.
    auto open = [&] {
        switch (entry.kind) {
        case Kind::Folder: m_current = entry.relPath; break;
        case Kind::Scene: ctx.openScene(entry.relPath); break;
        case Kind::Model: SpawnModel(ctx, entry.relPath); break; // a double click places the model in the scene
        case Kind::Material:
            ctx.inspectedMaterial = entry.relPath;
            ImGui::SetWindowFocus("Material");
            break;
        default: OpenWithDefaultApp(ctx.projectRoot / entry.relPath); break;
        }
    };

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        m_selected = entry.relPath;
        if (entry.kind == Kind::Material) ctx.inspectedMaterial = entry.relPath; // single click previews it
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) open();
    if (hovered) ImGui::SetTooltip("%s", entry.relPath.c_str());

    if (!entry.isDirectory && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("MAGNUM_ASSET", entry.relPath.c_str(), entry.relPath.size() + 1);
        ImGui::TextUnformatted(entry.name.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginPopupContextItem("##itemctx")) {
        if (ImGui::MenuItem("Open")) open();
        if (entry.kind == Kind::Model && ImGui::MenuItem("Place in Scene")) SpawnModel(ctx, entry.relPath);
        // Shaders open in the text editor by default; the node editor is one click away.
        if (entry.kind == Kind::Shader || entry.kind == Kind::GraphShader) {
            if (ImGui::MenuItem("Open in node editor")) ctx.graphRequest = entry.relPath;
        } else if (entry.kind == Kind::Material) {
            if (MaterialAsset* material = ctx.materials.Get(entry.relPath);
                material && ImGui::MenuItem("Open shader in node editor")) {
                ctx.graphRequest = material->Data().shader;
            }
        }
        if ((entry.kind == Kind::Material || entry.kind == Kind::Shader || entry.kind == Kind::GraphShader) &&
            ImGui::MenuItem("Open file in text editor")) {
            OpenWithDefaultApp(ctx.projectRoot / entry.relPath);
        }
        if (ImGui::MenuItem("Rename")) BeginRename(entry);
        if (ImGui::MenuItem("Show in Explorer")) RevealInExplorer(ctx.projectRoot / entry.relPath);
        ImGui::Separator();
        if (ImGui::MenuItem("Delete")) {
            m_deleteTarget = entry.relPath;
            m_openDelete = true;
        }
        ImGui::EndPopup();
    }
}

void ContentBrowserPanel::DrawBackgroundMenu(EditorContext& ctx) {
    if (ImGui::BeginPopupContextWindow("##bgctx", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("New Folder")) Create(ctx, "folder");
        if (ImGui::MenuItem("New Material")) Create(ctx, "material");
        if (ImGui::MenuItem("New Graph Shader")) Create(ctx, "graphshader");
        if (ImGui::MenuItem("New Shader (code)")) Create(ctx, "shader");
        if (ImGui::MenuItem("New Script (Lua)")) Create(ctx, "script");
        if (ImGui::MenuItem("New UI Document")) Create(ctx, "ui");
        if (ImGui::MenuItem("New Scene")) Create(ctx, "scene");
        ImGui::Separator();
        if (ImGui::MenuItem("Show in Explorer")) RevealInExplorer(ctx.projectRoot / m_current);
        ImGui::EndPopup();
    }
}

void ContentBrowserPanel::DrawPopups(EditorContext& ctx) {
    if (m_openRename) {
        ImGui::OpenPopup("Rename Asset");
        m_openRename = false;
    }
    if (ImGui::BeginPopup("Rename Asset")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", m_renameBuffer, sizeof(m_renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            const fs::path from = ctx.projectRoot / m_renameTarget;
            std::string newName = Trim(m_renameBuffer);
            // Keep the extension when the new name has none (renaming a shader should stay a shader).
            if (!fs::is_directory(from) && Extension(newName).empty()) newName += from.extension().string();

            const bool valid = !newName.empty() && newName.find_first_of("/\\:*?\"<>|") == std::string::npos;
            const fs::path to = from.parent_path() / newName;
            std::error_code ec;
            if (!valid) {
                Log::Warn("'{}' is not a valid file name", newName);
            } else if (to != from && fs::exists(to)) {
                Log::Warn("'{}' already exists", newName);
            } else if (to != from) {
                fs::rename(from, to, ec);
                if (ec) Log::Error("Rename failed: {}", ec.message());
                else {
                    Log::Info("Renamed {} -> {}", m_renameTarget, newName);
                    if (Extension(newName) == ".lua") Log::Warn("Actors that used the old shader path need their Shader field updated");
                    if (ctx.scenePath == m_renameTarget) ctx.scenePath = Generic(fs::relative(to, ctx.projectRoot));
                    m_selected = Generic(fs::relative(to, ctx.projectRoot));
                }
            }
            Refresh(ctx);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_openDelete) {
        ImGui::OpenPopup("Delete Asset");
        m_openDelete = false;
    }
    if (ImGui::BeginPopupModal("Delete Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Move '%s' to the Recycle Bin?", m_deleteTarget.c_str());
        if (ImGui::Button("Delete", ImVec2(100, 0))) {
            if (MoveToRecycleBin(ctx.projectRoot / m_deleteTarget)) Log::Info("Moved to Recycle Bin: {}", m_deleteTarget);
            else Log::Error("Could not delete {}", m_deleteTarget);
            if (m_selected == m_deleteTarget) m_selected.clear();
            Refresh(ctx);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ContentBrowserPanel::Draw(EditorContext& ctx) {
    if (!ImGui::Begin("Content Browser")) {
        ImGui::End();
        return;
    }
    if (ImGui::GetTime() - m_lastRefresh > 0.75 || m_root != ctx.projectRoot) Refresh(ctx);

    DrawBreadcrumbs();
    ImGui::SameLine();
    const float sliderWidth = 110.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - sliderWidth - 190.0f - ImGui::GetStyle().WindowPadding.x);
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint("##filter", "Search", m_filter, sizeof(m_filter));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(sliderWidth);
    ImGui::SliderFloat("##size", &m_tileSize, 56.0f, 140.0f, "");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tile size");

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::kTab);
    if (ImGui::BeginChild("##tree", ImVec2(200.0f, 0), ImGuiChildFlags_Borders)) {
        DrawFolderTree("Content", "Content");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::kBg);
    if (ImGui::BeginChild("##tiles", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        const float cell = m_tileSize + 8.0f;
        const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
        int shown = 0;
        for (size_t i = 0; i < m_entries.size(); ++i) {
            const Entry& entry = m_entries[i];
            if (m_filter[0] && !ContainsNoCase(entry.name, m_filter)) continue;
            if (shown % columns != 0) ImGui::SameLine(0.0f, 8.0f);
            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginGroup();
            DrawTile(ctx, entry, m_tileSize);
            ImGui::EndGroup();
            ImGui::PopID();
            ++shown;
        }
        if (shown == 0) ImGui::TextDisabled(m_filter[0] ? "Nothing matches the search." : "Empty folder. Right-click to create assets.");
        DrawBackgroundMenu(ctx);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    DrawPopups(ctx);
    ImGui::End();
}

} // namespace magnum::editor
