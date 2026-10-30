#pragma once

#include <string>

struct GLFWwindow;

namespace magnum {

struct WindowDesc {
    int width = 1600;
    int height = 900;
    std::string title = "Magnum Engine";
    bool vsync = false; // the editor runs uncapped; it throttles itself when it is not in front
};

// GLFW window that owns an OpenGL 4.5 core context (loaded through glad).
class Window {
public:
    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Create(const WindowDesc& desc);
    void Destroy();

    void PollEvents();
    void SwapBuffers();
    bool ShouldClose() const;
    void RequestClose();
    void CancelClose();
    void SetTitle(const std::string& title);

    void FramebufferSize(int& width, int& height) const;
    GLFWwindow* Handle() const { return m_window; }

private:
    GLFWwindow* m_window = nullptr;
};

} // namespace magnum
