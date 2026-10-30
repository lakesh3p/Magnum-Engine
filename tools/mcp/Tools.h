#pragma once

#include "EditorLink.h"

#include <functional>
#include <string>
#include <vector>

namespace magnum::mcp {

// What a tool answers: content blocks (text, images) and whether the call failed.
struct ToolResult {
    Json content = Json::array();
    bool isError = false;
};

ToolResult TextResult(const std::string& text, bool isError = false);
ToolResult ImageResult(const std::string& base64Png, const std::string& caption);

struct Tool {
    std::string name;
    std::string title;
    std::string description;
    Json inputSchema;
    bool readOnly = false;
    bool destructive = false;
    std::function<ToolResult(const Json& arguments)> run;
};

// Every tool of the server. They share one link to the editor.
class ToolSet {
public:
    explicit ToolSet(EditorLink& link);
    const std::vector<Tool>& All() const { return m_tools; }
    const Tool* Find(const std::string& name) const;
    ToolResult Call(const std::string& name, const Json& arguments) const;
    Json ListJson() const; // the answer of tools/list

    // Documentation files shipped with the engine (docs/*.md and CHANGELOG.md): name -> path.
    std::vector<std::pair<std::string, std::filesystem::path>> Docs() const;
    // The component list and the console help as text (also served as resources).
    std::string ComponentReference();
    std::string ConsoleReference();

private:
    void Register();
    EditorLink& m_link;
    std::vector<Tool> m_tools;
};

std::string Base64Encode(const std::string& bytes);

} // namespace magnum::mcp
