#pragma once

#include <memory>
#include <string>
#include <vector>

namespace magnum::editor {

// Every change made from the editor is a Command, so it can be undone and redone.
class Command {
public:
    virtual ~Command() = default;
    virtual void Do() = 0;
    virtual void Undo() = 0;
    virtual std::string Name() const = 0;
    // False for edits stored in other files (materials): they are undoable but do not make the scene "dirty".
    virtual bool ModifiesScene() const { return true; }
};

class CommandStack {
public:
    // Runs the command and records it. Clears the redo history.
    void Execute(std::unique_ptr<Command> command);
    // Records a command whose effect was already applied (live-edited widgets).
    void PushExecuted(std::unique_ptr<Command> command);

    bool Undo();
    bool Redo();
    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }
    std::string UndoName() const { return m_undo.empty() ? std::string() : m_undo.back()->Name(); }
    std::string RedoName() const { return m_redo.empty() ? std::string() : m_redo.back()->Name(); }
    void Clear();

    // Increases on every execute, undo and redo of a scene-changing command; compared with the value
    // at save time to know if the scene changed.
    uint64_t Revision() const { return m_revision; }

    // A session brackets play-in-editor. Inside it undo cannot go past the point where it began, and when it ends
    // everything recorded during it is dropped (the scene is restored from a snapshot, so those steps no longer
    // apply) and the revision goes back, so playing never makes the scene look modified.
    void BeginSession();
    void EndSession();
    bool InSession() const { return m_inSession; }

private:
    bool m_inSession = false;
    size_t m_sessionBase = 0;
    uint64_t m_sessionRevision = 0;
    uint64_t m_revision = 0;
    static constexpr size_t kMaxDepth = 256;
    std::vector<std::unique_ptr<Command>> m_undo;
    std::vector<std::unique_ptr<Command>> m_redo;
};

} // namespace magnum::editor
