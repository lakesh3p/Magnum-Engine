#include "render/Framebuffer.h"

#include "core/Log.h"

#include <glad/gl.h>

#include <algorithm>

namespace magnum {

Framebuffer::~Framebuffer() {
    Destroy();
}

void Framebuffer::Destroy() {
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    if (m_color) glDeleteTextures(1, &m_color);
    if (m_depth) glDeleteRenderbuffers(1, &m_depth);
    m_fbo = m_color = m_depth = 0;
}

void Framebuffer::Resize(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (m_fbo && width == m_width && height == m_height) {
        return;
    }
    Destroy();
    m_width = width;
    m_height = height;

    glCreateTextures(GL_TEXTURE_2D, 1, &m_color);
    glTextureStorage2D(m_color, 1, GL_RGBA8, width, height);
    glTextureParameteri(m_color, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_color, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_color, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_color, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glCreateRenderbuffers(1, &m_depth);
    glNamedRenderbufferStorage(m_depth, GL_DEPTH24_STENCIL8, width, height);

    glCreateFramebuffers(1, &m_fbo);
    glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, m_color, 0);
    glNamedFramebufferRenderbuffer(m_fbo, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depth);

    if (glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        Log::Error("Framebuffer {}x{} is incomplete", width, height);
    }
}

void Framebuffer::Bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
}

void Framebuffer::BindDefault() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

std::vector<uint8_t> Framebuffer::ReadPixels() const {
    std::vector<uint8_t> pixels(static_cast<size_t>(m_width) * m_height * 4);
    glGetTextureImage(m_color, 0, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(pixels.size()), pixels.data());
    return pixels;
}

} // namespace magnum
