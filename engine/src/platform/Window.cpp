#include "platform/Window.h"

#include "core/Log.h"

#include <glad/gl.h>

#include <GLFW/glfw3.h>

namespace magnum {

namespace {

void GLAPIENTRY OnGlDebug(GLenum /*source*/, GLenum type, GLuint id, GLenum severity, GLsizei length,
                          const GLchar* message, const void* /*user*/) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return;
    }
    const std::string text(message, static_cast<size_t>(length));
    if (type == GL_DEBUG_TYPE_ERROR) {
        Log::Error("GL {}: {}", id, text);
    } else {
        Log::Warn("GL {}: {}", id, text);
    }
}

} // namespace

Window::~Window() {
    Destroy();
}

bool Window::Create(const WindowDesc& desc) {
    glfwSetErrorCallback([](int code, const char* message) { Log::Error("GLFW {}: {}", code, message); });

    if (!glfwInit()) {
        Log::Error("glfwInit failed");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE); // validation costs CPU time: Debug builds only
#endif

    m_window = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
    if (!m_window) {
        Log::Error("Failed to create window (OpenGL 4.5 core is required)");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(desc.vsync ? 1 : 0);

    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        Log::Error("gladLoadGL failed");
        Destroy();
        return false;
    }

#ifndef NDEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(OnGlDebug, nullptr);
#endif

    Log::Info("OpenGL {} | {} | {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
              reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
              reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    return true;
}

void Window::Destroy() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        glfwTerminate();
    }
}

void Window::PollEvents() {
    glfwPollEvents();
}

void Window::SwapBuffers() {
    glfwSwapBuffers(m_window);
}

bool Window::ShouldClose() const {
    return glfwWindowShouldClose(m_window) != 0;
}

void Window::RequestClose() {
    glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void Window::CancelClose() {
    glfwSetWindowShouldClose(m_window, GLFW_FALSE);
}

void Window::SetTitle(const std::string& title) {
    glfwSetWindowTitle(m_window, title.c_str());
}

void Window::FramebufferSize(int& width, int& height) const {
    glfwGetFramebufferSize(m_window, &width, &height);
}

} // namespace magnum
