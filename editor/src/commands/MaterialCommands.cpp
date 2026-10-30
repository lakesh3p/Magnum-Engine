#include "commands/MaterialCommands.h"

namespace magnum::editor {

namespace {

class MaterialEditCmd final : public Command {
public:
    MaterialEditCmd(MaterialLibrary& library, std::string path, MaterialData before, MaterialData after)
        : m_library(library), m_path(std::move(path)), m_before(std::move(before)), m_after(std::move(after)) {}

    void Do() override { Apply(m_after); }
    void Undo() override { Apply(m_before); }
    std::string Name() const override { return "Edit material " + m_path.substr(m_path.rfind('/') + 1); }
    bool ModifiesScene() const override { return false; }

private:
    void Apply(const MaterialData& data) {
        if (MaterialAsset* asset = m_library.Get(m_path)) {
            asset->SetData(data);
            asset->Save();
        }
    }
    MaterialLibrary& m_library;
    std::string m_path;
    MaterialData m_before, m_after;
};

} // namespace

void RecordMaterialEdit(EditorContext& ctx, const std::string& materialPath, const MaterialData& before,
                        const MaterialData& after) {
    if (before == after) return;
    if (MaterialAsset* asset = ctx.materials.Get(materialPath)) asset->Save();
    ctx.commands.PushExecuted(std::make_unique<MaterialEditCmd>(ctx.materials, materialPath, before, after));
}

} // namespace magnum::editor
