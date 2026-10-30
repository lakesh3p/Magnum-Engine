#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace magnum::editor {

inline std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

inline bool ContainsNoCase(const std::string& text, const std::string& needle) {
    const auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != text.end();
}

} // namespace magnum::editor
