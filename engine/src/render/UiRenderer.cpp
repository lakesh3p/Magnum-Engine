#include "render/UiRenderer.h"

#include "core/Log.h"
#include "ui/UiFonts.h"

#include <glad/gl.h>

#include <algorithm>

namespace magnum {

namespace {

const char* const kVertexShader =
    "#version 450 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "layout(location = 3) in vec4 a_shape;\n"
    "layout(location = 4) in vec4 a_border;\n"
    "layout(location = 5) in vec2 a_local;\n"
    "layout(location = 6) in vec2 a_misc;\n"
    "uniform vec2 u_view;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_color;\n"
    "out vec4 v_shape;\n"
    "out vec4 v_border;\n"
    "out vec2 v_local;\n"
    "out vec2 v_misc;\n"
    "void main() {\n"
    "    v_uv = a_uv; v_color = a_color; v_shape = a_shape; v_border = a_border; v_local = a_local; v_misc = a_misc;\n"
    "    gl_Position = vec4(a_pos.x / u_view.x * 2.0 - 1.0, 1.0 - a_pos.y / u_view.y * 2.0, 0.0, 1.0);\n"
    "}\n";

const char* const kFragmentShader =
    "#version 450 core\n"
    "uniform sampler2D u_tex;\n"
    "in vec2 v_uv;\n"
    "in vec4 v_color;\n"
    "in vec4 v_shape;\n"
    "in vec4 v_border;\n"
    "in vec2 v_local;\n"
    "in vec2 v_misc;\n"
    "out vec4 o_color;\n"
    "float sdRoundBox(vec2 p, vec2 b, float r) {\n"
    "    vec2 q = abs(p) - b + r;\n"
    "    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;\n"
    "}\n"
    "void main() {\n"
    "    if (v_misc.x > 0.5) { // glyph: the atlas holds coverage\n"
    "        o_color = vec4(v_color.rgb, v_color.a * texture(u_tex, v_uv).r);\n"
    "        return;\n"
    "    }\n"
    "    float d = sdRoundBox(v_local, v_shape.xy, v_shape.z);\n"
    "    float cover = v_misc.y > 0.0 ? 1.0 - smoothstep(-v_misc.y, v_misc.y, d) : clamp(0.5 - d, 0.0, 1.0);\n"
    "    vec4 fill = v_color * texture(u_tex, v_uv);\n"
    "    float bw = v_shape.w;\n"
    "    if (bw > 0.0) {\n"
    "        float inner = clamp(0.5 - (d + bw), 0.0, 1.0);\n"
    "        float a = mix(v_border.a, fill.a, inner);\n"
    "        vec3 rgb = (v_border.rgb * v_border.a * (1.0 - inner) + fill.rgb * fill.a * inner) / max(a, 0.00001);\n"
    "        o_color = vec4(rgb, a * cover);\n"
    "    } else {\n"
    "        o_color = vec4(fill.rgb, fill.a * cover);\n"
    "    }\n"
    "}\n";

} // namespace

void UiRenderer::Init() {
    CompiledShaderSource source;
    source.name = "UI";
    source.luaFile = "(built in) ui";
    source.vertexGlsl = kVertexShader;
    source.fragmentGlsl = kFragmentShader;
    std::vector<ShaderDiagnostic> diagnostics;
    m_program = ShaderProgram::Create(source, diagnostics);
    for (const ShaderDiagnostic& d : diagnostics) Log::Error("{}", d.ToString());

    glCreateVertexArrays(1, &m_vao);
    glCreateBuffers(1, &m_vbo);
    glCreateBuffers(1, &m_ibo);
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, sizeof(UiVertex));
    glVertexArrayElementBuffer(m_vao, m_ibo);
    struct Attribute {
        GLint components;
        size_t offset;
    };
    const Attribute attributes[] = {{2, offsetof(UiVertex, pos)},   {2, offsetof(UiVertex, uv)},    {4, offsetof(UiVertex, color)}, {4, offsetof(UiVertex, shape)},
                                    {4, offsetof(UiVertex, border)}, {2, offsetof(UiVertex, local)}, {2, offsetof(UiVertex, misc)}};
    for (GLuint i = 0; i < 7; ++i) {
        glEnableVertexArrayAttrib(m_vao, i);
        glVertexArrayAttribFormat(m_vao, i, attributes[i].components, GL_FLOAT, GL_FALSE, static_cast<GLuint>(attributes[i].offset));
        glVertexArrayAttribBinding(m_vao, i, 0);
    }

    glCreateSamplers(2, m_samplers);
    for (int i = 0; i < 2; ++i) {
        const GLint filter = i == 0 ? GL_LINEAR : GL_NEAREST;
        glSamplerParameteri(m_samplers[i], GL_TEXTURE_MIN_FILTER, filter);
        glSamplerParameteri(m_samplers[i], GL_TEXTURE_MAG_FILTER, filter);
        glSamplerParameteri(m_samplers[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_samplers[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

void UiRenderer::Shutdown() {
    m_program.reset();
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_ibo) glDeleteBuffers(1, &m_ibo);
    m_vao = m_vbo = m_ibo = 0;
    if (m_samplers[0]) glDeleteSamplers(2, m_samplers);
    m_samplers[0] = m_samplers[1] = 0;
    for (auto& [id, atlas] : m_atlases) {
        if (atlas.id) glDeleteTextures(1, &atlas.id);
    }
    m_atlases.clear();
}

uint32_t UiRenderer::FontTexture(int atlasId) {
    const UiAtlas* atlas = UiFonts::Get().Atlas(atlasId);
    if (!atlas) return 0;
    AtlasTexture& t = m_atlases[atlasId];
    if (!t.id || t.version != atlas->version || t.width != atlas->width || t.height != atlas->height) {
        if (t.id) glDeleteTextures(1, &t.id);
        glCreateTextures(GL_TEXTURE_2D, 1, &t.id);
        glTextureStorage2D(t.id, 1, GL_R8, atlas->width, atlas->height);
        GLint previousAlignment = 4;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTextureSubImage2D(t.id, 0, 0, 0, atlas->width, atlas->height, GL_RED, GL_UNSIGNED_BYTE, atlas->pixels.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
        t.version = atlas->version;
        t.width = atlas->width;
        t.height = atlas->height;
    }
    return t.id;
}

void UiRenderer::Draw(const UiDrawList& list, TextureLibrary& textures, int viewWidth, int viewHeight) {
    if (!m_program || list.Empty() || list.vertices.empty()) return;

    glNamedBufferData(m_vbo, static_cast<GLsizeiptr>(list.vertices.size() * sizeof(UiVertex)), list.vertices.data(), GL_STREAM_DRAW);
    glNamedBufferData(m_ibo, static_cast<GLsizeiptr>(list.indices.size() * sizeof(uint32_t)), list.indices.data(), GL_STREAM_DRAW);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    m_program->Use();
    m_program->SetVec2("u_view", static_cast<float>(viewWidth), static_cast<float>(viewHeight));
    m_program->SetInt("u_tex", 0);
    glBindVertexArray(m_vao);

    bool scissor = false;
    for (const UiDrawCommand& cmd : list.commands) {
        if (cmd.indexCount == 0) continue;
        uint32_t texture = textures.White();
        uint32_t sampler = m_samplers[0];
        if (cmd.atlas >= 0) {
            if (const uint32_t t = FontTexture(cmd.atlas)) texture = t;
        } else if (cmd.image >= 0 && cmd.image < static_cast<int>(list.images.size())) {
            const Texture& image = textures.Get(list.images[static_cast<size_t>(cmd.image)], false);
            if (image.id) texture = image.id;
            if (cmd.pixelated) sampler = m_samplers[1];
        }
        glBindTextureUnit(0, texture);
        glBindSampler(0, sampler);
        if (cmd.clipped) {
            if (!scissor) glEnable(GL_SCISSOR_TEST);
            scissor = true;
            const int x = std::max(cmd.clip[0], 0), w = std::max(cmd.clip[2], 0), h = std::max(cmd.clip[3], 0);
            glScissor(x, viewHeight - (cmd.clip[1] + cmd.clip[3]), w, h);
        } else if (scissor) {
            glDisable(GL_SCISSOR_TEST);
            scissor = false;
        }
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(cmd.indexCount), GL_UNSIGNED_INT, reinterpret_cast<const void*>(static_cast<uintptr_t>(cmd.firstIndex) * sizeof(uint32_t)));
    }

    if (scissor) glDisable(GL_SCISSOR_TEST);
    glBindSampler(0, 0);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

} // namespace magnum
