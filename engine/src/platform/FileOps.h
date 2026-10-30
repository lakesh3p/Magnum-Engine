#pragma once

#include <filesystem>
#include <string>

namespace magnum {

// Windows shell helpers used by the Content Browser.
bool MoveToRecycleBin(const std::filesystem::path& path); // deletes recoverably
bool OpenWithDefaultApp(const std::filesystem::path& path);
bool RevealInExplorer(const std::filesystem::path& path);
// The system folder picker. Empty path if the user cancelled.
std::filesystem::path PickFolder(const std::wstring& title, const std::filesystem::path& startIn = {});
// Starts another copy of the running program with the given command line arguments.
bool LaunchSelf(const std::wstring& arguments);

} // namespace magnum
