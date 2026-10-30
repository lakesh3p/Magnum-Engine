#include "commands/CommandStack.h"

#include "core/Log.h"

namespace magnum::editor {

void CommandStack::Execute(std::unique_ptr<Command> command) {
    command->Do();
    PushExecuted(std::move(command));
}

void CommandStack::PushExecuted(std::unique_ptr<Command> command) {
    m_redo.clear();
    if (command->ModifiesScene()) ++m_revision;
    m_undo.push_back(std::move(command));
    if (m_undo.size() > kMaxDepth) {
        m_undo.erase(m_undo.begin());
    }
}

void CommandStack::BeginSession() {
    m_inSession = true;
    m_sessionBase = m_undo.size();
    m_sessionRevision = m_revision;
    m_redo.clear();
}

void CommandStack::EndSession() {
    if (!m_inSession) return;
    m_undo.resize(m_sessionBase); // unique_ptr elements: the extra ones are destroyed
    m_redo.clear();
    m_revision = m_sessionRevision;
    m_inSession = false;
}

bool CommandStack::Undo() {
    if (m_undo.empty() || (m_inSession && m_undo.size() <= m_sessionBase)) return false;
    std::unique_ptr<Command> command = std::move(m_undo.back());
    m_undo.pop_back();
    command->Undo();
    if (command->ModifiesScene()) ++m_revision;
    Log::Info("Undo: {}", command->Name());
    m_redo.push_back(std::move(command));
    return true;
}

bool CommandStack::Redo() {
    if (m_redo.empty()) return false;
    std::unique_ptr<Command> command = std::move(m_redo.back());
    m_redo.pop_back();
    command->Do();
    if (command->ModifiesScene()) ++m_revision;
    Log::Info("Redo: {}", command->Name());
    m_undo.push_back(std::move(command));
    return true;
}

void CommandStack::Clear() {
    m_undo.clear();
    m_redo.clear();
}

} // namespace magnum::editor
