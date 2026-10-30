#pragma once

#include "ui/UiSystem.h"

#include <filesystem>
#include <memory>
#include <string>

namespace magnum {

class Scene;

// Shows the Canvas components of a scene while nothing is playing: documents run in a Lua state of their own (with the
// game's globals stubbed out), animate and reload when their files change, but take no pointer input and run no game
// scripts. The editor draws it over the viewport so a UI can be built and checked without pressing Play.
class UiPreview {
public:
    UiPreview();
    ~UiPreview();

    // Starts over for a project (a new Lua state, no canvases).
    void Open(const std::filesystem::path& projectRoot);
    void Close();
    bool IsOpen() const;

    // `pointer` is what the mouse does over the view (nothing by default: the preview is not interactive).
    void Update(Scene& scene, float dt, int viewWidth, int viewHeight, const UiPointer& pointer = UiPointer{});
    UiSystem& System();

    // Runs Lua in the preview's state (the editor console, tests). False with the message in `error` on failure.
    bool Run(const std::string& source, std::string& error);
    // The value of a Lua expression as text ("nil" when it has none or fails).
    std::string Eval(const std::string& expression);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace magnum
