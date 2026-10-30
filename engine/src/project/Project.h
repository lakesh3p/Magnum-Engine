#pragma once

#include <filesystem>
#include <string>

namespace magnum {

// A project is a folder with a <Name>.magnum file, Content/, Game/, Plugins/ and Saved/. It lives anywhere on disk,
// outside the engine folder.
struct ProjectInfo {
    std::string name;
    std::string defaultScene = "Content/Scenes/Default.scene";
    std::filesystem::path folder;
    std::filesystem::path file; // the .magnum file
};

// Reads the .magnum file in `folder`. False if there is none.
bool LoadProjectInfo(const std::filesystem::path& folder, ProjectInfo& info);

// Creates a project in `folder` (which may be missing, or must be empty): copies the template (starter content,
// the default render pipeline, a game module) and writes <name>.magnum. On failure `error` says why and nothing is
// left half-written behind except what was copied before the failure.
bool CreateProject(const std::filesystem::path& folder, const std::string& name, const std::filesystem::path& templateDir,
                   std::string& error);

} // namespace magnum
