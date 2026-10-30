#include "project/Project.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace magnum {

namespace {

namespace fs = std::filesystem;

// A name that is safe as a file name.
std::string FileSafe(std::string name) {
    for (char& c : name) {
        if (std::string("\\/:*?\"<>|").find(c) != std::string::npos) c = '_';
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    return name.empty() ? "Project" : name;
}

} // namespace

bool LoadProjectInfo(const fs::path& folder, ProjectInfo& info) {
    std::error_code ec;
    fs::path found;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".magnum") {
            found = entry.path();
            break;
        }
    }
    if (found.empty()) return false;

    ProjectInfo result;
    result.folder = folder;
    result.file = found;
    result.name = found.stem().string();
    std::ifstream in(found, std::ios::binary);
    const nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
    if (json.is_object()) {
        if (json.contains("name") && json["name"].is_string() && !json["name"].get<std::string>().empty()) {
            result.name = json["name"].get<std::string>();
        }
        if (json.contains("defaultScene") && json["defaultScene"].is_string()) {
            result.defaultScene = json["defaultScene"].get<std::string>();
        }
    }
    info = std::move(result);
    return true;
}

bool CreateProject(const fs::path& folder, const std::string& name, const fs::path& templateDir, std::string& error) {
    std::error_code ec;
    if (name.empty()) {
        error = "the project needs a name";
        return false;
    }
    if (!fs::is_directory(templateDir, ec)) {
        error = "the project template is missing: " + templateDir.string();
        return false;
    }
    if (fs::exists(folder, ec)) {
        if (!fs::is_directory(folder, ec)) {
            error = folder.string() + " is a file, not a folder";
            return false;
        }
        if (fs::directory_iterator(folder, ec) != fs::directory_iterator()) {
            ProjectInfo existing;
            error = LoadProjectInfo(folder, existing) ? "a project already exists in " + folder.string()
                                                      : folder.string() + " is not empty; choose an empty folder";
            return false;
        }
    }

    fs::create_directories(folder, ec);
    if (ec) {
        error = "cannot create " + folder.string() + ": " + ec.message();
        return false;
    }
    fs::copy(templateDir, folder, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    if (ec) {
        error = "cannot copy the template: " + ec.message();
        return false;
    }
    for (const char* dir : {"Content/Scenes", "Content/Textures", "Plugins", "Saved"}) fs::create_directories(folder / dir, ec);

    nlohmann::ordered_json json;
    json["magnumProject"] = 1;
    json["name"] = name;
    json["defaultScene"] = "Content/Scenes/Default.scene";
    std::ofstream out(folder / (FileSafe(name) + ".magnum"), std::ios::binary);
    if (!out) {
        error = "cannot write the project file";
        return false;
    }
    out << json.dump(2) << '\n';
    return true;
}

} // namespace magnum
