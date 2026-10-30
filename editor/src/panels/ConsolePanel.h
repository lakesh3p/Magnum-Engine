#pragma once

#include "core/Log.h"

#include <functional>
#include <string>
#include <vector>

namespace magnum::editor {

// Shows the engine log and a command line. Commands are handed to onCommand.
class ConsolePanel {
public:
    std::function<void(const std::string&)> onCommand;

    void Draw(const char* title);

private:
    std::vector<LogEntry> m_entries;
    uint64_t m_revision = ~0ull;
    bool m_showInfo = true;
    bool m_showWarn = true;
    bool m_showError = true;
    bool m_autoScroll = true;
    char m_input[512] = {};
};

} // namespace magnum::editor
