#include "commands/GraphCommands.h"

#include "core/Log.h"

#include <filesystem>
#include <fstream>

namespace magnum::editor {

bool WriteTextFile(const std::filesystem::path& file, const std::string& text) {
    const std::filesystem::path temp = file.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary);
        if (!out) return false;
        out << text;
    }
    std::error_code ec;
    std::filesystem::rename(temp, file, ec);
    return !ec;
}

namespace {

class GraphEditCmd final : public Command {
public:
    GraphEditCmd(std::filesystem::path file, std::string path, std::string before, std::string after)
        : m_file(std::move(file)), m_path(std::move(path)), m_before(std::move(before)), m_after(std::move(after)) {}

    void Do() override { Write(m_after); }
    void Undo() override { Write(m_before); }
    std::string Name() const override { return "Edit graph " + m_path.substr(m_path.rfind('/') + 1); }
    bool ModifiesScene() const override { return false; }

private:
    void Write(const std::string& text) {
        if (!WriteTextFile(m_file, text)) Log::Error("Cannot write {}", m_path);
    }
    std::filesystem::path m_file;
    std::string m_path, m_before, m_after;
};

} // namespace

void RecordGraphEdit(EditorContext& ctx, const std::string& shaderPath, const std::string& before, const std::string& after) {
    if (before == after) return;
    ctx.commands.PushExecuted(std::make_unique<GraphEditCmd>(ctx.projectRoot / shaderPath, shaderPath, before, after));
}

} // namespace magnum::editor
